// Programma principale: ./ASGraph_Solver [dataset.bz2]
//
// Con il file come argomento il caricamento parte subito; senza argomenti
// General attiva la modalita' interattiva e chiede il percorso del file.

#include "General.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    General general(std::cin, std::cout);

    std::string dataset;
    if (argc >= 2) dataset = argv[1];
    if (argc > 2) {
        std::cerr << "Attenzione: gli argomenti dopo il primo sono ignorati (uso: " << argv[0]
                  << " [dataset.bz2]).\n";
    }
    return general.run(dataset);
}