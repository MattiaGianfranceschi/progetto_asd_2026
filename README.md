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

### 1.5 Strutture dati e librerie necessarie:
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

* Ricezione e costruzione graduale del grafo AS partendo dalle sequenze fornite da DataLoader
* Mappatura dei ID degli AS tramite tabelle hash
* Aggiornamento delle frequenze degli archi
* Individuazione della componente connessa massimale

### 2.2 Strutture dati necessarie

* Tabella hash per la mappatura degli ID
* Lista di adiacenza per la costruzione del grafo
* Struct personalizzata per associare destinazione e frequenza (peso) dell'arco
* Ricerca BFS per l'individuazione della componente connessa massimale

### 2.3 Funzioni implementate
### 2.4 Interazioni con gli altri moduli

## 3. Modulo di risoluzione (Solver)

### 3.1 Compiti principali del modulo

* Ricevere due nodi bersagli di cui calcolare il minimax
* Trovare il cammino minimax tra i due nodi e restituirne il valore
* Contare parallelamente il numero di cammini ottimali (sempre secondo la logica minimax) tra i due nodi 

### 3.2 Strutture dati necessarie

* Utilizzo dell'algoritmo di Dijkstra modificato per la ricerca del cammino minimax

### 3.3 Funzioni implementate
### 3.4 Interazioni con gli altri moduli

## 4. Modulo di analisi (General)

### 4.1 Compiti principali del modulo

* Coordinare il flusso di esecuzione (parsing, cotruzione del grafo, ricerca e conteggio minimax paths,...)
* Calcolare e stampare i dati strutturali della rete (numero di nodi, numero di archi, dimenzione della componente connessa massimale, distribuzione della frequenza degli archi)
* Misurare la tempistica di esecuzione (totale e dei vari step)
* Fornire un'interfaccia per la scelta dell'inserimento dei nodi sorgente-destinazione (manuale o randomica)
* Stampare per ogni query il costo del cammino minimax e il numero di percorsi ottimi individuati

### 4.2 Strutture dati necessarie
### 4.3 Funzioni implementate
### 4.4 Interazioni con gli altri moduli