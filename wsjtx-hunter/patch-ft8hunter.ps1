param(
    [Parameter(Mandatory=$true)][string]$SourceDir,
    [string]$HunterVersion = "2.0.0"
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
Copy-Item "wsjtx-hunter/monitor/MonitorHunterPanel.hpp" (Join-Path $hunterDir "MonitorHunterPanel.hpp") -Force
Copy-Item "wsjtx-hunter/monitor/MonitorHunterPanel.cpp" (Join-Path $hunterDir "MonitorHunterPanel.cpp") -Force

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
  FT8Hunter/MonitorHunterPanel.cpp
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
#include "FT8Hunter/MonitorHunterPanel.hpp"
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

          auto const band = m_config.bands ()->find (item.frequency_);
          static QStringList const hunterBands {
            QStringLiteral ("80m"), QStringLiteral ("60m"), QStringLiteral ("40m"),
            QStringLiteral ("30m"), QStringLiteral ("20m"), QStringLiteral ("17m"),
            QStringLiteral ("15m"), QStringLiteral ("12m"), QStringLiteral ("10m")};
          if (!hunterBands.contains (band)) continue;

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
          out.band = band;
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

  auto abortDxFunQso = [this] {
      if (m_auto) auto_tx_mode (false);
      if (m_transmitting) ui->stopTxButton->click ();
      clearDX ();
      monitor (true);
    };

  auto * dxFunPanel = new DxFunClusterPanel {
    m_config.my_callsign (), evaluateDxFunSpot, tuneDxFunSpot,
    abortDxFunQso, m_settings, this};
  g_dxFunPanel = dxFunPanel;
  addDockWidget (Qt::BottomDockWidgetArea, dxFunPanel);
  dxFunPanel->hide ();

  auto * dxFunAction = hunterMenu->addAction (tr ("DXFun Cluster..."));
  connect (dxFunAction, &QAction::triggered, [dxFunPanel] {
      dxFunPanel->openAndConnect ();
    });

  // FT8 Hunter 1.0.13 - Monitor Hunter automatico: finestra separata.
  // La RX segue il DX; la TX viene bloccata su uno slot indipendente per tutto il QSO.
  auto startMonitorHunterQso =
    [this] (MonitorHunterCandidate const& request, bool autoTxSlot) -> int {
      if (m_transmitting || m_tune || m_auto) return MonitorHunterPanel::StartUnavailable;
      if (g_dxFunPanel && g_dxFunPanel->huntActive ()) return MonitorHunterPanel::StartUnavailable;
      if (request.mode != m_mode) return MonitorHunterPanel::StartUnavailable;
      if (request.mode != QStringLiteral ("FT8") && request.mode != QStringLiteral ("FT4"))
        return MonitorHunterPanel::StartUnavailable;

      auto txOffset = ui->TxFreqSpinBox->value ();
      if (autoTxSlot)
        {
          txOffset = m_wideGraph->bestQuietTxFrequency (request.audioOffset, request.mode);
          if (txOffset < 0) return MonitorHunterPanel::StartUnavailable;
        }

      // Salva lo stato dell'operatore e impone Hold Tx Freq solo durante questo QSO Hunter.
      m_monitorHunterHoldSaved = true;
      m_monitorHunterPreviousHold = ui->cbHoldTxFreq->isChecked ();
      m_monitorHunterPreviousTxFreq = ui->TxFreqSpinBox->value ();

      ui->cbHoldTxFreq->setChecked (true);
      HoldTxFreqStatus = true;
      ui->TxFreqSpinBox->setValue (txOffset);

      DecodedText hunterDecode {request.rawText};
      tx_watchdog (false);
      m_bDoubleClicked = true;
      auto_tx_mode (true);
      processMessage (hunterDecode);

      // processMessage puo' spostare la RX sul DX, ma la TX resta protetta da Hold Tx Freq.
      ui->dxCallEntry->setText (request.callsign);
      ui->RxFreqSpinBox->setValue (request.audioOffset);
      ui->TxFreqSpinBox->setValue (txOffset);

      showStatusMessage (
        tr ("Monitor Hunter -> %1 | RX %2 Hz | TX bloccata %3 Hz%4")
          .arg (request.callsign)
          .arg (request.audioOffset)
          .arg (txOffset)
          .arg (autoTxSlot ? tr (" (slot libero dal waterfall)") : tr (" (slot manuale)")));
      return txOffset;
    };

  auto abortMonitorHunterQso = [this] {
      if (m_auto) auto_tx_mode (false);
      if (m_transmitting) ui->stopTxButton->click ();
      clearDX ();
      monitor (true);
    };

  auto releaseMonitorHunterTx = [this] {
      if (!m_monitorHunterHoldSaved) return;

      ui->cbHoldTxFreq->setChecked (m_monitorHunterPreviousHold);
      HoldTxFreqStatus = m_monitorHunterPreviousHold;
      ui->TxFreqSpinBox->setValue (m_monitorHunterPreviousTxFreq);
      m_monitorHunterHoldSaved = false;
    };

  auto * monitorHunterPanel = new MonitorHunterPanel {
    m_config.my_callsign (), startMonitorHunterQso, abortMonitorHunterQso,
    releaseMonitorHunterTx, m_settings, this};
  g_monitorHunterPanel = monitorHunterPanel;

  auto updateMonitorHunterFilter = [this, monitorHunterPanel] {
      monitorHunterPanel->setFilterActive (
        ui->actionHideWorkedCountry->isChecked () && !ui->cbBypass->isChecked ());
    };
  connect (ui->actionHideWorkedCountry, &QAction::toggled, this,
           [updateMonitorHunterFilter] (bool) { updateMonitorHunterFilter (); });
  connect (ui->cbBypass, &QCheckBox::toggled, this,
           [updateMonitorHunterFilter] (bool) { updateMonitorHunterFilter (); });
  updateMonitorHunterFilter ();

  auto * monitorHunterAction = hunterMenu->addAction (tr ("Monitor Hunter..."));
  connect (monitorHunterAction, &QAction::triggered, [monitorHunterPanel] {
      monitorHunterPanel->openWindow ();
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

  worked_before_database_type loader_mysql (Log4OmMysqlSettings const& settings, QString const& localPath, AD1CCty const * prefixes)
  {
    // MySQL remains read-only. Start with the local WSJT-X ADIF so Hunter-made
    // QSOs remain part of Worked Before after each automatic MySQL refresh.
    auto worked = loader (localPath, prefixes);
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
        async_loader_ = QtConcurrent::run (loader_mysql, mysql, path_, &prefixes_);
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


# FT8 Hunter 1.0.13: filtro country/DXCC gia' lavorati.
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


# FT8 Hunter 1.0.13: puntatore al pannello cluster usato dai hook decoder/TX.
Replace-Required "widgets/mainwindow.cpp" @'
QString earlyDecodes = "";  //ft8md
'@ @'
QString earlyDecodes = "";  //ft8md
DxFunClusterPanel * g_dxFunPanel = nullptr;
MonitorHunterPanel * g_monitorHunterPanel = nullptr;
'@

# FT8 Hunter 1.0.13: quando la stazione spottata viene realmente decodificata,
# passa dalla finestra di ascolto di 150 s al QSO automatico WSJT-X.
Replace-Required "widgets/mainwindow.cpp" @'
        QString text = decodedtext.string().replace("<","").replace(">","");   // for Wait & Reply/Call and filtering
'@ @'
        QString text = decodedtext.string().replace("<","").replace(">","");   // for Wait & Reply/Call and filtering

        if (g_dxFunPanel && (m_mode=="FT8" || m_mode=="FT4")) {
          QString hunterCall;
          QString hunterGrid;
          decodedtext.deCallAndGrid(hunterCall,hunterGrid);
          auto const hunterAction =
            g_dxFunPanel->observeDecode(hunterCall, text,
                                        decodedtext.frequencyOffset(),
                                        m_config.my_callsign());

          if (hunterAction == DxFunClusterPanel::DecodeAction::StartQso) {
            tx_watchdog(false);
            m_bDoubleClicked = true;
            processMessage(decodedtext0);
            ui->dxCallEntry->setText(hunterCall);
            ui->RxFreqSpinBox->setValue(decodedtext.frequencyOffset());
            auto_tx_mode(true);
          } else if (hunterAction == DxFunClusterPanel::DecodeAction::CompleteQso) {
            // Lascia terminare l'elaborazione del decode e poi registra localmente.
            QTimer::singleShot (500, this, [this] {
              if (m_auto) cease_auto_Tx_after_QSO ();
              if (!m_tune) {
                on_logQSOButton_clicked ();
              }
              if (g_dxFunPanel) g_dxFunPanel->qsoLogged ();
            });
          }
        }
'@

# FT8 Hunter 1.0.13: rampa RF del 7300 ad ogni trasmissione Hunter.
Replace-Required "widgets/mainwindow.cpp" @'
    transmit (snr);
'@ @'
    bool hunterTransmitAllowed = true;

    // Monitor Hunter ha priorita': 8 TX massime senza risposta, 50..85% a passi di 5%.
    if (g_monitorHunterPanel && g_monitorHunterPanel->qsoActive ()) {
      auto const monitorPower = g_monitorHunterPanel->nextTxPowerPercent ();
      if (monitorPower == MonitorHunterPanel::StopTransmission) {
        hunterTransmitAllowed = false;
        if (m_auto) auto_tx_mode (false);
        clearDX ();
        monitor (true);
        g_monitorHunterPanel->callingExhausted ();
      } else if (monitorPower >= 0) {
        m_config.transceiver_rf_power_percent (monitorPower);
      }
    } else if (g_dxFunPanel && g_dxFunPanel->qsoActive ()) {
      auto const hunterPower = g_dxFunPanel->nextTxPowerPercent ();
      if (hunterPower >= 0)
        m_config.transceiver_rf_power_percent (hunterPower);
    }

    if (hunterTransmitAllowed)
      transmit (snr);
'@

# Espone una percentuale RF nella TransceiverState.
Replace-Required "Transceiver/Transceiver.hpp" @'
      , level_ {0}
      , power_ {0}
'@ @'
      , level_ {0}
      , rf_power_percent_ {-1}
      , power_ {0}
'@

Replace-Required "Transceiver/Transceiver.hpp" @'
    int level () const {return level_;}
    unsigned int power () const {return power_;}
'@ @'
    int level () const {return level_;}
    int rf_power_percent () const {return rf_power_percent_;}
    unsigned int power () const {return power_;}
'@

Replace-Required "Transceiver/Transceiver.hpp" @'
    void level (int strength) {level_ = strength;}
    void power (unsigned int mwpower) {power_ = mwpower;}
'@ @'
    void level (int strength) {level_ = strength;}
    void rf_power_percent (int percent) {rf_power_percent_ = percent;}
    void power (unsigned int mwpower) {power_ = mwpower;}
'@

Replace-Required "Transceiver/Transceiver.hpp" @'
    int level_;
    unsigned int power_;
'@ @'
    int level_;
    int rf_power_percent_;
    unsigned int power_;
'@

Replace-Required "Transceiver/Transceiver.cpp" @'
    << "; LEVEL: " << s.level_ << "dBm"
    << "; POWER: " << s.power_ << "mWatts"
'@ @'
    << "; LEVEL: " << s.level_ << "dBm"
    << "; RF POWER SET: " << s.rf_power_percent_ << "%"
    << "; POWER: " << s.power_ << "mWatts"
'@

Replace-Required "Transceiver/Transceiver.cpp" @'
    || lhs.level_ != rhs.level_
    || lhs.power_ != rhs.power_
'@ @'
    || lhs.level_ != rhs.level_
    || lhs.rf_power_percent_ != rhs.rf_power_percent_
    || lhs.power_ != rhs.power_
'@

# Driver base: comando RF opzionale, implementato realmente dal backend Hamlib.
Replace-Required "Transceiver/TransceiverBase.hpp" @'
  virtual void do_txvolume (qreal) {}
  //parameters are MODE,symbolslength,framespersymbol,trfrequency,tonespacing,synchronize,FASTMODE,dbsdr,trperiod //parameters added by w3sz are in bold
'@ @'
  virtual void do_txvolume (qreal) {}
  virtual void do_rf_power_percent (int) {}
  //parameters are MODE,symbolslength,framespersymbol,trfrequency,tonespacing,synchronize,FASTMODE,dbsdr,trperiod //parameters added by w3sz are in bold
'@

Replace-Required "Transceiver/TransceiverBase.cpp" @'
      if (requested_.online ())
        {
          bool audio_cmd {false};
'@ @'
      if (requested_.online ())
        {
          if (s.rf_power_percent () >= 0
              && requested_.rf_power_percent () != s.rf_power_percent ()) {
            do_rf_power_percent (s.rf_power_percent ());
            requested_.rf_power_percent (s.rf_power_percent ());
            actual_.rf_power_percent (s.rf_power_percent ());
          }

          bool audio_cmd {false};
'@

# Hamlib: RIG_LEVEL_RFPOWER e' 0.0..1.0; sul IC-7300 equivale alla percentuale RF Power.
Replace-Required "Transceiver/HamlibTransceiver.hpp" @'
  void do_ptt (bool) override;
  void do_tune (bool) override;
'@ @'
  void do_ptt (bool) override;
  void do_tune (bool) override;
  void do_rf_power_percent (int) override;
'@

Replace-Required "Transceiver/HamlibTransceiver.cpp" @'
HamlibTransceiver::~HamlibTransceiver () = default;

void HamlibTransceiver::load_user_settings ()
'@ @'
HamlibTransceiver::~HamlibTransceiver () = default;

void HamlibTransceiver::do_rf_power_percent (int percent)
{
  if (!m_->rig_ || m_->is_dummy_) return;

  auto const canSet =
    rig_get_function_ptr (m_->model_, RIG_FUNCTION_SET_LEVEL)
    && ((rig_get_caps_int (m_->model_, RIG_CAPS_HAS_SET_LEVEL) & RIG_LEVEL_RFPOWER) == RIG_LEVEL_RFPOWER);

  if (!canSet) {
    CAT_WARNING ("FT8 Hunter: RF power setting is not supported by the selected Hamlib rig");
    return;
  }

  int safe = percent < 0 ? 0 : percent > 100 ? 100 : percent;
  value_t value {};
  value.f = static_cast<float> (safe) / 100.0f;
  auto const rc = rig_set_level (m_->rig_.data (), RIG_VFO_CURR, RIG_LEVEL_RFPOWER, value);
  if (RIG_OK != rc)
    CAT_WARNING ("FT8 Hunter: rig_set_level(RIG_LEVEL_RFPOWER) failed rc=" << rc);
  else
    CAT_TRACE ("FT8 Hunter: RF power set to " << safe << "%");
}

void HamlibTransceiver::load_user_settings ()
'@

# Configuration: porta il comando percentuale fino al backend CAT.
Replace-Required "Configuration.cpp" @'
  void transceiver_txvolume (double);
  void sync_transceiver (bool force_signal);
'@ @'
  void transceiver_txvolume (double);
  void transceiver_rf_power_percent (int);
  void sync_transceiver (bool force_signal);
'@

Replace-Required "Configuration.hpp" @'
public slots:
  Q_SLOT void transceiver_volume (double = 0);
  Q_SLOT void transceiver_txvolume (double = 0);
'@ @'
public slots:
  Q_SLOT void transceiver_volume (double = 0);
  Q_SLOT void transceiver_txvolume (double = 0);
  Q_SLOT void transceiver_rf_power_percent (int percent);
'@

Replace-Required "Configuration.cpp" @'
void Configuration::impl::transceiver_volume (double volume)
'@ @'
void Configuration::impl::transceiver_rf_power_percent (int percent)
{
  cached_rig_state_.online (true);
  cached_rig_state_.rf_power_percent (percent);
  Q_EMIT set_transceiver (cached_rig_state_, ++transceiver_command_number_);
}

void Configuration::impl::transceiver_volume (double volume)
'@

Replace-Required "Configuration.cpp" @'
void Configuration::transceiver_volume (qreal volume)
{
'@ @'
void Configuration::transceiver_rf_power_percent (int percent)
{
  LOG_TRACE (percent << ' ' << m_->cached_rig_state_);
  m_->transceiver_rf_power_percent (percent);
}

void Configuration::transceiver_volume (qreal volume)
{
'@

Write-Host "Applied FT8 Hunter $HunterVersion DXFun HF hunter + 150s acquire + auto-QSO + RF power ramp."


# FT8 Hunter 1.0.13: RF Power anche quando l'IC-7300 e' configurato tramite OmniRig.
# OmniRig accetta SAFEARRAY(BYTE); ActiveQt mappa QByteArray su SAFEARRAY(BYTE).
Replace-Required "Transceiver/OmniRigTransceiver.hpp" @'
  void do_mode (MODE) override;
  void do_ptt (bool on) override;
'@ @'
  void do_mode (MODE) override;
  void do_ptt (bool on) override;
  void do_rf_power_percent (int) override;
'@

Replace-Required "Transceiver/OmniRigTransceiver.cpp" @'
#include <QEventLoop>
'@ @'
#include <QEventLoop>
#include <QByteArray>
#include <QVariant>
'@

Replace-Required "Transceiver/OmniRigTransceiver.cpp" @'
void OmniRigTransceiver::do_ptt (bool on)
'@ @'
void OmniRigTransceiver::do_rf_power_percent (int percent)
{
  if (!rig_ || rig_->isNull ()) return;

  // Questa sequenza CI-V e' specifica dell'IC-7300 (indirizzo 94h).
  // 14 0A imposta RF POWER su scala 0000..0255.
  if (!rig_type_.contains (QStringLiteral ("IC-7300"), Qt::CaseInsensitive)) {
    CAT_WARNING ("FT8 Hunter: RF power via OmniRig supported here only for IC-7300");
    return;
  }

  int safe = percent < 0 ? 0 : percent > 100 ? 100 : percent;
  int level = (safe * 255 + 50) / 100;
  auto bcd = [] (int n) -> char {
      return static_cast<char> (((n / 10) << 4) | (n % 10));
    };

  QByteArray command;
  command.append (static_cast<char> (0xFE));
  command.append (static_cast<char> (0xFE));
  command.append (static_cast<char> (0x94));
  command.append (static_cast<char> (0xE0));
  command.append (static_cast<char> (0x14));
  command.append (static_cast<char> (0x0A));
  command.append (bcd (level / 100));
  command.append (bcd (level % 100));
  command.append (static_cast<char> (0xFD));

  rig_->SendCustomCommand (QVariant {command}, 0, QVariant {QString {}});
  CAT_TRACE ("FT8 Hunter: OmniRig IC-7300 RF power set to " << safe << "%");
}

void OmniRigTransceiver::do_ptt (bool on)
'@

Write-Host "Applied FT8 Hunter $HunterVersion IC-7300 RF power control through OmniRig."


# FT8 Hunter 1.0.13: la finestra Log QSO resta visibile per 4 secondi,
# poi viene confermata automaticamente come se l'operatore premesse OK.
# Se l'utente la conferma o la annulla prima, il timer non fa nulla.
Replace-Required "widgets/logqso.cpp" @'
  else
    {
      show();
    }
}
'@ @'
  else
    {
      show();
      QTimer::singleShot (4000, this, [this] {
          if (isVisible ()) accept ();
        });
    }
}
'@

Write-Host "Applied FT8 Hunter $HunterVersion Log QSO 4-second auto-confirm."


# FT8 Hunter 1.0.13: quando il Log QSO emette acceptQSO, il Monitor Hunter puo' passare al CQ successivo.
Replace-Required "widgets/mainwindow.cpp" @'
  connect (m_logDlg.data (), &LogQSO::acceptQSO, this, &MainWindow::acceptQSO);
'@ @'
  connect (m_logDlg.data (), &LogQSO::acceptQSO, this, &MainWindow::acceptQSO);
  connect (m_logDlg.data (), &LogQSO::acceptQSO, this,
           [] (QDateTime const&, QString const& call) {
      if (g_monitorHunterPanel) g_monitorHunterPanel->qsoLogged (call);
    });
'@

Write-Host "Applied FT8 Hunter $HunterVersion Monitor Hunter log-completion hook."


# FT8 Hunter 1.0.13: osserva i decode dopo i filtri.
# Un CQ entra nella coda solo se e' realmente visibile e il suo DXCC non risulta lavorato sulla banda.
Replace-Required "widgets/mainwindow.cpp" @'
        // show distance and bearing
        if (!filtered or m_config.filters_for_Wait_and_Pounce_only()) {  // show decodes if not filtered
'@ @'
        // Monitor Hunter: osserva i CQ realmente ammessi dal filtro e tutte le risposte del QSO attivo.
        if (g_monitorHunterPanel && (m_mode=="FT8" || m_mode=="FT4")) {
          QString mhDeCall;
          QString mhGrid;
          decodedtext.deCallAndGrid (mhDeCall, mhGrid);
          auto const mhCqCall = decodedtext.CQersCall ().trimmed ().toUpper ();
          bool mhEligibleCq = false;
          QString mhCountry;

          auto const mhFilterActive =
            ui->actionHideWorkedCountry->isChecked () && !ui->cbBypass->isChecked ();

          if (mhFilterActive && !filtered && !mhCqCall.isEmpty ()) {
            bool callB4Any;
            bool countryB4Any;
            bool gridB4Any;
            bool continentB4Any;
            bool cqB4Any;
            bool ituB4Any;
            auto const& mhEntity = m_logBook.countries ()->lookup (mhCqCall);
            mhCountry = mhEntity.entity_name;
            m_logBook.match (mhCqCall, QString {}, mhGrid, mhEntity,
                             callB4Any, countryB4Any, gridB4Any,
                             continentB4Any, cqB4Any, ituB4Any, m_currentBand);
            mhEligibleCq = !mhCountry.isEmpty () && !countryB4Any;
          }

          g_monitorHunterPanel->observeDecode (
            decodedtext0.string (), text, mhCqCall, mhDeCall, mhGrid,
            decodedtext.frequencyOffset (), decodedtext.snr (),
            m_mode, m_currentBand, mhCountry, mhEligibleCq);
        }

        // show distance and bearing
        if (!filtered or m_config.filters_for_Wait_and_Pounce_only()) {  // show decodes if not filtered
'@

Write-Host "Applied FT8 Hunter $HunterVersion Monitor Hunter decode queue."


# FT8 Hunter 1.0.13: stato temporaneo Hold Tx Freq usato dal Monitor Hunter.
Replace-Required "widgets/mainwindow.h" @'
  QScopedPointer<WideGraph> m_wideGraph;
  QScopedPointer<EchoGraph> m_echoGraph;
'@ @'
  QScopedPointer<WideGraph> m_wideGraph;
  bool m_monitorHunterHoldSaved {false};
  bool m_monitorHunterPreviousHold {false};
  int m_monitorHunterPreviousTxFreq {0};
  QScopedPointer<EchoGraph> m_echoGraph;
'@

Write-Host "Applied FT8 Hunter $HunterVersion Monitor Hunter Hold-TX state."


# FT8 Hunter 1.0.13: usa lo spettro reale del Grafico Ampio per trovare uno slot TX quieto.
Replace-Required "widgets/widegraph.h" @'
  void   setTxFreq(int n);
  void   setMode(QString mode);
'@ @'
  void   setTxFreq(int n);
  int    bestQuietTxFrequency(int avoidHz, QString const& mode);
  QVector<float> hunterRemoteSpectrum(int points = 256) const;
  void   setMode(QString mode);
'@

Replace-Required "widgets/widegraph.h" @'
  qint32 m_jz=MAX_SCREENSIZE;
  qint32 m_n;
  qint32 m_timestamp;
'@ @'
  qint32 m_jz=MAX_SCREENSIZE;
  qint32 m_n;
  qint32 m_timestamp;
  double m_hunterBinHz {0.0};
  int    m_hunterSpectrumFrames {0};
  float  m_hunterSpectrum[MAX_SCREENSIZE] {};
'@

Replace-Required "widgets/widegraph.cpp" @'
    m_tr0=tr;
    ui->widePlot->draw(m_swide,true,false);
'@ @'
    m_tr0=tr;

    // Media esponenziale dello spettro recente. Le righe speciali 1e30/2e30
    // usate dal waterfall per marcare i periodi non devono entrare nell'analisi.
    if (jz > 0 && m_swide[0] < 1.0e20f)
      {
        m_hunterBinHz = nbpp * df3;
        for (int j = 0; j < jz; ++j)
          {
            auto const v = m_swide[j];
            if (v >= 0.0f && v < 1.0e20f)
              {
                if (m_hunterSpectrumFrames == 0)
                  m_hunterSpectrum[j] = v;
                else
                  m_hunterSpectrum[j] = 0.85f * m_hunterSpectrum[j] + 0.15f * v;
              }
          }
        if (m_hunterSpectrumFrames < 1000) ++m_hunterSpectrumFrames;
      }

    ui->widePlot->draw(m_swide,true,false);
'@

Replace-Required "widgets/widegraph.cpp" @'
void WideGraph::setMode(QString mode)                              //setMode
'@ @'
int WideGraph::bestQuietTxFrequency (int avoidHz, QString const& mode)
{
  // Aspetta alcuni aggiornamenti reali del waterfall prima di prendere una decisione.
  if (m_hunterSpectrumFrames < 4 || m_hunterBinHz <= 0.0 || m_jz < 4) return -1;

  // FT8 e FT4 occupano poche decine di Hz. Valutiamo una finestra piu' larga
  // (segnale + guardia) per non piazzare la TX in una fessura troppo stretta.
  int const signalWidth = mode == QStringLiteral ("FT4") ? 70 : 50;
  int const guard = 35;
  int const avoidGuard = 100;

  double const graphStart = ui->widePlot->startFreq ();
  double const graphEnd = graphStart + m_jz * m_hunterBinHz;

  int const low = qMax (500, static_cast<int> (ceil (graphStart + guard)));
  int const high = qMin (2400, static_cast<int> (floor (graphEnd - signalWidth - guard)));
  if (high <= low) return -1;

  double bestScore = 1.0e300;
  int bestTx = -1;

  for (int tx = low; tx <= high; tx += 5)
    {
      int const protectedLow = tx - guard;
      int const protectedHigh = tx + signalWidth + guard;

      // Non trasmettere sopra o immediatamente accanto al DX che stiamo ascoltando.
      if (avoidHz > 0
          && protectedLow < avoidHz + avoidGuard
          && protectedHigh > avoidHz - avoidGuard)
        continue;

      int i0 = static_cast<int> (floor ((protectedLow - graphStart) / m_hunterBinHz));
      int i1 = static_cast<int> (ceil ((protectedHigh - graphStart) / m_hunterBinHz));
      i0 = qMax (0, i0);
      i1 = qMin (m_jz - 1, i1);
      if (i1 <= i0) continue;

      double sum = 0.0;
      double peak = 0.0;
      int count = 0;
      for (int i = i0; i <= i1; ++i)
        {
          auto const v = static_cast<double> (m_hunterSpectrum[i]);
          if (v <= 0.0 || v >= 1.0e20) continue;
          sum += v;
          peak = qMax (peak, v);
          ++count;
        }
      if (count < 3) continue;

      // Premia una zona con bassa energia media ma penalizza anche un picco stretto.
      auto const mean = sum / count;
      auto const score = mean + 0.35 * peak;
      if (score < bestScore)
        {
          bestScore = score;
          bestTx = tx;
        }
    }

  return bestTx;
}

QVector<float> WideGraph::hunterRemoteSpectrum (int points) const
{
  QVector<float> out;
  if (points < 16 || m_hunterSpectrumFrames < 1 || m_hunterBinHz <= 0.0 || m_jz < 4)
    return out;

  double const graphStart = ui->widePlot->startFreq ();
  int const lowHz = 500;
  int const highHz = 2400;
  out.reserve (points);

  for (int p = 0; p < points; ++p)
    {
      auto const hz =
        lowHz + (static_cast<double> (p) / qMax (1, points - 1)) * (highHz - lowHz);
      auto const index = static_cast<int> (qRound ((hz - graphStart) / m_hunterBinHz));
      if (index < 0 || index >= m_jz)
        out.append (0.0f);
      else
        out.append (m_hunterSpectrum[index]);
    }
  return out;
}

void WideGraph::setMode(QString mode)                              //setMode
'@

Write-Host "Applied FT8 Hunter $HunterVersion waterfall-based automatic TX slot selection."


# FT8 Hunter 1.0.13: watchdog QSO condiviso tra Monitor Hunter e DXFun.
Copy-Item "wsjtx-hunter/common/HunterQsoWatchdog.hpp" (Join-Path $hunterDir "HunterQsoWatchdog.hpp") -Force

# Il sorgente di controllo usa ../common; nel tree WSJT-X i file Hunter sono appiattiti nella stessa cartella.
Replace-Required "FT8Hunter/DxFunClusterPanel.hpp" @'
#include "../common/HunterQsoWatchdog.hpp"
'@ @'
#include "HunterQsoWatchdog.hpp"
'@

# DXFun non deve prendere la radio mentre Monitor Hunter e' in Calling/Busy/QSO.
Replace-Required "widgets/mainwindow.cpp" @'
      if (m_transmitting || m_tune || m_auto) return false;
      if (request.mode != QStringLiteral ("FT8") && request.mode != QStringLiteral ("FT4")) return false;
'@ @'
      if (m_transmitting || m_tune || m_auto) return false;
      if (g_monitorHunterPanel && g_monitorHunterPanel->huntActive ()) return false;
      if (request.mode != QStringLiteral ("FT8") && request.mode != QStringLiteral ("FT4")) return false;
'@

# Monitor Hunter: pausa quando il DX lavora un'altra stazione e ripresa sul nuovo CQ,
# mantenendo tentativo e potenza raggiunti.
Replace-Required "widgets/mainwindow.cpp" @'
  auto abortMonitorHunterQso = [this] {
      if (m_auto) auto_tx_mode (false);
      if (m_transmitting) ui->stopTxButton->click ();
      clearDX ();
      monitor (true);
    };

  auto releaseMonitorHunterTx = [this] {
'@ @'
  auto pauseMonitorHunterQso = [this] {
      if (m_transmitting)
        ui->stopTxButton->click ();
      else if (m_auto)
        auto_tx_mode (false);
      monitor (true);
    };

  auto resumeMonitorHunterQso = [this] (QString const& rawText, int audioOffset) -> bool {
      if (m_tune) return false;
      DecodedText hunterDecode {rawText};
      tx_watchdog (false);
      m_bDoubleClicked = true;
      processMessage (hunterDecode);
      ui->RxFreqSpinBox->setValue (audioOffset);
      auto_tx_mode (true);
      return true;
    };

  auto abortMonitorHunterQso = [this] {
      if (m_auto) auto_tx_mode (false);
      if (m_transmitting) ui->stopTxButton->click ();
      clearDX ();
      monitor (true);
    };

  auto releaseMonitorHunterTx = [this] {
'@

Replace-Required "widgets/mainwindow.cpp" @'
  auto * monitorHunterPanel = new MonitorHunterPanel {
    m_config.my_callsign (), startMonitorHunterQso, abortMonitorHunterQso,
    releaseMonitorHunterTx, m_settings, this};
'@ @'
  auto * monitorHunterPanel = new MonitorHunterPanel {
    m_config.my_callsign (), startMonitorHunterQso,
    pauseMonitorHunterQso, resumeMonitorHunterQso,
    abortMonitorHunterQso, releaseMonitorHunterTx, m_settings, this};
'@

# DXFun: classifica CQ, risposta a noi e risposta a terzi.
Replace-Required "widgets/mainwindow.cpp" @'
          auto const hunterAction =
            g_dxFunPanel->observeDecode(hunterCall, text,
                                        decodedtext.frequencyOffset(),
                                        m_config.my_callsign());

          if (hunterAction == DxFunClusterPanel::DecodeAction::StartQso) {
            tx_watchdog(false);
            m_bDoubleClicked = true;
            processMessage(decodedtext0);
            ui->dxCallEntry->setText(hunterCall);
            ui->RxFreqSpinBox->setValue(decodedtext.frequencyOffset());
            auto_tx_mode(true);
          } else if (hunterAction == DxFunClusterPanel::DecodeAction::CompleteQso) {
            // Lascia terminare l'elaborazione del decode e poi registra localmente.
            QTimer::singleShot (500, this, [this] {
              if (m_auto) cease_auto_Tx_after_QSO ();
              if (!m_tune) {
                on_logQSOButton_clicked ();
              }
              if (g_dxFunPanel) g_dxFunPanel->qsoLogged ();
            });
          }
'@ @'
          auto const hunterFirstCall = decodedtext.call ().trimmed ().toUpper ();
          auto const hunterCqCall = decodedtext.CQersCall ().trimmed ().toUpper ();
          auto const hunterAction =
            g_dxFunPanel->observeDecode(hunterFirstCall, hunterCall, hunterCqCall, text,
                                        decodedtext.frequencyOffset(),
                                        m_config.my_callsign());

          if (hunterAction == DxFunClusterPanel::DecodeAction::StartQso
              || hunterAction == DxFunClusterPanel::DecodeAction::ResumeQso) {
            tx_watchdog(false);
            m_bDoubleClicked = true;
            processMessage(decodedtext0);
            ui->dxCallEntry->setText(hunterCall);
            ui->RxFreqSpinBox->setValue(decodedtext.frequencyOffset());
            auto_tx_mode(true);
          } else if (hunterAction == DxFunClusterPanel::DecodeAction::PauseBusy) {
            if (m_transmitting)
              ui->stopTxButton->click ();
            else if (m_auto)
              auto_tx_mode (false);
            monitor (true);
          } else if (hunterAction == DxFunClusterPanel::DecodeAction::CompleteQso) {
            // Ferma la sequenza; il pannello DXFun verra' liberato soltanto
            // quando LogQSO avra' realmente emesso acceptQSO.
            QTimer::singleShot (500, this, [this] {
              if (m_auto) cease_auto_Tx_after_QSO ();
              if (!m_tune) on_logQSOButton_clicked ();
            });
          }
'@

# Monitor Hunter riceve anche il primo nominativo del messaggio per mostrare chi sta lavorando il DX.
Replace-Required "widgets/mainwindow.cpp" @'
          g_monitorHunterPanel->observeDecode (
            decodedtext0.string (), text, mhCqCall, mhDeCall, mhGrid,
            decodedtext.frequencyOffset (), decodedtext.snr (),
            m_mode, m_currentBand, mhCountry, mhEligibleCq);
'@ @'
          g_monitorHunterPanel->observeDecode (
            decodedtext0.string (), text, decodedtext.call ().trimmed ().toUpper (),
            mhCqCall, mhDeCall, mhGrid,
            decodedtext.frequencyOffset (), decodedtext.snr (),
            m_mode, m_currentBand, mhCountry, mhEligibleCq);
'@

# Il log effettivamente accettato libera entrambi i motori Hunter.
Replace-Required "widgets/mainwindow.cpp" @'
  connect (m_logDlg.data (), &LogQSO::acceptQSO, this,
           [] (QDateTime const&, QString const& call) {
      if (g_monitorHunterPanel) g_monitorHunterPanel->qsoLogged (call);
    });
'@ @'
  connect (m_logDlg.data (), &LogQSO::acceptQSO, this,
           [] (QDateTime const&, QString const& call) {
      if (g_monitorHunterPanel) g_monitorHunterPanel->qsoLogged (call);
      if (g_dxFunPanel) g_dxFunPanel->qsoLogged (call);
    });
'@

# Watchdog TX comune: 8 chiamate 50..85%, poi dopo la risposta max 4 TX dello stesso step senza progresso.
Replace-Required "widgets/mainwindow.cpp" @'
    bool hunterTransmitAllowed = true;

    // Monitor Hunter ha priorita': 8 TX massime senza risposta, 50..85% a passi di 5%.
    if (g_monitorHunterPanel && g_monitorHunterPanel->qsoActive ()) {
      auto const monitorPower = g_monitorHunterPanel->nextTxPowerPercent ();
      if (monitorPower == MonitorHunterPanel::StopTransmission) {
        hunterTransmitAllowed = false;
        if (m_auto) auto_tx_mode (false);
        clearDX ();
        monitor (true);
        g_monitorHunterPanel->callingExhausted ();
      } else if (monitorPower >= 0) {
        m_config.transceiver_rf_power_percent (monitorPower);
      }
    } else if (g_dxFunPanel && g_dxFunPanel->qsoActive ()) {
      auto const hunterPower = g_dxFunPanel->nextTxPowerPercent ();
      if (hunterPower >= 0)
        m_config.transceiver_rf_power_percent (hunterPower);
    }

    if (hunterTransmitAllowed)
      transmit (snr);
'@ @'
    bool hunterTransmitAllowed = true;
    auto hunterTxStep = m_currentMessage.trimmed ();
    if (hunterTxStep.isEmpty ()) {
      if (m_ntx == 1) hunterTxStep = ui->tx1->text ();
      if (m_ntx == 2) hunterTxStep = ui->tx2->text ();
      if (m_ntx == 3) hunterTxStep = ui->tx3->text ();
      if (m_ntx == 4) hunterTxStep = ui->tx4->text ();
      if (m_ntx == 5) hunterTxStep = ui->tx5->currentText ();
      if (m_ntx == 6) hunterTxStep = ui->tx6->text ();
    }

    if (g_monitorHunterPanel && g_monitorHunterPanel->qsoActive ()) {
      auto const monitorPower = g_monitorHunterPanel->nextTxPowerPercent (hunterTxStep);
      if (monitorPower == MonitorHunterPanel::StopTransmission) {
        hunterTransmitAllowed = false;
        if (m_auto) auto_tx_mode (false);
        monitor (true);
        g_monitorHunterPanel->transmissionExhausted ();
      } else if (monitorPower >= 0) {
        m_config.transceiver_rf_power_percent (monitorPower);
      }
    } else if (g_dxFunPanel && g_dxFunPanel->qsoActive ()) {
      auto const hunterPower = g_dxFunPanel->nextTxPowerPercent (hunterTxStep);
      if (hunterPower == -2) {
        hunterTransmitAllowed = false;
        if (m_auto) auto_tx_mode (false);
        monitor (true);
        g_dxFunPanel->transmissionExhausted ();
      } else if (hunterPower >= 0) {
        m_config.transceiver_rf_power_percent (hunterPower);
      }
    }

    if (hunterTransmitAllowed)
      transmit (snr);
'@

Write-Host "Applied FT8 Hunter $HunterVersion shared QSO watchdog: busy-pause, 8-call limit and 4-TX no-progress limit."


# FT8 Hunter 1.0.13: Band Hunter + QSY cluster esatta con split riallineato.
Copy-Item "wsjtx-hunter/band/BandHunterPanel.hpp" (Join-Path $hunterDir "BandHunterPanel.hpp") -Force
Copy-Item "wsjtx-hunter/band/BandHunterPanel.cpp" (Join-Path $hunterDir "BandHunterPanel.cpp") -Force

Replace-Required "CMakeLists.txt" @'
  FT8Hunter/DxFunClusterPanel.cpp
  FT8Hunter/MonitorHunterPanel.cpp
  Configuration.cpp
'@ @'
  FT8Hunter/DxFunClusterPanel.cpp
  FT8Hunter/MonitorHunterPanel.cpp
  FT8Hunter/BandHunterPanel.cpp
  Configuration.cpp
'@

Replace-Required "widgets/mainwindow.cpp" @'
#include "FT8Hunter/DxFunClusterPanel.hpp"
#include "FT8Hunter/MonitorHunterPanel.hpp"
'@ @'
#include "FT8Hunter/DxFunClusterPanel.hpp"
#include "FT8Hunter/MonitorHunterPanel.hpp"
#include "FT8Hunter/BandHunterPanel.hpp"
'@

Replace-Required "widgets/mainwindow.cpp" @'
DxFunClusterPanel * g_dxFunPanel = nullptr;
MonitorHunterPanel * g_monitorHunterPanel = nullptr;
'@ @'
DxFunClusterPanel * g_dxFunPanel = nullptr;
MonitorHunterPanel * g_monitorHunterPanel = nullptr;
BandHunterPanel * g_bandHunterPanel = nullptr;
'@

# Se il commento dello spot dichiara esplicitamente FT8/FT4, la frequenza del
# cluster diventa una vera QRG di dial (caso tipico DXpedition/special event).
# Senza modo esplicito resta la logica storica basata sulla vicinanza alle
# frequenze FT8/FT4 configurate.
Replace-Required "widgets/mainwindow.cpp" @'
  auto evaluateDxFunSpot = [this] (QString const& call, quint64 spotHz) -> DxFunEvaluation {
      DxFunEvaluation out;
      out.spotHz = spotHz;

      qint64 bestDistance = std::numeric_limits<qint64>::max ();
      auto const region = m_config.region ();
      for (auto const& item : m_config.frequencies ()->frequency_list ())
        {
          if (item.mode_ != Modes::FT8 && item.mode_ != Modes::FT4) continue;
          if (item.region_ != IARURegions::ALL && item.region_ != region) continue;

          auto const band = m_config.bands ()->find (item.frequency_);
          static QStringList const hunterBands {
            QStringLiteral ("80m"), QStringLiteral ("60m"), QStringLiteral ("40m"),
            QStringLiteral ("30m"), QStringLiteral ("20m"), QStringLiteral ("17m"),
            QStringLiteral ("15m"), QStringLiteral ("12m"), QStringLiteral ("10m")};
          if (!hunterBands.contains (band)) continue;

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
          out.band = band;
        }

      if (!out.digital || out.band.isEmpty ()) return out;
'@ @'
  auto evaluateDxFunSpot =
    [this] (QString const& call, quint64 spotHz, QString const& comment) -> DxFunEvaluation {
      DxFunEvaluation out;
      out.spotHz = spotHz;

      static QStringList const hunterBands {
        QStringLiteral ("80m"), QStringLiteral ("60m"), QStringLiteral ("40m"),
        QStringLiteral ("30m"), QStringLiteral ("20m"), QStringLiteral ("17m"),
        QStringLiteral ("15m"), QStringLiteral ("12m"), QStringLiteral ("10m")};

      auto const spotBand = m_config.bands ()->find (spotHz);
      if (!hunterBands.contains (spotBand)) return out;

      auto const note = comment.toUpper ();
      QString explicitMode;
      if (note.contains (QRegularExpression {QStringLiteral ("(^|[^A-Z0-9])FT[ -]?8([^A-Z0-9]|$)"),
                                             QRegularExpression::CaseInsensitiveOption}))
        explicitMode = QStringLiteral ("FT8");
      else if (note.contains (QRegularExpression {QStringLiteral ("(^|[^A-Z0-9])FT[ -]?4([^A-Z0-9]|$)"),
                                                  QRegularExpression::CaseInsensitiveOption}))
        explicitMode = QStringLiteral ("FT4");

      if (!explicitMode.isEmpty ())
        {
          out.digital = true;
          out.mode = explicitMode;
          out.band = spotBand;
          out.dialHz = spotHz;
          out.offsetHz = 1500;
          out.exactSpotDial = true;
        }
      else
        {
          qint64 bestDistance = std::numeric_limits<qint64>::max ();
          auto const region = m_config.region ();
          for (auto const& item : m_config.frequencies ()->frequency_list ())
            {
              if (item.mode_ != Modes::FT8 && item.mode_ != Modes::FT4) continue;
              if (item.region_ != IARURegions::ALL && item.region_ != region) continue;

              auto const band = m_config.bands ()->find (item.frequency_);
              if (!hunterBands.contains (band)) continue;

              auto const offset =
                static_cast<qint64> (spotHz) - static_cast<qint64> (item.frequency_);
              if (offset < -150 || offset > 5000) continue;

              auto distance = offset < 0 ? -offset : offset;
              if (item.region_ == region && distance > 0) --distance;
              if (distance >= bestDistance) continue;

              bestDistance = distance;
              out.digital = true;
              out.mode = QString::fromLatin1 (Modes::name (item.mode_));
              out.dialHz = item.frequency_;
              out.offsetHz = static_cast<int> (offset);
              out.band = band;
              out.exactSpotDial = false;
            }
        }

      if (!out.digital || out.band.isEmpty ()) return out;
'@

# Il dial richiesto dal cluster diventa la base completa RX/TX. setRig()
# riallinea m_freqNominal e m_freqTxNominal; setXIT() ricrea quindi lo split
# sulla nuova QRG e non sulla precedente frequenza standard.
Replace-Required "widgets/mainwindow.cpp" @'
  auto tuneDxFunSpot = [this] (DxFunTuneRequest const& request) -> bool {
      // Non interrompere una trasmissione o una sequenza Auto in corso.
      if (m_transmitting || m_tune || m_auto) return false;
      if (g_monitorHunterPanel && g_monitorHunterPanel->huntActive ()) return false;
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
'@ @'
  auto tuneDxFunSpot = [this] (DxFunTuneRequest const& request) -> bool {
      if (m_transmitting || m_tune || m_auto) return false;
      if (g_monitorHunterPanel && g_monitorHunterPanel->huntActive ()) return false;
      if (request.mode != QStringLiteral ("FT8") && request.mode != QStringLiteral ("FT4")) return false;

      ui->pbBandHopping->setChecked (false);
      if (request.mode == QStringLiteral ("FT8") && m_mode != QStringLiteral ("FT8"))
        on_actionFT8_triggered ();
      else if (request.mode == QStringLiteral ("FT4") && m_mode != QStringLiteral ("FT4"))
        on_actionFT4_triggered ();

      auto const row = m_config.frequencies ()->best_working_frequency (request.dialHz);
      if (row < 0)
        {
          showStatusMessage (tr ("DXFun: frequenza %1 %2 non disponibile")
                             .arg (request.band).arg (request.mode));
          return false;
        }

      if (g_bandHunterPanel && g_bandHunterPanel->active ())
        g_bandHunterPanel->pauseForExternalHunter ();

      QTimer::singleShot (120, this, [this, request, row] {
          ui->bandComboBox->setCurrentIndex (row);

          m_bandEdited = true;
          band_changed (request.dialHz);
          m_wideGraph->setRxBand (request.band);

          auto offset = request.offsetHz;
          if (offset < ui->RxFreqSpinBox->minimum ()) offset = ui->RxFreqSpinBox->minimum ();
          if (offset > ui->RxFreqSpinBox->maximum ()) offset = ui->RxFreqSpinBox->maximum ();
          ui->RxFreqSpinBox->setValue (offset);

          setRig (request.dialHz);
          setXIT (ui->TxFreqSpinBox->value (), request.dialHz);
          monitor (true);

          showStatusMessage (
            tr ("DXFun -> %1 %2 | %3 | %4 | %5")
              .arg (request.band)
              .arg (request.mode)
              .arg (request.callsign)
              .arg (request.exactSpotDial ? tr ("QSY SPOT ESATTA") : tr ("QSY STANDARD"))
              .arg (Radio::pretty_frequency_MHz_string (request.dialHz)));
        });
      return true;
    };
'@

# Band Hunter usa Monitor Hunter come motore CQ. Il timer avanza soltanto
# durante l'ascolto effettivo; QSO e DX OCCUPATO lo mettono in pausa.
Replace-Required "widgets/mainwindow.cpp" @'
  auto * monitorHunterAction = hunterMenu->addAction (tr ("Monitor Hunter..."));
  connect (monitorHunterAction, &QAction::triggered, [monitorHunterPanel] {
      monitorHunterPanel->openWindow ();
    });

  // FT8 Hunter: aggiorna automaticamente Worked Before da Log4OM.
'@ @'
  auto * monitorHunterAction = hunterMenu->addAction (tr ("Monitor Hunter..."));
  connect (monitorHunterAction, &QAction::triggered, [monitorHunterPanel] {
      monitorHunterPanel->openWindow ();
    });

  auto tuneBandHunterBand = [this] (QString const& band) -> int {
      if (m_transmitting || m_tune || m_auto) return BandHunterPanel::QsyBusy;
      if (g_dxFunPanel && g_dxFunPanel->huntActive ()) return BandHunterPanel::QsyBusy;

      if (m_mode != QStringLiteral ("FT8"))
        on_actionFT8_triggered ();

      quint64 preferred = 0;
      if (band == QStringLiteral ("80m")) preferred = 3573000;
      else if (band == QStringLiteral ("60m")) preferred = 5357000;
      else if (band == QStringLiteral ("40m")) preferred = 7074000;
      else if (band == QStringLiteral ("30m")) preferred = 10136000;
      else if (band == QStringLiteral ("20m")) preferred = 14074000;
      else if (band == QStringLiteral ("17m")) preferred = 18100000;
      else if (band == QStringLiteral ("15m")) preferred = 21074000;
      else if (band == QStringLiteral ("12m")) preferred = 24915000;
      else if (band == QStringLiteral ("10m")) preferred = 28074000;
      if (!preferred) return BandHunterPanel::QsyUnavailable;

      auto const row = m_config.frequencies ()->best_working_frequency (preferred);
      if (row < 0) return BandHunterPanel::QsyUnavailable;

      auto const& frequencies = m_config.frequencies ();
      auto const sourceIndex =
        frequencies->mapToSource (
          frequencies->index (row, FrequencyList_v2_101::frequency_column));
      if (!sourceIndex.isValid ()) return BandHunterPanel::QsyUnavailable;

      auto const dial = frequencies->frequency_list ()[sourceIndex.row ()].frequency_;
      if (m_config.bands ()->find (dial) != band)
        return BandHunterPanel::QsyUnavailable;

      ui->pbBandHopping->setChecked (false);
      ui->bandComboBox->setCurrentIndex (row);
      on_bandComboBox_activated (row);
      ui->RxFreqSpinBox->setValue (1500);
      monitor (true);

      showStatusMessage (
        tr ("Band Hunter -> %1 FT8 | %2")
          .arg (band)
          .arg (Radio::pretty_frequency_MHz_string (dial)));
      return BandHunterPanel::QsySuccess;
    };

  auto * bandHunterPanel = new BandHunterPanel {
    tuneBandHunterBand,
    [monitorHunterPanel] { return monitorHunterPanel->automaticEnabled (); },
    [monitorHunterPanel] (bool on) { return monitorHunterPanel->setAutomaticEnabled (on); },
    [monitorHunterPanel] { return monitorHunterPanel->huntActive (); },
    [] { return g_dxFunPanel && g_dxFunPanel->huntActive (); },
    m_settings, this};
  g_bandHunterPanel = bandHunterPanel;

  auto * bandHunterAction = hunterMenu->addAction (tr ("Band Hunter..."));
  connect (bandHunterAction, &QAction::triggered, [bandHunterPanel] {
      bandHunterPanel->openWindow ();
    });

  // FT8 Hunter: aggiorna automaticamente Worked Before da Log4OM.
'@

Write-Host "Applied FT8 Hunter $HunterVersion exact DXFun QSY/split + automatic Band Hunter."


# ---------------------------------------------------------------------------
# FT8 Hunter 1.0.14 stabile
# Branding IU2JMZ, PSK Reporter e integrazione Hunter.
# ---------------------------------------------------------------------------

# La 1.0.14 mantiene il nome applicazione "FT8 Hunter" della linea stabile:
# riutilizza quindi configurazione radio/audio, filtri e Log4OM/MySQL esistenti.

# Il logo approvato viene usato per splash e icona Windows dell'eseguibile.
if (-not (Test-Path "wsjtx-hunter/branding/FT8Hunter.png")) {
    throw "Logo FT8 Hunter non trovato: wsjtx-hunter/branding/FT8Hunter.png"
}
if (-not (Test-Path "wsjtx-hunter/branding/FT8Hunter.ico")) {
    throw "Icona FT8 Hunter non trovata: wsjtx-hunter/branding/FT8Hunter.ico"
}
Copy-Item "wsjtx-hunter/branding/FT8Hunter.ico" (Join-Path $SourceDir "icons/windows-icons/wsjtx.ico") -Force
Copy-Item "wsjtx-hunter/branding/FT8Hunter.png" (Join-Path $SourceDir "artwork/splash.png") -Force

# PSK Reporter: sorgente remota separata con conferma CQ locale.
Copy-Item "wsjtx-hunter/psk/PskReporterMqtt.hpp" (Join-Path $hunterDir "PskReporterMqtt.hpp") -Force
Copy-Item "wsjtx-hunter/psk/PskReporterMqtt.cpp" (Join-Path $hunterDir "PskReporterMqtt.cpp") -Force
Copy-Item "wsjtx-hunter/psk/PskReporterPanel.hpp" (Join-Path $hunterDir "PskReporterPanel.hpp") -Force
Copy-Item "wsjtx-hunter/psk/PskReporterPanel.cpp" (Join-Path $hunterDir "PskReporterPanel.cpp") -Force

Replace-Required "CMakeLists.txt" @'
  FT8Hunter/MonitorHunterPanel.cpp
  FT8Hunter/BandHunterPanel.cpp
  Configuration.cpp
'@ @'
  FT8Hunter/MonitorHunterPanel.cpp
  FT8Hunter/BandHunterPanel.cpp
  FT8Hunter/PskReporterMqtt.cpp
  FT8Hunter/PskReporterPanel.cpp
  Configuration.cpp
'@

Replace-Required "widgets/mainwindow.cpp" @'
#include "FT8Hunter/MonitorHunterPanel.hpp"
#include "FT8Hunter/BandHunterPanel.hpp"
'@ @'
#include "FT8Hunter/MonitorHunterPanel.hpp"
#include "FT8Hunter/BandHunterPanel.hpp"
#include "FT8Hunter/PskReporterPanel.hpp"
'@

Replace-Required "widgets/mainwindow.cpp" @'
DxFunClusterPanel * g_dxFunPanel = nullptr;
MonitorHunterPanel * g_monitorHunterPanel = nullptr;
BandHunterPanel * g_bandHunterPanel = nullptr;
'@ @'
DxFunClusterPanel * g_dxFunPanel = nullptr;
MonitorHunterPanel * g_monitorHunterPanel = nullptr;
BandHunterPanel * g_bandHunterPanel = nullptr;
PskReporterPanel * g_pskReporterPanel = nullptr;
'@

# Se arriva una priorita' DXFun mentre PSK Reporter sta solo ascoltando un
# candidato, PSK cede immediatamente la radio. DXFun resta la sorgente remota
# con priorita' maggiore.
Replace-Required "widgets/mainwindow.cpp" @'
  auto tuneDxFunSpot = [this] (DxFunTuneRequest const& request) -> bool {
      if (m_transmitting || m_tune || m_auto) return false;
      if (g_monitorHunterPanel && g_monitorHunterPanel->huntActive ()) return false;
'@ @'
  auto tuneDxFunSpot = [this] (DxFunTuneRequest const& request) -> bool {
      if (m_transmitting || m_tune || m_auto) return false;
      if (g_pskReporterPanel && g_pskReporterPanel->huntActive ())
        g_pskReporterPanel->yieldToDxFun ();
      if (g_monitorHunterPanel && g_monitorHunterPanel->huntActive ()) return false;
'@

# Monitor Hunter non deve partire su un altro candidato mentre PSK Reporter
# sta effettuando la verifica locale del proprio target.
Replace-Required "widgets/mainwindow.cpp" @'
      if (m_transmitting || m_tune || m_auto) return MonitorHunterPanel::StartUnavailable;
      if (g_dxFunPanel && g_dxFunPanel->huntActive ()) return MonitorHunterPanel::StartUnavailable;
'@ @'
      if (m_transmitting || m_tune || m_auto) return MonitorHunterPanel::StartUnavailable;
      if (g_dxFunPanel && g_dxFunPanel->huntActive ()) return MonitorHunterPanel::StartUnavailable;
      if (g_pskReporterPanel && g_pskReporterPanel->huntActive ()) return MonitorHunterPanel::StartUnavailable;
'@

# Crea la finestra PSK Reporter dopo Band Hunter.
Replace-Required "widgets/mainwindow.cpp" @'
  auto * bandHunterAction = hunterMenu->addAction (tr ("Band Hunter..."));
  connect (bandHunterAction, &QAction::triggered, [bandHunterPanel] {
      bandHunterPanel->openWindow ();
    });

  // FT8 Hunter: aggiorna automaticamente Worked Before da Log4OM.
'@ @'
  auto * bandHunterAction = hunterMenu->addAction (tr ("Band Hunter..."));
  connect (bandHunterAction, &QAction::triggered, [bandHunterPanel] {
      bandHunterPanel->openWindow ();
    });

  auto evaluatePskReporter =
    [this] (PskReporterReport const& report) -> PskReporterEvaluation {
      PskReporterEvaluation out;
      out.signalHz = report.frequencyHz;
      out.mode = report.mode;
      out.band = m_config.bands ()->find (report.frequencyHz);

      static QStringList const hunterBands {
        QStringLiteral ("80m"), QStringLiteral ("60m"), QStringLiteral ("40m"),
        QStringLiteral ("30m"), QStringLiteral ("20m"), QStringLiteral ("17m"),
        QStringLiteral ("15m"), QStringLiteral ("12m"), QStringLiteral ("10m")};

      if (!hunterBands.contains (out.band)) return out;
      if (out.mode != QStringLiteral ("FT8") && out.mode != QStringLiteral ("FT4")) return out;

      // PSK Reporter fornisce la frequenza RF del segnale, non necessariamente
      // il dial. Preferisci un dial FT8/FT4 configurato quando il segnale cade
      // nel passabanda; altrimenti crea una base con il segnale a 1500 Hz.
      qint64 bestDistance = std::numeric_limits<qint64>::max ();
      auto const region = m_config.region ();
      for (auto const& item : m_config.frequencies ()->frequency_list ())
        {
          auto const itemMode = QString::fromLatin1 (Modes::name (item.mode_));
          if (itemMode != out.mode) continue;
          if (item.region_ != IARURegions::ALL && item.region_ != region) continue;
          if (m_config.bands ()->find (item.frequency_) != out.band) continue;

          auto const offset =
            static_cast<qint64> (report.frequencyHz) - static_cast<qint64> (item.frequency_);
          if (offset < 200 || offset > 5000) continue;

          auto const distance = qAbs (offset - 1500);
          if (distance >= bestDistance) continue;
          bestDistance = distance;
          out.dialHz = item.frequency_;
          out.offsetHz = static_cast<int> (offset);
        }

      if (!out.dialHz)
        {
          if (report.frequencyHz <= 1500) return out;
          out.dialHz = report.frequencyHz - 1500;
          out.offsetHz = 1500;
        }

      auto const& entity = m_logBook.countries ()->lookup (report.callsign);
      out.country = entity.entity_name;
      if (out.country.isEmpty ()) return out;

      bool callB4 = true;
      bool countryB4 = true;
      bool gridB4 = true;
      bool continentB4 = true;
      bool cqB4 = true;
      bool ituB4 = true;

      m_logBook.match (report.callsign, QString {}, report.grid, entity,
                       callB4, countryB4, gridB4, continentB4, cqB4, ituB4);
      out.newDxcc = !countryB4;

      callB4 = countryB4 = gridB4 = continentB4 = cqB4 = ituB4 = true;
      m_logBook.match (report.callsign, QString {}, report.grid, entity,
                       callB4, countryB4, gridB4, continentB4, cqB4, ituB4, out.band);
      out.newBand = !countryB4;
      out.digital = true;
      return out;
    };

  auto tunePskReporter =
    [this] (PskReporterTuneRequest const& request) -> bool {
      if (m_transmitting || m_tune || m_auto) return false;
      if (g_dxFunPanel && g_dxFunPanel->huntActive ()) return false;
      if (g_monitorHunterPanel && g_monitorHunterPanel->huntActive ()) return false;
      if (request.mode != QStringLiteral ("FT8") && request.mode != QStringLiteral ("FT4"))
        return false;

      if (g_bandHunterPanel && g_bandHunterPanel->active ())
        g_bandHunterPanel->pauseForExternalHunter ();

      if (request.mode == QStringLiteral ("FT8") && m_mode != QStringLiteral ("FT8"))
        on_actionFT8_triggered ();
      else if (request.mode == QStringLiteral ("FT4") && m_mode != QStringLiteral ("FT4"))
        on_actionFT4_triggered ();

      auto const row = m_config.frequencies ()->best_working_frequency (request.dialHz);
      if (row < 0) return false;

      ui->pbBandHopping->setChecked (false);
      ui->bandComboBox->setCurrentIndex (row);
      m_bandEdited = true;
      band_changed (request.dialHz);
      m_wideGraph->setRxBand (request.band);

      auto offset = request.offsetHz;
      if (offset < ui->RxFreqSpinBox->minimum ()) offset = ui->RxFreqSpinBox->minimum ();
      if (offset > ui->RxFreqSpinBox->maximum ()) offset = ui->RxFreqSpinBox->maximum ();
      ui->RxFreqSpinBox->setValue (offset);

      setRig (request.dialHz);
      setXIT (ui->TxFreqSpinBox->value (), request.dialHz);
      monitor (true);

      showStatusMessage (
        tr ("PSK Reporter -> %1 %2 | %3 | ascolto CQ locale")
          .arg (request.callsign)
          .arg (request.band)
          .arg (request.status));
      return true;
    };

  auto handoffPskReporter =
    [monitorHunterPanel] (QString const& rawText,
                          QString const& callsign,
                          QString const& grid,
                          QString const& country,
                          QString const& mode,
                          QString const& band,
                          int audioOffset,
                          int snr) -> bool {
      MonitorHunterCandidate candidate;
      candidate.callsign = callsign;
      candidate.grid = grid;
      candidate.country = country;
      candidate.mode = mode;
      candidate.band = band;
      candidate.rawText = rawText;
      candidate.audioOffset = audioOffset;
      candidate.snr = snr;
      candidate.receivedUtc = QDateTime::currentDateTimeUtc ();
      return monitorHunterPanel->enqueueExternalCandidate (candidate);
    };

  auto * pskReporterPanel = new PskReporterPanel {
    evaluatePskReporter, tunePskReporter, handoffPskReporter, m_settings, this};
  g_pskReporterPanel = pskReporterPanel;

  // PSK Reporter deve classificare NEW DXCC/NEW BANDA solo dopo che il
  // Worked Before e' stato caricato. Se Log4OM/MySQL e' abilitato, quel
  // caricamento comprende anche il database remoto. In caso di errore,
  // AUTO QSY PSK resta disabilitato: meglio nessun candidato che un falso NEW.
  connect (&m_logBook, &LogBook::finished_loading, pskReporterPanel,
           [pskReporterPanel] (int recordCount, QString, QString const& error) {
      pskReporterPanel->setWorkedBeforeState (error.trimmed ().isEmpty (),
                                              recordCount, error);
    });

  // Forza un nuovo caricamento dopo aver collegato il pannello, cosi' la
  // 1.0.14 verifica subito la configurazione Log4OM/MySQL gia' usata dalla stabile.
  pskReporterPanel->setWorkedBeforeState (false, 0, QString {});
  m_logBook.rescan ();

  auto * pskReporterAction =
    hunterMenu->addAction (tr ("PSK Reporter..."));
  connect (pskReporterAction, &QAction::triggered, [pskReporterPanel] {
      pskReporterPanel->openWindow ();
    });

  // FT8 Hunter: aggiorna automaticamente Worked Before da Log4OM.
'@

# Il Band Hunter resta in pausa anche durante la verifica PSK Reporter.
Replace-Required "widgets/mainwindow.cpp" @'
    [] { return g_dxFunPanel && g_dxFunPanel->huntActive (); },
    m_settings, this};
'@ @'
    [] {
      return (g_dxFunPanel && g_dxFunPanel->huntActive ())
        || (g_pskReporterPanel && g_pskReporterPanel->huntActive ());
    },
    m_settings, this};
'@

# Nel decoder locale PSK Reporter puo' confermare il target solo tramite un
# vero CQ ricevuto da questa radio. Durante quella verifica gli altri CQ non
# vengono messi automaticamente in caccia dal Monitor Hunter.
Replace-Required "widgets/mainwindow.cpp" @'
          g_monitorHunterPanel->observeDecode (
            decodedtext0.string (), text, decodedtext.call ().trimmed ().toUpper (),
            mhCqCall, mhDeCall, mhGrid,
            decodedtext.frequencyOffset (), decodedtext.snr (),
            m_mode, m_currentBand, mhCountry, mhEligibleCq);
'@ @'
          bool const pskWasListening =
            g_pskReporterPanel && g_pskReporterPanel->huntActive ();

          if (g_pskReporterPanel)
            g_pskReporterPanel->observeLocalCq (
              decodedtext0.string (), mhCqCall, mhGrid, mhCountry,
              m_mode, m_currentBand,
              decodedtext.frequencyOffset (), decodedtext.snr (), mhEligibleCq);

          g_monitorHunterPanel->observeDecode (
            decodedtext0.string (), text, decodedtext.call ().trimmed ().toUpper (),
            mhCqCall, mhDeCall, mhGrid,
            decodedtext.frequencyOffset (), decodedtext.snr (),
            m_mode, m_currentBand, mhCountry, mhEligibleCq && !pskWasListening);
'@

Write-Host "Applied FT8 Hunter $HunterVersion branding + PSK Reporter local-CQ gate + Log4OM readiness gate."


# ---------------------------------------------------------------------------
# FT8 Hunter 2.0.0 - New Styling + FT8 Hunter Remote
# ---------------------------------------------------------------------------

Copy-Item "wsjtx-hunter/ui/HunterTheme.hpp" (Join-Path $hunterDir "HunterTheme.hpp") -Force
Copy-Item "wsjtx-hunter/ui/HunterTheme.cpp" (Join-Path $hunterDir "HunterTheme.cpp") -Force
Copy-Item "wsjtx-hunter/remote/HunterRemoteServer.hpp" (Join-Path $hunterDir "HunterRemoteServer.hpp") -Force
Copy-Item "wsjtx-hunter/remote/HunterRemoteServer.cpp" (Join-Path $hunterDir "HunterRemoteServer.cpp") -Force
Copy-Item "wsjtx-hunter/remote/HunterRemoteDialog.hpp" (Join-Path $hunterDir "HunterRemoteDialog.hpp") -Force
Copy-Item "wsjtx-hunter/remote/HunterRemoteDialog.cpp" (Join-Path $hunterDir "HunterRemoteDialog.cpp") -Force

Replace-Required "CMakeLists.txt" @'
  FT8Hunter/PskReporterMqtt.cpp
  FT8Hunter/PskReporterPanel.cpp
  Configuration.cpp
'@ @'
  FT8Hunter/PskReporterMqtt.cpp
  FT8Hunter/PskReporterPanel.cpp
  FT8Hunter/HunterTheme.cpp
  FT8Hunter/HunterRemoteServer.cpp
  FT8Hunter/HunterRemoteDialog.cpp
  Configuration.cpp
'@

Replace-Required "widgets/mainwindow.cpp" @'
#include "FT8Hunter/PskReporterPanel.hpp"
'@ @'
#include "FT8Hunter/PskReporterPanel.hpp"
#include "FT8Hunter/HunterTheme.hpp"
#include "FT8Hunter/HunterRemoteServer.hpp"
#include "FT8Hunter/HunterRemoteDialog.hpp"
#include <QJsonArray>
'@

# Stato RF mostrato nell'app remota.
Replace-Required "widgets/mainwindow.h" @'
  int m_monitorHunterPreviousTxFreq {0};
  QScopedPointer<EchoGraph> m_echoGraph;
'@ @'
  int m_monitorHunterPreviousTxFreq {0};
  int m_hunterRemoteRfPower {-1};
  QScopedPointer<EchoGraph> m_echoGraph;
'@

# Integra tema e server remoto dopo che tutti i pannelli Hunter sono stati creati.
Replace-Required "widgets/mainwindow.cpp" @'
  auto * pskReporterAction =
    hunterMenu->addAction (tr ("PSK Reporter..."));
  connect (pskReporterAction, &QAction::triggered, [pskReporterPanel] {
      pskReporterPanel->openWindow ();
    });

  // FT8 Hunter: aggiorna automaticamente Worked Before da Log4OM.
'@ @'
  auto * pskReporterAction =
    hunterMenu->addAction (tr ("PSK Reporter..."));
  connect (pskReporterAction, &QAction::triggered, [pskReporterPanel] {
      pskReporterPanel->openWindow ();
    });

  // FT8 Hunter 2.0: styling coerente PC/app con scelta Dark/Light.
  auto applyHunterTheme = [this] (HunterTheme::Mode mode) {
      HunterTheme::save (m_settings, mode);
      HunterTheme::apply (qApp, mode);
      m_useDarkStyle = mode == HunterTheme::Mode::Dark;
      m_wideGraph->setDarkStyle (m_useDarkStyle);
    };
  auto const savedHunterTheme = HunterTheme::load (m_settings);
  HunterTheme::apply (qApp, savedHunterTheme);
  m_useDarkStyle = savedHunterTheme == HunterTheme::Mode::Dark;
  m_wideGraph->setDarkStyle (m_useDarkStyle);

  auto * appearanceMenu = hunterMenu->addMenu (tr ("Aspetto 2.0"));
  auto * darkThemeAction = appearanceMenu->addAction (tr ("Dark"));
  auto * lightThemeAction = appearanceMenu->addAction (tr ("Light"));
  darkThemeAction->setCheckable (true);
  lightThemeAction->setCheckable (true);
  auto * themeGroup = new QActionGroup {appearanceMenu};
  themeGroup->setExclusive (true);
  themeGroup->addAction (darkThemeAction);
  themeGroup->addAction (lightThemeAction);
  darkThemeAction->setChecked (savedHunterTheme == HunterTheme::Mode::Dark);
  lightThemeAction->setChecked (savedHunterTheme == HunterTheme::Mode::Light);
  connect (darkThemeAction, &QAction::triggered, this,
           [applyHunterTheme] { applyHunterTheme (HunterTheme::Mode::Dark); });
  connect (lightThemeAction, &QAction::triggered, this,
           [applyHunterTheme] { applyHunterTheme (HunterTheme::Mode::Light); });

  auto remoteStateProvider =
    [this, monitorHunterPanel, bandHunterPanel, pskReporterPanel] () -> QJsonObject {
      QJsonObject state;
      state.insert (QStringLiteral ("version"), QStringLiteral ("2.0.0"));
      state.insert (QStringLiteral ("myCall"), m_config.my_callsign ());
      state.insert (QStringLiteral ("frequencyHz"), static_cast<double> (m_freqNominal));
      state.insert (QStringLiteral ("band"), m_currentBand);
      state.insert (QStringLiteral ("mode"), m_mode);
      state.insert (QStringLiteral ("rxOffset"), ui->RxFreqSpinBox->value ());
      state.insert (QStringLiteral ("txOffset"), ui->TxFreqSpinBox->value ());
      state.insert (QStringLiteral ("monitoring"), m_monitoring);
      state.insert (QStringLiteral ("transmitting"), m_transmitting);
      state.insert (QStringLiteral ("autoTx"), m_auto);
      state.insert (QStringLiteral ("tune"), m_tune);
      if (m_hunterRemoteRfPower >= 0)
        state.insert (QStringLiteral ("rfPower"), m_hunterRemoteRfPower);

      state.insert (QStringLiteral ("monitorHunter"), monitorHunterPanel->remoteSnapshot ());
      state.insert (QStringLiteral ("bandHunter"), bandHunterPanel->remoteSnapshot ());
      state.insert (QStringLiteral ("dxFun"),
                    g_dxFunPanel ? g_dxFunPanel->remoteSnapshot () : QJsonObject {});
      state.insert (QStringLiteral ("pskReporter"), pskReporterPanel->remoteSnapshot ());
      state.insert (QStringLiteral ("blacklist"), monitorHunterPanel->blacklistSnapshot ());
      return state;
    };

  auto remoteSpectrumProvider = [this] () -> QJsonObject {
      QJsonObject out;
      out.insert (QStringLiteral ("startHz"), 500);
      out.insert (QStringLiteral ("endHz"), 2400);
      QJsonArray values;
      auto const samples = m_wideGraph->hunterRemoteSpectrum (256);
      for (auto const sample : samples)
        values.append (static_cast<double> (sample));
      out.insert (QStringLiteral ("values"), values);
      return out;
    };

  auto remoteCommandHandler =
    [this, monitorHunterPanel, bandHunterPanel]
    (QJsonObject const& command, bool allowTransmit) -> QJsonObject {
      QJsonObject reply;
      auto ok = [&reply] (QString const& message) {
          reply.insert (QStringLiteral ("ok"), true);
          reply.insert (QStringLiteral ("message"), message);
        };
      auto fail = [&reply] (QString const& message) {
          reply.insert (QStringLiteral ("ok"), false);
          reply.insert (QStringLiteral ("message"), message);
        };

      auto const name = command.value (QStringLiteral ("name")).toString ();

      if (name == QStringLiteral ("refresh"))
        {
          ok (tr ("Stato aggiornato"));
        }
      else if (name == QStringLiteral ("stop_tx"))
        {
          if (m_auto) auto_tx_mode (false);
          if (m_transmitting) ui->stopTxButton->click ();
          ok (tr ("TX arrestata"));
        }
      else if (name == QStringLiteral ("monitor"))
        {
          monitor (command.value (QStringLiteral ("enabled")).toBool (true));
          ok (tr ("Monitor aggiornato"));
        }
      else if (name == QStringLiteral ("set_mode"))
        {
          if (m_transmitting || m_tune)
            {
              fail (tr ("Modo non modificabile durante TX/Tune"));
            }
          else
            {
              auto const mode = command.value (QStringLiteral ("mode")).toString ().toUpper ();
              if (mode == QStringLiteral ("FT8"))
                on_actionFT8_triggered ();
              else if (mode == QStringLiteral ("FT4"))
                on_actionFT4_triggered ();
              else
                {
                  fail (tr ("Modo remoto consentito: FT8 o FT4"));
                  return reply;
                }
              ok (tr ("Modo %1").arg (mode));
            }
        }
      else if (name == QStringLiteral ("set_frequency"))
        {
          if (m_transmitting || m_tune || m_auto)
            {
              fail (tr ("QSY non disponibile durante TX/Tune/Auto"));
            }
          else
            {
              auto const hz = static_cast<Frequency> (
                command.value (QStringLiteral ("frequencyHz")).toDouble ());
              if (hz < 1000000 || hz > 60000000)
                {
                  fail (tr ("Frequenza fuori intervallo"));
                  return reply;
                }

              auto const row = m_config.frequencies ()->best_working_frequency (hz);
              if (row < 0)
                {
                  fail (tr ("Nessuna working frequency compatibile"));
                  return reply;
                }

              ui->pbBandHopping->setChecked (false);
              ui->bandComboBox->setCurrentIndex (row);
              m_bandEdited = true;
              band_changed (hz);
              m_wideGraph->setRxBand (m_config.bands ()->find (hz));
              setRig (hz);
              setXIT (ui->TxFreqSpinBox->value (), hz);
              monitor (true);
              ok (tr ("QSY eseguita"));
            }
        }
      else if (name == QStringLiteral ("set_rx_offset"))
        {
          auto value = command.value (QStringLiteral ("value")).toInt ();
          value = qBound (ui->RxFreqSpinBox->minimum (), value, ui->RxFreqSpinBox->maximum ());
          ui->RxFreqSpinBox->setValue (value);
          ok (tr ("RX %1 Hz").arg (value));
        }
      else if (name == QStringLiteral ("set_tx_offset"))
        {
          auto value = command.value (QStringLiteral ("value")).toInt ();
          value = qBound (500, value, 2400);
          ui->TxFreqSpinBox->setValue (value);
          ok (tr ("TX %1 Hz").arg (value));
        }
      else if (name == QStringLiteral ("set_power"))
        {
          auto value = qBound (5, command.value (QStringLiteral ("value")).toInt (), 100);
          m_config.transceiver_rf_power_percent (value);
          m_hunterRemoteRfPower = value;
          ok (tr ("RF Power %1%").arg (value));
        }
      else if (name == QStringLiteral ("monitor_hunter_auto"))
        {
          auto const enabled = command.value (QStringLiteral ("enabled")).toBool ();
          if (enabled && !allowTransmit)
            {
              fail (tr ("TX remota non autorizzata nelle impostazioni PC"));
            }
          else if (!monitorHunterPanel->setAutomaticEnabled (enabled))
            {
              fail (tr ("Monitor Hunter non disponibile: verificare filtro DXCC"));
            }
          else
            {
              ok (enabled ? tr ("Monitor Hunter AUTO attivo") : tr ("Monitor Hunter AUTO disattivo"));
            }
        }
      else if (name == QStringLiteral ("band_hunter"))
        {
          auto const enabled = command.value (QStringLiteral ("enabled")).toBool ();
          if (enabled && !allowTransmit)
            {
              fail (tr ("TX remota non autorizzata nelle impostazioni PC"));
            }
          else if (!bandHunterPanel->setActive (enabled))
            {
              fail (tr ("Impossibile cambiare stato Band Hunter"));
            }
          else
            {
              ok (enabled ? tr ("Band Hunter attivo") : tr ("Band Hunter disattivo"));
            }
        }
      else if (name == QStringLiteral ("blacklist_reinstate"))
        {
          auto const call = command.value (QStringLiteral ("call")).toString ();
          if (monitorHunterPanel->reinstateCall (call))
            ok (tr ("%1 riabilitata").arg (call));
          else
            fail (tr ("Nominativo non presente in blacklist"));
        }
      else if (name == QStringLiteral ("get_log"))
        {
          auto const mysql = Log4OmMysql::loadSettings ();
          if (!mysql.enabled)
            {
              fail (tr ("Log4OM/MySQL non abilitato"));
            }
          else
            {
              QString error;
              auto const rows = Log4OmMysql::loadRecentQsos (mysql, 30, &error);
              if (!error.isEmpty ())
                {
                  fail (error);
                }
              else
                {
                  QJsonArray jsonRows;
                  for (auto const& row : rows)
                    {
                      QJsonObject item;
                      item.insert (QStringLiteral ("call"), row.callsign);
                      item.insert (QStringLiteral ("date"), row.qsoDate);
                      item.insert (QStringLiteral ("band"), row.band);
                      item.insert (QStringLiteral ("mode"), row.mode);
                      jsonRows.append (item);
                    }
                  reply.insert (QStringLiteral ("rows"), jsonRows);
                  ok (tr ("%1 QSO caricati").arg (rows.size ()));
                }
            }
        }
      else
        {
          fail (tr ("Comando remoto non riconosciuto"));
        }

      return reply;
    };

  auto * remoteServer = new HunterRemoteServer {
    remoteStateProvider, remoteSpectrumProvider, remoteCommandHandler, this};

  auto remoteSettings = HunterRemoteServer::loadSettings ();
  HunterRemoteServer::saveSettings (remoteSettings);
  if (remoteSettings.enabled)
    {
      QString error;
      if (!remoteServer->start (remoteSettings, &error))
        showStatusMessage (tr ("FT8 Hunter Remote non avviato: %1").arg (error));
    }

  auto * remoteAction = hunterMenu->addAction (tr ("FT8 Hunter Remote 2.0.0..."));
  connect (remoteAction, &QAction::triggered, this, [this, remoteServer] {
      auto settings = HunterRemoteServer::loadSettings ();
      HunterRemoteDialog dlg {settings, this};
      if (QDialog::Accepted != dlg.exec ()) return;

      settings = dlg.settings ();
      HunterRemoteServer::saveSettings (settings);
      remoteServer->stop ();

      if (settings.enabled)
        {
          QString error;
          if (!remoteServer->start (settings, &error))
            {
              MessageBox::warning_message (
                this, tr ("FT8 Hunter Remote"),
                tr ("Impossibile avviare il server remoto: %1").arg (error));
            }
          else
            {
              showStatusMessage (
                tr ("FT8 Hunter Remote attivo: %1")
                  .arg (HunterRemoteServer::appUrl (settings)));
            }
        }
      else
        {
          showStatusMessage (tr ("FT8 Hunter Remote disattivato"));
        }
    });

  // FT8 Hunter: aggiorna automaticamente Worked Before da Log4OM.
'@

Write-Host "Applied FT8 Hunter $HunterVersion 2.0 styling + authenticated Remote PWA server."
