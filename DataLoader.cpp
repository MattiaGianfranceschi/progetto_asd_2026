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