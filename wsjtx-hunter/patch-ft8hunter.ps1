param(
    [Parameter(Mandatory=$true)][string]$SourceDir,
    [string]$HunterVersion = "1.0.8"
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


# FT8 Hunter 1.0.8: filtro country/DXCC gia' lavorati.
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


# FT8 Hunter 1.0.8: puntatore al pannello cluster usato dai hook decoder/TX.
Replace-Required "widgets/mainwindow.cpp" @'
QString earlyDecodes = "";  //ft8md
'@ @'
QString earlyDecodes = "";  //ft8md
DxFunClusterPanel * g_dxFunPanel = nullptr;
'@

# FT8 Hunter 1.0.8: quando la stazione spottata viene realmente decodificata,
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
                if (m_logDlg->isVisible ()) m_logDlg->accept ();
              }
              if (g_dxFunPanel) g_dxFunPanel->qsoLogged ();
            });
          }
        }
'@

# FT8 Hunter 1.0.8: rampa RF del 7300 ad ogni trasmissione Hunter.
Replace-Required "widgets/mainwindow.cpp" @'
    transmit (snr);
'@ @'
    if (g_dxFunPanel && g_dxFunPanel->qsoActive ()) {
      auto const hunterPower = g_dxFunPanel->nextTxPowerPercent ();
      if (hunterPower >= 0)
        m_config.transceiver_rf_power_percent (hunterPower);
    }
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


# FT8 Hunter 1.0.8: RF Power anche quando l'IC-7300 e' configurato tramite OmniRig.
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
