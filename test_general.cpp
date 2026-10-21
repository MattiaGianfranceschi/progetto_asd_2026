// Test del modulo General (analisi): API non interattiva, file di esportazione e
// interfaccia a menu provata con input scriptato.
// Compilazione:
//   g++ -std=c++17 -O2 -Wall -Wextra DataLoader.cpp ASGraph.cpp Solver.cpp General.cpp test_general.cpp -lbz2 -o test_general

#include "General.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
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

static bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

static std::size_t countOccurrences(const std::string& text, const std::string& part) {
    std::size_t n = 0, pos = 0;
    while ((pos = text.find(part, pos)) != std::string::npos) {
        ++n;
        pos += part.size();
    }
    return n;
}

static void writeBz2(const std::string& file, const std::string& content) {
    std::FILE* f = std::fopen(file.c_str(), "wb");
    int err = BZ_OK;
    BZFILE* b = BZ2_bzWriteOpen(&err, f, 9, 0, 0);
    BZ2_bzWrite(&err, b, const_cast<char*>(content.data()), static_cast<int>(content.size()));
    BZ2_bzWriteClose(&err, b, 0, nullptr, nullptr);
    std::fclose(f);
}

static void writeRaw(const std::string& file, const std::string& content) {
    std::FILE* f = std::fopen(file.c_str(), "wb");
    std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
}

static std::string readFile(const std::string& file) {
    std::ifstream f(file.c_str());
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

static bool fileExists(const std::string& file) {
    std::ifstream f(file.c_str());
    return static_cast<bool>(f);
}

static std::string line(const std::string& path) {
    return "routeviews/routeviews|5 " + path + " 1.0.0.0/24 i 1.1.1.1\n";
}

// Esegue una sessione interattiva completa con l'input indicato.
static std::string session(const std::string& initialFile, const std::string& input, int* rc = nullptr) {
    std::istringstream in(input);
    std::ostringstream out;
    General g(in, out);
    g.setSeed(12345);
    const int code = g.run(initialFile);
    if (rc) *rc = code;
    return out.str();
}

int main() {
    // ------------------------------------------------------------------
    // Dataset di prova
    //   t1: componente A = {100..104} (5 nodi, 5 archi, peso totale 6),
    //       componente B = {200, 201}, nodo isolato 300, una riga con AS_SET scartata.
    //       Archi con frequenza 1: 100-101, 101-102, 100-102, 100-104, 200-201;
    //       frequenza 2: 102-103.
    //   t2: aggiunge il nodo 400 (arco 400-100) e l'arco 300-201.
    // ------------------------------------------------------------------
    writeBz2("t_gen1.bz2", line("100|101|102|103") + line("103|102|100") + line("100|100|104") +
                               line("200|201") + line("300") + line("100|{1,2}|9"));
    writeBz2("t_gen2.bz2", line("400|100") + line("300|201"));
    writeBz2("t_gen_junk.bz2", "solo testo\ninutile\n");
    writeRaw("t_gen_empty.bz2", "");
    writeRaw("t_gen_fake.bz2", "questo non e' un file bz2\n");
    writeRaw("t_gen_notbz2.txt", "x");

    // 1. Utilita' statiche.
    {
        ASN v = 0;
        std::string err;
        CHECK(General::parseASN("1239", v, err) && v == 1239);
        CHECK(General::parseASN("AS1239", v, err) && v == 1239);
        CHECK(General::parseASN("as5", v, err) && v == 5);
        CHECK(General::parseASN("  7 ", v, err) && v == 7);
        CHECK(General::parseASN("4294967295", v, err) && v == 4294967295u);
        CHECK(!General::parseASN("4294967296", v, err));
        CHECK(!General::parseASN("0", v, err));
        CHECK(!General::parseASN("AS0", v, err));
        CHECK(!General::parseASN("abc", v, err) && !err.empty());
        CHECK(!General::parseASN("12a", v, err));
        CHECK(!General::parseASN("-5", v, err));
        CHECK(!General::parseASN("", v, err));
        CHECK(!General::parseASN("AS", v, err));

        CHECK(General::hasBz2Extension("a.bz2"));
        CHECK(General::hasBz2Extension("A.BZ2"));
        CHECK(General::hasBz2Extension("dir/x.tar.bz2"));
        CHECK(!General::hasBz2Extension("a.txt"));
        CHECK(!General::hasBz2Extension(".bz2"));
        CHECK(!General::hasBz2Extension("bz2"));

        CHECK(General::formatPath({1239, 456, 789}) == "AS1239 -> AS456 -> AS789");
        CHECK(General::formatPath({7}) == "AS7");
        CHECK(General::formatPath({}) == "");
    }

    // 2. Grafo vuoto: ogni operazione dipendente dal grafo e' rifiutata.
    {
        std::istringstream in;
        std::ostringstream out;
        General g(in, out);
        CHECK(!g.hasGraph());
        CHECK(g.runQuery(1, 2).result.status == SolveStatus::EmptyGraph);
        CHECK(g.computeStats().numNodes == 0);
        ASN a = 0, b = 0;
        std::string err;
        CHECK(!g.randomPair(a, b, err) && !err.empty());
        CHECK(!g.runRandomQueries(10).ok && g.batches().empty());
        CHECK(!g.exportStats("t_gen_none").ok);
        CHECK(!fileExists("t_gen_none_summary.txt"));
    }

    // 3. Errori di caricamento: il grafo resta quello di prima.
    {
        std::istringstream in;
        std::ostringstream out;
        General g(in, out);

        LoadRecord r = g.loadDataset("t_gen_notbz2.txt");
        CHECK(!r.ok && contains(r.error, ".bz2"));
        r = g.loadDataset("");
        CHECK(!r.ok);
        r = g.loadDataset("file_che_non_esiste.bz2");
        CHECK(!r.ok && contains(r.error, "file_che_non_esiste.bz2") && contains(r.error, "No such file"));
        r = g.loadDataset("t_gen_empty.bz2");
        CHECK(!r.ok && !r.error.empty());
        r = g.loadDataset("t_gen_junk.bz2");
        CHECK(!r.ok && !r.error.empty());
        r = g.loadDataset("t_gen_fake.bz2");
        CHECK(!r.ok && !r.error.empty());
        CHECK(!g.hasGraph() && g.loads().empty() && g.datasetFiles().empty());

        // Con un grafo gia' caricato un errore non lo modifica.
        CHECK(g.loadDataset("t_gen1.bz2").ok);
        CHECK(!g.loadDataset("t_gen_fake.bz2").ok);
        CHECK(!g.loadDataset("t_gen_junk.bz2").ok);
        CHECK(g.graph().numNodes() == 8 && g.graph().numEdges() == 6);
        CHECK(g.loads().size() == 1 && g.datasetFiles().size() == 1);
    }

    // 4. Caricamento, statistiche e tempi; 5. aggiornamento con un secondo file.
    {
        std::istringstream in;
        std::ostringstream out;
        General g(in, out);

        const LoadRecord r = g.loadDataset("t_gen1.bz2");
        CHECK(r.ok && r.file == "t_gen1.bz2");
        CHECK(r.pathsRead == 5 && r.pathsAdded == 5 && r.pathsRejected == 0 && r.linesSkipped == 1);
        CHECK(r.nodesAfter == 8 && r.edgesAfter == 6);
        CHECK(r.loaderNanos >= 0 && r.graphNanos >= 0);
        CHECK(r.totalNanos >= r.loaderNanos + r.graphNanos);
        CHECK(g.hasGraph() && g.loads().size() == 1 && g.datasetFiles().size() == 1);
        CHECK(g.processNanos() >= r.totalNanos);

        CHECK(!g.statsValid());
        const NetworkStats& s = g.computeStats();
        CHECK(g.statsValid());
        CHECK(s.numNodes == 8 && s.numEdges == 6);
        CHECK(s.largestComponentSize == 5 && s.largestComponentEdges == 5 && s.largestComponentWeight == 6);
        const std::map<Weight, std::size_t> expected{{1, 5}, {2, 1}};
        CHECK(s.edgeWeightDistribution == expected);
        CHECK(&g.computeStats() == &g.stats());  // seconda chiamata: dati in cache

        // Un secondo file aggiorna il grafo esistente.
        const LoadRecord add = g.loadDataset("t_gen2.bz2");
        CHECK(add.ok && add.pathsRead == 2 && add.nodesAfter == 9 && add.edgesAfter == 8);
        CHECK(!g.statsValid());  // il grafo e' cambiato: statistiche da ricalcolare
        CHECK(g.loads().size() == 2 && g.datasetFiles().size() == 2);
        CHECK(g.datasetFiles()[0] == "t_gen1.bz2" && g.datasetFiles()[1] == "t_gen2.bz2");
        const NetworkStats& s2 = g.computeStats();
        CHECK(s2.numNodes == 9 && s2.numEdges == 8 && s2.largestComponentSize == 6);
        const std::map<Weight, std::size_t> expected2{{1, 7}, {2, 1}};
        CHECK(s2.edgeWeightDistribution == expected2);

        // Ricaricare lo stesso file somma le frequenze (le occorrenze si accumulano).
        CHECK(g.loadDataset("t_gen2.bz2").ok);
        CHECK(g.graph().numNodes() == 9 && g.graph().numEdges() == 8);
        Weight w = 0;
        NodeId a = 0, b = 0;
        CHECK(g.graph().findNode(400, a) && g.graph().findNode(100, b) && g.graph().edgeWeight(a, b, w) && w == 2);
    }

    // 6. Query singole.
    {
        std::istringstream in;
        std::ostringstream out;
        General g(in, out);
        CHECK(g.loadDataset("t_gen1.bz2").ok);

        const QueryRecord q = g.runQuery(100, 103);
        CHECK(q.result.ok() && q.result.weight == 2);
        CHECK(q.result.classicCount == 2 && q.result.minLength == 3 && q.result.minLengthCount == 1);
        CHECK((q.result.minLengthPath == std::vector<ASN>{100, 102, 103}));
        CHECK(q.origin == QueryOrigin::Manual && q.solveNanos >= 0);

        const QueryRecord same = g.runQuery(100, 100);
        CHECK(same.result.ok() && same.result.weight == 0 && same.result.classicCount == 1);
        CHECK(g.runQuery(100, 200).result.status == SolveStatus::NoPath);
        CHECK(g.runQuery(100, 999).result.status == SolveStatus::NodeNotFound);
        CHECK(g.runQuery(0, 100).result.status == SolveStatus::InvalidASN);
        CHECK(g.queries().size() == 5);
    }

    // 7. Estrazione casuale e batch.
    {
        std::istringstream in;
        std::ostringstream out;
        General g(in, out);
        CHECK(g.loadDataset("t_gen1.bz2").ok);
        g.setSeed(99);
        bool allValid = true;
        std::vector<std::pair<ASN, ASN>> sequence;
        for (int i = 0; i < 300; ++i) {
            ASN a = 0, b = 0;
            std::string err;
            CHECK(g.randomPair(a, b, err));
            NodeId ia = 0, ib = 0;
            allValid = allValid && a != b && g.graph().findNode(a, ia) && g.graph().findNode(b, ib) &&
                       g.graph().inLargestComponent(ia) && g.graph().inLargestComponent(ib);
            sequence.push_back(std::make_pair(a, b));
        }
        CHECK(allValid);  // solo ID validi, distinti, dalla componente massimale

        std::istringstream in2;
        std::ostringstream out2;
        General g2(in2, out2);
        CHECK(g2.loadDataset("t_gen1.bz2").ok);
        g2.setSeed(99);
        bool same = true;
        for (int i = 0; i < 300; ++i) {
            ASN a = 0, b = 0;
            std::string err;
            g2.randomPair(a, b, err);
            same = same && sequence[i] == std::make_pair(a, b);
        }
        CHECK(same);  // stesso seme, stessa sequenza

        // Batch: tutte le query vanno a buon fine, il seme e' riportato e riproducibile.
        g.setSeed(2026);
        const BatchSummary b1 = g.runRandomQueries(50);
        CHECK(b1.ok && b1.requested == 50 && b1.completed == 50);
        CHECK(b1.found == 50);  // coppie dalla componente massimale: sempre connesse
        CHECK(b1.seed == 2026 && b1.componentSize == 5);
        CHECK(b1.minNanos >= 0 && b1.minNanos <= b1.avgNanos && b1.avgNanos <= static_cast<double>(b1.maxNanos));
        CHECK(b1.totalNanos >= b1.maxNanos);
        CHECK(b1.avgWeight >= 1 && b1.avgMinLength >= 2);
        CHECK(g.batches().size() == 1 && g.queries().empty());
        CHECK(g.seed() != 2026);  // il batch successivo parte da un seme nuovo

        g.setSeed(2026);
        const BatchSummary b2 = g.runRandomQueries(50);
        CHECK(b2.seed == 2026 && b2.avgWeight == b1.avgWeight && b2.avgMinLength == b1.avgMinLength);
        CHECK(g.batches().size() == 2);

        // Batch non validi: nulla viene registrato.
        CHECK(!g.runRandomQueries(0).ok);
        CHECK(!g.runRandomQueries(General::kMaxBatchQueries + 1).ok);
        CHECK(g.batches().size() == 2);

        // Componente massimale con un solo nodo: nessuna coppia possibile.
        std::istringstream in3;
        std::ostringstream out3;
        General g3(in3, out3);
        writeBz2("t_gen_single.bz2", line("1") + line("2"));
        CHECK(g3.loadDataset("t_gen_single.bz2").ok);
        std::string err;
        ASN a = 0, b = 0;
        CHECK(!g3.randomPair(a, b, err) && !err.empty());
        CHECK(!g3.runRandomQueries(5).ok && g3.batches().empty());
        std::remove("t_gen_single.bz2");
    }

    // 8. File di esportazione.
    {
        std::istringstream in;
        std::ostringstream out;
        General g(in, out);

        // Senza query: il file delle query lo dice.
        CHECK(g.loadDataset("t_gen1.bz2").ok);
        ExportResult e0 = g.exportStats("t_gen_e0");
        CHECK(e0.ok && e0.files.size() == 5);
        CHECK(readFile("t_gen_e0_queries.txt") == "Nessuna query eseguita.\n");
        CHECK(readFile("t_gen_e0_timings.txt").find("Solver") == std::string::npos);

        g.setSeed(2026);
        g.runQuery(100, 103);                         // manuale, con cammino
        g.runQuery(100, 200);                         // errore: componenti separate
        g.runQuery(101, 104, QueryOrigin::Random);    // origine "random"
        g.runRandomQueries(10);

        const ExportResult e = g.exportStats("t_gen_export.txt");  // l'estensione .txt viene tolta
        CHECK(e.ok && e.files.size() == 5);
        CHECK(e.files[0] == "t_gen_export_summary.txt" && e.files[1] == "t_gen_export_graph.txt" &&
              e.files[2] == "t_gen_export_distribution.txt" && e.files[3] == "t_gen_export_queries.txt" &&
              e.files[4] == "t_gen_export_timings.txt");
        for (const std::string& f : e.files) CHECK(fileExists(f));

        CHECK(readFile("t_gen_export_summary.txt") ==
              "dataset_files: t_gen1.bz2\n"
              "paths_read: 5\n"
              "paths_added: 5\n"
              "paths_rejected: 0\n"
              "lines_skipped: 1\n"
              "nodes: 8\n"
              "edges: 6\n"
              "largest_component_nodes: 5\n"
              "largest_component_edges: 5\n"
              "queries_single: 3\n"
              "queries_batch: 1\n");

        CHECK(readFile("t_gen_export_graph.txt") ==
              "100 | 101 | 1\n100 | 102 | 1\n100 | 104 | 1\n101 | 102 | 1\n102 | 103 | 2\n200 | 201 | 1\n");

        CHECK(readFile("t_gen_export_distribution.txt") == "1 | 5\n2 | 1\n");

        const std::string queries = readFile("t_gen_export_queries.txt");
        CHECK(contains(queries,
                       "- Query numero 1 -\n\n"
                       "Origine dei nodi: manuale\n"
                       "Source: 100\n"
                       "Target: 103\n"
                       "Esito della query: positivo\n"
                       "Peso cammino minimax ottimale: 2\n"
                       "Numero cammini minimax ottimali (limite inferiore): 2\n"
                       "Numero cammini minimax ottimali più corti: 1\n"
                       "Path di un cammino minimax ottimale più corto: 100 -> 102 -> 103\n\n"));
        CHECK(contains(queries,
                       "- Query numero 2 -\n\n"
                       "Origine dei nodi: manuale\n"
                       "Source: 100\n"
                       "Target: 200\n"
                       "Esito della query: errore\n"));
        CHECK(contains(queries, "Motivo dell'errore: nessun percorso tra AS100 e AS200"));
        CHECK(contains(queries, "- Query numero 3 -\n\nOrigine dei nodi: random\nSource: 101\nTarget: 104\n"));
        CHECK(contains(queries,
                       "- Batch numero 1 -\n\n"
                       "Query eseguite: 10\n"
                       "Query andate a buon fine: 10\n"
                       "Seme del generatore casuale: 2026\n"
                       "Tempo medio per query: "));
        CHECK(contains(queries, "\nTempo minimo: ") && contains(queries, "\nTempo massimo: ") &&
              contains(queries, "\nTempo totale: "));
        CHECK(contains(queries, "\nPeso minimax medio: ") &&
              contains(queries, "\nLunghezza media dei cammini piu' corti: "));

        const std::string timings = readFile("t_gen_export_timings.txt");
        CHECK(timings.rfind("Tempo di esecuzione di DataLoader: ", 0) == 0);
        CHECK(contains(timings, "\nTempo di esecuzione di ASGraph: "));
        CHECK(contains(timings, "\nTempo di esecuzione di Solver nella query numero 1: "));
        CHECK(contains(timings, "\nTempo di esecuzione di Solver nella query numero 3: "));
        CHECK(!contains(timings, "query numero 4"));
        CHECK(contains(timings, "\nTempo totale di esecuzione: "));
        CHECK(countOccurrences(timings, "\n") == 2 + 3 + 1);

        // Due dataset caricati: i totali sono cumulativi.
        CHECK(g.loadDataset("t_gen2.bz2").ok);
        CHECK(g.exportStats("t_gen_export2").ok);
        const std::string summary2 = readFile("t_gen_export2_summary.txt");
        CHECK(contains(summary2, "dataset_files: t_gen1.bz2, t_gen2.bz2\n"));
        CHECK(contains(summary2, "paths_read: 7\n") && contains(summary2, "paths_added: 7\n"));
        CHECK(contains(summary2, "nodes: 9\n") && contains(summary2, "edges: 8\n"));
        CHECK(contains(summary2, "largest_component_nodes: 6\n") && contains(summary2, "largest_component_edges: 6\n"));
        CHECK(readFile("t_gen_export2_distribution.txt") == "1 | 7\n2 | 1\n");

        // Pesi senza archi: la distribuzione va da 1 al massimo e riporta anche gli zeri.
        std::istringstream inGap;
        std::ostringstream outGap;
        General gap(inGap, outGap);
        writeBz2("t_gen_gap.bz2", line("10|11") + line("10|11") + line("10|11") + line("20|21"));
        CHECK(gap.loadDataset("t_gen_gap.bz2").ok);
        CHECK(gap.exportStats("t_gen_gap").ok);
        CHECK(readFile("t_gen_gap_distribution.txt") == "1 | 1\n2 | 0\n3 | 1\n");
        std::remove("t_gen_gap.bz2");

        // Nome vuoto: prefisso predefinito. Cartella inesistente: errore senza eccezioni.
        CHECK(g.exportStats("").ok && fileExists("analisi_summary.txt") && fileExists("analisi_timings.txt"));
        const ExportResult bad = g.exportStats("cartella_che_non_esiste/x");
        CHECK(!bad.ok && contains(bad.error, "cartella_che_non_esiste"));
    }

    // 9. Interfaccia a menu con input scriptato.
    {
        int rc = -1;
        const std::string banner =
            "- ASGraph Solver - analisi sperimentale sulla topologia delle reti di Autonomous Systems";

        // 9a. Avvio senza argomenti: richiesta del percorso, estensione errata, poi file valido.
        std::string out = session("", "t_gen_notbz2.txt\nt_gen1.bz2\n0\n", &rc);
        CHECK(rc == 0);
        CHECK(contains(out, banner));
        CHECK(contains(out, "Nessun dataset rilevato. Inserisci il percorso del file .bz2: "));
        CHECK(contains(out, "Il file deve avere estensione .bz2."));
        CHECK(contains(out, "Dataset indicato: t_gen1.bz2\nCaricamento completato: t_gen1.bz2\n"));
        CHECK(contains(out, "Cammini letti: 5 (righe scartate: 1)\n"));
        CHECK(contains(out, "- Analisi Strutturale del grafo -\n\nNumero nodi: 8\nNumero archi: 6\nTempi: DataLoader: "));
        CHECK(contains(out, ", ASGraph: ") && contains(out, ", totale "));
        CHECK(contains(out,
                       "- Menu di scelta -\n\n"
                       "1) Nuova query sul grafo corrente\n"
                       "2) N query randomiche sul grafo corrente\n"
                       "3) Carica un altro file .bz2\n"
                       "4) Esporta le statistiche\n"
                       "0) Esci\n"));
        CHECK(contains(out, "Arrivederci."));

        // 9b. Avvio con file da riga di comando: nessuna richiesta del percorso.
        out = session("t_gen1.bz2", "0\n", &rc);
        CHECK(rc == 0 && !contains(out, "Nessun dataset rilevato"));
        CHECK(contains(out, "Dataset indicato: t_gen1.bz2") && contains(out, "Caricamento completato: t_gen1.bz2"));

        // 9c. Argomento non valido: errore, ma il menu parte lo stesso.
        out = session("t_gen_notbz2.txt", "0\n", &rc);
        CHECK(contains(out, "ERRORE nel caricamento") && contains(out, "- Menu di scelta -") && rc == 0);
        CHECK(!contains(out, "Caricamento completato"));
        out = session("file_che_non_esiste.bz2", "0\n", &rc);
        CHECK(contains(out, "ERRORE nel caricamento") && contains(out, "No such file"));

        // 9d. Sequenzialita': senza dataset niente query, batch ed esportazione.
        out = session("", "\n1\n2\n4\n3\n\n0\n", &rc);
        CHECK(contains(out, "Nessun dataset caricato: usa l'opzione 3 del menu per caricarne uno."));
        CHECK(countOccurrences(out, "Operazione non disponibile") == 3);
        CHECK(!contains(out, "- Analisi della Query -") && !contains(out, "- Analisi del batch -"));
        CHECK(contains(out, "Operazione annullata."));  // opzione 3 annullata con invio vuoto

        // 9e. Menu di una nuova query e inserimento manuale con input non valido.
        out = session("t_gen1.bz2", "1\n1\nabc\nAS999\n0\nAS100\nAS103\n0\n", &rc);
        CHECK(contains(out,
                       "1) Inserimento manuale degli ASN\n"
                       "2) Inserimento randomico degli ASN (dalla componente connessa massimale)\n"
                       "0) Annulla\n"));
        CHECK(contains(out, "\"abc\" non e' un numero valido"));
        CHECK(contains(out, "AS999 non e' presente nel grafo"));
        CHECK(contains(out, "riservato"));
        CHECK(contains(out, "- Analisi della Query -\n\n"));
        CHECK(contains(out, "Coppia inserita: AS100 e AS103\nQuery tra AS100 e AS103 (manuale)\n"));
        CHECK(contains(out, "Peso del cammino minimax: 2\n"));
        CHECK(contains(out, "Numero cammini minimax (limite inferiore): 2\n"));
        CHECK(contains(out, "Numero cammini minimax piu' corti (conteggio esatto): 1\n"));
        CHECK(contains(out, "Lunghezza dei cammini minimax più corti: 3 nodi\n"));
        CHECK(contains(out, "Tracciato di uno dei cammini minimax ottimali: AS100 -> AS102 -> AS103\n"));
        CHECK(contains(out, "Tempo di esecuzione del Solver: "));
        CHECK(countOccurrences(out, "- Menu di scelta -") == 2);  // il menu torna dopo la query

        // 9f. Inserimento randomico e annullamenti.
        out = session("t_gen1.bz2", "1\n2\n0\n", &rc);
        CHECK(contains(out, "Coppia estratta: AS") && contains(out, "(casuale)"));
        CHECK(contains(out, "Peso del cammino minimax:"));
        out = session("t_gen1.bz2", "1\n0\n1\n1\nq\n0\n", &rc);
        CHECK(!contains(out, "- Analisi della Query -") && contains(out, "Query annullata."));

        // 9g. Query senza percorso.
        out = session("t_gen1.bz2", "1\n1\nAS100\nAS200\n0\n", &rc);
        CHECK(contains(out, "Esito: nessun percorso tra AS100 e AS200"));

        // 9h. Batch: valori non validi, poi 30 query.
        out = session("t_gen1.bz2", "2\nabc\n0\n30\n0\n", &rc);
        CHECK(countOccurrences(out, "Valore non valido") == 2);
        CHECK(contains(out, "- Analisi del batch -\n\nQuery eseguite: 30\nSeme del generatore casuale: 12345\n"));
        for (const char* label : {"Tempo medio per query: ", "Tempo minimo: ", "Tempo massimo: ", "Tempo totale: ",
                                  "Peso minimax medio: ", "Lunghezza media dei cammini piu' corti: "}) {
            CHECK(contains(out, label));
        }

        // 9i. Caricare un altro file: il grafo si aggiorna e l'interfaccia riparte da capo.
        out = session("t_gen1.bz2", "3\nt_gen2.bz2\n0\n", &rc);
        CHECK(countOccurrences(out, banner) == 2);
        CHECK(contains(out, "Dataset indicato: t_gen2.bz2\nCaricamento completato: t_gen2.bz2\n"));
        CHECK(contains(out, "Cammini letti: 2 (righe scartate: 0)\n"));
        CHECK(contains(out, "Numero nodi: 9\nNumero archi: 8\n"));

        // 9j. Caricamento non riuscito: grafo invariato, messaggio, nuova richiesta in caso di estensione errata.
        out = session("t_gen1.bz2", "3\nfile_che_non_esiste.bz2\n3\nt_gen_notbz2.txt\n\n0\n", &rc);
        CHECK(contains(out, "ERRORE nel caricamento di file_che_non_esiste.bz2") && contains(out, "non e' stato modificato"));
        CHECK(contains(out, "Il file deve avere estensione .bz2.") && contains(out, "Operazione annullata."));
        CHECK(countOccurrences(out, banner) == 1);

        // 9k. Esportazione dalla UI, dopo una query.
        out = session("t_gen1.bz2", "1\n2\n4\nt_gen_ui\n0\n", &rc);
        CHECK(contains(out, "Esportazione completata. File creati:") && contains(out, "t_gen_ui_queries.txt"));
        CHECK(contains(out, "t_gen_ui_distribution.txt"));
        CHECK(fileExists("t_gen_ui_summary.txt") && fileExists("t_gen_ui_graph.txt") &&
              fileExists("t_gen_ui_distribution.txt") && fileExists("t_gen_ui_queries.txt") &&
              fileExists("t_gen_ui_timings.txt"));
        CHECK(contains(readFile("t_gen_ui_summary.txt"), "queries_single: 1\nqueries_batch: 0\n"));
        CHECK(contains(readFile("t_gen_ui_queries.txt"), "Origine dei nodi: random"));

        // 9l. Menu: scelte non valide e fine dell'input senza cicli infiniti.
        out = session("t_gen1.bz2", "abc\n99\n-1\n0\n", &rc);
        CHECK(countOccurrences(out, "Scelta non valida") == 3 && rc == 0);
        out = session("t_gen1.bz2", "1\n1\nAS10", &rc);
        CHECK(rc == 0);
        out = session("", "", &rc);
        CHECK(rc == 0);
        out = session("t_gen1.bz2", "", &rc);
        CHECK(rc == 0 && contains(out, "- Menu di scelta -"));
    }

    // Pulizia dei file temporanei.
    const char* temps[] = {"t_gen1.bz2", "t_gen2.bz2", "t_gen_junk.bz2", "t_gen_empty.bz2", "t_gen_fake.bz2",
                           "t_gen_notbz2.txt", "analisi_summary.txt", "analisi_graph.txt", "analisi_distribution.txt",
                           "analisi_queries.txt", "analisi_timings.txt"};
    for (const char* f : temps) std::remove(f);
    for (const char* prefix : {"t_gen_e0", "t_gen_export", "t_gen_export2", "t_gen_ui", "t_gen_gap"}) {
        for (const char* suffix : {"_summary.txt", "_graph.txt", "_distribution.txt", "_queries.txt", "_timings.txt"}) {
            std::remove((std::string(prefix) + suffix).c_str());
        }
    }

    if (failures == 0) std::cout << "Tutti i test sono passati.\n";
    return failures == 0 ? 0 : 1;
}