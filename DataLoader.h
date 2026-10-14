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