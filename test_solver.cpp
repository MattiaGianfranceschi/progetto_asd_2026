// Test del modulo Solver (versione 1 e versione 2 del conteggio).
// Compilazione:
//   g++ -std=c++17 -O2 -Wall -Wextra DataLoader.cpp ASGraph.cpp Solver.cpp test_solver.cpp -lbz2 -o test_solver

#include "Solver.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <set>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "FALLITO (riga " << __LINE__ << "): " #cond "\n";   \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

struct TestEdge {
    ASN a, b;
    unsigned weight;
};

// Costruisce un grafo con i pesi voluti: il peso di un arco e' il numero di
// volte che compare nei cammini, quindi lo si inserisce 'weight' volte.
static void buildGraph(ASGraph& g, const std::vector<TestEdge>& edges) {
    for (const TestEdge& e : edges) {
        for (unsigned k = 0; k < e.weight; ++k) g.addPath(ASPath{e.a, e.b});
    }
}

// Verifica che 'path' sia un cammino semplice source->target del grafo il cui
// peso (massimo degli archi) vale 'expectedWeight'.
static bool validPath(const ASGraph& g, const std::vector<ASN>& path, ASN source, ASN target,
                      Weight expectedWeight) {
    if (path.size() < 2 || path.front() != source || path.back() != target) return false;
    if (std::set<ASN>(path.begin(), path.end()).size() != path.size()) return false;
    Weight maxWeight = 0;
    for (std::size_t i = 1; i < path.size(); ++i) {
        NodeId u = 0, v = 0;
        Weight w = 0;
        if (!g.findNode(path[i - 1], u) || !g.findNode(path[i], v) || !g.edgeWeight(u, v, w)) {
            return false;
        }
        maxWeight = std::max(maxWeight, w);
    }
    return maxWeight == expectedWeight;
}

static void writeBz2(const std::string& file, const std::string& content) {
    std::FILE* f = std::fopen(file.c_str(), "wb");
    int err = BZ_OK;
    BZFILE* b = BZ2_bzWriteOpen(&err, f, 9, 0, 0);
    BZ2_bzWrite(&err, b, const_cast<char*>(content.data()), static_cast<int>(content.size()));
    BZ2_bzWriteClose(&err, b, 0, nullptr, nullptr);
    std::fclose(f);
}

// Catena di k "rombi": u_i -> (a_i | b_i) -> u_{i+1}. Il primo arco di ogni
// rombo e' leggero (peso 1), il secondo e' pesante e cresce lungo la catena
// (peso i+2): la distanza minimax di u_i e' i+1 e a_i, b_i vengono sempre
// estratti prima di u_{i+1}. Ci sono esattamente 2^k cammini ottimali da u_0 a
// u_k e il conteggio della versione 1 e' esatto. Peso minimax u_0 -> u_k = k+1.
static void buildDiamondChain(ASGraph& g, int k) {
    std::vector<TestEdge> edges;
    for (int i = 0; i < k; ++i) {
        const ASN u = 1000 + i, next = 1000 + i + 1, a = 2000 + i, b = 3000 + i;
        const unsigned heavy = static_cast<unsigned>(i + 2);
        edges.push_back({u, a, 1});
        edges.push_back({a, next, heavy});
        edges.push_back({u, b, 1});
        edges.push_back({b, next, heavy});
    }
    buildGraph(g, edges);
}

// Stessa forma, ma entrambi gli archi del rombo i hanno peso i+1: a_i, b_i e
// u_{i+1} hanno la stessa distanza minimax e sono collegati da archi di peso
// pari a tale distanza. Esistono 2^k cammini ottimali, ma la versione 1 ne
// conta meno perche' u_{i+1} viene estratto prima di b_i.
static void buildPlateauDiamondChain(ASGraph& g, int k) {
    std::vector<TestEdge> edges;
    for (int i = 0; i < k; ++i) {
        const ASN u = 1000 + i, next = 1000 + i + 1, a = 2000 + i, b = 3000 + i;
        const unsigned w = static_cast<unsigned>(i + 1);
        edges.push_back({u, a, w});
        edges.push_back({a, next, w});
        edges.push_back({u, b, w});
        edges.push_back({b, next, w});
    }
    buildGraph(g, edges);
}

// ----------------------------------------------------------------------
// Riferimento a forza bruta (grafi piccoli): enumera tutti i cammini semplici.
// ----------------------------------------------------------------------
struct Brute {
    const ASGraph& g;
    NodeId t;
    std::vector<Weight> best;      // minimax verso ogni nodo
    std::vector<char> reached;
    std::vector<char> onPath;
    std::uint64_t allOptimal = 0;     // cammini semplici s->t con peso == minimax
    std::uint64_t prefixOptimal = 0;  // quelli i cui prefissi sono tutti ottimali
    std::size_t minNodes = 0;         // nodi dei cammini minimax piu' corti
    std::uint64_t minNodesCount = 0;  // quanti sono

    Brute(const ASGraph& graph, NodeId source, NodeId target)
        : g(graph), t(target), best(graph.numNodes(), 0), reached(graph.numNodes(), 0),
          onPath(graph.numNodes(), 0) {
        dfsBest(source, 0);
        if (reached[t]) dfsCount(source, 0, true, 1);
    }

    bool reachable() const { return reached[t] != 0; }
    Weight minimax() const { return best[t]; }

    // C'e' un arco tra due nodi con la stessa distanza minimax e peso <= a tale
    // distanza? (in tal caso l'ordine di estrazione influenza la versione 1)
    bool hasPlateauEdge() const {
        for (NodeId u = 0; u < g.numNodes(); ++u) {
            if (!reached[u]) continue;
            for (const Edge& e : g.neighbors(u)) {
                if (reached[e.to] && best[e.to] == best[u] && e.weight <= best[u]) return true;
            }
        }
        return false;
    }

private:
    void dfsBest(NodeId u, Weight cur) {
        if (!reached[u] || cur < best[u]) {
            reached[u] = 1;
            best[u] = cur;
        }
        onPath[u] = 1;
        for (const Edge& e : g.neighbors(u)) {
            if (!onPath[e.to]) dfsBest(e.to, std::max(cur, e.weight));
        }
        onPath[u] = 0;
    }

    void dfsCount(NodeId u, Weight cur, bool prefixOk, std::size_t nodes) {
        if (u == t) {
            if (cur == best[t]) {
                ++allOptimal;
                if (prefixOk) ++prefixOptimal;
                if (minNodesCount == 0 || nodes < minNodes) {
                    minNodes = nodes;
                    minNodesCount = 1;
                } else if (nodes == minNodes) {
                    ++minNodesCount;
                }
            }
            return;
        }
        onPath[u] = 1;
        for (const Edge& e : g.neighbors(u)) {
            if (onPath[e.to]) continue;
            const Weight next = std::max(cur, e.weight);
            dfsCount(e.to, next, prefixOk && next == best[e.to], nodes + 1);
        }
        onPath[u] = 0;
    }
};

int main() {
    const CountVersion versions[] = {CountVersion::Classic, CountVersion::MinLength,
                                     CountVersion::Both};

    // 1. Casi limite sull'input (uguali per tutte le versioni).
    {
        ASGraph empty;
        Solver solver(empty);
        for (CountVersion v : versions) {
            CHECK(solver.solve(1, 2, v).status == SolveStatus::EmptyGraph);
            CHECK(solver.solve(0, 2, v).status == SolveStatus::InvalidASN);  // validazione prima
            CHECK(!solver.solve(1, 2, v).ok());
        }

        ASGraph g;
        g.addPath(ASPath{1, 2, 3});
        g.addPath(ASPath{10, 11});
        g.addPath(ASPath{5});  // nodo isolato
        Solver s(g);
        for (CountVersion v : versions) {
            CHECK(s.solve(1, 0, v).status == SolveStatus::InvalidASN);
            CHECK(s.solve(0, 0, v).status == SolveStatus::InvalidASN);
            CHECK(s.solve(1, 99, v).status == SolveStatus::NodeNotFound);
            CHECK(s.solve(99, 1, v).status == SolveStatus::NodeNotFound);
            CHECK(s.solve(99, 99, v).status == SolveStatus::NodeNotFound);

            // Componenti connesse separate: nessun percorso (in entrambi i versi).
            CHECK(s.solve(1, 10, v).status == SolveStatus::NoPath);
            CHECK(s.solve(11, 3, v).status == SolveStatus::NoPath);
            CHECK(s.solve(1, 5, v).status == SolveStatus::NoPath);
            MinimaxResult none = s.solve(1, 10, v);
            CHECK(none.path.empty() && none.minLengthPath.empty());
            CHECK(!none.classicComputed && !none.minLengthComputed);
        }

        // Sorgente e destinazione coincidenti: costo 0 e un cammino (un nodo).
        for (ASN a : {1u, 2u, 5u, 11u}) {
            MinimaxResult r = s.solve(a, a);
            CHECK(r.ok() && r.weight == 0);
            CHECK(r.path == std::vector<ASN>{a});
            CHECK(r.classicComputed && r.classicCount == 1 && !r.classicCountSaturated);
            CHECK(r.minLengthComputed && r.minLength == 1 && r.minLengthCount == 1);
            CHECK(r.minLengthPath == std::vector<ASN>{a});
        }

        // Caso normale.
        MinimaxResult r = s.solve(1, 3);
        CHECK(r.ok() && r.weight == 1);
        CHECK(r.classicCount == 1 && r.minLengthCount == 1 && r.minLength == 3);
        CHECK((r.path == std::vector<ASN>{1, 2, 3}));
        CHECK((r.minLengthPath == std::vector<ASN>{1, 2, 3}));
        CHECK(r.source == 1 && r.target == 3);
    }

    // 2. Esempi costruiti a mano (versione 1 e versione 2).
    {
        // (a) Rombo con pesi uguali: due cammini ottimali di 3 nodi.
        ASGraph g;
        buildGraph(g, {{1, 2, 1}, {1, 3, 1}, {2, 4, 2}, {3, 4, 2}});
        MinimaxResult r = Solver(g).solve(1, 4);
        CHECK(r.ok() && r.weight == 2);
        CHECK(r.classicCount == 2);
        CHECK(r.minLength == 3 && r.minLengthCount == 2);
        CHECK(validPath(g, r.path, 1, 4, 2));
        CHECK(validPath(g, r.minLengthPath, 1, 4, 2) && r.minLengthPath.size() == r.minLength);
    }
    {
        // (b) Conta il massimo, non la lunghezza: il cammino lungo ma leggero
        //     batte l'arco diretto pesante (che la BFS esclude: peso 5 > 1).
        ASGraph g;
        buildGraph(g, {{1, 4, 5}, {1, 2, 1}, {2, 3, 1}, {3, 4, 1}});
        MinimaxResult r = Solver(g).solve(1, 4);
        CHECK(r.ok() && r.weight == 1 && r.classicCount == 1);
        CHECK(r.minLength == 4 && r.minLengthCount == 1);
        CHECK((r.path == std::vector<ASN>{1, 2, 3, 4}));
        CHECK((r.minLengthPath == std::vector<ASN>{1, 2, 3, 4}));
    }
    {
        // (c) Il cammino con l'arco pesante (9) perde contro quello con pesi medi.
        ASGraph g;
        buildGraph(g, {{1, 2, 1}, {2, 4, 9}, {1, 3, 4}, {3, 4, 4}});
        MinimaxResult r = Solver(g).solve(1, 4);
        CHECK(r.ok() && r.weight == 4 && r.classicCount == 1);
        CHECK(r.minLength == 3 && r.minLengthCount == 1);
        CHECK((r.path == std::vector<ASN>{1, 3, 4}));
    }
    {
        // (d) Limite della versione 1: 1-2-4-5 e 1-3-4-5 hanno entrambi peso 10,
        //     ma il secondo ha un prefisso non ottimale (verso 4 il minimax e' 3,
        //     non 4) e la versione 1 non lo conta. La versione 2 li conta entrambi.
        ASGraph g;
        buildGraph(g, {{1, 2, 1}, {1, 3, 2}, {2, 4, 3}, {3, 4, 4}, {4, 5, 10}});
        MinimaxResult r = Solver(g).solve(1, 5);
        CHECK(r.ok() && r.weight == 10);
        CHECK(r.classicCount == 1);
        CHECK(r.minLength == 4 && r.minLengthCount == 2);
        CHECK(validPath(g, r.path, 1, 5, 10));
        CHECK(validPath(g, r.minLengthPath, 1, 5, 10));
        NodeId s = 0, t = 0;
        CHECK(g.findNode(1, s) && g.findNode(5, t));
        Brute brute(g, s, t);
        CHECK(brute.allOptimal == 2 && brute.prefixOptimal == 1);
        CHECK(brute.minNodes == 4 && brute.minNodesCount == 2);
    }

    // 3. Selezione della versione: cosa viene calcolato e cosa no.
    {
        ASGraph g;
        buildGraph(g, {{1, 2, 1}, {1, 3, 1}, {2, 4, 2}, {3, 4, 2}});
        Solver solver(g);

        MinimaxResult c = solver.solve(1, 4, CountVersion::Classic);
        CHECK(c.ok() && c.weight == 2 && validPath(g, c.path, 1, 4, 2));
        CHECK(c.classicComputed && c.classicCount == 2);
        CHECK(!c.minLengthComputed && c.minLengthPath.empty() && c.minLengthCount == 0);
        CHECK(c.bfsNodesVisited == 0 && c.bfsEdgesExamined == 0);

        MinimaxResult m = solver.solve(1, 4, CountVersion::MinLength);
        CHECK(m.ok() && m.weight == 2 && validPath(g, m.path, 1, 4, 2));
        CHECK(!m.classicComputed && m.classicCount == 0);
        CHECK(m.minLengthComputed && m.minLengthCount == 2 && m.minLength == 3);
        CHECK(m.bfsNodesVisited > 0 && m.bfsEdgesExamined > 0);

        MinimaxResult b = solver.solve(1, 4, CountVersion::Both);
        CHECK(b.classicComputed && b.minLengthComputed);
        CHECK(b.classicCount == 2 && b.minLengthCount == 2);
        CHECK(b.nodesSettled > 0 && b.edgesExamined > 0);
    }

    // 4. Integrazione con DataLoader e ASGraph (formato reale del dataset).
    {
        const std::string tmp = "test_solver.bz2";
        writeBz2(tmp,
                 "routeviews/routeviews|5 1239|6113|8063|3464 199.88.21.0/24 i 144.228.240.93\n"
                 "routeviews/routeviews|5 3464|8063|1239 10.0.0.0/8 i 1.1.1.1\n");
        DataLoader loader(tmp);
        ASGraph g;
        ASPath p;
        while (loader.nextPath(p)) g.addPath(p);

        Solver solver(g);
        MinimaxResult r = solver.solve(1239, 3464);
        // Archi: 1239-6113 (1), 6113-8063 (1), 8063-3464 (2), 1239-8063 (1).
        CHECK(r.ok() && r.weight == 2);
        CHECK(r.classicCount == 2);                       // 1239-8063-3464 e 1239-6113-8063-3464
        CHECK(r.minLength == 3 && r.minLengthCount == 1);  // solo 1239-8063-3464
        CHECK((r.minLengthPath == std::vector<ASN>{1239, 8063, 3464}));
        CHECK(validPath(g, r.path, 1239, 3464, 2));
        CHECK(solver.solve(1239, 6113).weight == 1);
        CHECK(r.nodesSettled > 0 && r.edgesExamined > 0);
        std::remove(tmp.c_str());
    }

    // 5. Overflow del numero di cammini: 2^k cammini ottimali (entrambe le versioni).
    {
        ASGraph g10;
        buildDiamondChain(g10, 10);
        MinimaxResult r10 = Solver(g10).solve(1000, 1010);
        CHECK(r10.ok() && r10.weight == 11);
        CHECK(r10.classicCount == 1024 && !r10.classicCountSaturated);
        CHECK(r10.minLengthCount == 1024 && !r10.minLengthCountSaturated && r10.minLength == 21);
        CHECK(r10.path.size() == 21);

        ASGraph g63;
        buildDiamondChain(g63, 63);
        MinimaxResult r63 = Solver(g63).solve(1000, 1063);
        CHECK(r63.ok());
        CHECK(r63.classicCount == (std::uint64_t{1} << 63) && !r63.classicCountSaturated);
        CHECK(r63.minLengthCount == (std::uint64_t{1} << 63) && !r63.minLengthCountSaturated);

        ASGraph g64;
        buildDiamondChain(g64, 64);
        MinimaxResult r64 = Solver(g64).solve(1000, 1064);  // 2^64: non rappresentabile
        CHECK(r64.ok() && r64.weight == 65);
        CHECK(r64.classicCountSaturated && r64.classicCount == UINT64_MAX);
        CHECK(r64.minLengthCountSaturated && r64.minLengthCount == UINT64_MAX);

        ASGraph g70;
        buildDiamondChain(g70, 70);
        MinimaxResult r70 = Solver(g70).solve(1000, 1070);
        CHECK(r70.classicCountSaturated && r70.classicCount == UINT64_MAX);
        CHECK(r70.minLengthCountSaturated && r70.minLengthCount == UINT64_MAX);
        CHECK(validPath(g70, r70.path, 1000, 1070, 71));
        CHECK(validPath(g70, r70.minLengthPath, 1000, 1070, 71));

        // Una destinazione prima del punto in cui il contatore satura non e' interessata.
        MinimaxResult early = Solver(g70).solve(1000, 1005);
        CHECK(early.ok() && early.weight == 6);
        CHECK(early.classicCount == 32 && !early.classicCountSaturated);
        CHECK(early.minLengthCount == 32 && !early.minLengthCountSaturated);
        CHECK(early.minLength == 11);
    }

    // 6. Limite noto della versione 1 con nodi a parita' di distanza minimax;
    //    la versione 2 e' esatta anche in questo caso.
    {
        ASGraph g;
        buildPlateauDiamondChain(g, 3);
        MinimaxResult r = Solver(g).solve(1000, 1003);
        NodeId s = 0, t = 0;
        CHECK(g.findNode(1000, s) && g.findNode(1003, t));
        Brute brute(g, s, t);
        CHECK(r.ok() && r.weight == 3 && validPath(g, r.path, 1000, 1003, 3));
        CHECK(brute.allOptimal == 8 && brute.prefixOptimal == 8);
        CHECK(r.classicCount >= 1 && r.classicCount <= brute.prefixOptimal);
        CHECK(r.minLength == 7 && r.minLengthCount == 8);
        std::cout << "[limite] catena a parita' di distanza, k=3: cammini ottimali reali = "
                  << brute.allOptimal << ", versione 1 = " << r.classicCount
                  << ", versione 2 = " << r.minLengthCount << "\n";
    }

    // 7. Confronto con la forza bruta su grafi casuali piccoli.
    {
        unsigned long long seed = 20261005ULL;
        auto rnd = [&seed]() {
            seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<unsigned>(seed >> 33);
        };

        int pairsChecked = 0, exactChecked = 0, plateauChecked = 0, unreachable = 0;
        int classicBelow = 0, minLengthBelow = 0, reachablePairs = 0;
        const unsigned maxWeights[] = {3, 8, 40};

        for (int iter = 0; iter < 600; ++iter) {
            const unsigned n = 2 + rnd() % 7;                  // 2..8 nodi
            const unsigned maxW = maxWeights[rnd() % 3];
            const unsigned density = 20 + rnd() % 50;          // % di archi presenti

            ASGraph g;
            for (unsigned i = 0; i < n; ++i) g.addPath(ASPath{100 + i});  // tutti i nodi
            for (unsigned i = 0; i < n; ++i) {
                for (unsigned j = i + 1; j < n; ++j) {
                    if (rnd() % 100 < density) {
                        buildGraph(g, {{100 + i, 100 + j, 1 + rnd() % maxW}});
                    }
                }
            }

            Solver solver(g);
            for (unsigned i = 0; i < n; ++i) {
                for (unsigned j = 0; j < n; ++j) {
                    if (i == j) continue;
                    const ASN a = 100 + i, b = 100 + j;
                    NodeId s = 0, t = 0;
                    g.findNode(a, s);
                    g.findNode(b, t);

                    Brute brute(g, s, t);
                    MinimaxResult r = solver.solve(a, b);
                    ++pairsChecked;

                    if (!brute.reachable()) {
                        CHECK(r.status == SolveStatus::NoPath);
                        ++unreachable;
                        continue;
                    }
                    ++reachablePairs;

                    CHECK(r.ok());
                    CHECK(r.weight == brute.minimax());
                    CHECK(validPath(g, r.path, a, b, brute.minimax()));

                    // Versione 1: limite inferiore, esatto senza parita' di distanza.
                    CHECK(r.classicComputed && !r.classicCountSaturated);
                    CHECK(r.classicCount >= 1);
                    CHECK(r.classicCount <= brute.prefixOptimal);
                    CHECK(brute.prefixOptimal <= brute.allOptimal);
                    if (!brute.hasPlateauEdge()) {
                        CHECK(r.classicCount == brute.prefixOptimal);
                        ++exactChecked;
                    } else {
                        ++plateauChecked;
                    }

                    // Versione 2: sempre esatta.
                    CHECK(r.minLengthComputed && !r.minLengthCountSaturated);
                    CHECK(r.minLength == brute.minNodes);
                    CHECK(r.minLengthCount == brute.minNodesCount);
                    CHECK(r.minLengthPath.size() == r.minLength);
                    CHECK(validPath(g, r.minLengthPath, a, b, brute.minimax()));

                    if (r.classicCount < brute.allOptimal) ++classicBelow;
                    if (r.minLengthCount < brute.allOptimal) ++minLengthBelow;
                }
            }
        }
        CHECK(exactChecked > 500);
        CHECK(plateauChecked > 500);
        CHECK(unreachable > 50);
        std::cout << "[casuali] coppie: " << pairsChecked << " (non connesse: " << unreachable
                  << "), versione 1 esatta senza parita' di distanza verificata su " << exactChecked
                  << " coppie, con parita' di distanza: " << plateauChecked << "\n";
        std::cout << "[casuali] su " << reachablePairs << " coppie connesse, rispetto a TUTTI i cammini "
                  << "minimax: versione 1 inferiore in " << classicBelow << ", versione 2 inferiore in "
                  << minLengthBelow << "\n";
    }

    if (failures == 0) std::cout << "Tutti i test sono passati.\n";
    return failures == 0 ? 0 : 1;
}