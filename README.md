# Progetto ASD 2026: Algoritmi per Grafi di Autonomous Systems

## Breve descrizione del progetto
Il progetto mira all'analisi e alla manipolazione della topologia di rete per la trasmissione dei dati su internet a livello di *Autonomous Systems* (AS). Il software consiste nella lettura di dataset di packet routing, nella loro conversione in grafi AS e nell'assegnazione ad ogni arco di un peso che si basa sulla frequenza con cui essi compaiono nei cammini analizzati. L'obiettivo finale è quello di stabilire, dati due nodi, il cammino ottimale (secondo il criterio *minimax*) che li collega e contare il numero di cammini ottimi presenti nel grafo, corredando poi il tutto con l'analisi sperimentale dei dati.

# Moduli principali del software

## 1. Modulo lettura ed interpretazione dei dati (DataLoader)

### 1.1 Compiti principali del modulo

* Leggere i file in input
* Decomprimere i file con estensione .bz2
* Isolare la stringa di path che ci interessa
* Eliminare eventuali duplicati ridondanti (1234 1234 5678 -> 1234 5678)
* Utilizzare spazi e punteggiatura ( | ) tra gli AS per creare un vettore che identifica una path
* Restituire la sequenza pulita come output finale

### 1.2 Input:
Il modulo deve ricevere in input file .bz2 contenenti dataset dei path di AS.

### 1.3 Output:
Il modulo deve restituire in output vettori dinamici contenenti i cammini AS.

### 1.4 Funzionalità richieste:
Il modulo deve:
* Decomprimere i file .bz2 in input.
* Compiere il parsing delle stringhe di testo, individuando prima il blocco dei cammini AS e poi utilizzando la spaziatura e punteggiatura per isolare ciascun codice AS.
* Eliminare evnetuali self-loop, ovvero ripetizioni.
* Inserire i codici AS in un vettore dinamica che rappresenterà l'output del modulo.

Ciascuna di queste operazioni dovrà essere svolta riga per riga per evitare problemi di memoria, dunque il flow di lavoro sarà: decompressione di una riga, parsing, inserimento in vettore dinamico, restituzione del vettore al modulo successivo, ripetere da capo.


Per raggiungere lo scopo sarà necessario implementare:
* ...

### 1.5 Strutture dati, librerie e algoritmi necessari:
* Vettori dinamici
* Libreria libbz2
* Librerie string e sstream per il parsing

### 1.6 Complessità attesa:
Il tempo richiesto dovrebbe essere lineare rispetto alla dimensione del file di input.

### 1.7 Casi limite:
Il modulo deve restituire errore se il file in input è vuoto, o se non è presente al suo interno alcun cammino AS.

### 1.8 Interazioni con gli altri moduli:
L'output di DataLoader sarà utilizzato dal modulo successivo (ASGraph) come input.

## 2. Modulo di gestione del grafo (ASGraph)

### 2.1 Compiti principali del modulo

* Ricezione e costruzione dinamica del grafo AS partendo dalle sequenze fornite da DataLoader
* Mappatura degli ASN (Autonomous System Number) tramite tabelle hash
* Aggiornamento delle frequenze degli archi
* Individuazione della componente connessa massimale

### 2.2 Input:
Il modulo deve ricevere in input vettori dinamici di ASN forniti da DataLoader

### 2.3 Output:
Il modulo dovrà restituire in output un Grafo, con interfaccia interrogabile dagli altri moduli e che contiente tutte le informazioni definite di seguito. 

### 2.4 Funzionalità richieste:
Il modulo deve:
* Creare un grafo non orientato, dove c'è un arco tra i nodi ASNi e ASNj se e solo se la sequenza ASNi|ASNj è presente nei path AS
* Mappare gli ASN in interi consecutivi tramite tabelle hash per evitare sprechi di memoria
* Assegnare un peso ad ogni arco presente nel grafo: l'arco ASNi - ASNj ha peso x, dove x è la frequenza dell'apparizione della sequenza ASNi|ASNj o ASNj|ASNi nei path AS
* Aggiornare in maniera dinamica il grafo, dunque ad ogni inserimento di nodi è necessario aggiornare la frequenza degli archi interessati
* Individuare la componente connessa massimale all'interno del grafo

### 2.5 Strutture dati, librerie e algoritmi necessari:
* Tabella hash per la mappatura degli ASN
* Lista di adiacenza per la costruzione del grafo
* Struct personalizzata per associare destinazione e frequenza (peso) dell'arco
* Ricerca BFS per l'individuazione della componente connessa massimale

### 2.6 Complessità attesa:
Il tempo richiesto per la costruzione del grafo dovrebbe essere lineare rispetto alla dimensione dei vettori di input. Il tempo richiesto per la ricerca della componente connessa massimale dovrebbe essere lineare rispetto al numero dei nodi + il numero degli archi del grafo. 

### 2.7 Casi limite:
Il modulo deve gestire i casi limite come: input non accettabili, input vuoto, input vettore singolo (nodo senza archi), frequenza degli archi troppo elevata (overflow, usare interi a 64 bit), componenti connesse massimali non uniche (in caso di pareggio selezionare la componente connessa la cui somma dei pesi degli archi è maggiore).

### 2.8 Interazione con gli altri moduli:
Il modulo riceve in input vettori provenienti dal modulo DataLoader e deve anche fornire informazioni al modulo di analisi quali numero di nodi, numero di archi, dimensione della componente connessa massimale, distribuzione della frequenza degli archi. Inoltre deve fornire l'accesso a tutte le informazione del grafo al modulo Solver.

## 3. Modulo di risoluzione (Solver)

### 3.1 Compiti principali del modulo

* Ricevere due nodi bersaglio di cui calcolare il minimax
* Trovare il cammino minimax tra i due nodi e restituirne il valore
* Contare parallelamente il numero di cammini ottimali (sempre secondo la logica minimax) tra i due nodi 

### 3.2 Input:
Il modulo deve ricevere in input il codice di due AS dei quali si vuole calcolare peso e numero dei cammini minimax.

### 3.3 Output:
Il modulo dovrà restituire in output il peso del cammino minimax, quanti cammini ottimali ci sono tra il nodo ASNi e il nodo ASNj dati in input e la sequenza effettiva dei nodi presenti in uno dei cammini ottimali trovati.

### 3.4 Funzionalità richieste:
Il modulo deve:
* Trovare il path ottimale tra ASNi e ASNj dove il peso di un cammino è definito come il massimo peso tra i pesi degli archi attraversati dal cammino. Dunque vogliamo cercare il path tra ASNi e ASNj con il peso minore.
* Contare il numero di path tra ASNi e ASNj che hanno tale peso (questo va fatto parallelamente alla ricerca del cammino ottimale).

### - Approfondimento sul conteggio di cammini ottimali:
Nel conteggio si presenta un problema dovuto alla complessità attesa: contare il numero di cammini ottimali parallelamente alla ricerca in tempo lineare è impossibile e invece implementare un conteggio separato alla ricerca prevederebbe tempo esponenziale, se non fattoriale. L'algoritmo descritto nella specifica non conta TUTTI i cammini di peso minimo, ma rappresenta un limite inferiore. I motivi sono due:

* Prefissi non ottimali: con il massimo (Dijkstra modificato), a differenza della somma (Dijkstra classico), un cammino ottimale può avere prefissi non ottimali, perché l'arco più pesante "copre" i pesi precedenti. Esempio: archi 1-2 (1), 1-3 (2), 2-4 (3), 3-4 (4), 4-5 (10). I cammini 1-2-4-5 e 1-3-4-5 pesano entrambi 10, ma il secondo non viene contato.

* Parità di distanza: se due nodi hanno la stessa distanza minimax e sono collegati da un arco di peso pari a quella distanza, il risultato dipende dall'ordine di estrazione dalla coda.

Per aggirare questo problema il modulo prevede due versioni dell'algoritmo:
* Versione 1: conteggio classico parallelo alla ricerca. Questo restituisce un lower bound.
* Versione 2: conteggio dei cammini minimax CON LUNGHEZZA MINORE (con lunghezza si intende il numero di nodi attraversati da quel cammino).

Entrambi i dati verrano poi presentati nell'analisi sperimentale.

### 3.5 Strutture dati, librerie e algoritmi necessari:
* Utilizzo dell'algoritmo di Dijkstra modificato: durante la ricerca i pesi degli archi non andranno sommati come nel caso classico, ma andrà conservato solo il massimo peso degli archi attraversati.
* Coda di priorità per la ricerca con Dijkstra
* Vettori dinamici per la memorizzazione delle distanze minimax, dei contatori dei cammini e dei padri (per memorizzare la strada percorsa).

### 3.6 Complessità attesa:
Il tempo richiesto per la ricerca del cammino ottimale è $O((V + E) \log V)$, dove $V$ è il numero di nodi ed $E$ il numero di archi esplorati. Poiché il conteggio del numero di cammini ottimali avviene parallelamente durante la fase di rilassamento degli archi, la complessità del conteggio è assorbita da quella della ricerca, mantenendo il tempo totale di esecuzione a $O((V + E) \log V)$

### 3.7 Casi limite:
Il modulo dovrà gestire i casi limite: input non validi (ASN non validi, ASN non presenti nel grafo), grafo vuoto, nodi sorgente e destinazione coincidenti (che devono restituire costo 0 e 1 cammino ottimale), nodi di input validi ma appartenenti a componenti connesse separate (che devono restituire l'assenza di percorsi), e il potenziale overflow matematico per il numero di cammini ottimali.

### 3.8 Interazione con gli altri moduli:
Il modulo deve accedere all'interfaccia di ASGraph per consultare il grafo e ricavare tutte le informazioni relative ad esso. Inoltre deve interagire con il modulo di analisi, al quale dovrà fornire l'output.

## 4. Modulo di analisi (General)

### 4.1 Compiti principali del modulo

* Coordinare il flusso di esecuzione (parsing, cotruzione del grafo, ricerca e conteggio minimax paths,...)
* Calcolare e stampare i dati strutturali della rete (numero di nodi, numero di archi, dimensione della componente connessa massimale, distribuzione della frequenza degli archi)
* Misurare la tempistica di esecuzione (totale e dei vari step)
* Fornire un'interfaccia per la scelta dell'inserimento dei nodi sorgente-destinazione (manuale o randomica)
* Stampare per ogni query il costo del cammino minimax e il numero di percorsi ottimi individuati

### 4.2 Strutture dati necessarie
### 4.3 Funzioni implementate
### 4.4 Interazioni con gli altri moduli