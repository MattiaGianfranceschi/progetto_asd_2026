// Test del modulo ASGraph.
// Compilazione:
//   g++ -std=c++17 -O2 -Wall -Wextra DataLoader.cpp ASGraph.cpp test_asgraph.cpp -lbz2 -o test_asgraph

#include "ASGraph.h"

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "FALLITO (riga " << __LINE__ << "): " #cond "\n";   \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static_assert(sizeof(Weight) == 8, "le frequenze devono essere a 64 bit");

static std::set<ASN> asnsOf(const ASGraph& g, const Component& c) {
    std::set<ASN> result;
    for (NodeId id : c.nodes) result.insert(g.asnOf(id));
    return result;
}

static bool weightOf(const ASGraph& g, ASN a, ASN b, Weight& w) {
    NodeId u = 0, v = 0;
    return g.findNode(a, u) && g.findNode(b, v) && g.edgeWeight(u, v, w);
}

static void writeBz2(const std::string& file, const std::string& content) {
    std::FILE* f = std::fopen(file.c_str(), "wb");
    int err = BZ_OK;
    BZFILE* b = BZ2_bzWriteOpen(&err, f, 9, 0, 0);
    BZ2_bzWrite(&err, b, const_cast<char*>(content.data()), static_cast<int>(content.size()));
    BZ2_bzWriteClose(&err, b, 0, nullptr, nullptr);
    std::fclose(f);
}

// Confronto con un'implementazione di riferimento (std::map + union-find) su
// cammini casuali: pesi degli archi, gradi, distribuzione e componente massimale.
static void randomTest(unsigned asnRange, int numPaths, unsigned maxLen) {
    ASGraph g;
    std::map<std::pair<ASN, ASN>, Weight> ref;
    std::set<ASN> seen;

    unsigned long long seed = 987654321ULL + asnRange;
    auto rnd = [&seed]() {
        seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<unsigned>(seed >> 33);
    };

    for (int i = 0; i < numPaths; ++i) {
        ASPath p;
        const unsigned len = 1 + rnd() % maxLen;
        for (unsigned k = 0; k < len; ++k) p.push_back(1 + rnd() % asnRange);
        CHECK(g.addPath(p));
        for (std::size_t k = 0; k < p.size(); ++k) {
            seen.insert(p[k]);
            if (k > 0 && p[k] != p[k - 1]) {
                ++ref[std::make_pair(std::min(p[k], p[k - 1]), std::max(p[k], p[k - 1]))];
            }
        }
    }

    CHECK(g.numNodes() == seen.size());
    CHECK(g.numEdges() == ref.size());

    bool weightsOk = true;
    std::map<Weight, std::size_t> refDistribution;
    for (const auto& kv : ref) {
        Weight w1 = 0, w2 = 0;
        weightsOk = weightsOk && weightOf(g, kv.first.first, kv.first.second, w1) &&
                    weightOf(g, kv.first.second, kv.first.first, w2) &&
                    w1 == kv.second && w2 == kv.second;
        ++refDistribution[kv.second];
    }
    CHECK(weightsOk);
    CHECK(g.edgeWeightDistribution() == refDistribution);

    std::size_t degreeSum = 0;
    for (NodeId id = 0; id < g.numNodes(); ++id) degreeSum += g.degree(id);
    CHECK(degreeSum == 2 * ref.size());

    // Componente massimale di riferimento con union-find.
    std::vector<unsigned> parent(asnRange + 1);
    for (unsigned i = 0; i <= asnRange; ++i) parent[i] = i;
    auto find = [&parent](unsigned x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    for (const auto& kv : ref) parent[find(kv.first.first)] = find(kv.first.second);

    std::map<unsigned, std::pair<std::size_t, Weight>> comps;  // radice -> (nodi, peso)
    for (ASN a : seen) ++comps[find(a)].first;
    for (const auto& kv : ref) comps[find(kv.first.first)].second += kv.second;
    std::pair<std::size_t, Weight> best(0, 0);
    for (const auto& kv : comps) best = std::max(best, kv.second);

    CHECK(g.largestComponent().nodes.size() == best.first);
    CHECK(g.largestComponent().totalWeight == best.second);
}

int main() {
    // 1. Grafo vuoto e input vuoto.
    {
        ASGraph g;
        CHECK(g.numNodes() == 0 && g.numEdges() == 0);
        CHECK(g.largestComponent().nodes.empty());
        CHECK(g.largestComponentSize() == 0);
        CHECK(g.edgeWeightDistribution().empty());
        CHECK(!g.addPath(ASPath{}));
        CHECK(g.pathsRejected() == 1 && g.pathsAdded() == 0);
        CHECK(g.numNodes() == 0);
    }

    // 2. Costruzione base e mappatura degli ASN in interi consecutivi.
    {
        ASGraph g;
        CHECK(g.addPath(ASPath{1239, 6113, 8063, 3464}));
        CHECK(g.numNodes() == 4 && g.numEdges() == 3);

        NodeId id = 99;
        CHECK(g.findNode(1239, id) && id == 0);
        CHECK(g.findNode(6113, id) && id == 1);
        CHECK(g.findNode(8063, id) && id == 2);
        CHECK(g.findNode(3464, id) && id == 3);
        CHECK(!g.findNode(1, id));
        CHECK(g.asnOf(3) == 3464);

        Weight w = 0;
        CHECK(weightOf(g, 1239, 6113, w) && w == 1);
        CHECK(weightOf(g, 8063, 6113, w) && w == 1);
        CHECK(!weightOf(g, 1239, 3464, w));  // non consecutivi: nessun arco
        CHECK(g.degree(1) == 2 && g.degree(0) == 1);

        // 3. Aggiornamento dinamico delle frequenze (anche in verso opposto).
        CHECK(g.addPath(ASPath{6113, 1239}));
        CHECK(weightOf(g, 1239, 6113, w) && w == 2);
        CHECK(weightOf(g, 6113, 1239, w) && w == 2);
        CHECK(g.numEdges() == 3);
        std::map<Weight, std::size_t> expected{{1, 2}, {2, 1}};
        CHECK(g.edgeWeightDistribution() == expected);

        // La distribuzione conservata segue ogni aggiornamento e sopravvive alla copia.
        const ASGraph copy = g;
        CHECK(copy.edgeWeightDistribution() == expected);
        g.addPath(ASPath{1239, 6113});  // 1239-6113 passa da 2 a 3: sparisce il valore 2
        std::map<Weight, std::size_t> afterThird{{1, 2}, {3, 1}};
        CHECK(g.edgeWeightDistribution() == afterThird);
        CHECK(copy.edgeWeightDistribution() == expected);  // la copia non cambia
        g.addPath(ASPath{1239});            // un nodo isolato non cambia nulla
        CHECK(g.edgeWeightDistribution() == afterThird);

        // Coerenza delle due liste di adiacenza.
        bool symmetric = true;
        for (NodeId u = 0; u < g.numNodes(); ++u) {
            for (const Edge& e : g.neighbors(u)) {
                Weight back = 0;
                symmetric = symmetric && g.edgeWeight(e.to, u, back) && back == e.weight;
            }
        }
        CHECK(symmetric);

        // Accessi non validi.
        bool thrown = false;
        try { g.neighbors(100); } catch (const std::out_of_range&) { thrown = true; }
        CHECK(thrown);
        thrown = false;
        try { g.asnOf(100); } catch (const std::out_of_range&) { thrown = true; }
        CHECK(thrown);
        CHECK(!g.edgeWeight(0, 100, w));
    }

    // 4. Vettore singolo: nodo senza archi. Self-loop ignorati.
    {
        ASGraph g;
        CHECK(g.addPath(ASPath{42}));
        CHECK(g.numNodes() == 1 && g.numEdges() == 0);
        NodeId id = 0;
        CHECK(g.findNode(42, id) && g.degree(id) == 0);
        CHECK(g.largestComponentSize() == 1);
        CHECK(g.largestComponent().edgeCount == 0 && g.largestComponent().totalWeight == 0);

        CHECK(g.addPath(ASPath{9, 9, 9}));
        CHECK(g.numNodes() == 2 && g.numEdges() == 0);
    }

    // 5. Input non accettabili: ASN 0, rifiuto senza modifiche parziali.
    {
        ASGraph g;
        CHECK(g.addPath(ASPath{1, 2}));
        CHECK(!g.addPath(ASPath{7, 0, 8}));
        CHECK(!g.addPath(ASPath{0}));
        CHECK(g.numNodes() == 2 && g.numEdges() == 1);
        NodeId id = 0;
        CHECK(!g.findNode(7, id) && !g.findNode(8, id));
        CHECK(g.pathsAdded() == 1 && g.pathsRejected() == 2);
    }

    // 6. Componente connessa massimale: dimensione, pareggi e aggiornamenti dinamici.
    {
        ASGraph g;
        g.addPath(ASPath{1, 2, 3});  // componente A: 3 nodi, peso 2
        for (int i = 0; i < 3; ++i) g.addPath(ASPath{10, 11, 12});  // B: 3 nodi, peso 6

        // Stessa dimensione: vince la somma dei pesi maggiore (B).
        CHECK((asnsOf(g, g.largestComponent()) == std::set<ASN>{10, 11, 12}));
        CHECK(g.largestComponent().totalWeight == 6 && g.largestComponent().edgeCount == 2);

        // Una componente piu' numerosa (C, 4 nodi) vince a prescindere dal peso.
        g.addPath(ASPath{20, 21, 22, 23});
        CHECK((asnsOf(g, g.largestComponent()) == std::set<ASN>{20, 21, 22, 23}));
        CHECK(g.largestComponent().totalWeight == 3);

        // Un nuovo cammino unisce A e C: la cache deve essere aggiornata.
        g.addPath(ASPath{3, 20});
        CHECK((asnsOf(g, g.largestComponent()) == std::set<ASN>{1, 2, 3, 20, 21, 22, 23}));
        CHECK(g.largestComponentSize() == 7);
        CHECK(g.largestComponent().edgeCount == 6 && g.largestComponent().totalWeight == 6);

        NodeId id1 = 0, id10 = 0;
        CHECK(g.findNode(1, id1) && g.findNode(10, id10));
        CHECK(g.inLargestComponent(id1) && !g.inLargestComponent(id10));
        CHECK(g.largestComponent().minAsn == 1);
    }

    // 6b. Pareggio di nodi e di somma dei pesi: vince la componente che contiene
    //     l'ASN originale numericamente piu' basso, anche se compare dopo nel file.
    {
        ASGraph g;
        g.addPath(ASPath{900, 901, 902});   // inserita per prima, ASN piu' alti
        g.addPath(ASPath{50, 51, 52});      // stessa forma e stessi pesi, ASN piu' bassi
        g.addPath(ASPath{700, 701, 702});   // altra componente identica
        CHECK((asnsOf(g, g.largestComponent()) == std::set<ASN>{50, 51, 52}));
        CHECK(g.largestComponent().minAsn == 50);
        CHECK(g.largestComponent().totalWeight == 2 && g.largestComponent().edgeCount == 2);

        // Se i pesi differiscono, la somma dei pesi viene prima dell'ASN.
        g.addPath(ASPath{900, 901});        // 900-901 passa a frequenza 2: componente da 3 nodi, peso 3
        CHECK((asnsOf(g, g.largestComponent()) == std::set<ASN>{900, 901, 902}));
        CHECK(g.largestComponent().totalWeight == 3 && g.largestComponent().minAsn == 900);

        // Un'altra componente piu' numerosa batte sia i pesi sia gli ASN.
        g.addPath(ASPath{5000, 5001, 5002, 5003});
        CHECK(g.largestComponentSize() == 4 && g.largestComponent().minAsn == 5000);
    }

    // 7. Confronto con l'implementazione di riferimento su cammini casuali.
    randomTest(300, 20000, 8);     // grafo denso, molti aggiornamenti di frequenza
    randomTest(3000, 2000, 4);     // vicino alla soglia di connessione
    randomTest(50000, 20000, 3);   // molto sparso, molte componenti

    // 8. Integrazione con DataLoader (formato reale del dataset).
    {
        const std::string tmp = "test_asgraph.bz2";
        writeBz2(tmp,
                 "routeviews/routeviews|5 1239|6113|8063|3464 199.88.21.0/24 i 144.228.240.93\n"
                 "routeviews/routeviews|5 3464|8063|1239 10.0.0.0/8 i 1.1.1.1\n");
        DataLoader loader(tmp);
        ASGraph g;
        ASPath p;
        while (loader.nextPath(p)) g.addPath(p);

        CHECK(g.pathsAdded() == 2);
        CHECK(g.numNodes() == 4 && g.numEdges() == 4);
        Weight w = 0;
        CHECK(weightOf(g, 8063, 3464, w) && w == 2);
        CHECK(weightOf(g, 1239, 8063, w) && w == 1);
        std::map<Weight, std::size_t> expected{{1, 3}, {2, 1}};
        CHECK(g.edgeWeightDistribution() == expected);
        CHECK(g.largestComponentSize() == 4 && g.largestComponent().totalWeight == 5);
        std::remove(tmp.c_str());
    }

    if (failures == 0) std::cout << "Tutti i test sono passati.\n";
    return failures == 0 ? 0 : 1;
}