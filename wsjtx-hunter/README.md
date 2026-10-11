# FT8 Hunter v1 — WSJT-X base

Questa linea di sviluppo riparte da **WSJT-X** come base del motore digitale e della sincronizzazione audio/UTC.

- Versione iniziale FT8 Hunter: **1.0.0**
- Base upstream: repository ufficiale `WSJTX/wsjtx`
- Licenza: **GNU GPL v3**, in continuità con WSJT-X
- Installer Windows: auto-installante
- OmniRig: disponibile come componente opzionale nel setup

La prima milestone mantiene deliberatamente intatti DSP, FT8 decoder, audio timing e TX timing di WSJT-X. Le funzioni Hunter verranno aggiunte sopra questa base senza sostituire il motore originale.

Il prodotto è una versione modificata e indipendente: **non è supportato né approvato dal WSJT Development Group**. I copyright originali e la licenza GPL vengono mantenuti.

## v1.0.1 - Log4OM MySQL

FT8 Hunter puo' usare direttamente il database MySQL/MariaDB di Log4OM 2 su un altro PC della rete come sorgente **worked-before**.

- accesso in sola lettura alla tabella `log`
- host/IP e porta configurabili (default 3306)
- database, utente e password configurabili
- filtro opzionale per `stationcallsign`
- test connessione con conteggio QSO
- caricamento asincrono nel motore Worked Before di WSJT-X
- driver QMYSQL e runtime MariaDB inclusi nell'installer Windows

La configurazione si trova nel menu **Tools > FT8 Hunter > Log4OM MySQL...**.


## v1.0.4 - TLS MariaDB/Qt5

Correzione della connessione Log4OM su MariaDB Connector/C 3.4+ con Qt 5.
Quando la verifica del certificato TLS e' disattivata, FT8 Hunter imposta
`MARIADB_TLS_DISABLE_PEER_VERIFICATION=1` prima di creare il driver QMYSQL.
Questo consente l'uso di certificati locali/self-signed sulla LAN senza
disattivare TLS. Se la verifica e' attivata, il certificato deve essere
considerato attendibile dal sistema.

Build marker: v1.0.4 TLS Qt5 fix.


## v1.0.6 - Refresh automatico e filtro country sulla banda

- refresh automatico del Worked Before da Log4OM MySQL ogni 60 secondi, disattivabile dalla configurazione
- nuovo filtro selezionabile: **Escludi country/DXCC gia lavorati sulla banda corrente**
- il filtro considera il DXCC gia lavorato soltanto sulla banda attualmente selezionata; FT8 e FT4 sono trattati insieme e il filtro rispetta il bypass BP


## v1.0.7 - DXFun Cluster FT8/FT4 Hunter

- connessione Telnet automatica a `dxfun.com:8000`
- lettura continua di tutti gli spot cluster, con selezione dei soli spot che cadono nelle frequenze FT8/FT4 configurate in WSJT-X
- classificazione automatica `NEW DXCC` e `NEW su banda` usando il database Log4OM/MySQL gia' caricato nel Worked Before
- FT8 e FT4 sono considerati insieme per stabilire se un DXCC e' gia' lavorato
- pannello dedicato con Connetti/Disconnetti, AUTO QSY, filtri NEW DXCC / NEW su banda, FT8 / FT4, visualizzazione spot e pulsante Vai allo spot
- AUTO QSY con priorita' NEW DXCC > NEW su banda, permanenza minima configurabile e blocco durante TX/Auto/Tune
- la QSY imposta il modo corretto, il dial FT8/FT4 della banda e il marker RX sulla frequenza dello spot


## v1.0.8 - DXFun Hunter automatico HF

- cluster limitato alle sole bande HF **80m, 60m, 40m, 30m, 20m, 17m, 15m, 12m e 10m**
- campo dedicato per il nominativo di login a DXFun
- dopo una AUTO QSY FT8 Hunter ascolta la stazione spottata per **150 secondi**
- se il nominativo non viene decodificato entro 150 s, la caccia termina ed il cluster torna pronto per un nuovo spot
- quando il nominativo viene realmente decodificato, parte il QSO automatico FT8/FT4 con timeout massimo di **3 minuti**
- QSO concluso su 73/RR73: registrazione nel **log locale del programma**; il database MySQL Log4OM resta in sola lettura
- la rampa RF parte dal **50%** e aumenta di **9 punti percentuali** ad ogni successiva trasmissione senza risposta, fino al 100%; dopo una risposta la potenza resta congelata al livello raggiunto
- la regolazione RF CAT usa `RIG_LEVEL_RFPOWER` con Hamlib; con **OmniRig + IC-7300** usa il comando CI-V RF POWER `14 0A` (indirizzo 94h)


## v1.0.9 - Log QSO automatico dopo 4 secondi

- quando compare la finestra **Log QSO**, resta visibile per **4 secondi**
- trascorsi 4 secondi viene eseguita automaticamente la stessa conferma del pulsante **OK**
- il QSO viene quindi registrato normalmente e la finestra si chiude
- se l'operatore preme **OK** o **Cancel** prima dei 4 secondi, il timer non esegue una seconda azione
- rimossa la conferma immediata specifica del DXFun Hunter: anche i QSO automatici rispettano ora i 4 secondi


## v1.0.10 - Monitor Hunter

Nuova finestra separata **Tools > FT8 Hunter > Monitor Hunter...**.

- interruttore indipendente **Attiva Monitor Hunter automatico**
- l'automazione e' disponibile soltanto quando e' attivo **Escludi country/DXCC gia lavorati sulla banda corrente** e il bypass BP non e' attivo
- legge i CQ FT8/FT4 che superano realmente i filtri del monitor
- seleziona il **primo CQ valido** e conserva gli altri CQ recenti in coda
- ogni candidato in coda scade dopo 45 secondi se non viene piu' ricevuto; un CQ ripetuto aggiorna la propria presenza senza cambiare l'ordine
- avvia il normale motore WSJT-X / Auto Seq sul nominativo selezionato
- senza risposta effettua al massimo **8 trasmissioni** con RF Power **50%, 55%, 60%, 65%, 70%, 75%, 80%, 85%**
- appena viene ricevuta una risposta diretta, la rampa viene congelata al livello raggiunto e Auto Seq completa normalmente il QSO
- il passaggio al CQ successivo avviene soltanto quando il QSO e' stato realmente accettato dal **Log QSO**; resta quindi valida l'auto-conferma dopo 4 secondi introdotta in v1.0.9
- dopo 8 chiamate senza risposta, quel nominativo entra in pausa per 5 minuti per evitare cicli infiniti e viene provato il successivo CQ recente
- il Monitor Hunter non prende il controllo se radio, Auto, Tune o DXFun Hunter sono gia' impegnati
- chiudere la finestra Monitor Hunter disattiva l'automazione, evitando trasmissioni automatiche nascoste


## v1.0.11 - Monitor Hunter visuale e TX libera dal waterfall

- la finestra separata **Monitor Hunter** mostra ora uno stato grande e immediato: **ASCOLTO / CHIAMATA / QSO**
- tre indicatori dedicati mostrano **TARGET/RX**, **TX SLOT BLOCCATA** e progresso **x/8 + RF Power**
- nuova opzione **Scegli automaticamente una frequenza TX libera**
- la scelta automatica legge direttamente lo spettro che alimenta il **Grafico Ampio**
- viene mantenuta una media recente dell'energia del waterfall per evitare decisioni basate su un singolo istante
- per FT8/FT4 viene cercata la finestra a energia piu' bassa con margine di guardia, evitando i bordi del passabanda e almeno +/-100 Hz attorno alla stazione DX
- la **RX segue la stazione chiamata**, mentre la **TX resta bloccata** sullo slot scelto per tutta la sequenza
- il blocco usa il comportamento nativo **Hold Tx Freq** di WSJT-X e viene rimosso soltanto a fine QSO, dopo il logging, oppure in caso di abbandono
- lo stato Hold Tx Freq e la frequenza TX presenti prima dell'Hunter vengono ripristinati al termine del QSO
- se la selezione automatica e' disattivata, viene bloccata per il QSO la frequenza TX scelta manualmente dall'operatore
- restano invariati gli 8 tentativi con rampa **50, 55, 60, 65, 70, 75, 80, 85%**, con congelamento della potenza appena il DX risponde
