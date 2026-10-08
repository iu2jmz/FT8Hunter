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
