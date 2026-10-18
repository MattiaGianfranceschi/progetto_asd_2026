#ifndef SOLVER_H
#define SOLVER_H

#include "ASGraph.h"  // per ASGraph, ASN, NodeId, Weight

#include <cstddef>
#include <cstdint>
#include <vector>

// Esito di una richiesta al Solver.
enum class SolveStatus {
    Ok,            // cammino trovato (anche per sorgente == destinazione)
    NoPath,        // nodi validi ma in componenti connesse separate
    InvalidASN,    // almeno uno dei due ASN e' 0 (riservato, mai valido)
    EmptyGraph,    // il grafo non contiene nodi
    NodeNotFound   // almeno uno dei due ASN non e' presente nel grafo
};

// Quale conteggio dei cammini ottimali calcolare.
enum class CountVersion {
    Classic,    // versione 1: conteggio nel rilassamento (limite inferiore)
    MinLength,  // versione 2: cammini minimax con il minor numero di nodi (esatto)
    Both        // entrambe le versioni
};

// Risultato di una ricerca minimax tra due AS (output per il modulo di analisi).
struct MinimaxResult {
    SolveStatus status = SolveStatus::NoPath;

    ASN source = 0;
    ASN target = 0;

    // Peso del cammino minimax: il minimo, tra tutti i cammini source->target,
    // del massimo peso degli archi attraversati. Comune alle due versioni.
    // Valido solo se status == Ok.
    Weight weight = 0;

    // Sequenza degli ASN di un cammino minimax trovato da Dijkstra (tramite i
    // padri), da source a target, estremi inclusi. Vuota se status != Ok.
    std::vector<ASN> path;

    // --- Versione 1: conteggio classico parallelo alla ricerca -------------
    bool classicComputed = false;
    // Limite inferiore del numero di cammini minimax (vedi Solver::solve).
    std::uint64_t classicCount = 0;
    // true se il conteggio ha superato 2^64-1: classicCount vale 2^64-1 ed e'
    // solo un limite inferiore del valore calcolato.
    bool classicCountSaturated = false;

    // --- Versione 2: cammini minimax con lunghezza minore -------------------
    bool minLengthComputed = false;
    // Numero di nodi attraversati dai cammini minimax piu' corti (estremi inclusi).
    std::size_t minLength = 0;
    // Numero ESATTO di cammini minimax con quel numero di nodi.
    std::uint64_t minLengthCount = 0;
    bool minLengthCountSaturated = false;
    // Uno dei cammini minimax piu' corti, da source a target.
    std::vector<ASN> minLengthPath;

    // --- Statistiche della ricerca, utili per l'analisi sperimentale --------
    std::size_t nodesSettled = 0;       // Dijkstra: nodi estratti dalla coda
    std::size_t edgesExamined = 0;      // Dijkstra: archi scorsi
    std::size_t bfsNodesVisited = 0;    // versione 2: nodi estratti dalla BFS
    std::size_t bfsEdgesExamined = 0;   // versione 2: archi scorsi dalla BFS

    bool ok() const noexcept { return status == SolveStatus::Ok; }
};

// Cammino minimax tra due AS con Dijkstra modificato.
//
// Il peso di un cammino e' il MASSIMO peso dei suoi archi (non la somma): si
// cerca il cammino di peso minimo. Il Solver mantiene un riferimento al grafo,
// che deve restare in vita (e non venire modificato) mentre lo si usa.
//
// Uso tipico:
//     Solver solver(graph);
//     MinimaxResult r = solver.solve(1239, 3464);              // entrambe le versioni
//     MinimaxResult c = solver.solve(1239, 3464, CountVersion::Classic);
//     if (r.ok()) { /* r.weight, r.classicCount, r.minLengthCount, ... */ }
class Solver {
public:
    explicit Solver(const ASGraph& graph) : graph_(graph) {}

    // Calcola il peso minimax tra due AS e il numero di cammini ottimali.
    // Costo: O((V + E) log V) per Dijkstra (la ricerca si ferma appena la
    // destinazione viene estratta dalla coda) piu' O(V + E) per la BFS della
    // versione 2, che parte solo se richiesta.
    //
    // 'version' sceglie quali conteggi calcolare:
    //  - Classic   : Dijkstra con conteggio nel rilassamento;
    //  - MinLength : Dijkstra senza conteggio (serve solo il peso) + BFS;
    //  - Both      : Dijkstra con conteggio + BFS.
    //
    // Ordine dei controlli: ASN 0 -> InvalidASN; grafo senza nodi ->
    // EmptyGraph; ASN assente -> NodeNotFound; source == target -> Ok con
    // peso 0, un cammino (di un solo nodo) e path = {source}; nessun
    // collegamento -> NoPath.
    //
    // VERSIONE 1 (classicCount). Quando un arco u-v da' a v la stessa
    // distanza minimax gia' nota, il contatore di v aumenta di quello di u. Il
    // risultato e' il numero di cammini source->target in cui ogni nodo e'
    // raggiunto da un nodo estratto dalla coda prima di lui e, a ogni passo, il
    // peso del prefisso e' il minimax verso quel nodo. Quindi:
    //  - ogni cammino contato e' davvero un cammino semplice di peso minimax;
    //  - con il massimo (a differenza della somma) un cammino ottimale puo'
    //    avere prefissi non ottimali, perche' l'arco piu' pesante "copre" i
    //    pesi precedenti: tali cammini NON vengono contati;
    //  - tra nodi con la stessa distanza minimax collegati da un arco di
    //    peso <= a tale distanza, l'ordine di estrazione (a parita' di
    //    distanza, indice del nodo) decide quali cammini si contano.
    //  Il valore e' deterministico ma e' solo un limite inferiore del numero
    //  di tutti i cammini semplici di peso minimax.
    //
    // VERSIONE 2 (minLength, minLengthCount). Dopo aver trovato il peso minimax
    // W, si fa una BFS sul sottografo con i soli archi di peso <= W (ogni
    // cammino che lo attraversa ha peso esattamente W, perche' nessuno puo'
    // avere peso minore) e si contano i cammini con il minor numero di nodi.
    // Il valore e' esatto e non dipende dall'ordine di estrazione, ma riguarda
    // solo i cammini minimax piu' corti, non tutti.
    MinimaxResult solve(ASN source, ASN target,
                        CountVersion version = CountVersion::Both) const;

private:
    // Versione 2: BFS limitata agli archi di peso <= limit; completa i campi
    // minLength* del risultato.
    void countMinLength(NodeId source, NodeId target, Weight limit,
                        MinimaxResult& result) const;

    const ASGraph& graph_;
};

#endif  // SOLVER_H