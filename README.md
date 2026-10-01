# FT8 Hunter

Primo banco prova del progetto FT8 Hunter.

Questa milestone contiene esclusivamente:

- controllo radio IC-7300 tramite OmniRig;
- motore FT8 interno basato su HamDigiSharp;
- acquisizione audio USB del 7300;
- self-test encode/decode FT8;
- build Windows x64 self-contained come `FT8Hunter_Test.exe`.

Non contiene ancora Hunter, PSK Reporter, DX Cluster, Log4OM/MySQL o automazione QSO.

## Build automatica

GitHub Actions compila il progetto su Windows e pubblica una prerelease `core-test-latest` con l'EXE di prova.

## Licenza

Il motore HamDigiSharp è GPLv3-or-later; questa milestone è pertanto destinata a una distribuzione compatibile GPLv3-or-later.
