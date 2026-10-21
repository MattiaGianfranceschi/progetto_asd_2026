# Progetto ASD 2026: Algoritmi per Grafi di Autonomous Systems

## Breve descrizione del progetto
Il progetto mira all'analisi e alla manipolazione della topologia di rete per la trasmissione dei dati su internet a livello di *Autonomous Systems* (**AS**). Il software consiste nella lettura di dataset di packet routing, nella loro conversione in grafi AS e nell'assegnazione ad ogni arco di un peso che si basa sulla frequenza con cui essi compaiono nei cammini analizzati. L'obiettivo finale è quello di stabilire, dati due nodi, il cammino ottimale (secondo il criterio *minimax*) che li collega e contare il numero di cammini ottimi presenti nel grafo, corredando poi il tutto con l'analisi sperimentale dei dati.

# Moduli principali del software

## 1. Modulo lettura ed interpretazione dei dati (DataLoader)

### 1.1 Input:
Il modulo deve ricevere in input file `.bz2` contenenti dataset dei path di AS.

### 1.2 Output:
Il modulo deve restituire in output vettori dinamici contenenti i cammini AS presenti nel file in input.

### 1.3 Funzionalità richieste:
Il modulo deve:
* Decomprimere i file `.bz2` in input.
* Compiere il parsing delle stringhe di testo, individuando prima il blocco dei cammini AS e poi utilizzando la punteggiatura per isolare ciascun ASN (Autonomous System Number). Se una riga è dunque della forma: \
`routeviews/routeviews|5 1849|702|701|3479 141.165.0.0/16 i 158.43.133.48` \
sarà necessario isolare prima il blocco `1849|702|701|3479` e poi utilizzare `|` per separare ciascun ASN.
* Eliminare eventuali *self-loop*, ovvero ripetizioni consecutive dello stesso ASN.
* Inserire gli ASN in vettori dinamici che rappresenteranno l'output del modulo.

Ciascuna di queste operazioni dovrà essere svolta riga per riga per evitare problemi di memoria, dunque il flow di lavoro sarà: decompressione di una riga, parsing, inserimento in vettore dinamico, restituzione del vettore al modulo successivo, ripetere da capo.

### 1.4 Strutture dati, librerie e algoritmi necessari:
* Vettori dinamici.
* Libreria `libbz2`.
* Librerie `string` e `sstream` per il parsing.

### 1.5 Complessità attesa:
Il tempo richiesto dovrebbe essere lineare rispetto alla dimensione del file di input.

### 1.6 Casi limite:
Il modulo deve restituire errore se il file in input è vuoto, o se non è presente al suo interno alcun cammino AS.

### 1.7 Interazioni con gli altri moduli:
L'output di **DataLoader** sarà utilizzato dal modulo successivo (**ASGraph**) come input.

## 2. Modulo di gestione del grafo (ASGraph)

### 2.1 Input:
Il modulo deve ricevere in input vettori dinamici di ASN forniti da **DataLoader**

### 2.2 Output:
Il modulo dovrà restituire in output un Grafo, con interfaccia interrogabile dagli altri moduli e che contiente tutte le informazioni definite di seguito. 

### 2.3 Funzionalità richieste:
Il modulo deve:
* Mappare gli ASN in input in interi consecutivi tramite tabelle hash per evitare sprechi di memoria.
* Creare un grafo non orientato tramite liste di adiacenza, dove c'è un arco tra i nodi `ASNi` e `ASNj` se e solo se la sequenza `ASNi|ASNj` è presente nei path AS.
* Assegnare un peso ad ogni arco presente nel grafo: l'arco `ASNi - ASNj` ha peso `x`, dove `x` è la frequenza dell'apparizione della sequenza `ASNi|ASNj` o `ASNj|ASNi` nei path AS
* Aggiornare in maniera dinamica il grafo, dunque ad ogni inserimento di nodi è necessario aggiornare la frequenza degli archi interessati
* Individuare la componente connessa massimale all'interno del grafo
* Calcolare e conservare le informazioni strutturali del grafo, ovvero numero di nodi, numero di archi e il numero di archi presenti per ogni peso possibile (ovvero per ogni n intero positivo, quanti archi ASNi|ASNj ci sono con peso n).

### 2.4 Strutture dati, librerie e algoritmi necessari:
* Tabella hash per la mappatura degli ASN.
* Lista di adiacenza per la costruzione del grafo.
* Struct per associare destinazione e frequenza (peso) dell'arco.
* Ricerca *BFS* per l'individuazione della componente connessa massimale.

### 2.5 Complessità attesa:
Il tempo richiesto per la costruzione del grafo dovrebbe essere lineare rispetto alla dimensione dei vettori di input. Il tempo richiesto per la ricerca della componente connessa massimale dovrebbe essere $O(V+E)$, dove $V$ è il numero di nodi ed $E$ il numero di archi del grafo. 

### 2.6 Casi limite:
Il modulo deve gestire i casi limite come: input non accettabili, input vuoto, input vettore singolo (nodo senza archi), frequenza degli archi troppo elevata (*overflow*, usare interi a 64 bit).Nel caso di componenti connesse massimali non uniche selezionare la componente connessa la cui somma dei pesi degli archi è maggiore ed a ulteriore parità di nodi e di somma dei pesi, si seleziona la componente che contiene il nodo con l'ASN originale numericamente più basso.

### 2.7 Interazione con gli altri moduli:
Il modulo riceve in input vettori provenienti dal modulo **DataLoader** e deve fornire le informazioni strutturali del grafo a **General**. Inoltre deve fornire l'accesso a tutte le informazione del grafo al modulo **Solver**.

## 3. Modulo di risoluzione (Solver)

### 3.1 Input:
Il modulo deve ricevere in input il codice di due AS dei quali si vuole calcolare peso e numero dei cammini *minimax*.

### 3.2 Output:
Il modulo dovrà restituire in output: 
* Il peso del cammino *minimax* ottimale.
* Lower bound del numero di cammini *minimax* totali.
* Il numero esatto dei cammini *minimax* con lunghezza minima.
* La sequenza effettiva dei nodi di uno dei cammini ottimali.

### 3.3 Funzionalità richieste:
Il modulo deve:
* Trovare il cammino ottimale tra `ASNi` e `ASNj` dove il costo di un cammino è definito come il massimo peso tra i pesi degli archi attraversati dal cammino. Dunque vogliamo cercare il cammino tra `ASNi` e `ASNj` con il costo minore.
* Contare il numero di cammini tra `ASNi` e `ASNj` che hanno tale costo (questo va fatto parallelamente alla ricerca del cammino ottimale).

### - Approfondimento sul conteggio di cammini ottimali:
Nel conteggio si presenta un problema dovuto alla complessità attesa: contare il numero esatto di cammini ottimali parallelamente alla ricerca in tempo lineare è impossibile e invece implementare un conteggio separato alla ricerca prevederebbe tempo esponenziale, se non fattoriale. L'algoritmo descritto nella specifica non conta TUTTI i cammini di costo minimo, ma rappresenta un limite inferiore. I motivi sono due:

* Prefissi non ottimali: con il massimo (*Dijkstra* modificato), a differenza della somma (*Dijkstra* classico), un cammino ottimale può avere prefissi non ottimali, perché l'arco più pesante "copre" i pesi precedenti. Esempio: archi `1-2 (1), 1-3 (2), 2-4 (3), 3-4 (4), 4-5 (10)`. I cammini `1-2-4-5` e `1-3-4-5` pesano entrambi 10, ma il secondo non viene contato.

* Parità di distanza: se due nodi hanno la stessa distanza *minimax* e sono collegati da un arco di peso pari a quella distanza, il risultato dipende dall'ordine di estrazione dalla coda.

Per aggirare questo problema il modulo prevede due versioni dell'algoritmo:
* Versione 1: conteggio classico parallelo alla ricerca. Questo restituisce un lower bound.
* Versione 2: conteggio dei cammini minimax CON LUNGHEZZA MINIMA (con lunghezza si intende il numero di nodi attraversati da quel cammino).

Entrambi i dati verrano poi presentati nell'analisi sperimentale.

### 3.4 Strutture dati, librerie e algoritmi necessari:
* Utilizzo dell'algoritmo di *Dijkstra* modificato: durante la ricerca i pesi degli archi non andranno sommati come nel caso classico, ma andrà conservato solo il massimo peso degli archi attraversati.
* Coda di priorità per la ricerca con *Dijkstra*.
* Vettori dinamici per la memorizzazione delle distanze *minimax*, dei contatori dei cammini e dei padri (per memorizzare la strada percorsa).

### 3.5 Complessità attesa:
Il tempo richiesto per la ricerca del cammino ottimale è $O((V + E) \log V)$, dove $V$ è il numero di nodi ed $E$ il numero di archi esplorati. Poiché il conteggio del numero di cammini ottimali avviene parallelamente durante la fase di rilassamento degli archi, la complessità del conteggio è assorbita da quella della ricerca, mantenendo il tempo totale di esecuzione a $O((V + E) \log V)$

### 3.6 Casi limite:
Il modulo dovrà gestire i casi limite: input non validi (ASN non validi, ASN non presenti nel grafo), grafo vuoto, nodi sorgente e destinazione coincidenti (che devono restituire costo 0 e 1 cammino ottimale), nodi di input validi ma appartenenti a componenti connesse separate (che devono restituire l'assenza di percorsi), e il potenziale overflow matematico per il numero di cammini ottimali.

### 3.7 Interazione con gli altri moduli:
Il modulo deve accedere all'interfaccia di **ASGraph** per consultare il grafo e ricavare tutte le informazioni relative ad esso. Inoltre deve interagire con **General**, al quale dovrà fornire l'output.

## 4. Modulo di analisi (General)

### 4.1 Compiti principali del modulo

* Coordinare il flusso di esecuzione (parsing, cotruzione del grafo, ricerca e conteggio cammini minimax,...).
* Raccogliere i dati strutturali della rete (numero di nodi, numero di archi, distribuzione della frequenza degli archi).
* Misurare la tempistica di esecuzione (totale e dei vari step).
* Raccogliere per ogni query i dati: costo del cammino *minimax* e numero di percorsi ottimi individuati.

### 4.2 Input:
Il modulo deve ricevere in input:
* uno o potenzialmente più file `.bz2` dei path AS
* una o potenzialmente più coppie di ASN dei nodi su cui svolgere l'analisi

### 4.3 Output:
Il modulo dovrà restituire in output un'interfaccia di accesso ai dati dell'analisi sperimentale. In particolare, l'interfaccia da linea di comando dovrà essere così composta:

\- ASGraph Solver - analisi sperimentale sulla topologia delle reti di Autonomous Systems

Dataset indicato: *nome file dataset* \
Caricamento completato: *nome file dataset, se il caricamento è andato a buon fine* \
Cammini letti: *n cammini letti* (righe scartate: *n righe scartate*)

\- Analisi Strutturale del grafo -

Numero nodi: *numero nodi* \
Numero archi: *numero archi* \
Tempi: DataLoader: *tempo di esecuzione*, ASGraph: *tempo di esecuzione*, totale *tempo di esecuzione*

\- Menu di scelta -

1) Nuova query sul grafo corrente
2) N query randomiche sul grafo corrente
3) Carica un altro file `.bz2`
4) Esporta le statistiche
0) Esci

In particolare:
* l'opzione 1. dovrà a sua volta proporre le opzioni:

    1) Inserimento manuale degli ASN
    2) Inserimento randomico degli ASN (dalla componente connessa massimale)
    0) Annulla

    Una volta terminata l'esecuzione dovranno essere visualizzati:

    \- Analisi della Query -

    Coppia estratta: ASNi e ASNj \
    Query tra ASNi e ASNj (casuale)

    Peso del cammino minimax \
    Numero cammini minimax (limite inferiore) \
    Numero cammini minimax piu' corti (conteggio esatto) \
    Lunghezza dei cammini minimax più corti \
    Tracciato di uno dei cammini minimax ottimali: ASNi -> ASNi2 -> ... -> ASNj \
    Tempo di esecuzione del Solver \

* l'opzione 2. dovrà chiedere in input il numero di query da effettuare.

    Una volta terminata l'esecuzione dovranno essere visualizzati:

    \- Analisi del batch -

    Query eseguite \
    Seme del generatore casuale \
    Tempo medio per query \
    Tempo minimo \
    Tempo massimo \
    Tempo totale \
    Peso minimax medio \
    Lunghezza media dei cammini piu' corti \   

* l'opzione 3. dovrà chiedere il percorso file del nuovo dataset e poi, dopo che il grafo è stato aggiornato, riproporre l'interfaccia da capo.

* l'opzione 4. dovrà chiedere all'utente il nome con cui salvare i file in esportazione: *name*. Successivamente creare 5 file:
    * *name*_summary.txt con struttura:
    
        dataset_files: *files di dataset caricati* \
        paths_read: *numero di cammini letti* \
        paths_added: *numero di cammini aggiunti al grafo* \
        paths_rejected: *numero di cammini rifiutati* \
        lines_skipped: *numero di righe del dataset saltate* \
        nodes: *numero di nodi nel grafo* \
        edges: *numero di archi nel grafo* \
        largest_component_nodes: *numero di nodi nella componente connessa massimale* \
        largest_component_edges: *numero di archi nella componente connessa massimale* \
        queries_single: *numero di queries singole avviate* \
        queries_batch: *numero di batches di queries avviati*

    * *name*_graph.txt che rappresenta il grafo nella sua interezza. Deve avere il seguente formato per ogni riga:

        `ASNi | ASNj | peso dell'arco ASNi-ASNj`

    * *name*_distribution.txt dove viene riportata la distribuzione della frequenza degli archi in ordine crescente (da 1 a *max*, dove *max* è il massimo tra i pesi degli archi presenti nel grafo). Deve avere il seguente formato per ogni riga:

        `peso n | numero di archi con peso n`
    
    * *name*_queries.txt con struttura:

        \- Query numero *k* - (per la k-esima query singola)

        Origine dei nodi: (*random/manuale*) \
        Source: *ASNi* \
        Target: *ASNj* \
        Esito della query: (*positivo/errore*) \
        Peso cammino minimax ottimale: \
        Numero cammini minimax ottimali (limite inferiore): \
        Numero cammini minimax ottimali più corti: \
        Path di un cammino minimax ottimale più corto: *ASNi -> ... -> ASNj*

        \- Batch numero *l* - (per la l-esima batch di queries)
        
        Query eseguite: \
        Query andate a buon fine: \
        Seme del generatore casuale: \
        Tempo medio per query: \
        Tempo minimo: \
        Tempo massimo: \
        Tempo totale: \
        Peso minimax medio: \
        Lunghezza media dei cammini piu' corti:
    
    * *name*_timings.txt con struttura:
        
        Tempo di esecuzione di DataLoader:
        Tempo di esecuzione di ASGraph:
        Tempo di esecuzione di Solver nella query numero *i*:
        Tempo totale di esecuzione:

### 4.4 Funzionalità richieste:
Il modulo deve:
* Gestire l'input: sarà necessario controllare se l'utente ha avviato il programma fornendo un file (ad esempio digitando nel terminale `./ASGraph_Solver dataset.bz2`). Se l'utente lancia il programma "a vuoto" (solo `./ASGraph_Solver`), **General** dovrà rilevare l'assenza del parametro e attivare la modalità interattiva, stampando a schermo una richiesta come: "`Nessun dataset rilevato. Inserisci il percorso del file .bz2: `", richiedendo quindi all'utente di inserire un percorso file nel quale si trova il file `.bz2`.
* Avviare **DataLoader** passandogli il file `.bz2`.
* Avviare **ASGraph** passandogli l'output di **DataLoader**, vettore per vettore.
* Una volta finito di costruire il grafo, avviare **Solver** passandogli i due ASN richiesti in input.
* Raccogliere tutti i dati provenienti da **ASGraph** e **Solver** (numero di nodi, numero di archi, distribuzione della frequenza degli archi, il peso del cammino minimax ottimale, lower bound del numero di cammini minimax totali, il numero esatto dei cammini minimax con lunghezza minima, la sequenza effettiva dei nodi di uno dei cammini ottimali).
* Calcolare i tempi di esecuzione di ogni modulo e di tutto il processo.
* Fornire i dati all'utente tramite l'interfaccia descritta nella sezione 4.3.
* Gestire gli errori provenienti dagli altri moduli presentandoli all'utente.

### 4.5 Strutture dati, librerie e algoritmi necessari:
* Libreria `chrono` per calcolare i tempi di esecuzione in millisecondi/microsecondi.
* Libreria `random` per estrarre in modo statisticamente uniforme gli indici da passare al **Solver** durante i test automatizzati.
* Libreria `fstream` per generare e scrivere i risultati nei file `.csv`
* Librerie `iostream` e `limits` per la gestione sicura dell'input da tastiera: permettono di utilizzare `std::cin.clear()` e `std::cin.ignore()` per resettare lo stato di errore e svuotare il buffer quando l'utente inserisce tipi di dato sbalgliati (es. lettere al posto di numeri), prevenendo loop infiniti del terminale.
* Variabili `std::string` per catturare l'input da tastiera e struct/vettori di supporto temporanei per immagazzinare i risultati delle esecuzioni in batch prima di scriverli su file.

### 4.6 Complessità attesa:
La complessità di **General** dipende esclusivamente da cosa l'utente decide di chiamare:
* Gestione Menù e I/O: $O(1)$ per le operazioni di stampa e navigazione a schermo.
* Query Singola: la complessità è dominata da quella del **Solver**, ovvero $O((V+E)\log V)$.
* Esecuzione in batch (N query): la complessità temporale sarà $O(N \cdot ((V+E)\log V))$, dove $N$ è il numero di esecuzioni casuali richieste dall'utente.

### 4.7 Casi limite:
* Input utente non valido: il modulo deve gestire l'inserimento in input di lettere invece di numeri (ripulendo il buffer di input e riproponendo la richiesta senza crashare), di ASN non accettabili o di ASN non presenti nel grafo, catturando l'errore e avvisando l'utente. Inoltre deve controllare che il file in input abbia estensione `.bz2`.
* Estrazione casuale isolata: i nodi estratti randomicamente non devono essere "estratti a vuoto" (numeri interi generici fino a 4 miliardi), ma vincolati agli ASN validi di **ASGraph**. Inoltre, per evitare che test automatizzati massicci restituiscano sempre "`nessun cammino trovato`", la randomizzazione dovrebbe pescare solo nodi appartenenti alla componente connessa massimale.

### 4.8 Interazione con gli altri moduli:
Il modulo interagisce con tutti gli altri, istanziando e distruggendo gli oggetti di **DataLoader**, **ASGraph** e **Solver**, passando gli input e output tra di loro e interrogando i metodi const in sola lettura di ogni modulo per recuperare i dati e impacchettarli nei report finali.