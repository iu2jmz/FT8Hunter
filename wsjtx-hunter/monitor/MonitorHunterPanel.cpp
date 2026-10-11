#include "MonitorHunterPanel.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
  constexpr int CandidateFreshSeconds = 45;
  constexpr int FailedCooldownSeconds = 300;
  constexpr int BlacklistSeconds = 5 * 60 * 60;
}

MonitorHunterPanel::MonitorHunterPanel (QString const& myCall,
                                        Starter starter,
                                        PauseHandler pauseHandler,
                                        ResumeHandler resumeHandler,
                                        AbortHandler abortHandler,
                                        ReleaseHandler releaseHandler,
                                        QSettings * settings,
                                        QWidget * parent)
  : QDialog {parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint}
  , myCall_ {myCall.trimmed ().toUpper ()}
  , starter_ {std::move (starter)}
  , pauseHandler_ {std::move (pauseHandler)}
  , resumeHandler_ {std::move (resumeHandler)}
  , abortHandler_ {std::move (abortHandler)}
  , releaseHandler_ {std::move (releaseHandler)}
  , settings_ {settings}
  , autoCheck_ {new QCheckBox {tr ("Attiva Monitor Hunter automatico"), this}}
  , autoTxSlotCheck_ {new QCheckBox {tr ("Scegli automaticamente una frequenza TX libera"), this}}
  , filterLabel_ {new QLabel {this}}
  , stateLabel_ {new QLabel {tr ("OFF"), this}}
  , targetLabel_ {new QLabel {tr ("TARGET: --"), this}}
  , txSlotLabel_ {new QLabel {tr ("TX SLOT: --"), this}}
  , progressLabel_ {new QLabel {tr ("HUNTER: --"), this}}
  , queueLabel_ {new QLabel {this}}
  , table_ {new QTableWidget {0, 8, this}}
  , stopButton_ {new QPushButton {tr ("Ferma QSO Hunter"), this}}
  , clearButton_ {new QPushButton {tr ("Svuota coda"), this}}
  , blacklistButton_ {new QPushButton {tr ("Blacklist 5h"), this}}
  , blacklistDialog_ {new QDialog {this}}
  , blacklistTable_ {new QTableWidget {0, 4, blacklistDialog_}}
  , reinstateButton_ {new QPushButton {tr ("Riabilita"), blacklistDialog_}}
  , reinstateAllButton_ {new QPushButton {tr ("Riabilita tutte"), blacklistDialog_}}
  , retryTimer_ {new QTimer {this}}
  , busyTimer_ {new QTimer {this}}
  , blacklistTimer_ {new QTimer {this}}
{
  setWindowTitle (tr ("FT8 Hunter - Monitor Hunter"));
  setModal (false);
  setMinimumSize (930, 500);
  setAttribute (Qt::WA_DeleteOnClose, false);

  autoCheck_->setEnabled (false);
  autoCheck_->setToolTip (
    tr ("Disponibile solo con il filtro 'Escludi country/DXCC gia lavorati sulla banda corrente' attivo e BP disattivato."));

  autoTxSlotCheck_->setChecked (true);
  autoTxSlotCheck_->setToolTip (
    tr ("Analizza lo spettro recente del Grafico Ampio, sceglie una zona quieta e blocca la TX li' per tutto il QSO."));

  stateLabel_->setAlignment (Qt::AlignCenter);
  stateLabel_->setMinimumHeight (54);
  QFont stateFont = stateLabel_->font ();
  stateFont.setBold (true);
  stateFont.setPointSize (14);
  stateLabel_->setFont (stateFont);

  for (auto * label : {targetLabel_, txSlotLabel_, progressLabel_})
    {
      QFont f = label->font ();
      f.setBold (true);
      f.setPointSize (10);
      label->setFont (f);
      label->setMinimumHeight (28);
    }

  table_->setHorizontalHeaderLabels (
    {tr ("UTC"), tr ("DX"), tr ("DXCC"), tr ("Banda"), tr ("Modo"), tr ("Audio Hz"), tr ("SNR"), tr ("Stato")});
  table_->setSelectionBehavior (QAbstractItemView::SelectRows);
  table_->setSelectionMode (QAbstractItemView::SingleSelection);
  table_->setEditTriggers (QAbstractItemView::NoEditTriggers);
  table_->verticalHeader ()->setVisible (false);
  for (int i = 0; i < 7; ++i)
    table_->horizontalHeader ()->setSectionResizeMode (i, QHeaderView::ResizeToContents);
  table_->horizontalHeader ()->setSectionResizeMode (7, QHeaderView::Stretch);

  stopButton_->setEnabled (false);

  auto * top = new QHBoxLayout;
  top->addWidget (autoCheck_);
  top->addSpacing (12);
  top->addWidget (autoTxSlotCheck_);
  top->addSpacing (12);
  top->addWidget (filterLabel_);
  top->addStretch ();
  top->addWidget (blacklistButton_);
  top->addWidget (stopButton_);
  top->addWidget (clearButton_);

  auto * qsoInfo = new QHBoxLayout;
  qsoInfo->addWidget (targetLabel_, 3);
  qsoInfo->addWidget (txSlotLabel_, 2);
  qsoInfo->addWidget (progressLabel_, 2);

  auto * root = new QVBoxLayout;
  root->addLayout (top);
  root->addWidget (stateLabel_);
  root->addLayout (qsoInfo);
  root->addWidget (queueLabel_);
  root->addWidget (table_);
  setLayout (root);

  blacklistDialog_->setWindowTitle (tr ("FT8 Hunter - Blacklist Monitor Hunter"));
  blacklistDialog_->setModal (false);
  blacklistDialog_->setMinimumSize (560, 320);
  blacklistTable_->setHorizontalHeaderLabels (
    {tr ("Nominativo"), tr ("Banda"), tr ("Inserita UTC"), tr ("Tempo residuo")});
  blacklistTable_->setSelectionBehavior (QAbstractItemView::SelectRows);
  blacklistTable_->setSelectionMode (QAbstractItemView::SingleSelection);
  blacklistTable_->setEditTriggers (QAbstractItemView::NoEditTriggers);
  blacklistTable_->verticalHeader ()->setVisible (false);
  for (int i = 0; i < 3; ++i)
    blacklistTable_->horizontalHeader ()->setSectionResizeMode (i, QHeaderView::ResizeToContents);
  blacklistTable_->horizontalHeader ()->setSectionResizeMode (3, QHeaderView::Stretch);

  auto * blacklistButtons = new QHBoxLayout;
  blacklistButtons->addStretch ();
  blacklistButtons->addWidget (reinstateButton_);
  blacklistButtons->addWidget (reinstateAllButton_);
  auto * blacklistRoot = new QVBoxLayout;
  blacklistRoot->addWidget (new QLabel {
    tr ("Le stazioni che non rispondono dopo 8/8 chiamate sono escluse per 5 ore."), blacklistDialog_});
  blacklistRoot->addWidget (blacklistTable_);
  blacklistRoot->addLayout (blacklistButtons);
  blacklistDialog_->setLayout (blacklistRoot);

  retryTimer_->setSingleShot (true);
  busyTimer_->setSingleShot (true);
  blacklistTimer_->setInterval (1000);
  connect (retryTimer_, &QTimer::timeout, this, [this] { attemptStart (); });
  connect (busyTimer_, &QTimer::timeout, this, [this] { busyTimeout (); });
  connect (blacklistTimer_, &QTimer::timeout, this, [this] {
      pruneBlacklist ();
      updateBlacklistWindow ();
    });
  connect (blacklistButton_, &QPushButton::clicked, this, [this] { openBlacklistWindow (); });
  connect (reinstateButton_, &QPushButton::clicked, this, [this] { reinstateSelected (); });
  connect (reinstateAllButton_, &QPushButton::clicked, this, [this] { reinstateAll (); });
  blacklistTimer_->start ();

  connect (autoCheck_, &QCheckBox::toggled, this, [this] (bool checked) {
      if (checked)
        {
          showListeningState (tr ("attendo il primo CQ valido"));
          QTimer::singleShot (0, this, [this] { attemptStart (); });
        }
      else
        {
          retryTimer_->stop ();
          busyTimer_->stop ();
          queue_.clear ();
          if (state_ != HuntState::Idle)
            resetActive (tr ("automazione disattivata"), true, false);
          showOffState (filterActive_ ? tr ("pronto") : tr ("attivare il filtro DXCC sulla banda"));
        }
      updateSummary ();
    });

  connect (autoTxSlotCheck_, &QCheckBox::toggled, this, [this] (bool checked) {
      if (settings_)
        {
          settings_->beginGroup (QStringLiteral ("FT8HunterMonitor"));
          settings_->setValue (QStringLiteral ("autoTxSlot"), checked);
          settings_->endGroup ();
          settings_->sync ();
        }
    });

  connect (stopButton_, &QPushButton::clicked, this, [this] {
      if (state_ != HuntState::Idle)
        resetActive (tr ("QSO Hunter fermato manualmente"), true, true);
    });

  connect (clearButton_, &QPushButton::clicked, this, [this] {
      queue_.clear ();
      table_->setRowCount (0);
      updateSummary ();
    });

  if (settings_)
    {
      settings_->beginGroup (QStringLiteral ("FT8HunterMonitor"));
      restoreGeometry (settings_->value (QStringLiteral ("geometry"), saveGeometry ()).toByteArray ());
      autoTxSlotCheck_->setChecked (settings_->value (QStringLiteral ("autoTxSlot"), true).toBool ());
      settings_->endGroup ();
    }

  loadBlacklist ();
  updateBlacklistWindow ();
  setFilterActive (false);
  updateSummary ();
}

void MonitorHunterPanel::showOffState (QString const& detail)
{
  stateLabel_->setText (tr ("OFF") + (detail.isEmpty () ? QString {} : QStringLiteral ("  -  ") + detail));
  stateLabel_->setStyleSheet (
    QStringLiteral ("QLabel { background:#606060; color:white; border:1px solid #404040; padding:8px; }"));
}

void MonitorHunterPanel::showListeningState (QString const& detail)
{
  stateLabel_->setText (tr ("ASCOLTO") + (detail.isEmpty () ? QString {} : QStringLiteral ("  -  ") + detail));
  stateLabel_->setStyleSheet (
    QStringLiteral ("QLabel { background:#55c271; color:black; border:1px solid #2f8f49; padding:8px; }"));
}

void MonitorHunterPanel::showCallingState (QString const& detail)
{
  stateLabel_->setText (tr ("CHIAMATA") + (detail.isEmpty () ? QString {} : QStringLiteral ("  -  ") + detail));
  stateLabel_->setStyleSheet (
    QStringLiteral ("QLabel { background:#ffd54f; color:black; border:1px solid #b7950b; padding:8px; }"));
}

void MonitorHunterPanel::showBusyState (QString const& detail)
{
  stateLabel_->setText (tr ("DX OCCUPATO") + (detail.isEmpty () ? QString {} : QStringLiteral ("  -  ") + detail));
  stateLabel_->setStyleSheet (
    QStringLiteral ("QLabel { background:#ff9800; color:black; border:1px solid #b26a00; padding:8px; }"));
}

void MonitorHunterPanel::showQsoState (QString const& detail)
{
  stateLabel_->setText (tr ("QSO") + (detail.isEmpty () ? QString {} : QStringLiteral ("  -  ") + detail));
  stateLabel_->setStyleSheet (
    QStringLiteral ("QLabel { background:#e95b54; color:white; border:1px solid #a93226; padding:8px; }"));
}

void MonitorHunterPanel::updateActiveLabels ()
{
  if (state_ == HuntState::Idle || active_.callsign.isEmpty ())
    {
      targetLabel_->setText (tr ("TARGET: --"));
      txSlotLabel_->setText (tr ("TX SLOT: --"));
      progressLabel_->setText (tr ("HUNTER: --"));
      return;
    }

  targetLabel_->setText (
    tr ("TARGET: %1 | %2 | RX %3 Hz")
      .arg (active_.callsign)
      .arg (active_.country.isEmpty () ? active_.band : active_.country + QStringLiteral (" | ") + active_.band)
      .arg (active_.audioOffset));

  txSlotLabel_->setText (
    lockedTxOffset_ >= 0
      ? tr ("TX SLOT: %1 Hz - BLOCCATA%2")
          .arg (lockedTxOffset_)
          .arg (lockedTxWasAutomatic_ ? tr (" (AUTO WATERFALL)") : tr (" (MANUALE)"))
      : tr ("TX SLOT: --"));

  if (state_ == HuntState::Busy)
    {
      progressLabel_->setText (
        tr ("HUNTER: pausa | %1/8 | RF %2%%")
          .arg (watchdog_.callingTransmissions ())
          .arg (lastPowerPercent_));
    }
  else if (watchdog_.answered ())
    {
      progressLabel_->setText (
        tr ("HUNTER: attendo progresso %1/4 | RF %2%%")
          .arg (watchdog_.noProgressTransmissions ())
          .arg (lastPowerPercent_));
    }
  else
    {
      progressLabel_->setText (
        tr ("HUNTER: %1/8 | RF %2%%")
          .arg (watchdog_.callingTransmissions ())
          .arg (lastPowerPercent_));
    }
}

void MonitorHunterPanel::openWindow ()
{
  show ();
  raise ();
  activateWindow ();
}

void MonitorHunterPanel::setFilterActive (bool active)
{
  filterActive_ = active;
  filterLabel_->setText (
    active ? tr ("Filtro DXCC banda: ATTIVO") : tr ("Filtro DXCC banda: NON ATTIVO"));

  autoCheck_->setEnabled (active);
  autoTxSlotCheck_->setEnabled (active && state_ == HuntState::Idle);

  if (!active)
    {
      if (autoCheck_->isChecked ())
        autoCheck_->setChecked (false);
      showOffState (tr ("attivare il filtro DXCC sulla banda"));
    }
  else if (!autoCheck_->isChecked ())
    {
      showOffState (tr ("pronto"));
    }
  updateSummary ();
}

bool MonitorHunterPanel::isBlacklisted (QString const& call, QDateTime const& now) const
{
  auto const key = call.trimmed ().toUpper ();
  auto const it = blacklist_.constFind (key);
  return it != blacklist_.constEnd () && it->untilUtc.isValid () && now < it->untilUtc;
}

bool MonitorHunterPanel::inCooldown (QString const& call, QDateTime const& now) const
{
  if (isBlacklisted (call, now)) return true;
  auto const until = cooldownUntil_.value (call.trimmed ().toUpper ());
  return until.isValid () && now < until;
}

void MonitorHunterPanel::pruneQueue ()
{
  auto const now = QDateTime::currentDateTimeUtc ();
  for (int i = queue_.size () - 1; i >= 0; --i)
    {
      auto const& candidate = queue_.at (i);
      if (candidate.receivedUtc.secsTo (now) > CandidateFreshSeconds
          || inCooldown (candidate.callsign, now))
        queue_.removeAt (i);
    }
}

void MonitorHunterPanel::enqueueCandidate (MonitorHunterCandidate const& candidate)
{
  if (!autoCheck_->isChecked () || !filterActive_) return;

  auto const call = candidate.callsign.trimmed ().toUpper ();
  if (call.isEmpty ()) return;
  if (state_ != HuntState::Idle
      && call == active_.callsign.trimmed ().toUpper ())
    return;

  auto const now = QDateTime::currentDateTimeUtc ();
  if (inCooldown (call, now)) return;

  for (int i = 0; i < queue_.size (); ++i)
    {
      if (queue_.at (i).callsign.trimmed ().toUpper () == call)
        {
          queue_[i] = candidate;
          updateSummary ();
          return;
        }
    }

  queue_.append (candidate);
  updateSummary ();
  QTimer::singleShot (0, this, [this] { attemptStart (); });
}

void MonitorHunterPanel::attemptStart ()
{
  if (!autoCheck_->isChecked () || !filterActive_ || state_ != HuntState::Idle) return;

  pruneQueue ();
  if (queue_.isEmpty ())
    {
      showListeningState (tr ("nessun CQ valido in coda"));
      updateSummary ();
      return;
    }

  auto candidate = queue_.takeFirst ();
  auto const automaticTx = autoTxSlotCheck_->isChecked ();
  auto const txOffset = starter_ ? starter_ (candidate, automaticTx) : StartUnavailable;

  if (txOffset < 0)
    {
      queue_.prepend (candidate);
      showListeningState (
        automaticTx
          ? tr ("attesa: radio/Auto/DXFun occupati oppure spettro non ancora pronto")
          : tr ("attesa: radio/Auto/DXFun occupati"));
      updateSummary ();
      retryTimer_->start (1500);
      return;
    }

  active_ = candidate;
  state_ = HuntState::Calling;
  watchdog_.reset (active_.callsign, myCall_);
  pendingStopReason_ = HunterQsoWatchdog::StopReason::None;
  lastPowerPercent_ = 50;
  lockedTxOffset_ = txOffset;
  lockedTxWasAutomatic_ = automaticTx;
  stopButton_->setEnabled (true);
  autoTxSlotCheck_->setEnabled (false);

  showCallingState (tr ("preparo %1 | 0/8").arg (active_.callsign));
  updateActiveLabels ();
  appendEvent (active_, tr ("CHIAMO - 0/8 | TX %1 Hz").arg (lockedTxOffset_));
  updateSummary ();
}

void MonitorHunterPanel::enterBusy (QString const& otherCall)
{
  if (state_ != HuntState::Calling || watchdog_.answered ()) return;

  state_ = HuntState::Busy;
  if (pauseHandler_) pauseHandler_ ();
  busyTimer_->start (HunterQsoWatchdog::BusyTimeoutSeconds * 1000);

  auto const detail = otherCall.trimmed ().isEmpty () || otherCall == QStringLiteral ("CQ")
    ? tr ("%1 sta lavorando un'altra stazione - attendo nuovo CQ").arg (active_.callsign)
    : tr ("%1 sta lavorando %2 - attendo nuovo CQ").arg (active_.callsign).arg (otherCall);

  showBusyState (detail);
  appendEvent (active_, tr ("DX OCCUPATO - pausa %1/8").arg (watchdog_.callingTransmissions ()));
  updateActiveLabels ();
}

void MonitorHunterPanel::busyTimeout ()
{
  if (state_ != HuntState::Busy) return;
  appendEvent (active_, tr ("DX OCCUPATO > 90 s - ABBANDONO"));
  resetActive (tr ("DX occupato oltre 90 s - cerco il prossimo CQ"), true, true);
}

void MonitorHunterPanel::observeDecode (QString const& rawText,
                                        QString const& cleanText,
                                        QString const& firstCall,
                                        QString const& cqCall,
                                        QString const& deCall,
                                        QString const& grid,
                                        int audioOffset,
                                        int snr,
                                        QString const& mode,
                                        QString const& band,
                                        QString const& country,
                                        bool eligibleCq)
{
  auto const now = QDateTime::currentDateTimeUtc ();
  auto const event = watchdog_.observe (firstCall, deCall, cqCall, cleanText);

  if (state_ == HuntState::Calling)
    {
      if (event == HunterQsoWatchdog::DecodeEvent::TargetToMe)
        {
          busyTimer_->stop ();
          state_ = HuntState::Qso;
          lastPowerPercent_ = watchdog_.powerPercent ();
          showQsoState (tr ("%1 ha risposto - Auto Seq completa il QSO").arg (active_.callsign));
          appendEvent (active_, tr ("RISPOSTA - RF BLOCCATA %1%%").arg (lastPowerPercent_));
          updateActiveLabels ();
        }
      else if (event == HunterQsoWatchdog::DecodeEvent::TargetToOther)
        {
          enterBusy (deCall);
        }
    }
  else if (state_ == HuntState::Busy)
    {
      bool const directToMe = event == HunterQsoWatchdog::DecodeEvent::TargetToMe;
      bool const returnedCq = event == HunterQsoWatchdog::DecodeEvent::TargetCq;
      if (directToMe || returnedCq)
        {
          if (resumeHandler_ && resumeHandler_ (rawText, audioOffset))
            {
              busyTimer_->stop ();
              active_.audioOffset = audioOffset;
              if (directToMe)
                {
                  state_ = HuntState::Qso;
                  showQsoState (tr ("%1 e' tornato da noi - riprendo Auto Seq").arg (active_.callsign));
                  appendEvent (active_, tr ("DX TORNA A NOI - QSO"));
                }
              else
                {
                  state_ = HuntState::Calling;
                  showCallingState (
                    tr ("%1 torna in CQ - riprendo da %2/8")
                      .arg (active_.callsign)
                      .arg (watchdog_.callingTransmissions ()));
                  appendEvent (
                    active_,
                    tr ("NUOVO CQ - RIPRENDO DA %1/8").arg (watchdog_.callingTransmissions ()));
                }
              updateActiveLabels ();
            }
        }
    }
  else if (state_ == HuntState::Qso
           && event == HunterQsoWatchdog::DecodeEvent::TargetToMe)
    {
      showQsoState (tr ("%1 - progresso ricevuto, watchdog azzerato").arg (active_.callsign));
      appendEvent (active_, tr ("PROGRESSO RX - CONTATORE 0/4"));
      updateActiveLabels ();
    }

  if (!eligibleCq || cqCall.trimmed ().isEmpty ()) return;

  MonitorHunterCandidate candidate;
  candidate.callsign = cqCall.trimmed ().toUpper ();
  candidate.grid = grid.trimmed ().toUpper ();
  candidate.country = country;
  candidate.mode = mode;
  candidate.band = band;
  candidate.rawText = rawText;
  candidate.audioOffset = audioOffset;
  candidate.snr = snr;
  candidate.receivedUtc = now;

  appendEvent (candidate, autoCheck_->isChecked () ? tr ("CQ VALIDO") : tr ("CQ VALIDO - AUTO OFF"));
  enqueueCandidate (candidate);
}

bool MonitorHunterPanel::qsoActive () const
{
  return state_ == HuntState::Calling || state_ == HuntState::Qso;
}

bool MonitorHunterPanel::huntActive () const
{
  return state_ != HuntState::Idle;
}

bool MonitorHunterPanel::automaticEnabled () const
{
  return autoCheck_->isChecked ();
}

bool MonitorHunterPanel::setAutomaticEnabled (bool enabled)
{
  if (enabled && !filterActive_) return false;
  if (autoCheck_->isChecked () != enabled)
    autoCheck_->setChecked (enabled);
  return autoCheck_->isChecked () == enabled;
}

bool MonitorHunterPanel::enqueueExternalCandidate (MonitorHunterCandidate const& candidate)
{
  if (!filterActive_) return false;
  if (!autoCheck_->isChecked () && !setAutomaticEnabled (true)) return false;
  if (isBlacklisted (candidate.callsign, QDateTime::currentDateTimeUtc ())) return false;
  enqueueCandidate (candidate);
  return true;
}

int MonitorHunterPanel::nextTxPowerPercent (QString const& txMessage)
{
  if (!qsoActive ()) return -1;

  auto const result = watchdog_.beforeTransmit (txMessage);
  if (!result.allow)
    {
      pendingStopReason_ = result.stopReason;
      return StopTransmission;
    }

  pendingStopReason_ = HunterQsoWatchdog::StopReason::None;
  lastPowerPercent_ = result.powerPercent;

  if (watchdog_.answered ())
    {
      showQsoState (
        tr ("%1 - attendo progresso %2/4")
          .arg (active_.callsign)
          .arg (result.noProgressTransmission));
      appendEvent (
        active_,
        tr ("TX QSO %1/4 | RF %2%% | TX %3 Hz")
          .arg (result.noProgressTransmission)
          .arg (lastPowerPercent_)
          .arg (lockedTxOffset_));
    }
  else
    {
      showCallingState (
        tr ("%1 | %2/8")
          .arg (active_.callsign)
          .arg (result.callingTransmission));
      appendEvent (
        active_,
        tr ("TX %1/8 | RF %2%% | TX %3 Hz")
          .arg (result.callingTransmission)
          .arg (lastPowerPercent_)
          .arg (lockedTxOffset_));
    }

  updateActiveLabels ();
  return lastPowerPercent_;
}

void MonitorHunterPanel::transmissionExhausted ()
{
  if (state_ == HuntState::Idle) return;

  QString status;
  QString event;
  if (pendingStopReason_ == HunterQsoWatchdog::StopReason::NoProgressLimit)
    {
      event = tr ("QSO ABBANDONATO - 4 TX SENZA PROGRESSO");
      status = tr ("4 ritrasmissioni senza progresso - cerco il prossimo CQ");
    }
  else
    {
      ++failedCalls_;
      event = tr ("8/8 - NESSUNA RISPOSTA - BLACKLIST 5 ORE");
      status = tr ("8 chiamate senza risposta - blacklist 5 ore; cerco il prossimo CQ");
      addToBlacklist (active_);
      appendEvent (active_, event);
      resetActive (status, true, false);
      return;
    }

  appendEvent (active_, event);
  resetActive (status, true, true);
}

void MonitorHunterPanel::qsoLogged (QString const& call)
{
  if (!huntActive ()) return;
  if (call.trimmed ().toUpper () != active_.callsign.trimmed ().toUpper ()) return;

  ++completedQsos_;
  appendEvent (active_, tr ("QSO LOGGATO"));

  busyTimer_->stop ();
  if (releaseHandler_) releaseHandler_ ();

  active_ = MonitorHunterCandidate {};
  state_ = HuntState::Idle;
  watchdog_.clear ();
  pendingStopReason_ = HunterQsoWatchdog::StopReason::None;
  lastPowerPercent_ = 50;
  lockedTxOffset_ = -1;
  lockedTxWasAutomatic_ = false;
  stopButton_->setEnabled (false);
  autoTxSlotCheck_->setEnabled (filterActive_);
  updateActiveLabels ();
  showListeningState (tr ("QSO completato - cerco il prossimo CQ"));
  updateSummary ();
  retryTimer_->start (500);
}

void MonitorHunterPanel::resetActive (QString const& status, bool abortRadio, bool cooldown)
{
  retryTimer_->stop ();
  busyTimer_->stop ();
  auto const hadActive = state_ != HuntState::Idle;

  if (hadActive && cooldown && !active_.callsign.isEmpty ())
    cooldownUntil_.insert (
      active_.callsign.trimmed ().toUpper (),
      QDateTime::currentDateTimeUtc ().addSecs (FailedCooldownSeconds));

  if (hadActive && abortRadio && abortHandler_) abortHandler_ ();
  if (hadActive && releaseHandler_) releaseHandler_ ();

  active_ = MonitorHunterCandidate {};
  state_ = HuntState::Idle;
  watchdog_.clear ();
  pendingStopReason_ = HunterQsoWatchdog::StopReason::None;
  lastPowerPercent_ = 50;
  lockedTxOffset_ = -1;
  lockedTxWasAutomatic_ = false;
  stopButton_->setEnabled (false);
  autoTxSlotCheck_->setEnabled (filterActive_);
  updateActiveLabels ();

  if (autoCheck_->isChecked () && filterActive_)
    {
      showListeningState (status);
      retryTimer_->start (500);
    }
  else
    {
      showOffState (status);
    }

  updateSummary ();
}

void MonitorHunterPanel::addToBlacklist (MonitorHunterCandidate const& candidate)
{
  auto const call = candidate.callsign.trimmed ().toUpper ();
  if (call.isEmpty ()) return;

  auto const now = QDateTime::currentDateTimeUtc ();
  BlacklistEntry entry;
  entry.band = candidate.band;
  entry.addedUtc = now;
  entry.untilUtc = now.addSecs (BlacklistSeconds);
  blacklist_.insert (call, entry);

  for (int i = queue_.size () - 1; i >= 0; --i)
    if (queue_.at (i).callsign.trimmed ().toUpper () == call)
      queue_.removeAt (i);

  saveBlacklist ();
  updateBlacklistWindow ();
}

void MonitorHunterPanel::pruneBlacklist ()
{
  auto const now = QDateTime::currentDateTimeUtc ();
  bool changed = false;
  for (auto it = blacklist_.begin (); it != blacklist_.end (); )
    {
      if (!it->untilUtc.isValid () || now >= it->untilUtc)
        {
          it = blacklist_.erase (it);
          changed = true;
        }
      else
        {
          ++it;
        }
    }
  if (changed) saveBlacklist ();
}

void MonitorHunterPanel::loadBlacklist ()
{
  if (!settings_) return;

  settings_->beginGroup (QStringLiteral ("FT8HunterMonitor"));
  auto const rows = settings_->value (QStringLiteral ("blacklist5h")).toStringList ();
  settings_->endGroup ();

  auto const now = QDateTime::currentDateTimeUtc ();
  for (auto const& row : rows)
    {
      auto const parts = row.split (QLatin1Char ('|'));
      if (parts.size () != 4) continue;

      bool okAdded = false;
      bool okUntil = false;
      auto const added = parts.at (2).toLongLong (&okAdded);
      auto const until = parts.at (3).toLongLong (&okUntil);
      if (!okAdded || !okUntil) continue;

      BlacklistEntry entry;
      entry.band = parts.at (1);
      entry.addedUtc = QDateTime::fromSecsSinceEpoch (added, Qt::UTC);
      entry.untilUtc = QDateTime::fromSecsSinceEpoch (until, Qt::UTC);
      if (entry.untilUtc > now)
        blacklist_.insert (parts.at (0).trimmed ().toUpper (), entry);
    }
  saveBlacklist ();
}

void MonitorHunterPanel::saveBlacklist () const
{
  if (!settings_) return;

  QStringList rows;
  for (auto it = blacklist_.constBegin (); it != blacklist_.constEnd (); ++it)
    {
      rows.append (
        QStringLiteral ("%1|%2|%3|%4")
          .arg (it.key ())
          .arg (it->band)
          .arg (it->addedUtc.toSecsSinceEpoch ())
          .arg (it->untilUtc.toSecsSinceEpoch ()));
    }

  settings_->beginGroup (QStringLiteral ("FT8HunterMonitor"));
  settings_->setValue (QStringLiteral ("blacklist5h"), rows);
  settings_->endGroup ();
  settings_->sync ();
}

void MonitorHunterPanel::openBlacklistWindow ()
{
  pruneBlacklist ();
  updateBlacklistWindow ();
  blacklistDialog_->show ();
  blacklistDialog_->raise ();
  blacklistDialog_->activateWindow ();
}

void MonitorHunterPanel::updateBlacklistWindow ()
{
  auto const selectedCall =
    blacklistTable_->currentRow () >= 0 && blacklistTable_->item (blacklistTable_->currentRow (), 0)
      ? blacklistTable_->item (blacklistTable_->currentRow (), 0)->text ()
      : QString {};

  blacklistTable_->setRowCount (0);
  auto const now = QDateTime::currentDateTimeUtc ();
  QStringList calls = blacklist_.keys ();
  calls.sort ();

  for (auto const& call : calls)
    {
      auto const entry = blacklist_.value (call);
      if (!entry.untilUtc.isValid () || now >= entry.untilUtc) continue;

      auto seconds = now.secsTo (entry.untilUtc);
      if (seconds < 0) seconds = 0;
      auto const h = seconds / 3600;
      auto const m = (seconds % 3600) / 60;
      auto const s = seconds % 60;

      auto const row = blacklistTable_->rowCount ();
      blacklistTable_->insertRow (row);
      blacklistTable_->setItem (row, 0, new QTableWidgetItem {call});
      blacklistTable_->setItem (row, 1, new QTableWidgetItem {entry.band});
      blacklistTable_->setItem (row, 2, new QTableWidgetItem {
        entry.addedUtc.toUTC ().toString (QStringLiteral ("dd/MM HH:mm:ss"))});
      blacklistTable_->setItem (row, 3, new QTableWidgetItem {
        QStringLiteral ("%1:%2:%3")
          .arg (h, 2, 10, QLatin1Char ('0'))
          .arg (m, 2, 10, QLatin1Char ('0'))
          .arg (s, 2, 10, QLatin1Char ('0'))});

      if (!selectedCall.isEmpty () && selectedCall == call)
        blacklistTable_->selectRow (row);
    }

  blacklistButton_->setText (tr ("Blacklist 5h (%1)").arg (blacklistTable_->rowCount ()));
  reinstateButton_->setEnabled (blacklistTable_->rowCount () > 0);
  reinstateAllButton_->setEnabled (blacklistTable_->rowCount () > 0);
}

void MonitorHunterPanel::reinstateSelected ()
{
  auto const row = blacklistTable_->currentRow ();
  if (row < 0 || !blacklistTable_->item (row, 0)) return;

  auto const call = blacklistTable_->item (row, 0)->text ().trimmed ().toUpper ();
  blacklist_.remove (call);
  cooldownUntil_.remove (call);
  saveBlacklist ();
  updateBlacklistWindow ();
  updateSummary ();
}

void MonitorHunterPanel::reinstateAll ()
{
  blacklist_.clear ();
  saveBlacklist ();
  updateBlacklistWindow ();
  updateSummary ();
}

void MonitorHunterPanel::appendEvent (MonitorHunterCandidate const& candidate, QString const& state)
{
  table_->insertRow (0);
  table_->setItem (0, 0, new QTableWidgetItem {
    candidate.receivedUtc.isValid ()
      ? candidate.receivedUtc.toUTC ().toString (QStringLiteral ("HH:mm:ss"))
      : QDateTime::currentDateTimeUtc ().toString (QStringLiteral ("HH:mm:ss"))});
  table_->setItem (0, 1, new QTableWidgetItem {candidate.callsign});
  table_->setItem (0, 2, new QTableWidgetItem {candidate.country});
  table_->setItem (0, 3, new QTableWidgetItem {candidate.band});
  table_->setItem (0, 4, new QTableWidgetItem {candidate.mode});
  table_->setItem (0, 5, new QTableWidgetItem {QString::number (candidate.audioOffset)});
  table_->setItem (0, 6, new QTableWidgetItem {QString::number (candidate.snr)});
  table_->setItem (0, 7, new QTableWidgetItem {state});

  while (table_->rowCount () > 250)
    table_->removeRow (table_->rowCount () - 1);
}

void MonitorHunterPanel::updateSummary ()
{
  queueLabel_->setText (
    tr ("Coda CQ recenti: %1   |   QSO completati: %2   |   8/8 senza risposta: %3   |   Blacklist 5h: %4")
      .arg (queue_.size ())
      .arg (completedQsos_)
      .arg (failedCalls_)
      .arg (blacklist_.size ()));
}

void MonitorHunterPanel::closeEvent (QCloseEvent * event)
{
  if (autoCheck_->isChecked ())
    autoCheck_->setChecked (false);

  if (settings_)
    {
      settings_->beginGroup (QStringLiteral ("FT8HunterMonitor"));
      settings_->setValue (QStringLiteral ("geometry"), saveGeometry ());
      settings_->setValue (QStringLiteral ("autoTxSlot"), autoTxSlotCheck_->isChecked ());
      settings_->endGroup ();
      settings_->sync ();
    }

  QDialog::closeEvent (event);
}
