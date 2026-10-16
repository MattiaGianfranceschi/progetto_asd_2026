#ifndef ASGRAPH_H
#define ASGRAPH_H

#include "DataLoader.h"  // per i tipi ASN e ASPath

#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

// Indice interno di un nodo. Gli ASN vengono rimappati in interi consecutivi
// 0..n-1, nell'ordine di prima apparizione.
using NodeId = std::uint32_t;

// Frequenza (peso) di un arco: 64 bit, il contatore non puo' andare in
// overflow in pratica (e in ogni caso satura al valore massimo).
using Weight = std::uint64_t;

// Elemento della lista di adiacenza: destinazione e frequenza dell'arco.
struct Edge {
    NodeId to;
    Weight weight;
};

// Una componente connessa del grafo.
struct Component {
    std::vector<NodeId> nodes;  // nodi della componente, in ordine di visita BFS
    std::size_t edgeCount = 0;  // archi interni (ogni arco contato una volta)
    Weight totalWeight = 0;     // somma dei pesi degli archi interni
    ASN minAsn = 0;             // ASN originale numericamente piu' basso tra i suoi nodi
};

// Grafo non orientato e pesato degli Autonomous System.
//
// Esiste un arco tra ASNi e ASNj se e solo se la sequenza ASNi|ASNj (o
// ASNj|ASNi) compare in almeno uno dei cammini inseriti; il peso dell'arco e'
// il numero di volte che compare.
//
// Uso tipico:
//     DataLoader loader("dataset.bz2");
//     ASGraph graph;
//     ASPath path;
//     while (loader.nextPath(path)) graph.addPath(path);
//     // ... graph viene poi passato (const) ai moduli Solver e di analisi ...
//
// Complessita': addPath e' O(lunghezza del cammino) in media (tabelle hash);
// le query sul grafo sono O(1), tranne largestComponent() (O(V+E), poi
// memorizzata finche' il grafo non cambia) e edgeWeightDistribution() (O(D log D)).
class ASGraph {
public:
    ASGraph() = default;

    // ------------------------------------------------------------------
    // Costruzione (aggiornamento dinamico)
    // ------------------------------------------------------------------

    // Inserisce un cammino: crea i nodi mancanti e incrementa di 1 la
    // frequenza di ogni arco tra due AS consecutivi.
    //  - cammino di un solo AS: viene creato il nodo, nessun arco;
    //  - coppie consecutive uguali (self-loop) vengono ignorate;
    //  - cammino vuoto o contenente l'ASN 0 (riservato, mai valido in un AS
    //    path): rifiutato, il grafo non cambia e ritorna false.
    // Ritorna true se il cammino e' stato inserito.
    bool addPath(const ASPath& path);

    // ------------------------------------------------------------------
    // Informazioni generali (modulo di analisi)
    // ------------------------------------------------------------------

    std::size_t numNodes() const noexcept { return adj_.size(); }
    std::size_t numEdges() const noexcept { return numEdges_; }
    std::size_t pathsAdded() const noexcept { return pathsAdded_; }
    std::size_t pathsRejected() const noexcept { return pathsRejected_; }

    // Componente connessa massimale: la piu' numerosa (numero di nodi); a
    // parita' quella con la somma dei pesi degli archi maggiore; a ulteriore
    // parita' di nodi e di somma dei pesi quella che contiene il nodo con
    // l'ASN originale numericamente piu' basso.
    // Per un grafo vuoto ritorna una componente vuota.
    const Component& largestComponent() const;
    std::size_t largestComponentSize() const { return largestComponent().nodes.size(); }

    // true se il nodo appartiene alla componente connessa massimale.
    bool inLargestComponent(NodeId id) const;

    // Distribuzione delle frequenze degli archi: frequenza -> numero di archi
    // che hanno quella frequenza (ordinata per frequenza crescente; le
    // frequenze senza archi non compaiono). Il conteggio e' calcolato e
    // conservato dal grafo, aggiornato a ogni inserimento: la chiamata non
    // scorre il grafo, costa O(D log D) con D = valori distinti di frequenza.
    std::map<Weight, std::size_t> edgeWeightDistribution() const;

    // ------------------------------------------------------------------
    // Accesso al grafo (modulo Solver)
    // ------------------------------------------------------------------

    // Cerca l'indice di un ASN; ritorna false se l'ASN non e' nel grafo.
    bool findNode(ASN asn, NodeId& id) const;

    // ASN corrispondente a un indice. Lancia std::out_of_range se non esiste.
    ASN asnOf(NodeId id) const;

    // Lista di adiacenza di un nodo (destinazione e frequenza di ogni arco).
    // Lancia std::out_of_range se il nodo non esiste.
    const std::vector<Edge>& neighbors(NodeId id) const;

    // Numero di archi incidenti sul nodo. Lancia std::out_of_range se non esiste.
    std::size_t degree(NodeId id) const { return neighbors(id).size(); }

    // Frequenza dell'arco u-v in O(1). Ritorna false se l'arco (o uno dei
    // nodi) non esiste.
    bool edgeWeight(NodeId u, NodeId v, Weight& weight) const;

private:
    // Posizione dell'arco nelle liste di adiacenza dei suoi due estremi.
    // Serve ad aggiornare la frequenza in O(1) invece di scorrere la lista.
    struct EdgeSlots {
        std::uint32_t posInLow;   // posizione in adj_[estremo con indice minore]
        std::uint32_t posInHigh;  // posizione in adj_[estremo con indice maggiore]
    };

    NodeId getOrCreateNode(ASN asn);
    void addEdgeOccurrence(NodeId u, NodeId v);
    void computeLargestComponent() const;

    std::unordered_map<ASN, NodeId> asnToId_;  // ASN -> indice
    std::vector<ASN> idToAsn_;                 // indice -> ASN
    std::vector<std::vector<Edge>> adj_;       // liste di adiacenza
    std::unordered_map<std::uint64_t, EdgeSlots> edgeSlots_;
    std::unordered_map<Weight, std::size_t> weightCounts_;  // frequenza -> numero di archi

    std::size_t numEdges_ = 0;
    std::size_t pathsAdded_ = 0;
    std::size_t pathsRejected_ = 0;

    // Cache della componente massimale, invalidata da ogni inserimento.
    mutable bool cacheValid_ = false;
    mutable Component largest_;
    mutable std::vector<char> inLargest_;
};

#endif  // ASGRAPH_H