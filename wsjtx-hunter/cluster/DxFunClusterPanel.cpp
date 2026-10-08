#include "DxFunClusterPanel.hpp"
#include "DxFunCluster.hpp"

#include <QCheckBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
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

DxFunClusterPanel::DxFunClusterPanel (QString const& callsign,
                                      Evaluator evaluator,
                                      Tuner tuner,
                                      QSettings * settings,
                                      QWidget * parent)
  : QDockWidget {tr ("FT8 Hunter - DXFun Cluster"), parent}
  , callsign_ {callsign.trimmed ().toUpper ()}
  , evaluator_ {std::move (evaluator)}
  , tuner_ {std::move (tuner)}
  , settings_ {settings}
  , client_ {new DxFunClusterClient {this}}
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
{
  setObjectName (QStringLiteral ("FT8HunterDXFunCluster"));
  setAllowedAreas (Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::BottomDockWidgetArea);
  setMinimumWidth (650);

  connectButton_->setCheckable (true);
  autoQsyButton_->setCheckable (true);
  autoQsyButton_->setToolTip (
    tr ("Quando attivo, FT8 Hunter segue automaticamente il miglior NEW DXCC o NEW su banda. "
        "La QSY viene sospesa mentre Auto/TX/Tune e' attivo."));

  newDxccCheck_->setChecked (true);
  newBandCheck_->setChecked (true);
  ft8Check_->setChecked (true);
  ft4Check_->setChecked (true);

  holdSeconds_->setRange (15, 300);
  holdSeconds_->setSuffix (tr (" s"));
  holdSeconds_->setValue (60);
  holdSeconds_->setToolTip (tr ("Tempo minimo prima di una nuova QSY automatica."));

  table_->setHorizontalHeaderLabels (
    {tr ("UTC"), tr ("DX"), tr ("MHz"), tr ("Banda"), tr ("Modo"), tr ("Stato"), tr ("Commento")});
  table_->setSelectionBehavior (QAbstractItemView::SelectRows);
  table_->setSelectionMode (QAbstractItemView::SingleSelection);
  table_->setEditTriggers (QAbstractItemView::NoEditTriggers);
  table_->verticalHeader ()->setVisible (false);
  table_->horizontalHeader ()->setSectionResizeMode (0, QHeaderView::ResizeToContents);
  table_->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::ResizeToContents);
  table_->horizontalHeader ()->setSectionResizeMode (2, QHeaderView::ResizeToContents);
  table_->horizontalHeader ()->setSectionResizeMode (3, QHeaderView::ResizeToContents);
  table_->horizontalHeader ()->setSectionResizeMode (4, QHeaderView::ResizeToContents);
  table_->horizontalHeader ()->setSectionResizeMode (5, QHeaderView::ResizeToContents);
  table_->horizontalHeader ()->setSectionResizeMode (6, QHeaderView::Stretch);
  table_->setSortingEnabled (false);

  auto * hostLabel = new QLabel {tr ("Cluster: dxfun.com:8000"), this};

  auto * top = new QHBoxLayout;
  top->addWidget (connectButton_);
  top->addWidget (autoQsyButton_);
  top->addSpacing (10);
  top->addWidget (newDxccCheck_);
  top->addWidget (newBandCheck_);
  top->addSpacing (10);
  top->addWidget (ft8Check_);
  top->addWidget (ft4Check_);
  top->addWidget (showWorkedCheck_);
  top->addStretch ();

  auto * options = new QHBoxLayout;
  options->addWidget (hostLabel);
  options->addSpacing (12);
  options->addWidget (new QLabel {tr ("Permanenza minima:"), this});
  options->addWidget (holdSeconds_);
  options->addStretch ();
  options->addWidget (goButton_);
  options->addWidget (clearButton_);

  auto * root = new QVBoxLayout;
  root->addLayout (top);
  root->addLayout (options);
  root->addWidget (statusLabel_);
  root->addWidget (countersLabel_);
  root->addWidget (table_);

  auto * body = new QWidget {this};
  body->setLayout (root);
  setWidget (body);

  settleTimer_->setSingleShot (true);
  connect (settleTimer_, &QTimer::timeout, this, [this] { attemptAutoQsy (); });

  if (settings_)
    {
      settings_->beginGroup (QStringLiteral ("FT8HunterDXFun"));
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

  client_->onStatus = [this] (QString const& text) {
      statusLabel_->setText (text);
    };
  client_->onConnected = [this] (bool connected) {
      if (connected)
        connectButton_->setText (tr ("Disconnetti"));
      else if (!client_->wanted ())
        connectButton_->setText (tr ("Connetti"));
    };
  client_->onSpot = [this] (QString const& call, quint64 hz, QString const& comment,
                            QString const& spotter, QDateTime const& when) {
      handleSpot (call, hz, comment, spotter, when);
    };

  connect (connectButton_, &QPushButton::toggled, this, [this] (bool checked) {
      saveOption (QStringLiteral ("connected"), checked);
      setWanted (checked);
    });
  connect (autoQsyButton_, &QPushButton::toggled, this, [this] (bool checked) {
      saveOption (QStringLiteral ("autoQsy"), checked);
      if (checked && pendingValid_) settleTimer_->start (100);
    });
  connect (newDxccCheck_, &QCheckBox::toggled, this, [this] (bool v) {
      saveOption (QStringLiteral ("newDxcc"), v);
    });
  connect (newBandCheck_, &QCheckBox::toggled, this, [this] (bool v) {
      saveOption (QStringLiteral ("newBand"), v);
    });
  connect (ft8Check_, &QCheckBox::toggled, this, [this] (bool v) {
      saveOption (QStringLiteral ("ft8"), v);
    });
  connect (ft4Check_, &QCheckBox::toggled, this, [this] (bool v) {
      saveOption (QStringLiteral ("ft4"), v);
    });
  connect (showWorkedCheck_, &QCheckBox::toggled, this, [this] (bool v) {
      saveOption (QStringLiteral ("showWorked"), v);
    });
  connect (holdSeconds_, QOverload<int>::of (&QSpinBox::valueChanged), this, [this] (int v) {
      saveOption (QStringLiteral ("holdSeconds"), v);
    });

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
      connectButton_->setText (tr ("Disconnetti"));
      client_->start (callsign_);
    }
  else
    {
      connectButton_->setText (tr ("Connetti"));
      client_->stop ();
    }
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

  auto const duplicateKey = callsign + QLatin1Char ('|') + evaluation.band
                          + QLatin1Char ('|') + evaluation.mode;
  auto const old = seen_.value (duplicateKey);
  if (old.isValid () && old.secsTo (receivedUtc) < 300)
    {
      if (relevant) queueCandidate (candidate);
      updateCounters ();
      return;
    }
  seen_.insert (duplicateKey, receivedUtc);

  if (relevant) ++candidateSpots_;

  if (relevant || showWorkedCheck_->isChecked ())
    appendSpot (candidate, comment, spotter, relevant);

  if (relevant)
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
  if (!autoQsyButton_->isChecked () || !pendingValid_) return;

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

  if (tuner_ && tuner_ (pending_.request))
    {
      lastQsyUtc_ = now;
      statusLabel_->setText (
        tr ("AUTO QSY -> %1 %2 %3 (%4)")
          .arg (pending_.request.band)
          .arg (pending_.request.mode)
          .arg (pending_.request.callsign)
          .arg (pending_.request.status));
      pendingValid_ = false;
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
    tr ("Spot totali: %1   FT8/FT4: %2   candidati: %3")
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
