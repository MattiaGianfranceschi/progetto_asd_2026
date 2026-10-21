#include "General.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <tuple>
#include <utility>

namespace {

using Clock = std::chrono::steady_clock;

std::int64_t nanosBetween(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
}

// 1234 -> "1.23 us", 5000000 -> "5.00 ms" (ASCII, valido su ogni terminale).
std::string formatNanos(std::int64_t ns) {
    if (ns < 0) return "n/d";
    std::ostringstream o;
    o << std::fixed << std::setprecision(2);
    if (ns < 1000) {
        o << ns << " ns";
    } else if (ns < 1000000) {
        o << ns / 1e3 << " us";
    } else if (ns < 1000000000LL) {
        o << ns / 1e6 << " ms";
    } else {
        o << ns / 1e9 << " s";
    }
    return o.str();
}

std::string trim(const std::string& s) {
    const std::size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const std::size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Toglie spazi e virgolette/apici attorno a un percorso.
std::string cleanPath(const std::string& raw) {
    std::string p = trim(raw);
    if (p.size() >= 2 && ((p.front() == '"' && p.back() == '"') ||
                          (p.front() == '\'' && p.back() == '\''))) {
        p = trim(p.substr(1, p.size() - 2));
    }
    return p;
}

bool allDigits(const std::string& s) {
    return !s.empty() &&
           std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// Numeri senza prefisso "AS" per i file: 1239 -> 456 -> 789
std::string plainPath(const std::vector<ASN>& path) {
    std::string s;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i) s += " -> ";
        s += std::to_string(path[i]);
    }
    return s;
}

std::string countText(std::uint64_t count, bool saturated) {
    std::string s = std::to_string(count);
    if (saturated) s += " (overflow: il valore reale e' maggiore)";
    return s;
}

std::string decimals(double value) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(3) << value;
    return o.str();
}

}  // namespace

// ======================================================================
// Costruzione e utilita'
// ======================================================================

General::General(std::istream& in, std::ostream& out) : in_(in), out_(out) {
    setSeed(freshSeed());
}

std::uint64_t General::freshSeed() {
    std::random_device rd;
    return (static_cast<std::uint64_t>(rd()) << 32) ^ rd();
}

void General::setSeed(std::uint64_t seed) {
    seed_ = seed;
    rng_.seed(seed);
}

bool General::parseASN(const std::string& text, ASN& out, std::string& error) {
    std::string t = trim(text);
    if (t.size() >= 2 && (t[0] == 'A' || t[0] == 'a') && (t[1] == 'S' || t[1] == 's')) {
        t = t.substr(2);
    }
    if (t.empty()) {
        error = "valore vuoto";
        return false;
    }
    unsigned long long value = 0;
    for (char c : t) {
        if (c < '0' || c > '9') {
            error = "\"" + trim(text) + "\" non e' un numero valido";
            return false;
        }
        value = value * 10 + static_cast<unsigned>(c - '0');
        if (value > std::numeric_limits<ASN>::max()) {
            error = "l'ASN supera il massimo di 32 bit (4294967295)";
            return false;
        }
    }
    if (value == 0) {
        error = "l'ASN 0 e' riservato e non e' valido";
        return false;
    }
    out = static_cast<ASN>(value);
    return true;
}

bool General::hasBz2Extension(const std::string& path) {
    static const std::string ext = ".bz2";
    if (path.size() <= ext.size()) return false;
    return toLower(path.substr(path.size() - ext.size())) == ext;
}

std::string General::formatPath(const std::vector<ASN>& path) {
    std::string s;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (i) s += " -> ";
        s += "AS" + std::to_string(path[i]);
    }
    return s;
}

std::string General::describeStatus(SolveStatus status, ASN source, ASN target) {
    switch (status) {
        case SolveStatus::Ok:
            return "cammino trovato";
        case SolveStatus::NoPath:
            return "nessun percorso tra AS" + std::to_string(source) + " e AS" +
                   std::to_string(target) + ": appartengono a componenti connesse separate";
        case SolveStatus::InvalidASN:
            return "ASN non valido (l'ASN 0 e' riservato)";
        case SolveStatus::EmptyGraph:
            return "il grafo e' vuoto: carica prima un dataset";
        case SolveStatus::NodeNotFound:
            return "almeno uno dei due AS non e' presente nel grafo";
    }
    return "esito sconosciuto";
}

// ======================================================================
// API non interattiva: caricamento
// ======================================================================

// Legge il file con DataLoader e passa ogni cammino a ASGraph, misurando
// separatamente il tempo speso nei due moduli. Non lancia eccezioni.
void General::loadInto(ASGraph& target, LoadRecord& record) {
    std::unique_ptr<DataLoader> loader;
    std::int64_t loaderNanos = 0;
    std::int64_t graphNanos = 0;

    try {
        loader.reset(new DataLoader(record.file));
        ASPath path;
        Clock::time_point last = Clock::now();
        while (true) {
            const bool more = loader->nextPath(path);
            const Clock::time_point afterLoad = Clock::now();
            loaderNanos += nanosBetween(last, afterLoad);
            if (!more) break;

            if (target.addPath(path)) {
                ++record.pathsAdded;
            } else {
                ++record.pathsRejected;
            }
            last = Clock::now();
            graphNanos += nanosBetween(afterLoad, last);
        }
        record.ok = true;
    } catch (const DataLoaderError& e) {
        record.error = e.what();
    } catch (const std::exception& e) {
        record.error = std::string("errore inatteso: ") + e.what();
    }

    if (loader) {
        record.pathsRead = loader->pathsRead();
        record.linesSkipped = loader->linesSkipped();
    }
    record.loaderNanos = loaderNanos;
    record.graphNanos = graphNanos;
}

LoadRecord General::loadDataset(const std::string& rawPath) {
    const Clock::time_point start = Clock::now();
    LoadRecord rec;
    rec.file = cleanPath(rawPath);

    if (rec.file.empty()) {
        rec.error = "percorso del file vuoto";
        return rec;
    }
    if (!hasBz2Extension(rec.file)) {
        rec.error = "il file deve avere estensione .bz2: " + rec.file;
        return rec;
    }

    // Si lavora su una copia: il grafo attuale cambia solo se il caricamento riesce.
    ASGraph candidate = graph_;
    loadInto(candidate, rec);
    if (!rec.ok) return rec;
    graph_ = std::move(candidate);

    // La componente connessa massimale (BFS) serve all'estrazione casuale e
    // all'esportazione: si calcola subito e il tempo conta come tempo di ASGraph.
    const Clock::time_point t0 = Clock::now();
    graph_.largestComponent();
    rec.graphNanos += nanosBetween(t0, Clock::now());

    rec.nodesAfter = graph_.numNodes();
    rec.edgesAfter = graph_.numEdges();
    rec.totalNanos = nanosBetween(start, Clock::now());

    statsValid_ = false;
    datasetFiles_.push_back(rec.file);
    loads_.push_back(rec);
    processNanos_ += rec.totalNanos;
    return rec;
}

// ======================================================================
// API non interattiva: statistiche, query, batch
// ======================================================================

const NetworkStats& General::computeStats() {
    if (statsValid_) return stats_;

    NetworkStats s;
    s.numNodes = graph_.numNodes();
    s.numEdges = graph_.numEdges();
    if (s.numNodes > 0) {
        const Component& c = graph_.largestComponent();
        s.largestComponentSize = c.nodes.size();
        s.largestComponentEdges = c.edgeCount;
        s.largestComponentWeight = c.totalWeight;
        s.edgeWeightDistribution = graph_.edgeWeightDistribution();
    }
    stats_ = std::move(s);
    statsValid_ = true;
    return stats_;
}

QueryRecord General::executeQuery(ASN source, ASN target, QueryOrigin origin) {
    QueryRecord rec;
    rec.origin = origin;

    Solver solver(graph_);
    const Clock::time_point t0 = Clock::now();
    rec.result = solver.solve(source, target, CountVersion::Both);
    rec.solveNanos = nanosBetween(t0, Clock::now());
    processNanos_ += rec.solveNanos;
    return rec;
}

const QueryRecord& General::runQuery(ASN source, ASN target, QueryOrigin origin) {
    queries_.push_back(executeQuery(source, target, origin));
    return queries_.back();
}

bool General::randomPair(ASN& source, ASN& target, std::string& error) {
    if (!hasGraph()) {
        error = "nessun dataset caricato";
        return false;
    }
    const Component& comp = graph_.largestComponent();
    if (comp.nodes.size() < 2) {
        error = "la componente connessa massimale ha un solo nodo: impossibile estrarre due AS distinti";
        return false;
    }

    // Due indici distinti, uniformi, tra i nodi della componente massimale
    // (quindi sempre ID validi di ASGraph e sempre collegati).
    std::uniform_int_distribution<std::size_t> first(0, comp.nodes.size() - 1);
    std::uniform_int_distribution<std::size_t> second(0, comp.nodes.size() - 2);
    const std::size_t i = first(rng_);
    std::size_t j = second(rng_);
    if (j >= i) ++j;

    source = graph_.asnOf(comp.nodes[i]);
    target = graph_.asnOf(comp.nodes[j]);
    return true;
}

BatchSummary General::runRandomQueries(std::size_t n, bool showProgress) {
    BatchSummary summary;
    summary.requested = n;
    summary.seed = seed_;

    if (!hasGraph()) {
        summary.error = "nessun dataset caricato";
        return summary;
    }
    if (n == 0 || n > kMaxBatchQueries) {
        summary.error = "il numero di query deve essere tra 1 e " + std::to_string(kMaxBatchQueries);
        return summary;
    }
    const Component& comp = graph_.largestComponent();
    if (comp.nodes.size() < 2) {
        summary.error = "la componente connessa massimale ha un solo nodo: impossibile estrarre due AS distinti";
        return summary;
    }
    summary.componentSize = comp.nodes.size();

    // Il batch riparte dal seme riportato: con lo stesso seme ripete le stesse coppie.
    rng_.seed(seed_);

    summary.minNanos = std::numeric_limits<std::int64_t>::max();
    double weightSum = 0, lengthSum = 0;
    const std::size_t step = std::max<std::size_t>(1, n / 10);
    for (std::size_t i = 0; i < n; ++i) {
        ASN a = 0, b = 0;
        std::string err;
        randomPair(a, b, err);  // gia' verificato sopra: non puo' fallire
        const QueryRecord q = executeQuery(a, b, QueryOrigin::Random);

        summary.totalNanos += q.solveNanos;
        summary.minNanos = std::min(summary.minNanos, q.solveNanos);
        summary.maxNanos = std::max(summary.maxNanos, q.solveNanos);
        if (q.result.ok()) {
            ++summary.found;
            weightSum += static_cast<double>(q.result.weight);
            lengthSum += static_cast<double>(q.result.minLength);
            if (q.result.classicCountSaturated) ++summary.classicSaturated;
            if (q.result.minLengthCountSaturated) ++summary.minLengthSaturated;
        }
        if (showProgress && n >= 1000 && (i + 1) % step == 0) {
            out_ << "  ... " << (i + 1) << "/" << n << " query\n";
            out_.flush();
        }
    }

    summary.ok = true;
    summary.completed = n;
    summary.avgNanos = static_cast<double>(summary.totalNanos) / static_cast<double>(n);
    if (summary.found > 0) {
        summary.avgWeight = weightSum / static_cast<double>(summary.found);
        summary.avgMinLength = lengthSum / static_cast<double>(summary.found);
    }
    batches_.push_back(summary);
    seed_ = freshSeed();  // il prossimo batch parte da un seme nuovo
    return summary;
}

// ======================================================================
// Esportazione
// ======================================================================

ExportResult General::exportStats(const std::string& rawName) {
    ExportResult res;
    if (!hasGraph()) {
        res.error = "nessun dataset caricato: niente da esportare";
        return res;
    }
    const NetworkStats& s = computeStats();

    std::string prefix = cleanPath(rawName);
    if (prefix.size() > 4 && toLower(prefix.substr(prefix.size() - 4)) == ".txt") {
        prefix.erase(prefix.size() - 4);
    }
    if (prefix.empty()) prefix = "analisi";

    auto writeFile = [&](const std::string& suffix, const std::function<void(std::ostream&)>& body) {
        const std::string name = prefix + suffix;
        std::ofstream f(name.c_str());
        if (!f) {
            res.error = "impossibile creare il file " + name;
            return false;
        }
        body(f);
        f.flush();
        if (!f) {
            res.error = "errore di scrittura sul file " + name;
            return false;
        }
        res.files.push_back(name);
        return true;
    };

    // --- <nome>_summary.txt ---
    if (!writeFile("_summary.txt", [&](std::ostream& f) {
            std::string files;
            std::size_t read = 0, added = 0, rejected = 0, skipped = 0;
            for (const LoadRecord& l : loads_) {
                if (!files.empty()) files += ", ";
                files += l.file;
                read += l.pathsRead;
                added += l.pathsAdded;
                rejected += l.pathsRejected;
                skipped += l.linesSkipped;
            }
            f << "dataset_files: " << files << "\n"
              << "paths_read: " << read << "\n"
              << "paths_added: " << added << "\n"
              << "paths_rejected: " << rejected << "\n"
              << "lines_skipped: " << skipped << "\n"
              << "nodes: " << s.numNodes << "\n"
              << "edges: " << s.numEdges << "\n"
              << "largest_component_nodes: " << s.largestComponentSize << "\n"
              << "largest_component_edges: " << s.largestComponentEdges << "\n"
              << "queries_single: " << queries_.size() << "\n"
              << "queries_batch: " << batches_.size() << "\n";
        })) {
        return res;
    }

    // --- <nome>_graph.txt: un arco per riga, ordinati per ASN ---
    if (!writeFile("_graph.txt", [&](std::ostream& f) {
            std::vector<std::tuple<ASN, ASN, Weight>> edges;
            edges.reserve(graph_.numEdges());
            for (NodeId u = 0; u < graph_.numNodes(); ++u) {
                for (const Edge& e : graph_.neighbors(u)) {
                    const ASN a = graph_.asnOf(u), b = graph_.asnOf(e.to);
                    if (a < b) edges.emplace_back(a, b, e.weight);  // ogni arco una sola volta
                }
            }
            std::sort(edges.begin(), edges.end());
            for (const auto& e : edges) {
                f << std::get<0>(e) << " | " << std::get<1>(e) << " | " << std::get<2>(e) << "\n";
            }
        })) {
        return res;
    }

    // --- <nome>_distribution.txt: da 1 al peso massimo, anche i pesi senza archi ---
    if (!writeFile("_distribution.txt", [&](std::ostream& f) {
            const std::map<Weight, std::size_t>& dist = s.edgeWeightDistribution;
            if (dist.empty()) return;
            const Weight maxWeight = dist.rbegin()->first;
            auto it = dist.begin();
            for (Weight n = 1;; ++n) {
                std::size_t count = 0;
                if (it != dist.end() && it->first == n) {
                    count = it->second;
                    ++it;
                }
                f << n << " | " << count << "\n";
                if (n == maxWeight) break;
            }
        })) {
        return res;
    }

    // --- <nome>_queries.txt ---
    if (!writeFile("_queries.txt", [&](std::ostream& f) {
            if (queries_.empty() && batches_.empty()) f << "Nessuna query eseguita.\n";
            for (std::size_t i = 0; i < queries_.size(); ++i) {
                const QueryRecord& q = queries_[i];
                const MinimaxResult& r = q.result;
                f << "- Query numero " << (i + 1) << " -\n\n"
                  << "Origine dei nodi: " << (q.origin == QueryOrigin::Random ? "random" : "manuale") << "\n"
                  << "Source: " << r.source << "\n"
                  << "Target: " << r.target << "\n"
                  << "Esito della query: " << (r.ok() ? "positivo" : "errore") << "\n";
                if (r.ok()) {
                    f << "Peso cammino minimax ottimale: " << r.weight << "\n"
                      << "Numero cammini minimax ottimali (limite inferiore): "
                      << countText(r.classicCount, r.classicCountSaturated) << "\n"
                      << "Numero cammini minimax ottimali più corti: "
                      << countText(r.minLengthCount, r.minLengthCountSaturated) << "\n"
                      << "Path di un cammino minimax ottimale più corto: " << plainPath(r.minLengthPath) << "\n";
                } else {
                    f << "Motivo dell'errore: " << describeStatus(r.status, r.source, r.target) << "\n";
                }
                f << "\n";
            }
            for (std::size_t i = 0; i < batches_.size(); ++i) {
                const BatchSummary& b = batches_[i];
                f << "- Batch numero " << (i + 1) << " -\n\n"
                  << "Query eseguite: " << b.completed << "\n"
                  << "Query andate a buon fine: " << b.found << "\n"
                  << "Seme del generatore casuale: " << b.seed << "\n"
                  << "Tempo medio per query: " << formatNanos(static_cast<std::int64_t>(b.avgNanos)) << "\n"
                  << "Tempo minimo: " << formatNanos(b.minNanos) << "\n"
                  << "Tempo massimo: " << formatNanos(b.maxNanos) << "\n"
                  << "Tempo totale: " << formatNanos(b.totalNanos) << "\n"
                  << "Peso minimax medio: " << decimals(b.avgWeight) << "\n"
                  << "Lunghezza media dei cammini piu' corti: " << decimals(b.avgMinLength) << "\n\n";
            }
        })) {
        return res;
    }

    // --- <nome>_timings.txt ---
    if (!writeFile("_timings.txt", [&](std::ostream& f) {
            std::int64_t loaderNanos = 0, graphNanos = 0;
            for (const LoadRecord& l : loads_) {
                loaderNanos += l.loaderNanos;
                graphNanos += l.graphNanos;
            }
            f << "Tempo di esecuzione di DataLoader: " << formatNanos(loaderNanos) << "\n"
              << "Tempo di esecuzione di ASGraph: " << formatNanos(graphNanos) << "\n";
            for (std::size_t i = 0; i < queries_.size(); ++i) {
                f << "Tempo di esecuzione di Solver nella query numero " << (i + 1) << ": "
                  << formatNanos(queries_[i].solveNanos) << "\n";
            }
            f << "Tempo totale di esecuzione: " << formatNanos(processNanos_) << "\n";
        })) {
        return res;
    }

    res.ok = true;
    return res;
}

// ======================================================================
// Input da tastiera
// ======================================================================

// Legge una riga intera: l'input sbagliato non lascia mai residui nel buffer e
// la fine dell'input (EOF) viene segnalata, evitando cicli infiniti.
bool General::readLine(std::string& line) {
    if (eof_) return false;
    if (!std::getline(in_, line)) {
        eof_ = true;
        return false;
    }
    return true;
}

bool General::promptChoice(const std::string& prompt, int lo, int hi, int& choice) {
    while (true) {
        out_ << prompt;
        out_.flush();
        std::string line;
        if (!readLine(line)) return false;
        const std::string t = trim(line);
        if (allDigits(t) && t.size() <= 9) {
            const int v = std::stoi(t);
            if (v >= lo && v <= hi) {
                choice = v;
                return true;
            }
        }
        out_ << "Scelta non valida: inserisci un numero tra " << lo << " e " << hi << ".\n";
    }
}

// Ritorna false se l'utente annulla (invio vuoto) o l'input termina.
bool General::promptDatasetPath(const std::string& prompt, std::string& path) {
    std::string currentPrompt = prompt;
    while (true) {
        out_ << currentPrompt;
        out_.flush();
        std::string line;
        if (!readLine(line)) return false;
        const std::string p = cleanPath(line);
        if (p.empty()) return false;
        if (hasBz2Extension(p)) {
            path = p;
            return true;
        }
        out_ << "Il file deve avere estensione .bz2.\n";
        currentPrompt = "Inserisci il percorso del file .bz2 (invio vuoto per annullare): ";
    }
}

// Chiede un ASN presente nel grafo; ripropone la richiesta se l'input non e'
// valido o l'AS non esiste. Ritorna false se l'utente annulla (q) o l'input termina.
bool General::promptASN(const std::string& label, ASN& value) {
    while (true) {
        out_ << label << " (q per annullare): ";
        out_.flush();
        std::string line;
        if (!readLine(line)) return false;
        const std::string t = trim(line);
        if (t == "q" || t == "Q") return false;

        ASN asn = 0;
        std::string err;
        if (!parseASN(t, asn, err)) {
            out_ << "Input non valido: " << err << ". Riprova.\n";
            continue;
        }
        NodeId id = 0;
        if (!graph_.findNode(asn, id)) {
            out_ << "AS" << asn << " non e' presente nel grafo. Riprova.\n";
            continue;
        }
        value = asn;
        return true;
    }
}

bool General::promptCount(const std::string& prompt, std::size_t maxValue, std::size_t& value) {
    while (true) {
        out_ << prompt;
        out_.flush();
        std::string line;
        if (!readLine(line)) return false;
        const std::string t = trim(line);
        if (t == "q" || t == "Q") return false;
        if (allDigits(t) && t.size() <= 9) {
            const std::size_t v = static_cast<std::size_t>(std::stoul(t));
            if (v >= 1 && v <= maxValue) {
                value = v;
                return true;
            }
        }
        out_ << "Valore non valido: inserisci un intero tra 1 e " << maxValue << ".\n";
    }
}

bool General::requireGraph(const std::string& action) {
    if (hasGraph()) return true;
    out_ << "Operazione non disponibile (" << action
         << "): nessun dataset caricato, il grafo e' vuoto. Carica prima un dataset (opzione 3).\n";
    return false;
}

// ======================================================================
// Interfaccia
// ======================================================================

void General::printBanner() {
    out_ << "- ASGraph Solver - analisi sperimentale sulla topologia delle reti di Autonomous Systems\n\n";
}

void General::printDatasetInfo(const LoadRecord& rec) {
    out_ << "Dataset indicato: " << rec.file << "\n";
    if (!rec.ok) {
        out_ << "ERRORE nel caricamento: " << rec.error << "\n";
        return;
    }
    out_ << "Caricamento completato: " << rec.file << "\n"
         << "Cammini letti: " << rec.pathsRead << " (righe scartate: " << rec.linesSkipped << ")\n\n"
         << "- Analisi Strutturale del grafo -\n\n"
         << "Numero nodi: " << rec.nodesAfter << "\n"
         << "Numero archi: " << rec.edgesAfter << "\n"
         << "Tempi: DataLoader: " << formatNanos(rec.loaderNanos) << ", ASGraph: " << formatNanos(rec.graphNanos)
         << ", totale " << formatNanos(rec.totalNanos) << "\n";
}

void General::printMenu() {
    out_ << "\n- Menu di scelta -\n\n"
         << "1) Nuova query sul grafo corrente\n"
         << "2) N query randomiche sul grafo corrente\n"
         << "3) Carica un altro file .bz2\n"
         << "4) Esporta le statistiche\n"
         << "0) Esci\n";
}

void General::printQuery(const QueryRecord& rec) {
    const MinimaxResult& r = rec.result;
    const bool random = rec.origin == QueryOrigin::Random;
    out_ << "\n- Analisi della Query -\n\n"
         << (random ? "Coppia estratta: " : "Coppia inserita: ") << "AS" << r.source << " e AS" << r.target << "\n"
         << "Query tra AS" << r.source << " e AS" << r.target << (random ? " (casuale)" : " (manuale)") << "\n\n";
    if (!r.ok()) {
        out_ << "Esito: " << describeStatus(r.status, r.source, r.target) << "\n"
             << "Tempo di esecuzione del Solver: " << formatNanos(rec.solveNanos) << "\n";
        return;
    }
    out_ << "Peso del cammino minimax: " << r.weight << "\n"
         << "Numero cammini minimax (limite inferiore): " << countText(r.classicCount, r.classicCountSaturated) << "\n"
         << "Numero cammini minimax piu' corti (conteggio esatto): "
         << countText(r.minLengthCount, r.minLengthCountSaturated) << "\n"
         << "Lunghezza dei cammini minimax più corti: " << r.minLength << " nodi\n"
         << "Tracciato di uno dei cammini minimax ottimali: " << formatPath(r.minLengthPath) << "\n"
         << "Tempo di esecuzione del Solver: " << formatNanos(rec.solveNanos) << "\n";
}

void General::printBatch(const BatchSummary& b) {
    out_ << "\n- Analisi del batch -\n\n"
         << "Query eseguite: " << b.completed << "\n"
         << "Seme del generatore casuale: " << b.seed << "\n"
         << "Tempo medio per query: " << formatNanos(static_cast<std::int64_t>(b.avgNanos)) << "\n"
         << "Tempo minimo: " << formatNanos(b.minNanos) << "\n"
         << "Tempo massimo: " << formatNanos(b.maxNanos) << "\n"
         << "Tempo totale: " << formatNanos(b.totalNanos) << "\n"
         << "Peso minimax medio: " << decimals(b.avgWeight) << "\n"
         << "Lunghezza media dei cammini piu' corti: " << decimals(b.avgMinLength) << "\n";
    if (b.classicSaturated > 0 || b.minLengthSaturated > 0) {
        out_ << "Conteggi in overflow: limite inferiore in " << b.classicSaturated
             << " query, conteggio esatto in " << b.minLengthSaturated << " query\n";
    }
}

void General::queryFlow() {
    if (!requireGraph("query del Solver")) return;
    out_ << "\n1) Inserimento manuale degli ASN\n"
         << "2) Inserimento randomico degli ASN (dalla componente connessa massimale)\n"
         << "0) Annulla\n";
    int choice = 0;
    if (!promptChoice("Scelta: ", 0, 2, choice) || choice == 0) return;

    ASN source = 0, target = 0;
    QueryOrigin origin = QueryOrigin::Manual;
    if (choice == 1) {
        if (!promptASN("ASN sorgente", source) || !promptASN("ASN destinazione", target)) {
            out_ << "Query annullata.\n";
            return;
        }
    } else {
        std::string err;
        if (!randomPair(source, target, err)) {
            out_ << "ERRORE: " << err << "\n";
            return;
        }
        origin = QueryOrigin::Random;
    }
    printQuery(runQuery(source, target, origin));
}

void General::batchFlow() {
    if (!requireGraph("query randomiche")) return;
    std::size_t n = 0;
    if (!promptCount("Numero di query da eseguire (1-" + std::to_string(kMaxBatchQueries) + ", q per annullare): ",
                     kMaxBatchQueries, n)) {
        out_ << "Operazione annullata.\n";
        return;
    }
    const BatchSummary summary = runRandomQueries(n, true);
    if (!summary.ok) {
        out_ << "ERRORE: " << summary.error << "\n";
        return;
    }
    printBatch(summary);
}

void General::loadFlow() {
    std::string path;
    if (!promptDatasetPath("Inserisci il percorso del nuovo file .bz2 (invio vuoto per annullare): ", path)) {
        out_ << "Operazione annullata.\n";
        return;
    }
    const LoadRecord rec = loadDataset(path);
    if (!rec.ok) {
        out_ << "ERRORE nel caricamento di " << rec.file << ": " << rec.error
             << "\nIl grafo corrente non e' stato modificato.\n";
        return;
    }
    // Grafo aggiornato: si ripropone l'interfaccia da capo.
    out_ << "\n";
    printBanner();
    printDatasetInfo(rec);
}

void General::exportFlow() {
    if (!requireGraph("esportazione delle statistiche")) return;
    out_ << "Nome con cui salvare i file di esportazione (invio vuoto = analisi): ";
    out_.flush();
    std::string line;
    if (!readLine(line)) return;
    const ExportResult res = exportStats(line);
    if (!res.ok) {
        out_ << "ERRORE nell'esportazione: " << res.error << "\n";
        return;
    }
    out_ << "Esportazione completata. File creati:\n";
    for (const std::string& f : res.files) out_ << "  " << f << "\n";
}

void General::mainMenu() {
    while (!eof_) {
        printMenu();
        int choice = 0;
        if (!promptChoice("Scelta: ", 0, 4, choice)) return;
        switch (choice) {
            case 0:
                out_ << "Arrivederci.\n";
                return;
            case 1: queryFlow(); break;
            case 2: batchFlow(); break;
            case 3: loadFlow(); break;
            case 4: exportFlow(); break;
        }
    }
}

int General::run(const std::string& initialFile) {
    printBanner();

    std::string path = cleanPath(initialFile);
    if (path.empty()) {
        out_ << "Premi invio senza scrivere nulla per passare al menu senza dataset.\n";
        if (!promptDatasetPath("Nessun dataset rilevato. Inserisci il percorso del file .bz2: ", path)) {
            path.clear();
            if (!eof_) out_ << "Nessun dataset caricato: usa l'opzione 3 del menu per caricarne uno.\n";
        }
    }
    if (!path.empty()) {
        printDatasetInfo(loadDataset(path));
    }

    mainMenu();
    return 0;
}