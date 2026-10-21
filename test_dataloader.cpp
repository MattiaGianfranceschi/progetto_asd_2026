// Test del modulo DataLoader.
// Compilazione: g++ -std=c++17 -O2 -Wall -Wextra DataLoader.cpp test_dataloader.cpp -lbz2 -o test_dataloader

#include "DataLoader.h"

#include <cstdio>
#include <iostream>
#include <string>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "FALLITO (riga " << __LINE__ << "): " #cond "\n";   \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

// Scrive (o accoda) 'content' come stream .bz2 autonomo.
static void writeBz2(const std::string& file, const std::string& content, bool append = false) {
    std::FILE* f = std::fopen(file.c_str(), append ? "ab" : "wb");
    int err = BZ_OK;
    BZFILE* b = BZ2_bzWriteOpen(&err, f, 9, 0, 0);
    BZ2_bzWrite(&err, b, const_cast<char*>(content.data()), static_cast<int>(content.size()));
    BZ2_bzWriteClose(&err, b, 0, nullptr, nullptr);
    std::fclose(f);
}

static void writeRaw(const std::string& file, const std::string& content) {
    std::FILE* f = std::fopen(file.c_str(), "wb");
    std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
}

int main() {
    const std::string tmp = "test_dl.bz2";

    // 1. Riga reale del dataset + casi misti: duplicati, AS_SET, testo, overflow,
    //    CRLF, riga vuota, ultima riga senza '\n' con il cammino come ultimo blocco.
    {
        writeBz2(tmp,
                 "routeviews/routeviews|5 1239|6113|8063|3464 199.88.21.0/24 i 144.228.240.93\n"
                 "routeviews/routeviews|5 1239|1239|6113|6113|3464 10.0.0.0/8 i 1.1.1.1\n"
                 "routeviews/routeviews|5 1239|{6113,8063}|3464 11.0.0.0/8 i 1.1.1.1\n"
                 "routeviews/routeviews|5 abc|123 12.0.0.0/8 i 1.1.1.1\n"
                 "riga_corta\n"
                 "\n"
                 "routeviews/routeviews|5 4200000000|7 13.0.0.0/8 i 1.1.1.1\n"
                 "routeviews/routeviews|5 99999999999|7 15.0.0.0/8 i 1.1.1.1\r\n"
                 "routeviews/routeviews|5 5|6 14.0.0.0/8 i 1.1.1.1\r\n"
                 "routeviews/routeviews|5 8|9");
        DataLoader dl(tmp);
        ASPath p;
        CHECK(dl.nextPath(p) && p == (ASPath{1239, 6113, 8063, 3464}));
        CHECK(dl.nextPath(p) && p == (ASPath{1239, 6113, 3464}));
        CHECK(dl.nextPath(p) && p == (ASPath{4200000000u, 7}));
        CHECK(dl.nextPath(p) && p == (ASPath{5, 6}));
        CHECK(dl.nextPath(p) && p == (ASPath{8, 9}));
        CHECK(!dl.nextPath(p));
        CHECK(!dl.nextPath(p));  // chiamate successive: sempre false
        CHECK(dl.pathsRead() == 5);
        CHECK(dl.linesSkipped() == 4);
    }

    // 2. File grande: le righe attraversano molte volte i confini del buffer.
    {
        std::string big;
        const int N = 100000;
        for (int i = 0; i < N; ++i) {
            big += "routeviews/routeviews|5 1|2|2|3|" + std::to_string(i + 1000) +
                   " 10.0.0.0/8 i 1.2.3.4\n";
        }
        writeBz2(tmp, big);
        DataLoader dl(tmp);
        ASPath p;
        int count = 0;
        bool ok = true;
        while (dl.nextPath(p)) {
            ok = ok && p == (ASPath{1, 2, 3, static_cast<ASN>(count + 1000)});
            ++count;
        }
        CHECK(ok);
        CHECK(count == N);
        CHECK(dl.linesSkipped() == 0);
    }

    // 3. Piu' stream .bz2 concatenati.
    {
        writeBz2(tmp, "a|1 1|2 x i y\n");
        writeBz2(tmp, "a|1 3|3|4 x i y\n", true);
        DataLoader dl(tmp);
        ASPath p;
        CHECK(dl.nextPath(p) && p == (ASPath{1, 2}));
        CHECK(dl.nextPath(p) && p == (ASPath{3, 4}));
        CHECK(!dl.nextPath(p));
    }

    // 4. Blocco del cammino e separatore configurabili.
    {
        writeBz2(tmp, "10;20;20;30 x\n");
        DataLoader dl(tmp, 0, ';');
        ASPath p;
        CHECK(dl.nextPath(p) && p == (ASPath{10, 20, 30}));
    }

    // 5. Errori.
    {
        bool thrown = false;

        writeRaw(tmp, "");  // file vuoto
        try { DataLoader dl(tmp); } catch (const DataLoaderError&) { thrown = true; }
        CHECK(thrown);

        thrown = false;
        writeBz2(tmp, "solo testo\ninutile e basta\n");  // nessun cammino AS
        try { DataLoader dl(tmp); ASPath p; dl.nextPath(p); } catch (const DataLoaderError&) { thrown = true; }
        CHECK(thrown);

        thrown = false;
        writeRaw(tmp, "questo non e' un file bz2\n");  // formato errato
        try { DataLoader dl(tmp); ASPath p; dl.nextPath(p); } catch (const DataLoaderError&) { thrown = true; }
        CHECK(thrown);

        thrown = false;
        std::string message;
        try { DataLoader dl("file_che_non_esiste.bz2"); } catch (const DataLoaderError& e) { thrown = true; message = e.what(); }
        CHECK(thrown);
        CHECK(message.find("file_che_non_esiste.bz2") != std::string::npos);
        CHECK(message.find("No such file") != std::string::npos);  // causa di sistema nel messaggio

        // File .bz2 troncato: l'errore emerge durante la lettura.
        {
            std::string big;
            for (int i = 0; i < 20000; ++i) big += "x|5 1|2|" + std::to_string(i + 10) + " 1.0.0.0/8 i 1.1.1.1\n";
            writeBz2(tmp, big);
            std::FILE* f = std::fopen(tmp.c_str(), "rb");
            std::string bytes;
            char buf[4096];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) bytes.append(buf, n);
            std::fclose(f);

            writeRaw(tmp, bytes.substr(0, bytes.size() / 2));  // meta' del file
            thrown = false;
            message.clear();
            try {
                DataLoader dl(tmp);
                ASPath p;
                while (dl.nextPath(p)) {}
            } catch (const DataLoaderError& e) { thrown = true; message = e.what(); }
            CHECK(thrown);
            CHECK(message.find("troncato") != std::string::npos);

            // File corrotto: byte alterati nel mezzo dei dati compressi.
            std::string damaged = bytes;
            for (std::size_t i = damaged.size() / 3; i < damaged.size() / 3 + 64; ++i) damaged[i] = static_cast<char>(damaged[i] ^ 0x5A);
            writeRaw(tmp, damaged);
            thrown = false;
            try {
                DataLoader dl(tmp);
                ASPath p;
                while (dl.nextPath(p)) {}
            } catch (const DataLoaderError&) { thrown = true; }
            CHECK(thrown);
        }
    }

    std::remove(tmp.c_str());
    if (failures == 0) std::cout << "Tutti i test sono passati.\n";
    return failures == 0 ? 0 : 1;
}