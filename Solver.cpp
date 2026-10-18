#include "Solver.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace {

constexpr std::uint64_t kMaxCount = std::numeric_limits<std::uint64_t>::max();

// Somma con saturazione: se il risultato supera il massimo, 'overflow' diventa
// true e il risultato resta al valore massimo.
std::uint64_t saturatingAdd(std::uint64_t a, std::uint64_t b, bool& overflow) {
    if (a > kMaxCount - b) {
        overflow = true;
        return kMaxCount;
    }
    return a + b;
}

}  // namespace

MinimaxResult Solver::solve(ASN source, ASN target, CountVersion version) const {
    MinimaxResult result;
    result.source = source;
    result.target = target;

    const bool wantClassic = (version != CountVersion::MinLength);
    const bool wantMinLength = (version != CountVersion::Classic);

    // ------------------------------------------------------------------
    // Controlli sull'input e casi limite
    // ------------------------------------------------------------------
    if (source == 0 || target == 0) {
        result.status = SolveStatus::InvalidASN;
        return result;
    }
    if (graph_.numNodes() == 0) {
        result.status = SolveStatus::EmptyGraph;
        return result;
    }

    NodeId s = 0, t = 0;
    if (!graph_.findNode(source, s) || !graph_.findNode(target, t)) {
        result.status = SolveStatus::NodeNotFound;
        return result;
    }

    if (s == t) {  // costo 0, un solo cammino (quello di un solo nodo)
        result.status = SolveStatus::Ok;
        result.weight = 0;
        result.path.push_back(source);
        if (wantClassic) {
            result.classicComputed = true;
            result.classicCount = 1;
        }
        if (wantMinLength) {
            result.minLengthComputed = true;
            result.minLength = 1;
            result.minLengthCount = 1;
            result.minLengthPath.push_back(source);
        }
        return result;
    }

    // ------------------------------------------------------------------
    // Dijkstra modificato: distanza = massimo peso lungo il cammino
    // ------------------------------------------------------------------
    const std::size_t n = graph_.numNodes();
    std::vector<Weight> dist(n, 0);     // distanza minimax da s
    std::vector<NodeId> parent(n, 0);   // padre nel cammino ottimale
    std::vector<char> reached(n, 0);    // nodo gia' scoperto
    std::vector<char> settled(n, 0);    // distanza definitiva

    // Versione 1: contatori dei cammini (e flag di overflow) per nodo.
    std::vector<std::uint64_t> count;
    std::vector<char> saturated;
    if (wantClassic) {
        count.assign(n, 0);
        saturated.assign(n, 0);
        count[s] = 1;
    }

    // Coda di priorita' con la coppia (distanza, nodo): a parita' di distanza
    // si estrae il nodo con indice minore, quindi l'ordine e' deterministico.
    using QueueItem = std::pair<Weight, NodeId>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;

    reached[s] = 1;
    dist[s] = 0;
    parent[s] = s;
    queue.push(QueueItem(0, s));

    while (!queue.empty()) {
        const NodeId u = queue.top().second;
        queue.pop();
        if (settled[u]) continue;  // voce obsoleta (eliminazione pigra)
        settled[u] = 1;
        ++result.nodesSettled;
        if (u == t) break;  // distanza e contatore di t sono ormai definitivi

        for (const Edge& e : graph_.neighbors(u)) {
            ++result.edgesExamined;
            const NodeId v = e.to;
            if (settled[v]) continue;

            // Invece di sommare i pesi si tiene il massimo lungo il cammino.
            const Weight candidate = std::max(dist[u], e.weight);

            if (!reached[v] || candidate < dist[v]) {
                // Cammino migliore: contatore e padre ripartono da u.
                reached[v] = 1;
                dist[v] = candidate;
                parent[v] = u;
                if (wantClassic) {
                    count[v] = count[u];
                    saturated[v] = saturated[u];
                }
                queue.push(QueueItem(candidate, v));
            } else if (wantClassic && candidate == dist[v]) {
                // Altri cammini ottimali verso v passando per u.
                bool overflow = false;
                count[v] = saturatingAdd(count[v], count[u], overflow);
                saturated[v] = (saturated[v] || saturated[u] || overflow) ? 1 : 0;
            }
        }
    }

    if (!settled[t]) {  // coda esaurita senza raggiungere t
        result.status = SolveStatus::NoPath;
        return result;
    }

    // ------------------------------------------------------------------
    // Risultato: peso, cammino (tramite i padri) e conteggi
    // ------------------------------------------------------------------
    result.status = SolveStatus::Ok;
    result.weight = dist[t];

    std::vector<NodeId> chain;
    for (NodeId v = t; v != s; v = parent[v]) chain.push_back(v);
    chain.push_back(s);
    std::reverse(chain.begin(), chain.end());

    result.path.reserve(chain.size());
    for (NodeId id : chain) result.path.push_back(graph_.asnOf(id));

    if (wantClassic) {
        result.classicComputed = true;
        result.classicCount = count[t];
        result.classicCountSaturated = saturated[t] != 0;
    }

    if (wantMinLength) countMinLength(s, t, result.weight, result);
    return result;
}

// Versione 2: BFS da source sul sottografo formato dai soli archi di peso
// <= limit (il peso minimax). Conta i cammini con il minor numero di nodi;
// ways[v] e' definitivo quando v viene estratto, perche' tutti i nodi del
// livello precedente sono stati estratti prima.
void Solver::countMinLength(NodeId s, NodeId t, Weight limit, MinimaxResult& result) const {
    const std::size_t n = graph_.numNodes();
    std::vector<std::size_t> level(n, 0);      // numero di archi da s
    std::vector<std::uint64_t> ways(n, 0);     // cammini piu' corti verso il nodo
    std::vector<NodeId> parent(n, 0);
    std::vector<char> visited(n, 0);
    std::vector<char> saturated(n, 0);

    std::vector<NodeId> queue;  // coda FIFO: vettore con indice di testa
    visited[s] = 1;
    ways[s] = 1;
    parent[s] = s;
    queue.push_back(s);

    for (std::size_t head = 0; head < queue.size(); ++head) {
        const NodeId u = queue[head];
        ++result.bfsNodesVisited;
        if (u == t) break;

        for (const Edge& e : graph_.neighbors(u)) {
            ++result.bfsEdgesExamined;
            if (e.weight > limit) continue;  // fuori dal sottografo minimax
            const NodeId v = e.to;

            if (!visited[v]) {
                visited[v] = 1;
                level[v] = level[u] + 1;
                ways[v] = ways[u];
                saturated[v] = saturated[u];
                parent[v] = u;
                queue.push_back(v);
            } else if (level[v] == level[u] + 1) {
                bool overflow = false;
                ways[v] = saturatingAdd(ways[v], ways[u], overflow);
                saturated[v] = (saturated[v] || saturated[u] || overflow) ? 1 : 0;
            }
        }
    }

    if (!visited[t]) return;  // non puo' accadere: il cammino minimax esiste

    result.minLengthComputed = true;
    result.minLength = level[t] + 1;  // nodi = archi + 1
    result.minLengthCount = ways[t];
    result.minLengthCountSaturated = saturated[t] != 0;

    std::vector<NodeId> chain;
    for (NodeId v = t; v != s; v = parent[v]) chain.push_back(v);
    chain.push_back(s);
    std::reverse(chain.begin(), chain.end());
    result.minLengthPath.reserve(chain.size());
    for (NodeId id : chain) result.minLengthPath.push_back(graph_.asnOf(id));
}