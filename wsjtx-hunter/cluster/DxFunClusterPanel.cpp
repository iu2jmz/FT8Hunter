#include "DxFunClusterPanel.hpp"
#include "DxFunCluster.hpp"

#include <QCheckBox>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>
#include <utility>

namespace
{
  enum Roles
  {
    CallRole = Qt::UserRole + 1,
    ModeRole,
    BandRole,
    CountryRole,
    SpotHzRole,
    DialHzRole,
    OffsetRole,
    StatusRole
  };

  QString frequencyText (quint64 hz)
  {
    return QString::number (static_cast<double> (hz) / 1000000.0, 'f', 6);
  }
}

DxFunClusterPanel::DxFunClusterPanel (QString const& defaultCallsign,
                                      Evaluator evaluator,
                                      Tuner tuner,
                                      AbortHandler abortHandler,
                                      QSettings * settings,
                                      QWidget * parent)
  : QDockWidget {tr ("FT8 Hunter - DXFun Cluster"), parent}
  , defaultCallsign_ {defaultCallsign.trimmed ().toUpper ()}
  , evaluator_ {std::move (evaluator)}
  , tuner_ {std::move (tuner)}
  , abortHandler_ {std::move (abortHandler)}
  , settings_ {settings}
  , client_ {new DxFunClusterClient {this}}
  , clusterCall_ {new QLineEdit {this}}
  , connectButton_ {new QPushButton {tr ("Connetti"), this}}
  , autoQsyButton_ {new QPushButton {tr ("AUTO QSY"), this}}
  , newDxccCheck_ {new QCheckBox {tr ("NEW DXCC"), this}}
  , newBandCheck_ {new QCheckBox {tr ("NEW su banda"), this}}
  , ft8Check_ {new QCheckBox {QStringLiteral ("FT8"), this}}
  , ft4Check_ {new QCheckBox {QStringLiteral ("FT4"), this}}
  , showWorkedCheck_ {new QCheckBox {tr ("Mostra anche lavorati"), this}}
  , holdSeconds_ {new QSpinBox {this}}
  , statusLabel_ {new QLabel {tr ("DXFun disconnesso"), this}}
  , countersLabel_ {new QLabel {this}}
  , table_ {new QTableWidget {0, 7, this}}
  , goButton_ {new QPushButton {tr ("Vai allo spot"), this}}
  , clearButton_ {new QPushButton {tr ("Pulisci"), this}}
  , settleTimer_ {new QTimer {this}}
  , listenTimer_ {new QTimer {this}}
  , qsoTimer_ {new QTimer {this}}
{
  setObjectName (QStringLiteral ("FT8HunterDXFunCluster"));
  setAllowedAreas (Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
  setMinimumWidth (760);

  clusterCall_->setMaxLength (20);
  clusterCall_->setPlaceholderText (defaultCallsign_);
  connectButton_->setCheckable (true);
  autoQsyButton_->setCheckable (true);
  autoQsyButton_->setToolTip (
    tr ("Segue automaticamente NEW DXCC o NEW su banda. "
        "Dopo la QSY ascolta 150 s; se trova lo spot avvia il QSO automatico."));

  newDxccCheck_->setChecked (true);
  newBandCheck_->setChecked (true);
  ft8Check_->setChecked (true);
  ft4Check_->setChecked (true);

  holdSeconds_->setRange (15, 300);
  holdSeconds_->setSuffix (tr (" s"));
  holdSeconds_->setValue (60);
  holdSeconds_->setToolTip (tr ("Tempo minimo tra due QSY automatiche."));

  table_->setHorizontalHeaderLabels (
    {tr ("UTC"), tr ("DX"), tr ("MHz"), tr ("Banda"), tr ("Modo"), tr ("Stato"), tr ("Commento")});
  table_->setSelectionBehavior (QAbstractItemView::SelectRows);
  table_->setSelectionMode (QAbstractItemView::SingleSelection);
  table_->setEditTriggers (QAbstractItemView::NoEditTriggers);
  table_->verticalHeader ()->setVisible (false);
  for (int i = 0; i < 6; ++i)
    table_->horizontalHeader ()->setSectionResizeMode (i, QHeaderView::ResizeToContents);
  table_->horizontalHeader ()->setSectionResizeMode (6, QHeaderView::Stretch);
  table_->setSortingEnabled (false);

  auto * connectionRow = new QHBoxLayout;
  connectionRow->addWidget (new QLabel {tr ("Cluster:"), this});
  connectionRow->addWidget (new QLabel {QStringLiteral ("dxfun.com:8000"), this});
  connectionRow->addSpacing (10);
  connectionRow->addWidget (new QLabel {tr ("Nominativo login:"), this});
  connectionRow->addWidget (clusterCall_);
  connectionRow->addWidget (connectButton_);
  connectionRow->addWidget (autoQsyButton_);
  connectionRow->addStretch ();

  auto * filters = new QHBoxLayout;
  filters->addWidget (new QLabel {tr ("HF 80-10 m:"), this});
  filters->addWidget (newDxccCheck_);
  filters->addWidget (newBandCheck_);
  filters->addSpacing (10);
  filters->addWidget (ft8Check_);
  filters->addWidget (ft4Check_);
  filters->addWidget (showWorkedCheck_);
  filters->addSpacing (10);
  filters->addWidget (new QLabel {tr ("Min. tra QSY:"), this});
  filters->addWidget (holdSeconds_);
  filters->addStretch ();
  filters->addWidget (goButton_);
  filters->addWidget (clearButton_);

  auto * root = new QVBoxLayout;
  root->addLayout (connectionRow);
  root->addLayout (filters);
  root->addWidget (statusLabel_);
  root->addWidget (countersLabel_);
  root->addWidget (table_);

  auto * body = new QWidget {this};
  body->setLayout (root);
  setWidget (body);

  settleTimer_->setSingleShot (true);
  listenTimer_->setSingleShot (true);
  qsoTimer_->setSingleShot (true);
  connect (settleTimer_, &QTimer::timeout, this, [this] { attemptAutoQsy (); });
  connect (listenTimer_, &QTimer::timeout, this, [this] { listenTimeout (); });
  connect (qsoTimer_, &QTimer::timeout, this, [this] { qsoTimeout (); });

  if (settings_)
    {
      settings_->beginGroup (QStringLiteral ("FT8HunterDXFun"));
      clusterCall_->setText (settings_->value (QStringLiteral ("clusterCall"), defaultCallsign_).toString ().trimmed ().toUpper ());
      connectButton_->setChecked (settings_->value (QStringLiteral ("connected"), true).toBool ());
      autoQsyButton_->setChecked (settings_->value (QStringLiteral ("autoQsy"), false).toBool ());
      newDxccCheck_->setChecked (settings_->value (QStringLiteral ("newDxcc"), true).toBool ());
      newBandCheck_->setChecked (settings_->value (QStringLiteral ("newBand"), true).toBool ());
      ft8Check_->setChecked (settings_->value (QStringLiteral ("ft8"), true).toBool ());
      ft4Check_->setChecked (settings_->value (QStringLiteral ("ft4"), true).toBool ());
      showWorkedCheck_->setChecked (settings_->value (QStringLiteral ("showWorked"), false).toBool ());
      holdSeconds_->setValue (settings_->value (QStringLiteral ("holdSeconds"), 60).toInt ());
      settings_->endGroup ();
    }
  if (clusterCall_->text ().trimmed ().isEmpty ())
    clusterCall_->setText (defaultCallsign_);

  client_->onStatus = [this] (QString const& text) {
      statusLabel_->setText (text);
    };
  client_->onConnected = [this] (bool connected) {
      connectButton_->setText (connected ? tr ("Disconnetti") : tr ("Connetti"));
    };
  client_->onSpot = [this] (QString const& call, quint64 hz, QString const& comment,
                            QString const& spotter, QDateTime const& when) {
      handleSpot (call, hz, comment, spotter, when);
    };

  connect (clusterCall_, &QLineEdit::editingFinished, this, [this] {
      auto call = clusterCall_->text ().trimmed ().toUpper ();
      if (call.isEmpty ()) call = defaultCallsign_;
      clusterCall_->setText (call);
      saveOption (QStringLiteral ("clusterCall"), call);
      reconnectForCallsign ();
    });
  connect (connectButton_, &QPushButton::toggled, this, [this] (bool checked) {
      saveOption (QStringLiteral ("connected"), checked);
      setWanted (checked);
    });
  connect (autoQsyButton_, &QPushButton::toggled, this, [this] (bool checked) {
      saveOption (QStringLiteral ("autoQsy"), checked);
      if (!checked && huntState_ != HuntState::Idle)
        resetHunt (tr ("AUTO QSY disattivato"), true);
      else if (checked && pendingValid_)
        settleTimer_->start (100);
    });
  connect (newDxccCheck_, &QCheckBox::toggled, this, [this] (bool v) { saveOption (QStringLiteral ("newDxcc"), v); });
  connect (newBandCheck_, &QCheckBox::toggled, this, [this] (bool v) { saveOption (QStringLiteral ("newBand"), v); });
  connect (ft8Check_, &QCheckBox::toggled, this, [this] (bool v) { saveOption (QStringLiteral ("ft8"), v); });
  connect (ft4Check_, &QCheckBox::toggled, this, [this] (bool v) { saveOption (QStringLiteral ("ft4"), v); });
  connect (showWorkedCheck_, &QCheckBox::toggled, this, [this] (bool v) { saveOption (QStringLiteral ("showWorked"), v); });
  connect (holdSeconds_, QOverload<int>::of (&QSpinBox::valueChanged), this,
           [this] (int v) { saveOption (QStringLiteral ("holdSeconds"), v); });

  connect (goButton_, &QPushButton::clicked, this, [this] { tuneSelected (); });
  connect (clearButton_, &QPushButton::clicked, this, [this] {
      table_->setRowCount (0);
      seen_.clear ();
    });
  connect (table_, &QTableWidget::cellDoubleClicked, this, [this] (int, int) { tuneSelected (); });

  updateCounters ();

  if (connectButton_->isChecked ())
    QTimer::singleShot (1200, this, [this] { setWanted (true); });
}

void DxFunClusterPanel::openAndConnect ()
{
  show ();
  raise ();
  if (!client_->wanted ())
    connectButton_->setChecked (true);
}

void DxFunClusterPanel::setWanted (bool wanted)
{
  if (wanted)
    {
      auto call = clusterCall_->text ().trimmed ().toUpper ();
      if (call.isEmpty ()) call = defaultCallsign_;
      clusterCall_->setText (call);
      saveOption (QStringLiteral ("clusterCall"), call);
      client_->start (call);
    }
  else
    {
      resetHunt (tr ("DXFun disconnesso"), true);
      client_->stop ();
    }
}

void DxFunClusterPanel::reconnectForCallsign ()
{
  if (!connectButton_->isChecked ()) return;
  client_->stop ();
  QTimer::singleShot (250, this, [this] {
      client_->start (clusterCall_->text ().trimmed ().toUpper ());
    });
}

void DxFunClusterPanel::handleSpot (QString const& callsign, quint64 frequencyHz,
                                    QString const& comment, QString const& spotter,
                                    QDateTime const& receivedUtc)
{
  ++allSpots_;

  auto const evaluation = evaluator_ ? evaluator_ (callsign, frequencyHz) : DxFunEvaluation {};
  if (!evaluation.digital)
    {
      updateCounters ();
      return;
    }

  ++digitalSpots_;
  if ((evaluation.mode == QStringLiteral ("FT8") && !ft8Check_->isChecked ())
      || (evaluation.mode == QStringLiteral ("FT4") && !ft4Check_->isChecked ()))
    {
      updateCounters ();
      return;
    }

  bool const relevantDxcc = evaluation.newDxcc && newDxccCheck_->isChecked ();
  bool const relevantBand = !evaluation.newDxcc && evaluation.newBand && newBandCheck_->isChecked ();
  bool const relevant = relevantDxcc || relevantBand;

  Candidate candidate;
  candidate.request.callsign = callsign;
  candidate.request.mode = evaluation.mode;
  candidate.request.band = evaluation.band;
  candidate.request.country = evaluation.country;
  candidate.request.spotHz = evaluation.spotHz;
  candidate.request.dialHz = evaluation.dialHz;
  candidate.request.offsetHz = evaluation.offsetHz;
  candidate.request.status = evaluation.newDxcc
    ? QStringLiteral ("NEW DXCC")
    : evaluation.newBand
      ? QStringLiteral ("NEW BANDA")
      : QStringLiteral ("LAVORATO");
  candidate.priority = evaluation.newDxcc ? 2 : evaluation.newBand ? 1 : 0;
  candidate.receivedUtc = receivedUtc;

  auto const duplicateKey = callsign + QLatin1Char ('|') + evaluation.band + QLatin1Char ('|') + evaluation.mode;
  auto const old = seen_.value (duplicateKey);
  if (old.isValid () && old.secsTo (receivedUtc) < 300)
    {
      updateCounters ();
      return;
    }
  seen_.insert (duplicateKey, receivedUtc);

  if (relevant) ++candidateSpots_;
  if (relevant || showWorkedCheck_->isChecked ())
    appendSpot (candidate, comment, spotter, relevant);

  // Durante ascolto/QSO non si salta su altri spot: si torna al cluster
  // soltanto alla fine della finestra corrente.
  if (relevant && huntState_ == HuntState::Idle)
    queueCandidate (candidate);

  updateCounters ();
}

void DxFunClusterPanel::appendSpot (Candidate const& candidate, QString const& comment,
                                    QString const& spotter, bool relevant)
{
  table_->insertRow (0);
  auto * utc = new QTableWidgetItem {candidate.receivedUtc.toUTC ().toString (QStringLiteral ("HH:mm:ss"))};
  auto * call = new QTableWidgetItem {candidate.request.callsign};
  auto * freq = new QTableWidgetItem {frequencyText (candidate.request.spotHz)};
  auto * band = new QTableWidgetItem {candidate.request.band};
  auto * mode = new QTableWidgetItem {candidate.request.mode};
  auto * state = new QTableWidgetItem {candidate.request.status};
  auto * note = new QTableWidgetItem {
    comment + (spotter.isEmpty () ? QString {} : QStringLiteral ("  [de %1]").arg (spotter))};

  utc->setData (CallRole, candidate.request.callsign);
  utc->setData (ModeRole, candidate.request.mode);
  utc->setData (BandRole, candidate.request.band);
  utc->setData (CountryRole, candidate.request.country);
  utc->setData (SpotHzRole, QVariant::fromValue<qulonglong> (candidate.request.spotHz));
  utc->setData (DialHzRole, QVariant::fromValue<qulonglong> (candidate.request.dialHz));
  utc->setData (OffsetRole, candidate.request.offsetHz);
  utc->setData (StatusRole, candidate.request.status);

  if (relevant)
    {
      QFont bold = state->font ();
      bold.setBold (true);
      state->setFont (bold);
      call->setFont (bold);
    }

  table_->setItem (0, 0, utc);
  table_->setItem (0, 1, call);
  table_->setItem (0, 2, freq);
  table_->setItem (0, 3, band);
  table_->setItem (0, 4, mode);
  table_->setItem (0, 5, state);
  table_->setItem (0, 6, note);

  while (table_->rowCount () > 250)
    table_->removeRow (table_->rowCount () - 1);
}

void DxFunClusterPanel::queueCandidate (Candidate const& candidate)
{
  if (!pendingValid_
      || candidate.priority > pending_.priority
      || (candidate.priority == pending_.priority && candidate.receivedUtc > pending_.receivedUtc))
    {
      pending_ = candidate;
      pendingValid_ = true;
    }

  if (autoQsyButton_->isChecked ())
    settleTimer_->start (1500);
}

void DxFunClusterPanel::attemptAutoQsy ()
{
  if (!autoQsyButton_->isChecked () || !pendingValid_ || huntState_ != HuntState::Idle) return;

  auto const now = QDateTime::currentDateTimeUtc ();
  if (lastQsyUtc_.isValid ())
    {
      auto const elapsed = lastQsyUtc_.secsTo (now);
      auto const hold = holdSeconds_->value ();
      if (elapsed < hold)
        {
          settleTimer_->start (qMax (1000, (hold - static_cast<int> (elapsed)) * 1000));
          statusLabel_->setText (
            tr ("AUTO QSY: attendo %1 s, candidato %2 %3 %4")
              .arg (hold - elapsed)
              .arg (pending_.request.callsign)
              .arg (pending_.request.band)
              .arg (pending_.request.status));
          return;
        }
    }

  auto candidate = pending_;
  if (tuner_ && tuner_ (candidate.request))
    {
      lastQsyUtc_ = now;
      pendingValid_ = false;
      beginHunt (candidate);
    }
  else
    {
      statusLabel_->setText (
        tr ("AUTO QSY sospesa: TX/Auto/Tune attivo. Candidato %1 %2")
          .arg (pending_.request.callsign)
          .arg (pending_.request.band));
      settleTimer_->start (5000);
    }
}

void DxFunClusterPanel::beginHunt (Candidate const& candidate)
{
  active_ = candidate;
  huntState_ = HuntState::Listening;
  answered_ = false;
  txCycles_ = 0;
  lastPowerPercent_ = 50;
  listenTimer_->start (150000);
  qsoTimer_->stop ();
  statusLabel_->setText (
    tr ("ASCOLTO 150 s -> %1 %2 %3 (%4)")
      .arg (active_.request.band)
      .arg (active_.request.mode)
      .arg (active_.request.callsign)
      .arg (active_.request.status));
}

void DxFunClusterPanel::listenTimeout ()
{
  if (huntState_ != HuntState::Listening) return;
  resetHunt (
    tr ("Spot %1 non ascoltato in 150 s - pronto per il prossimo spot DXFun")
      .arg (active_.request.callsign),
    false);
}

void DxFunClusterPanel::qsoTimeout ()
{
  if (huntState_ != HuntState::Qso) return;
  resetHunt (
    tr ("QSO %1: timeout 3 minuti - ritorno al cluster")
      .arg (active_.request.callsign),
    true);
}

void DxFunClusterPanel::resetHunt (QString const& status, bool abortRadio)
{
  listenTimer_->stop ();
  qsoTimer_->stop ();
  settleTimer_->stop ();
  if (abortRadio && abortHandler_) abortHandler_ ();
  huntState_ = HuntState::Idle;
  answered_ = false;
  txCycles_ = 0;
  lastPowerPercent_ = 50;
  active_ = Candidate {};
  pendingValid_ = false;
  statusLabel_->setText (status);
}

DxFunClusterPanel::DecodeAction DxFunClusterPanel::observeDecode (
  QString const& deCall, QString const& cleanText, int audioOffset, QString const& myCall)
{
  if (!autoQsyButton_->isChecked () || deCall.trimmed ().isEmpty ()) return DecodeAction::None;
  if (huntState_ == HuntState::Idle) return DecodeAction::None;

  auto const call = deCall.trimmed ().toUpper ();
  auto const target = active_.request.callsign.trimmed ().toUpper ();
  if (call != target) return DecodeAction::None;

  if (huntState_ == HuntState::Listening)
    {
      listenTimer_->stop ();
      huntState_ = HuntState::Qso;
      active_.request.offsetHz = audioOffset;
      answered_ = false;
      txCycles_ = 0;
      lastPowerPercent_ = 50;
      qsoTimer_->start (180000);
      statusLabel_->setText (
        tr ("TROVATO %1 - QSO automatico, timeout 3 minuti, potenza iniziale 50%%")
          .arg (target));
      return DecodeAction::StartQso;
    }

  if (huntState_ == HuntState::Qso)
    {
      auto text = cleanText;
      text.remove ('<');
      text.remove ('>');
      auto const my = myCall.trimmed ().toUpper ();
      if (!my.isEmpty () && text.contains (my, Qt::CaseInsensitive))
        answered_ = true;

      if (!my.isEmpty ()
          && text.contains (my, Qt::CaseInsensitive)
          && (text.contains (QStringLiteral (" RR73"), Qt::CaseInsensitive)
              || text.contains (QStringLiteral (" 73"), Qt::CaseInsensitive)))
        {
          qsoTimer_->stop ();
          statusLabel_->setText (tr ("73 ricevuto da %1 - log locale e ritorno al cluster").arg (target));
          return DecodeAction::CompleteQso;
        }
    }

  return DecodeAction::None;
}

bool DxFunClusterPanel::qsoActive () const
{
  return huntState_ == HuntState::Qso;
}

int DxFunClusterPanel::nextTxPowerPercent ()
{
  if (huntState_ != HuntState::Qso) return -1;

  if (txCycles_ == 0)
    lastPowerPercent_ = 50;
  else if (!answered_)
    lastPowerPercent_ = qMin (100, 50 + 9 * txCycles_);

  ++txCycles_;
  statusLabel_->setText (
    tr ("QSO %1 - TX ciclo %2 - RF Power %3%%")
      .arg (active_.request.callsign)
      .arg (txCycles_)
      .arg (lastPowerPercent_));
  return lastPowerPercent_;
}

void DxFunClusterPanel::qsoLogged ()
{
  if (huntState_ != HuntState::Qso) return;
  resetHunt (
    tr ("QSO %1 registrato nel log locale - pronto per il prossimo spot DXFun")
      .arg (active_.request.callsign),
    false);
}

void DxFunClusterPanel::tuneSelected ()
{
  auto const row = table_->currentRow ();
  if (row < 0) return;

  auto * item = table_->item (row, 0);
  if (!item) return;

  DxFunTuneRequest request;
  request.callsign = item->data (CallRole).toString ();
  request.mode = item->data (ModeRole).toString ();
  request.band = item->data (BandRole).toString ();
  request.country = item->data (CountryRole).toString ();
  request.spotHz = item->data (SpotHzRole).toULongLong ();
  request.dialHz = item->data (DialHzRole).toULongLong ();
  request.offsetHz = item->data (OffsetRole).toInt ();
  request.status = item->data (StatusRole).toString ();

  if (tuner_ && tuner_ (request))
    statusLabel_->setText (
      tr ("QSY manuale -> %1 %2 %3").arg (request.band).arg (request.mode).arg (request.callsign));
}

void DxFunClusterPanel::updateCounters ()
{
  countersLabel_->setText (
    tr ("Spot totali: %1   FT8/FT4 HF 80-10 m: %2   candidati: %3")
      .arg (allSpots_)
      .arg (digitalSpots_)
      .arg (candidateSpots_));
}

void DxFunClusterPanel::saveOption (QString const& key, QVariant const& value)
{
  if (!settings_) return;
  settings_->beginGroup (QStringLiteral ("FT8HunterDXFun"));
  settings_->setValue (key, value);
  settings_->endGroup ();
  settings_->sync ();
}
