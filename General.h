#ifndef GENERAL_H
#define GENERAL_H

#include "ASGraph.h"
#include "DataLoader.h"
#include "Solver.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

// Chi ha scelto la coppia di AS di una query.
enum class QueryOrigin { Manual, Random };

// Esito e tempi del caricamento di un file (DataLoader + ASGraph).
// Tutti i tempi sono in nanosecondi (std::chrono::steady_clock).
struct LoadRecord {
    bool ok = false;
    std::string error;            // messaggio per l'utente se ok == false
    std::string file;

    std::size_t pathsRead = 0;      // cammini restituiti da DataLoader
    std::size_t pathsAdded = 0;     // cammini accettati da ASGraph
    std::size_t pathsRejected = 0;  // cammini rifiutati da ASGraph
    std::size_t linesSkipped = 0;   // righe scartate da DataLoader
    std::size_t nodesAfter = 0;     // dimensioni del grafo dopo il caricamento
    std::size_t edgesAfter = 0;

    std::int64_t loaderNanos = 0;   // tempo speso in DataLoader (lettura, bz2, parsing)
    std::int64_t graphNanos = 0;    // tempo speso in ASGraph (inserimenti + componente massimale)
    std::int64_t totalNanos = 0;    // tempo totale del caricamento
};

// Dati strutturali della rete (ASGraph).
struct NetworkStats {
    std::size_t numNodes = 0;
    std::size_t numEdges = 0;
    std::size_t largestComponentSize = 0;   // nodi della componente massimale
    std::size_t largestComponentEdges = 0;
    Weight largestComponentWeight = 0;      // somma delle frequenze dei suoi archi
    std::map<Weight, std::size_t> edgeWeightDistribution;  // frequenza -> numero di archi
};

// Dati di una query singola al Solver.
struct QueryRecord {
    QueryOrigin origin = QueryOrigin::Manual;
    MinimaxResult result;            // peso, conteggi (versioni 1 e 2), cammini, statistiche
    std::int64_t solveNanos = 0;     // tempo di Solver::solve (entrambe le versioni)
};

// Riepilogo di un batch di N query casuali.
struct BatchSummary {
    bool ok = false;
    std::string error;
    std::size_t requested = 0;
    std::size_t completed = 0;           // query eseguite
    std::size_t found = 0;               // query andate a buon fine (con un cammino)
    std::size_t classicSaturated = 0;    // conteggi della versione 1 andati in overflow
    std::size_t minLengthSaturated = 0;  // idem per la versione 2
    std::int64_t totalNanos = 0;
    std::int64_t minNanos = 0;
    std::int64_t maxNanos = 0;
    double avgNanos = 0;                 // tempo medio per query
    double avgWeight = 0;                // peso minimax medio (query con cammino)
    double avgMinLength = 0;             // lunghezza media dei cammini piu' corti (nodi)
    std::uint64_t seed = 0;              // seme del generatore casuale usato dal batch
    std::size_t componentSize = 0;       // nodi tra cui sono state estratte le coppie
};

// Esito dell'esportazione.
struct ExportResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> files;      // file scritti
};

// Coordinatore dell'analisi sperimentale.
//
// Istanzia DataLoader (uno per file caricato) e Solver (uno per query), possiede
// l'ASGraph e raccoglie dati e tempi di tutti i moduli. Offre:
//  - un'API non interattiva (loadDataset, computeStats, runQuery,
//    runRandomQueries, exportStats e gli accessori const) che e' anche
//    l'"interfaccia di accesso ai dati dell'analisi sperimentale";
//  - l'interfaccia testuale a menu run(), che usa quell'API.
//
// Input e output sono flussi iniettabili (default: std::cin / std::cout) per
// poter provare l'interfaccia con input scriptati.
class General {
public:
    // Limite al numero di query di un batch.
    static constexpr std::size_t kMaxBatchQueries = 1000000;

    explicit General(std::istream& in = std::cin, std::ostream& out = std::cout);

    General(const General&) = delete;
    General& operator=(const General&) = delete;

    // ------------------------------------------------------------------
    // Interfaccia utente
    // ------------------------------------------------------------------

    // Avvia l'interfaccia. Se 'initialFile' non e' vuoto viene caricato subito;
    // altrimenti si chiede il percorso del file. Poi mostra il menu di scelta.
    // Ritorna 0 a fine sessione (anche se l'input termina).
    int run(const std::string& initialFile = "");

    // ------------------------------------------------------------------
    // API non interattiva
    // ------------------------------------------------------------------

    // Carica un file .bz2 e ne aggiunge i cammini al grafo attuale (DataLoader
    // -> ASGraph, un vettore alla volta); con il grafo vuoto lo costruisce.
    // Non lancia eccezioni: gli errori dei moduli finiscono in LoadRecord::error.
    // Se il caricamento fallisce il grafo resta quello di prima.
    LoadRecord loadDataset(const std::string& path);

    // Statistiche di rete; ricalcolate solo se il grafo e' cambiato.
    // Con grafo vuoto restituisce statistiche vuote.
    const NetworkStats& computeStats();
    bool statsValid() const noexcept { return statsValid_; }

    // Esegue il Solver (versioni 1 e 2) e registra la query nello storico.
    // La coppia di ASN non viene validata qui: lo fa il Solver (vedi status).
    const QueryRecord& runQuery(ASN source, ASN target,
                                QueryOrigin origin = QueryOrigin::Manual);

    // Estrae due AS distinti, uniformemente, tra i nodi della componente
    // connessa massimale (cosi' esiste sempre un cammino).
    bool randomPair(ASN& source, ASN& target, std::string& error);

    // Esegue n query casuali (1..kMaxBatchQueries) e ne registra il riepilogo.
    // Il batch riparte dal seme riportato nel riepilogo, quindi con lo stesso
    // seme (setSeed) ripete le stesse coppie. Se non puo' partire (ok == false)
    // non viene registrato nulla. Con showProgress stampa l'avanzamento.
    BatchSummary runRandomQueries(std::size_t n, bool showProgress = false);

    // Scrive cinque file di testo con il nome indicato come prefisso:
    // <nome>_summary.txt, <nome>_graph.txt, <nome>_distribution.txt,
    // <nome>_queries.txt, <nome>_timings.txt.
    // La distribuzione elenca ogni frequenza da 1 al massimo, comprese quelle
    // con zero archi, nel formato "n | numero di archi con frequenza n".
    ExportResult exportStats(const std::string& name);

    // Seme del prossimo batch (dopo ogni batch ne viene scelto uno nuovo).
    void setSeed(std::uint64_t seed);
    std::uint64_t seed() const noexcept { return seed_; }

    // ------------------------------------------------------------------
    // Accesso ai dati raccolti (sola lettura)
    // ------------------------------------------------------------------
    const ASGraph& graph() const noexcept { return graph_; }
    bool hasGraph() const noexcept { return graph_.numNodes() > 0; }
    const std::vector<std::string>& datasetFiles() const noexcept { return datasetFiles_; }
    const std::vector<LoadRecord>& loads() const noexcept { return loads_; }
    const NetworkStats& stats() const noexcept { return stats_; }
    const std::vector<QueryRecord>& queries() const noexcept { return queries_; }
    const std::vector<BatchSummary>& batches() const noexcept { return batches_; }
    // Tempo totale misurato (caricamenti + query + batch), in nanosecondi.
    // Non comprende le attese dell'utente davanti ai menu.
    std::int64_t processNanos() const noexcept { return processNanos_; }

    // ------------------------------------------------------------------
    // Utilita'
    // ------------------------------------------------------------------

    // Interpreta "1239" o "AS1239". Rifiuta testo, 0 e valori oltre 32 bit.
    static bool parseASN(const std::string& text, ASN& out, std::string& error);
    // true se il percorso termina con ".bz2" (maiuscole/minuscole indifferenti).
    static bool hasBz2Extension(const std::string& path);
    // "AS1239 -> AS456 -> AS789"
    static std::string formatPath(const std::vector<ASN>& path);
    // Descrizione per l'utente di un esito del Solver.
    static std::string describeStatus(SolveStatus status, ASN source, ASN target);

private:
    // --- caricamento e query ---
    void loadInto(ASGraph& target, LoadRecord& record);
    QueryRecord executeQuery(ASN source, ASN target, QueryOrigin origin);
    std::uint64_t freshSeed();

    // --- input da tastiera ---
    bool readLine(std::string& line);
    bool promptChoice(const std::string& prompt, int lo, int hi, int& choice);
    bool promptDatasetPath(const std::string& prompt, std::string& path);
    bool promptASN(const std::string& label, ASN& value);
    bool promptCount(const std::string& prompt, std::size_t maxValue, std::size_t& value);
    bool requireGraph(const std::string& action);

    // --- interfaccia ---
    void printBanner();
    void printDatasetInfo(const LoadRecord& record);
    void printMenu();
    void mainMenu();
    void queryFlow();
    void batchFlow();
    void loadFlow();
    void exportFlow();
    void printQuery(const QueryRecord& record);
    void printBatch(const BatchSummary& summary);

    std::istream& in_;
    std::ostream& out_;
    bool eof_ = false;

    ASGraph graph_;
    std::vector<std::string> datasetFiles_;    // file che compongono il grafo attuale
    std::vector<LoadRecord> loads_;            // caricamenti riusciti

    NetworkStats stats_;
    bool statsValid_ = false;

    std::vector<QueryRecord> queries_;         // query singole (manuali o casuali)
    std::vector<BatchSummary> batches_;        // batch eseguiti

    std::int64_t processNanos_ = 0;

    std::mt19937_64 rng_;
    std::uint64_t seed_ = 0;
};

#endif  // GENERAL_H