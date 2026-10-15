#ifndef DATALOADER_H
#define DATALOADER_H

#include <bzlib.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

// Numero di un Autonomous System (gli ASN moderni sono a 32 bit).
using ASN = std::uint32_t;

// Un cammino AS: sequenza ordinata di ASN, senza ripetizioni consecutive.
using ASPath = std::vector<ASN>;

// Errore sollevato dal DataLoader (file mancante/vuoto/corrotto, nessun cammino).
class DataLoaderError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Legge un file .bz2 riga per riga e restituisce un cammino AS alla volta.
//
// Uso tipico:
//     DataLoader loader("dataset.bz2");
//     ASPath path;
//     while (loader.nextPath(path)) {
//         // ... passa 'path' al modulo ASGraph ...
//     }
//
// In memoria c'e' sempre e solo un buffer di 64 KiB di dati decompressi
// piu' la riga corrente: l'uso di memoria non dipende dalla dimensione del file.
class DataLoader {
public:
    // filename    : file .bz2 da leggere (sono supportati anche file con
    //               piu' stream .bz2 concatenati).
    // pathBlock   : indice (da 0) del blocco della riga, separato da spazi o
    //               tab, che contiene il cammino AS. Nel formato del dataset
    //                 routeviews/routeviews|5 1239|6113|8063|3464 199.88.21.0/24 i 144.228.240.93
    //               i blocchi sono: [0] "routeviews/routeviews|5",
    //               [1] "1239|6113|8063|3464" (il cammino, default),
    //               [2] il prefisso, [3] "i", [4] l'indirizzo IP.
    // asSeparator : carattere che separa gli AS dentro il blocco ('|' di default).
    //
    // Lancia DataLoaderError se il file non si apre o e' vuoto.
    explicit DataLoader(const std::string& filename,
                        std::size_t pathBlock = 1,
                        char asSeparator = '|');
    ~DataLoader();

    DataLoader(const DataLoader&) = delete;
    DataLoader& operator=(const DataLoader&) = delete;

    // Legge la prossima riga valida e ne scrive il cammino in 'path'
    // (il contenuto precedente viene sostituito).
    // Ritorna false a fine file.
    // Lancia DataLoaderError se il file e' corrotto oppure se e' finito senza
    // che sia stato trovato nemmeno un cammino AS valido.
    bool nextPath(ASPath& path);

    // Numero di cammini restituiti finora.
    std::size_t pathsRead() const noexcept { return pathsRead_; }

    // Numero di righe non vuote scartate perche' non contengono un cammino valido.
    std::size_t linesSkipped() const noexcept { return linesSkipped_; }

private:
    bool fillBuffer();
    bool readLine(std::string& line);
    bool parseLine(const std::string& line, ASPath& path) const;
    void closeBz() noexcept;

    std::string filename_;
    std::size_t pathBlock_;
    char asSeparator_;

    std::FILE* file_ = nullptr;
    BZFILE* bz_ = nullptr;
    bool finished_ = false;

    std::vector<char> buffer_;
    std::size_t bufPos_ = 0;
    std::size_t bufLen_ = 0;
    std::string line_;

    std::size_t pathsRead_ = 0;
    std::size_t linesSkipped_ = 0;
};

#endif  // DATALOADER_H