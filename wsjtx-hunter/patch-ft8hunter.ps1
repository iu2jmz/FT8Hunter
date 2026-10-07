param(
    [Parameter(Mandatory=$true)][string]$SourceDir,
    [string]$HunterVersion = "1.0.1"
)

$ErrorActionPreference = "Stop"

function Replace-Required([string]$Path, [string]$Old, [string]$New) {
    $full = Join-Path $SourceDir $Path
    if (-not (Test-Path $full)) { throw "File non trovato: $full" }
    $text = Get-Content $full -Raw
    if (-not $text.Contains($Old)) { throw "Pattern non trovato in $Path : $Old" }
    $text = $text.Replace($Old, $New)
    Set-Content -Path $full -Value $text -Encoding utf8
}

# Branding applicativo. DSP, decoder e sincronizzazione restano quelli originali WSJT-X.
Replace-Required "main.cpp" 'a.setApplicationName ("WSJT-X");' 'a.setApplicationName ("FT8 Hunter");'
Replace-Required "main.cpp" 'a.setApplicationVersion (version ());' ('a.setApplicationVersion ("' + $HunterVersion + '");')
Replace-Required "widgets/mainwindow.ui" '<string>WSJT-X   by K1JT</string>' ('<string>FT8 Hunter ' + $HunterVersion + ' - WSJT-X Engine</string>')

# About: identifica chiaramente il fork e mantiene copyright/licenza originali.
Replace-Required "widgets/about.cpp" '"WSJT-X implements a number of digital modes designed for <br />"' '"FT8 Hunter is a modified derivative of WSJT-X. <br />"'

# Copia il modulo FT8 Hunter Log4OM/MySQL dentro il sorgente WSJT-X.
$hunterDir = Join-Path $SourceDir "FT8Hunter"
New-Item -ItemType Directory -Force -Path $hunterDir | Out-Null
Copy-Item "wsjtx-hunter/log4om/Log4OmMysql.hpp" (Join-Path $hunterDir "Log4OmMysql.hpp") -Force
Copy-Item "wsjtx-hunter/log4om/Log4OmMysql.cpp" (Join-Path $hunterDir "Log4OmMysql.cpp") -Force
Copy-Item "wsjtx-hunter/log4om/Log4OmMysqlDialog.hpp" (Join-Path $hunterDir "Log4OmMysqlDialog.hpp") -Force
Copy-Item "wsjtx-hunter/log4om/Log4OmMysqlDialog.cpp" (Join-Path $hunterDir "Log4OmMysqlDialog.cpp") -Force

# Compila il modulo Log4OM/MySQL insieme all'applicazione principale.
Replace-Required "CMakeLists.txt" @'
  widgets/mainwindow.cpp
  Configuration.cpp
'@ @'
  widgets/mainwindow.cpp
  FT8Hunter/Log4OmMysql.cpp
  FT8Hunter/Log4OmMysqlDialog.cpp
  Configuration.cpp
'@

# Menu FT8 Hunter -> Log4OM MySQL... e ricarica worked-before dopo il salvataggio.
Replace-Required "widgets/mainwindow.cpp" @'
#include "Network/eqsl.h"
'@ @'
#include "Network/eqsl.h"
#include "FT8Hunter/Log4OmMysqlDialog.hpp"
'@

Replace-Required "widgets/mainwindow.cpp" @'
  ui->setupUi(this);
  setUnifiedTitleAndToolBarOnMac (true);
'@ @'
  ui->setupUi(this);

  // FT8 Hunter: configurazione del log remoto Log4OM/MySQL.
  auto * hunterMenu = ui->menuTools->addMenu (tr ("FT8 Hunter"));
  auto * log4omMysqlAction = hunterMenu->addAction (tr ("Log4OM MySQL..."));
  connect (log4omMysqlAction, &QAction::triggered, [this] {
      Log4OmMysqlDialog dlg {this};
      if (QDialog::Accepted == dlg.exec ())
        {
          showStatusMessage (tr ("FT8 Hunter: ricarico il log Log4OM/MySQL..."));
          m_logBook.rescan ();
        }
    });

  setUnifiedTitleAndToolBarOnMac (true);
'@

# Integra il database remoto nel motore Worked Before nativo di WSJT-X.
Replace-Required "logbook/WorkedBefore.cpp" @'
#include "pimpl_impl.hpp"
'@ @'
#include "pimpl_impl.hpp"
#include "FT8Hunter/Log4OmMysql.hpp"
'@

Replace-Required "logbook/WorkedBefore.cpp" @'
    return worked;
  }
}

class WorkedBefore::impl final
'@ @'
    return worked;
  }

  worked_before_database_type loader_mysql (Log4OmMysqlSettings const& settings, AD1CCty const * prefixes)
  {
    worked_before_database_type worked;
    QString error;
    auto const rows = Log4OmMysql::loadWorkedQsos (settings, &error);
    if (!error.isEmpty ())
      {
        throw LoaderException (std::runtime_error {
          QCoreApplication::translate ("WorkedBefore", "Log4OM MySQL read error: %1")
            .arg (error).toLocal8Bit ().constData ()});
      }

    for (auto const& row : rows)
      {
        auto const& entity = prefixes->lookup (row.callsign);
        worked.emplace (row.callsign.toUpper (),
                        row.gridsquare.left (4).toUpper (),
                        row.band.toUpper (),
                        row.mode.toUpper (),
                        entity.entity_name,
                        entity.continent,
                        entity.CQ_zone,
                        entity.ITU_zone);
      }
    return worked;
  }
}

class WorkedBefore::impl final
'@

Replace-Required "logbook/WorkedBefore.cpp" @'
  void reload ()
  {
    prefixes_.reload (configuration_);
    async_loader_ = QtConcurrent::run (loader, path_, &prefixes_);
    loader_watcher_.setFuture (async_loader_);
  }
'@ @'
  void reload ()
  {
    prefixes_.reload (configuration_);
    auto const mysql = Log4OmMysql::loadSettings ();
    if (mysql.enabled)
      {
        async_loader_ = QtConcurrent::run (loader_mysql, mysql, &prefixes_);
      }
    else
      {
        async_loader_ = QtConcurrent::run (loader, path_, &prefixes_);
      }
    loader_watcher_.setFuture (async_loader_);
  }
'@

Replace-Required "widgets/mainwindow.cpp" @'
          MessageBox::warning_message (this, tr ("Error Scanning ADIF Log"), error);
'@ @'
          MessageBox::warning_message (this, tr ("Error loading worked-before log"), error);
'@

Write-Host "Applied FT8 Hunter $HunterVersion branding + read-only Log4OM MySQL integration."
