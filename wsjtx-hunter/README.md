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
