#include "ASGraph.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {

constexpr Weight kMaxWeight = std::numeric_limits<Weight>::max();

// Somma con saturazione: in caso di overflow resta al valore massimo.
Weight saturatingAdd(Weight a, Weight b) {
    return a > kMaxWeight - b ? kMaxWeight : a + b;
}

// Chiave di un arco non orientato: (estremo minore, estremo maggiore).
std::uint64_t edgeKey(NodeId a, NodeId b) {
    if (a > b) std::swap(a, b);
    return (static_cast<std::uint64_t>(a) << 32) | b;
}

}  // namespace

// ----------------------------------------------------------------------
// Costruzione
// ----------------------------------------------------------------------

NodeId ASGraph::getOrCreateNode(ASN asn) {
    if (adj_.size() >= std::numeric_limits<NodeId>::max()) {
        throw std::length_error("ASGraph: troppi nodi");
    }
    const NodeId candidate = static_cast<NodeId>(adj_.size());

    // Un'unica ricerca hash: se l'ASN c'e' gia', emplace non inserisce nulla.
    const auto result = asnToId_.emplace(asn, candidate);
    if (!result.second) return result.first->second;

    idToAsn_.push_back(asn);
    adj_.emplace_back();
    return candidate;
}

// Registra una occorrenza dell'arco u-v (u != v): lo crea con frequenza 1 se
// e' nuovo, altrimenti incrementa la frequenza in entrambe le liste.
void ASGraph::addEdgeOccurrence(NodeId u, NodeId v) {
    const NodeId lo = std::min(u, v);
    const NodeId hi = std::max(u, v);
    const std::uint64_t key = edgeKey(lo, hi);

    const auto it = edgeSlots_.find(key);
    if (it == edgeSlots_.end()) {
        EdgeSlots slots;
        slots.posInLow = static_cast<std::uint32_t>(adj_[lo].size());
        slots.posInHigh = static_cast<std::uint32_t>(adj_[hi].size());
        adj_[lo].push_back(Edge{hi, 1});
        adj_[hi].push_back(Edge{lo, 1});
        edgeSlots_.emplace(key, slots);
        ++numEdges_;
        ++weightCounts_[1];  // un nuovo arco ha frequenza 1
        return;
    }

    Weight& weightLow = adj_[lo][it->second.posInLow].weight;
    Weight& weightHigh = adj_[hi][it->second.posInHigh].weight;
    if (weightLow < kMaxWeight) {  // saturazione: i due contatori restano uguali
        const Weight oldWeight = weightLow;
        ++weightLow;
        ++weightHigh;

        // L'arco passa dalla frequenza oldWeight a oldWeight + 1.
        const auto old = weightCounts_.find(oldWeight);
        if (--old->second == 0) weightCounts_.erase(old);
        ++weightCounts_[oldWeight + 1];
    }
}

bool ASGraph::addPath(const ASPath& path) {
    // Validazione prima di toccare il grafo: l'inserimento e' tutto o niente.
    bool valid = !path.empty();
    for (std::size_t i = 0; valid && i < path.size(); ++i) {
        if (path[i] == 0) valid = false;  // ASN 0 riservato (RFC 7607)
    }
    if (!valid) {
        ++pathsRejected_;
        return false;
    }

    NodeId previous = getOrCreateNode(path[0]);
    for (std::size_t i = 1; i < path.size(); ++i) {
        const NodeId current = getOrCreateNode(path[i]);
        if (current != previous) addEdgeOccurrence(previous, current);
        previous = current;
    }

    ++pathsAdded_;
    cacheValid_ = false;
    return true;
}

// ----------------------------------------------------------------------
// Accesso al grafo
// ----------------------------------------------------------------------

bool ASGraph::findNode(ASN asn, NodeId& id) const {
    const auto it = asnToId_.find(asn);
    if (it == asnToId_.end()) return false;
    id = it->second;
    return true;
}

ASN ASGraph::asnOf(NodeId id) const {
    if (id >= idToAsn_.size()) throw std::out_of_range("ASGraph: nodo inesistente");
    return idToAsn_[id];
}

const std::vector<Edge>& ASGraph::neighbors(NodeId id) const {
    if (id >= adj_.size()) throw std::out_of_range("ASGraph: nodo inesistente");
    return adj_[id];
}

bool ASGraph::edgeWeight(NodeId u, NodeId v, Weight& weight) const {
    if (u == v || u >= adj_.size() || v >= adj_.size()) return false;

    const NodeId lo = std::min(u, v);
    const NodeId hi = std::max(u, v);
    const auto it = edgeSlots_.find(edgeKey(lo, hi));
    if (it == edgeSlots_.end()) return false;

    weight = adj_[lo][it->second.posInLow].weight;
    return true;
}

// ----------------------------------------------------------------------
// Informazioni per il modulo di analisi
// ----------------------------------------------------------------------

// BFS da ogni nodo non ancora visitato: O(V + E). Ogni arco non orientato
// compare in due liste, ma viene contato una volta sola (dall'estremo minore).
void ASGraph::computeLargestComponent() const {
    const std::size_t n = adj_.size();
    largest_ = Component();
    inLargest_.assign(n, 0);

    std::vector<char> visited(n, 0);
    std::vector<NodeId> nodes;  // la coda della BFS e' anche la lista dei nodi
    bool found = false;

    for (NodeId start = 0; start < n; ++start) {
        if (visited[start]) continue;

        nodes.clear();
        nodes.push_back(start);
        visited[start] = 1;
        std::size_t edgeCount = 0;
        Weight totalWeight = 0;
        ASN minAsn = std::numeric_limits<ASN>::max();

        for (std::size_t head = 0; head < nodes.size(); ++head) {
            const NodeId u = nodes[head];
            minAsn = std::min(minAsn, idToAsn_[u]);
            for (const Edge& e : adj_[u]) {
                if (e.to > u) {
                    ++edgeCount;
                    totalWeight = saturatingAdd(totalWeight, e.weight);
                }
                if (!visited[e.to]) {
                    visited[e.to] = 1;
                    nodes.push_back(e.to);
                }
            }
        }

        // Piu' nodi vince; a parita' vince la somma dei pesi maggiore; a ulteriore
        // parita' vince la componente con l'ASN originale numericamente piu' basso.
        const bool better =
            !found || nodes.size() > largest_.nodes.size() ||
            (nodes.size() == largest_.nodes.size() &&
             (totalWeight > largest_.totalWeight ||
              (totalWeight == largest_.totalWeight && minAsn < largest_.minAsn)));
        if (better) {
            largest_.nodes = nodes;
            largest_.edgeCount = edgeCount;
            largest_.totalWeight = totalWeight;
            largest_.minAsn = minAsn;
            found = true;
        }
    }

    for (NodeId id : largest_.nodes) inLargest_[id] = 1;
    cacheValid_ = true;
}

const Component& ASGraph::largestComponent() const {
    if (!cacheValid_) computeLargestComponent();
    return largest_;
}

bool ASGraph::inLargestComponent(NodeId id) const {
    if (id >= adj_.size()) throw std::out_of_range("ASGraph: nodo inesistente");
    if (!cacheValid_) computeLargestComponent();
    return inLargest_[id] != 0;
}

std::map<Weight, std::size_t> ASGraph::edgeWeightDistribution() const {
    return std::map<Weight, std::size_t>(weightCounts_.begin(), weightCounts_.end());
}