param(
    [Parameter(Mandatory=$true)][string]$SourceDir,
    [string]$HunterVersion = "1.0.7"
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
Copy-Item "wsjtx-hunter/cluster/DxFunCluster.hpp" (Join-Path $hunterDir "DxFunCluster.hpp") -Force
Copy-Item "wsjtx-hunter/cluster/DxFunCluster.cpp" (Join-Path $hunterDir "DxFunCluster.cpp") -Force
Copy-Item "wsjtx-hunter/cluster/DxFunClusterPanel.hpp" (Join-Path $hunterDir "DxFunClusterPanel.hpp") -Force
Copy-Item "wsjtx-hunter/cluster/DxFunClusterPanel.cpp" (Join-Path $hunterDir "DxFunClusterPanel.cpp") -Force

# Compila il modulo Log4OM/MySQL insieme all'applicazione principale.
Replace-Required "CMakeLists.txt" @'
  widgets/mainwindow.cpp
  Configuration.cpp
'@ @'
  widgets/mainwindow.cpp
  FT8Hunter/Log4OmMysql.cpp
  FT8Hunter/Log4OmMysqlDialog.cpp
  FT8Hunter/DxFunCluster.cpp
  FT8Hunter/DxFunClusterPanel.cpp
  Configuration.cpp
'@

# Menu FT8 Hunter -> Log4OM MySQL... e ricarica worked-before dopo il salvataggio.
Replace-Required "widgets/mainwindow.cpp" @'
#include "Network/eqsl.h"
'@ @'
#include "Network/eqsl.h"
#include "models/FrequencyList.hpp"
#include "models/Modes.hpp"
#include "FT8Hunter/Log4OmMysql.hpp"
#include "FT8Hunter/Log4OmMysqlDialog.hpp"
#include "FT8Hunter/DxFunClusterPanel.hpp"
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

  // FT8 Hunter: DXFun Cluster telnet dxfun.com:8000.
  // Ogni spot viene classificato usando le frequenze FT8/FT4 configurate in WSJT-X,
  // quindi il filtro non dipende dal testo/commento dello spot.
  auto evaluateDxFunSpot = [this] (QString const& call, quint64 spotHz) -> DxFunEvaluation {
      DxFunEvaluation out;
      out.spotHz = spotHz;

      qint64 bestDistance = std::numeric_limits<qint64>::max ();
      auto const region = m_config.region ();
      for (auto const& item : m_config.frequencies ()->frequency_list ())
        {
          if (item.mode_ != Modes::FT8 && item.mode_ != Modes::FT4) continue;
          if (item.region_ != IARURegions::ALL && item.region_ != region) continue;

          auto const offset = static_cast<qint64> (spotHz) - static_cast<qint64> (item.frequency_);
          // Gli spot cluster riportano normalmente la frequenza RF del segnale:
          // accetta il dial FT8/FT4 e la finestra audio fino a 5 kHz.
          if (offset < -150 || offset > 5000) continue;

          auto distance = offset < 0 ? -offset : offset;
          if (item.region_ == region && distance > 0) --distance;
          if (distance >= bestDistance) continue;

          bestDistance = distance;
          out.digital = true;
          out.mode = QString::fromLatin1 (Modes::name (item.mode_));
          out.dialHz = item.frequency_;
          out.offsetHz = static_cast<int> (offset);
          out.band = m_config.bands ()->find (item.frequency_);
        }

      if (!out.digital || out.band.isEmpty ()) return out;

      auto const& entity = m_logBook.countries ()->lookup (call);
      out.country = entity.entity_name;
      if (out.country.isEmpty ()) return out;

      bool callB4 = true;
      bool countryB4 = true;
      bool gridB4 = true;
      bool continentB4 = true;
      bool cqB4 = true;
      bool ituB4 = true;

      // NEW DXCC: mai lavorato in nessuna banda; FT8 e FT4 sono uniti.
      m_logBook.match (call, QString {}, QString {}, entity,
                       callB4, countryB4, gridB4, continentB4, cqB4, ituB4);
      out.newDxcc = !countryB4;

      // NEW su banda: DXCC mai lavorato sulla banda dello spot; FT8 e FT4 sono uniti.
      callB4 = countryB4 = gridB4 = continentB4 = cqB4 = ituB4 = true;
      m_logBook.match (call, QString {}, QString {}, entity,
                       callB4, countryB4, gridB4, continentB4, cqB4, ituB4, out.band);
      out.newBand = !countryB4;
      return out;
    };

  auto tuneDxFunSpot = [this] (DxFunTuneRequest const& request) -> bool {
      // Non interrompere una trasmissione o una sequenza Auto in corso.
      if (m_transmitting || m_tune || m_auto) return false;
      if (request.mode != QStringLiteral ("FT8") && request.mode != QStringLiteral ("FT4")) return false;

      ui->pbBandHopping->setChecked (false);
      if (request.mode == QStringLiteral ("FT8") && m_mode != QStringLiteral ("FT8"))
        on_actionFT8_triggered ();
      else if (request.mode == QStringLiteral ("FT4") && m_mode != QStringLiteral ("FT4"))
        on_actionFT4_triggered ();

      QTimer::singleShot (120, this, [this, request] {
          auto const row = m_config.frequencies ()->best_working_frequency (request.dialHz);
          if (row < 0)
            {
              showStatusMessage (tr ("DXFun: frequenza %1 %2 non disponibile")
                                 .arg (request.band).arg (request.mode));
              return;
            }

          ui->bandComboBox->setCurrentIndex (row);
          on_bandComboBox_activated (row);

          auto offset = request.offsetHz;
          if (offset < ui->RxFreqSpinBox->minimum ()) offset = ui->RxFreqSpinBox->minimum ();
          if (offset > ui->RxFreqSpinBox->maximum ()) offset = ui->RxFreqSpinBox->maximum ();
          ui->RxFreqSpinBox->setValue (offset);

          showStatusMessage (
            tr ("DXFun -> %1 %2 | %3 | %4")
              .arg (request.band).arg (request.mode).arg (request.callsign).arg (request.status));
        });
      return true;
    };

  auto * dxFunPanel = new DxFunClusterPanel {
    m_config.my_callsign (), evaluateDxFunSpot, tuneDxFunSpot, m_settings, this};
  addDockWidget (Qt::BottomDockWidgetArea, dxFunPanel);
  dxFunPanel->hide ();

  auto * dxFunAction = hunterMenu->addAction (tr ("DXFun Cluster..."));
  connect (dxFunAction, &QAction::triggered, [dxFunPanel] {
      dxFunPanel->openAndConnect ();
    });

  // FT8 Hunter: aggiorna automaticamente Worked Before da Log4OM.
  auto * log4omRefreshTimer = new QTimer {this};
  log4omRefreshTimer->setInterval (60 * 1000);
  connect (log4omRefreshTimer, &QTimer::timeout, [this] {
      auto const mysql = Log4OmMysql::loadSettings ();
      if (mysql.enabled && mysql.autoRefresh)
        {
          m_logBook.rescan ();
        }
    });
  log4omRefreshTimer->start ();

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


# FT8 Hunter 1.0.7: filtro country/DXCC gia' lavorati.
Replace-Required "widgets/mainwindow.ui" @'
    <addaction name="actionHideB4"/>
    <addaction name="actionHideToday"/>
'@ @'
    <addaction name="actionHideB4"/>
    <addaction name="actionHideWorkedCountry"/>
    <addaction name="actionHideToday"/>
'@

Replace-Required "widgets/mainwindow.ui" @'
  <action name="actionHideB4">
   <property name="checkable">
    <bool>true</bool>
   </property>
   <property name="text">
    <string>Hide stations worked before on band</string>
   </property>
  </action>
  <action name="actionHideToday">
'@ @'
  <action name="actionHideB4">
   <property name="checkable">
    <bool>true</bool>
   </property>
   <property name="text">
    <string>Hide stations worked before on band</string>
   </property>
  </action>
  <action name="actionHideWorkedCountry">
   <property name="checkable">
    <bool>true</bool>
   </property>
   <property name="text">
    <string>Escludi country/DXCC gia lavorati sulla banda corrente</string>
   </property>
  </action>
  <action name="actionHideToday">
'@

Replace-Required "widgets/mainwindow.cpp" @'
  m_settings->setValue ("HideB4", ui->actionHideB4->isChecked() );
  m_settings->setValue ("HideToday", ui->actionHideToday->isChecked() );
'@ @'
  m_settings->setValue ("HideB4", ui->actionHideB4->isChecked() );
  m_settings->setValue ("HideWorkedCountry", ui->actionHideWorkedCountry->isChecked() );
  m_settings->setValue ("HideToday", ui->actionHideToday->isChecked() );
'@

Replace-Required "widgets/mainwindow.cpp" @'
  ui->actionHideB4->setChecked(m_settings->value("HideB4", false).toBool());
  ui->actionHideToday->setChecked(m_settings->value("HideToday", false).toBool());
'@ @'
  ui->actionHideB4->setChecked(m_settings->value("HideB4", false).toBool());
  ui->actionHideWorkedCountry->setChecked(m_settings->value("HideWorkedCountry", false).toBool());
  ui->actionHideToday->setChecked(m_settings->value("HideToday", false).toBool());
'@

Replace-Required "widgets/mainwindow.cpp" 'ui->actionHideB4->isChecked() or ui->actionHideEU->isChecked()' 'ui->actionHideB4->isChecked() or ui->actionHideWorkedCountry->isChecked() or ui->actionHideEU->isChecked()'

Replace-Required "widgets/mainwindow.cpp" @'
            if (callB4onBand && ui->actionHideB4->isChecked() && !ui->cbBypass->isChecked()) filtered = true;
          }
          // search for continents
'@ @'
            if (callB4onBand && ui->actionHideB4->isChecked() && !ui->cbBypass->isChecked()) filtered = true;
          }
          // FT8 Hunter: escludi DXCC gia' lavorati sulla banda corrente, indipendentemente da FT8/FT4.
          if (ui->actionHideWorkedCountry->isChecked()) {
            bool callB4Any;
            bool countryB4Any;
            bool gridB4Any;
            bool continentB4Any;
            bool CQZoneB4Any;
            bool ITUZoneB4Any;
            auto const& looked_up = m_logBook.countries ()->lookup (deCall);
            m_logBook.match (deCall, QString {}, deGrid, looked_up, callB4Any, countryB4Any, gridB4Any,
              continentB4Any, CQZoneB4Any, ITUZoneB4Any, m_currentBand);
            if (!looked_up.entity_name.isEmpty () && countryB4Any && !ui->cbBypass->isChecked()) filtered = true;
          }
          // search for continents
'@

Replace-Required "widgets/mainwindow.cpp" @'
                      if (callB4onBand && ui->actionHideB4->isChecked() && !ui->cbBypass->isChecked()) filtered = true;
                    }
                    // search for continents
'@ @'
                      if (callB4onBand && ui->actionHideB4->isChecked() && !ui->cbBypass->isChecked()) filtered = true;
                    }
                    // FT8 Hunter: escludi DXCC gia' lavorati sulla banda corrente, indipendentemente da FT8/FT4.
                    if (ui->actionHideWorkedCountry->isChecked()) {
                      bool callB4Any;
                      bool countryB4Any;
                      bool gridB4Any;
                      bool continentB4Any;
                      bool CQZoneB4Any;
                      bool ITUZoneB4Any;
                      auto const& looked_up = m_logBook.countries ()->lookup (deCall);
                      m_logBook.match (deCall, QString {}, deGrid, looked_up, callB4Any, countryB4Any, gridB4Any,
                        continentB4Any, CQZoneB4Any, ITUZoneB4Any, m_currentBand);
                      if (!looked_up.entity_name.isEmpty () && countryB4Any && !ui->cbBypass->isChecked()) filtered = true;
                    }
                    // search for continents
'@

Write-Host "Applied FT8 Hunter $HunterVersion auto-refresh + worked-country filter + DXFun cluster."
