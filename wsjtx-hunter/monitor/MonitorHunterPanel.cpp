#include "MonitorHunterPanel.hpp"

#include <QCheckBox>
#include <QCloseEvent>
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
}

MonitorHunterPanel::MonitorHunterPanel (QString const& myCall,
                                        Starter starter,
                                        AbortHandler abortHandler,
                                        QSettings * settings,
                                        QWidget * parent)
  : QDialog {parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint}
  , myCall_ {myCall.trimmed ().toUpper ()}
  , starter_ {std::move (starter)}
  , abortHandler_ {std::move (abortHandler)}
  , settings_ {settings}
  , autoCheck_ {new QCheckBox {tr ("Attiva Monitor Hunter automatico"), this}}
  , filterLabel_ {new QLabel {this}}
  , stateLabel_ {new QLabel {tr ("STATO: OFF"), this}}
  , activeLabel_ {new QLabel {tr ("Nessun QSO Hunter attivo"), this}}
  , queueLabel_ {new QLabel {this}}
  , table_ {new QTableWidget {0, 8, this}}
  , stopButton_ {new QPushButton {tr ("Ferma QSO Hunter"), this}}
  , clearButton_ {new QPushButton {tr ("Svuota coda"), this}}
  , retryTimer_ {new QTimer {this}}
{
  setWindowTitle (tr ("FT8 Hunter - Monitor Hunter"));
  setModal (false);
  setMinimumSize (860, 420);
  setAttribute (Qt::WA_DeleteOnClose, false);

  autoCheck_->setEnabled (false);
  autoCheck_->setToolTip (
    tr ("Disponibile solo con il filtro 'Escludi country/DXCC gia lavorati sulla banda corrente' attivo e BP disattivato."));

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
  top->addSpacing (15);
  top->addWidget (filterLabel_);
  top->addStretch ();
  top->addWidget (stopButton_);
  top->addWidget (clearButton_);

  auto * root = new QVBoxLayout;
  root->addLayout (top);
  root->addWidget (stateLabel_);
  root->addWidget (activeLabel_);
  root->addWidget (queueLabel_);
  root->addWidget (table_);
  setLayout (root);

  retryTimer_->setSingleShot (true);
  connect (retryTimer_, &QTimer::timeout, this, [this] { attemptStart (); });

  connect (autoCheck_, &QCheckBox::toggled, this, [this] (bool checked) {
      if (checked)
        {
          stateLabel_->setText (tr ("STATO: IN ASCOLTO - attendo il primo CQ valido"));
          QTimer::singleShot (0, this, [this] { attemptStart (); });
        }
      else
        {
          retryTimer_->stop ();
          queue_.clear ();
          if (state_ != HuntState::Idle)
            resetActive (tr ("STATO: OFF"), true, false);
          else
            stateLabel_->setText (tr ("STATO: OFF"));
        }
      updateSummary ();
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
      settings_->endGroup ();
    }

  setFilterActive (false);
  updateSummary ();
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
  if (active)
    {
      filterLabel_->setText (tr ("Filtro DXCC banda: ATTIVO"));
      autoCheck_->setEnabled (true);
      if (!autoCheck_->isChecked ())
        stateLabel_->setText (tr ("STATO: OFF - pronto"));
    }
  else
    {
      filterLabel_->setText (tr ("Filtro DXCC banda: NON ATTIVO"));
      if (autoCheck_->isChecked ())
        autoCheck_->setChecked (false);
      autoCheck_->setEnabled (false);
      stateLabel_->setText (tr ("STATO: OFF - attivare il filtro DXCC sulla banda"));
    }
  updateSummary ();
}

bool MonitorHunterPanel::inCooldown (QString const& call, QDateTime const& now) const
{
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

  // Mantiene l'ordine del primo CQ visto, ma aggiorna i dati se lo stesso CQ viene ridichiarato.
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
      stateLabel_->setText (tr ("STATO: IN ASCOLTO - nessun CQ valido in coda"));
      updateSummary ();
      return;
    }

  auto candidate = queue_.takeFirst ();
  if (!starter_ || !starter_ (candidate))
    {
      // Radio/Auto/DXFun occupati: conserva il primo candidato finche' resta recente.
      queue_.prepend (candidate);
      stateLabel_->setText (
        tr ("STATO: ATTESA - radio/Auto/DXFun occupati; candidato %1").arg (candidate.callsign));
      updateSummary ();
      retryTimer_->start (1500);
      return;
    }

  active_ = candidate;
  state_ = HuntState::Calling;
  answered_ = false;
  txCycles_ = 0;
  lastPowerPercent_ = 50;
  stopButton_->setEnabled (true);
  stateLabel_->setText (tr ("STATO: CHIAMATA"));
  activeLabel_->setText (
    tr ("%1  %2  %3  |  audio %4 Hz  |  potenza iniziale 50%%")
      .arg (active_.callsign)
      .arg (active_.country)
      .arg (active_.band)
      .arg (active_.audioOffset));
  appendEvent (active_, tr ("CHIAMO - 0/8"));
  updateSummary ();
}

void MonitorHunterPanel::observeDecode (QString const& rawText,
                                        QString const& cleanText,
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

  // Durante un QSO Hunter continua a leggere tutti i decode, per riconoscere la risposta.
  if (state_ != HuntState::Idle)
    {
      auto const target = active_.callsign.trimmed ().toUpper ();
      auto text = cleanText;
      text.remove ('<');
      text.remove ('>');
      auto const upper = text.toUpper ();
      auto const de = deCall.trimmed ().toUpper ();

      if (!myCall_.isEmpty ()
          && (de == target || upper.contains (target))
          && upper.contains (myCall_))
        {
          if (!answered_)
            {
              answered_ = true;
              state_ = HuntState::Qso;
              stateLabel_->setText (tr ("STATO: RISPOSTA RICEVUTA - Auto Seq completa il QSO"));
              activeLabel_->setText (
                tr ("%1 ha risposto - RF congelata al %2%% - attendo fine QSO e logging")
                  .arg (target)
                  .arg (lastPowerPercent_));
              appendEvent (active_, tr ("RISPOSTA - QSO IN CORSO"));
            }
        }
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

int MonitorHunterPanel::nextTxPowerPercent ()
{
  if (!qsoActive ()) return -1;

  // Dopo otto chiamate senza alcuna risposta la nona TX non deve partire.
  if (!answered_ && txCycles_ >= 8)
    return StopTransmission;

  if (txCycles_ == 0)
    lastPowerPercent_ = 50;
  else if (!answered_)
    lastPowerPercent_ = 50 + 5 * txCycles_;

  ++txCycles_;

  if (answered_)
    {
      stateLabel_->setText (tr ("STATO: QSO IN CORSO"));
      activeLabel_->setText (
        tr ("%1 - risposta ricevuta - TX ciclo %2 - RF congelata %3%%")
          .arg (active_.callsign)
          .arg (txCycles_)
          .arg (lastPowerPercent_));
    }
  else
    {
      stateLabel_->setText (tr ("STATO: CHIAMATA %1/8").arg (txCycles_));
      activeLabel_->setText (
        tr ("%1 - TX ciclo %2/8 - RF Power %3%%")
          .arg (active_.callsign)
          .arg (txCycles_)
          .arg (lastPowerPercent_));
      appendEvent (active_, tr ("TX %1/8 - %2%%").arg (txCycles_).arg (lastPowerPercent_));
    }

  return lastPowerPercent_;
}

void MonitorHunterPanel::callingExhausted ()
{
  if (!qsoActive () || answered_) return;

  ++failedCalls_;
  appendEvent (active_, tr ("8/8 - NESSUNA RISPOSTA"));
  cooldownUntil_.insert (
    active_.callsign.trimmed ().toUpper (),
    QDateTime::currentDateTimeUtc ().addSecs (FailedCooldownSeconds));

  active_ = MonitorHunterCandidate {};
  state_ = HuntState::Idle;
  answered_ = false;
  txCycles_ = 0;
  lastPowerPercent_ = 50;
  stopButton_->setEnabled (false);
  stateLabel_->setText (tr ("STATO: 8 chiamate senza risposta - cerco il prossimo CQ"));
  activeLabel_->setText (tr ("Nessun QSO Hunter attivo"));
  updateSummary ();
  retryTimer_->start (500);
}

void MonitorHunterPanel::qsoLogged (QString const& call)
{
  if (!qsoActive ()) return;
  if (call.trimmed ().toUpper () != active_.callsign.trimmed ().toUpper ()) return;

  ++completedQsos_;
  appendEvent (active_, tr ("QSO LOGGATO"));
  active_ = MonitorHunterCandidate {};
  state_ = HuntState::Idle;
  answered_ = false;
  txCycles_ = 0;
  lastPowerPercent_ = 50;
  stopButton_->setEnabled (false);
  stateLabel_->setText (tr ("STATO: QSO completato - cerco il prossimo CQ"));
  activeLabel_->setText (tr ("Nessun QSO Hunter attivo"));
  updateSummary ();
  retryTimer_->start (500);
}

void MonitorHunterPanel::resetActive (QString const& status, bool abortRadio, bool cooldown)
{
  retryTimer_->stop ();
  if (state_ != HuntState::Idle && cooldown && !active_.callsign.isEmpty ())
    cooldownUntil_.insert (
      active_.callsign.trimmed ().toUpper (),
      QDateTime::currentDateTimeUtc ().addSecs (FailedCooldownSeconds));

  if (abortRadio && abortHandler_) abortHandler_ ();

  active_ = MonitorHunterCandidate {};
  state_ = HuntState::Idle;
  answered_ = false;
  txCycles_ = 0;
  lastPowerPercent_ = 50;
  stopButton_->setEnabled (false);
  stateLabel_->setText (status);
  activeLabel_->setText (tr ("Nessun QSO Hunter attivo"));
  updateSummary ();

  if (autoCheck_->isChecked () && filterActive_)
    retryTimer_->start (500);
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
    tr ("Coda CQ recenti: %1   |   QSO completati: %2   |   8/8 senza risposta: %3")
      .arg (queue_.size ())
      .arg (completedQsos_)
      .arg (failedCalls_));
}

void MonitorHunterPanel::closeEvent (QCloseEvent * event)
{
  // Chiudere la finestra disattiva l'automazione: nessuna TX automatica resta nascosta.
  if (autoCheck_->isChecked ())
    autoCheck_->setChecked (false);

  if (settings_)
    {
      settings_->beginGroup (QStringLiteral ("FT8HunterMonitor"));
      settings_->setValue (QStringLiteral ("geometry"), saveGeometry ());
      settings_->endGroup ();
      settings_->sync ();
    }

  QDialog::closeEvent (event);
}
