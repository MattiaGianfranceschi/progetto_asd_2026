#include "DataLoader.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <sstream>

namespace {

constexpr std::size_t kBufferSize = 1 << 16;  // 64 KiB di dati decompressi alla volta

std::string bzMessage(int code) {
    switch (code) {
        case BZ_IO_ERROR:         return "errore di I/O";
        case BZ_UNEXPECTED_EOF:   return "file .bz2 troncato";
        case BZ_DATA_ERROR:       return "dati .bz2 corrotti";
        case BZ_DATA_ERROR_MAGIC: return "il file non e' in formato .bz2";
        case BZ_MEM_ERROR:        return "memoria insufficiente";
        default:                  return "errore libbz2 (codice " + std::to_string(code) + ")";
    }
}

// Converte un token di sole cifre in un ASN a 32 bit; false se non valido.
bool parseASN(const std::string& token, ASN& out) {
    if (token.empty() || token.size() > 10) return false;
    unsigned long long value = 0;
    for (char c : token) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<unsigned>(c - '0');
    }
    if (value > std::numeric_limits<ASN>::max()) return false;
    out = static_cast<ASN>(value);
    return true;
}

}  // namespace

DataLoader::DataLoader(const std::string& filename, std::size_t pathBlock, char asSeparator)
    : filename_(filename),
      pathBlock_(pathBlock),
      asSeparator_(asSeparator),
      buffer_(kBufferSize) {
    errno = 0;
    file_ = std::fopen(filename.c_str(), "rb");
    if (!file_) {
        const int reason = errno;  // file mancante, permesso negato, ...
        throw DataLoaderError("impossibile aprire il file: " + filename +
                              (reason != 0 ? " (" + std::string(std::strerror(reason)) + ")" : ""));
    }

    // Controllo "file vuoto" (0 byte) prima ancora di invocare libbz2.
    int first = std::fgetc(file_);
    if (first == EOF) {
        const bool ioError = std::ferror(file_) != 0;
        std::fclose(file_);
        file_ = nullptr;
        throw DataLoaderError(ioError ? "errore di lettura del file: " + filename
                                      : "il file e' vuoto: " + filename);
    }
    std::ungetc(first, file_);

    int err = BZ_OK;
    bz_ = BZ2_bzReadOpen(&err, file_, 0, 0, nullptr, 0);
    if (!bz_) {
        std::fclose(file_);
        file_ = nullptr;
        throw DataLoaderError(filename + ": " + bzMessage(err));
    }
}

DataLoader::~DataLoader() {
    closeBz();
    if (file_) std::fclose(file_);
}

void DataLoader::closeBz() noexcept {
    if (bz_) {
        int err = BZ_OK;
        BZ2_bzReadClose(&err, bz_);
        bz_ = nullptr;
    }
}

// Decomprime il blocco successivo di dati in buffer_.
// Ritorna false solo quando non c'e' piu' nulla da leggere.
bool DataLoader::fillBuffer() {
    bufPos_ = bufLen_ = 0;
    while (!finished_) {
        int err = BZ_OK;
        const int n = BZ2_bzRead(&err, bz_, buffer_.data(), static_cast<int>(buffer_.size()));

        if (err == BZ_OK) {
            if (n > 0) {
                bufLen_ = static_cast<std::size_t>(n);
                return true;
            }
            continue;
        }

        if (err == BZ_STREAM_END) {
            // Fine di uno stream .bz2: recupero i byte letti dal file ma non
            // consumati, perche' potrebbe iniziare subito un altro stream.
            void* unusedPtr = nullptr;
            int unusedLen = 0;
            int err2 = BZ_OK;
            BZ2_bzReadGetUnused(&err2, bz_, &unusedPtr, &unusedLen);
            std::vector<char> leftover(static_cast<const char*>(unusedPtr),
                                       static_cast<const char*>(unusedPtr) + unusedLen);
            closeBz();

            bool another = unusedLen > 0;
            if (!another) {
                int c = std::fgetc(file_);
                if (c != EOF) {
                    std::ungetc(c, file_);
                    another = true;
                }
            }

            if (another) {
                bz_ = BZ2_bzReadOpen(&err2, file_, 0, 0,
                                     unusedLen > 0 ? leftover.data() : nullptr, unusedLen);
                if (!bz_) throw DataLoaderError(filename_ + ": " + bzMessage(err2));
            } else {
                finished_ = true;
            }

            if (n > 0) {
                bufLen_ = static_cast<std::size_t>(n);
                return true;
            }
            continue;
        }

        throw DataLoaderError(filename_ + ": " + bzMessage(err));
    }
    return false;
}

// Legge una riga (senza '\n' e senza eventuale '\r' finale).
// Ritorna false a fine file.
bool DataLoader::readLine(std::string& line) {
    line.clear();
    while (true) {
        if (bufPos_ == bufLen_ && !fillBuffer()) {
            if (line.empty()) return false;  // ultima riga senza '\n'
            if (line.back() == '\r') line.pop_back();
            return true;
        }

        const char* start = buffer_.data() + bufPos_;
        const std::size_t avail = bufLen_ - bufPos_;
        const char* nl = static_cast<const char*>(std::memchr(start, '\n', avail));

        if (nl) {
            line.append(start, static_cast<std::size_t>(nl - start));
            bufPos_ += static_cast<std::size_t>(nl - start) + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }

        // La riga continua nel blocco successivo.
        line.append(start, avail);
        bufPos_ = bufLen_;
    }
}

// Isola il blocco del cammino AS, lo spezza negli ASN e ne elimina le
// ripetizioni consecutive (1234 1234 5678 -> 1234 5678).
// Esempio di riga:
//   routeviews/routeviews|5 1239|6113|8063|3464 199.88.21.0/24 i 144.228.240.93
//   -> blocco [1] = "1239|6113|8063|3464" -> {1239, 6113, 8063, 3464}
// Ritorna false se la riga non contiene un cammino valido.
bool DataLoader::parseLine(const std::string& line, ASPath& path) const {
    path.clear();
    static const char* const kWhitespace = " \t";

    // 1. Raggiungo l'inizio del blocco numero 'pathBlock_' (blocchi separati da spazi).
    std::size_t begin = line.find_first_not_of(kWhitespace);
    for (std::size_t i = 0; i < pathBlock_ && begin != std::string::npos; ++i) {
        const std::size_t endOfBlock = line.find_first_of(kWhitespace, begin);
        if (endOfBlock == std::string::npos) return false;
        begin = line.find_first_not_of(kWhitespace, endOfBlock);
    }
    if (begin == std::string::npos) return false;

    std::size_t end = line.find_first_of(kWhitespace, begin);
    if (end == std::string::npos) end = line.size();

    // 2. Il separatore tra gli AS diventa uno spazio e leggo gli ASN con sstream.
    std::string block = line.substr(begin, end - begin);
    std::replace(block.begin(), block.end(), asSeparator_, ' ');

    std::istringstream tokens(block);
    std::string token;
    while (tokens >> token) {
        ASN asn = 0;
        if (!parseASN(token, asn)) {  // AS_SET "{1,2}", testo, overflow...
            path.clear();
            return false;
        }
        if (path.empty() || path.back() != asn) path.push_back(asn);
    }
    return !path.empty();
}

bool DataLoader::nextPath(ASPath& path) {
    while (readLine(line_)) {
        if (line_.empty()) continue;
        if (parseLine(line_, path)) {
            ++pathsRead_;
            return true;
        }
        ++linesSkipped_;
    }
    if (pathsRead_ == 0) {
        throw DataLoaderError("nessun cammino AS valido trovato in: " + filename_);
    }
    return false;
}