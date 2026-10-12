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


## v1.0.12 - Watchdog comune Hunter

La protezione dei QSO automatici e' ora condivisa da **Monitor Hunter** e **DXFun Cluster**.

- prima risposta: massimo **8 trasmissioni** con RF Power **50, 55, 60, 65, 70, 75, 80, 85%**
- la stessa rampa vale ora anche per i QSO automatici avviati dal cluster DXFun
- alla prima risposta valida del DX la potenza viene congelata al livello raggiunto
- dopo ogni messaggio valido del DX indirizzato a noi viene azzerato il watchdog di progresso
- dopo la risposta sono consentite al massimo **4 trasmissioni consecutive dello stesso step senza nuovo progresso**
- prima della quinta trasmissione senza progresso il QSO viene abbandonato, Auto viene fermato e Hunter passa al prossimo candidato
- se prima della risposta il DX viene decodificato mentre risponde a un'altra stazione, Hunter entra in **DX OCCUPATO - ATTESA** e sospende le trasmissioni
- il contatore delle chiamate e la potenza raggiunta vengono conservati durante la pausa
- se il DX torna in CQ entro **90 secondi**, Hunter riprende dal tentativo successivo; se torna direttamente verso di noi, riprende il QSO
- se il DX non torna disponibile entro 90 secondi, il target viene abbandonato
- la logica DX OCCUPATO e il watchdog di progresso valgono sia per Monitor Hunter sia per DXFun
- DXFun non puo' eseguire una nuova AUTO QSY mentre Monitor Hunter mantiene un target, anche durante lo stato DX OCCUPATO
- il pannello DXFun viene liberato soltanto dopo la reale accettazione del **Log QSO**, coerentemente con l'auto-conferma a 4 secondi


## v1.0.13 - QSY spot esatta e Band Hunter

### DXFun: frequenza cluster e split

- gli spot che dichiarano esplicitamente **FT8** o **FT4** nel commento vengono considerati QRG digitali esplicite
- per questi spot la frequenza indicata dal cluster diventa direttamente il **dial RX**
- la stessa QRG diventa anche la nuova **base TX**: setRig() riallinea m_freqNominal e m_freqTxNominal, quindi setXIT() ricostruisce lo split sulla nuova frequenza
- non rimane quindi alcun riferimento TX/split alla precedente frequenza FT8 standard
- per gli spot senza modo esplicito resta il comportamento precedente: riconoscimento FT8/FT4 per vicinanza alle frequenze configurate e uso del relativo offset audio
- il pannello distingue **QSY SPOT** da **QSY STANDARD**
- questa logica consente di seguire DXpedition e stazioni che lavorano su una propria frequenza FT8/FT4

### Band Hunter automatico

Nuova finestra **Tools > FT8 Hunter > Band Hunter...**.

- funzione attivabile separatamente
- ciclo: **80, 60, 40, 30, 20, 17, 15, 12, 10 m**, comprese le bande WARC
- ogni banda puo' essere inclusa o esclusa singolarmente
- permanenza normale: **15 minuti effettivi di ascolto per banda**
- tra le **23:00 e le 06:00 locali**, sulle bande **20/17/15/12/10 m** la permanenza scende a **4 minuti**
- 80/60/40/30 m restano a 15 minuti anche nella fascia notturna
- dopo ogni cambio banda ci sono **5 secondi di stabilizzazione** prima di riattivare il Monitor Hunter
- il tempo di permanenza non avanza mentre Monitor Hunter sta chiamando, gestendo **DX OCCUPATO** o completando un QSO
- **DXFun AUTO QSY ha sempre priorita'**: Band Hunter sospende il Monitor Hunter, conserva il tempo residuo della banda e, terminata la caccia cluster, ritorna sulla banda del ciclo
- Band Hunter usa il normale **Monitor Hunter** come motore CQ, quindi conserva filtri Worked/DXCC, TX slot libero dal waterfall, 8 chiamate, pausa DX occupato e watchdog 4/4 della v1.0.12
- chiudere la finestra Band Hunter disattiva il ciclo automatico
- le frequenze di banda sono prese dalla lista FT8 configurata in WSJT-X; una banda non disponibile viene saltata


## v1.0.14 stabile - funzioni integrate

La 1.0.14 integra nella linea stabile tutte le funzioni collaudate nella 1.0.13 Beta.
Mantiene il nome applicazione **FT8 Hunter**, quindi riutilizza direttamente le configurazioni
della 1.0.12/1.0.13, inclusi radio/audio e Log4OM/MySQL.

### Branding IU2JMZ

- logo unico **IU2JMZ / FT8 Hunter** per eseguibile, taskbar/finestra, installer, collegamenti Windows e splash
- la 1.0.14 usa l'identita' stabile FT8 Hunter e conserva i dati/configurazioni esistenti

### Disinstallazione Windows

- voce dedicata **FT8 Hunter 1.0.14** in App installate / Programmi e funzionalita'
- disinstallazione standard di programma e collegamenti
- durante la rimozione viene chiesto se eliminare anche configurazioni e dati personali
- la scelta predefinita conserva i dati personali

### Monitor Hunter - blacklist 5 ore

- una stazione che arriva a **8/8 chiamate senza alcuna risposta** entra in blacklist per **5 ore**
- la blacklist non si applica al watchdog 4/4 dopo che il DX aveva gia' risposto
- i CQ dei nominativi in blacklist vengono ignorati fino alla scadenza
- finestra dedicata con nominativo, banda, ora di inserimento e tempo residuo
- pulsanti **Riabilita** e **Riabilita tutte**
- la blacklist viene salvata nelle impostazioni e sopravvive al riavvio della Beta

### PSK Reporter

Nuova finestra **Tools > FT8 Hunter > PSK Reporter...**.

- sorgente MQTT near-real-time separata dal DXFun Cluster
- classificazione PSK abilitata solo dopo il caricamento riuscito del Worked Before/Log4OM; in errore non produce falsi NEW
- la 1.0.14 ricarica esplicitamente Worked Before/Log4OM all'avvio del pannello PSK
- ascolto dei report FT8/FT4 sulle bande 80/60/40/30/20/17/15/12/10 m
- classificazione NEW DXCC / NEW su banda tramite lo stesso Worked Before Log4OM
- deduplicazione e scarto dei report vecchi
- la frequenza PSK Reporter viene trattata come frequenza RF del segnale e convertita in dial + offset audio
- un report PSK e' soltanto un candidato: dopo la QSY la radio ascolta localmente
- **nessuna trasmissione parte sulla sola base del report PSK**
- il QSO viene affidato al Monitor Hunter soltanto se il decoder locale riceve realmente un CQ del target
- DXFun mantiene priorita' rispetto a PSK Reporter; Band Hunter resta sospeso durante la verifica del candidato
