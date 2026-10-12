#include "PskReporterPanel.hpp"
#include "PskReporterMqtt.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFont>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariant>
#include <QtGlobal>
#include <utility>

namespace
{
  constexpr int FreshSeconds = 60;
  constexpr int DuplicateSeconds = 45;
  constexpr int LocalListenSeconds = 60;

  enum Roles
  {
    CallRole = Qt::UserRole + 1,
    CountryRole,
    ModeRole,
    BandRole,
    SignalHzRole,
    DialHzRole,
    OffsetRole,
    StatusRole
  };

  QString frequencyText (quint64 hz)
  {
    return QString::number (static_cast<double> (hz) / 1000000.0, 'f', 6);
  }
}

PskReporterPanel::PskReporterPanel (Evaluator evaluator,
                                    Tuner tuner,
                                    Handoff handoff,
                                    QSettings * settings,
                                    QWidget * parent)
  : QDialog {parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint}
  , evaluator_ {std::move (evaluator)}
  , tuner_ {std::move (tuner)}
  , handoff_ {std::move (handoff)}
  , settings_ {settings}
  , client_ {new PskReporterMqttClient {this}}
  , connectCheck_ {new QCheckBox {tr ("Connetti PSK Reporter MQTT"), this}}
  , autoQsyCheck_ {new QCheckBox {tr ("AUTO QSY"), this}}
  , newDxccCheck_ {new QCheckBox {tr ("NEW DXCC"), this}}
  , newBandCheck_ {new QCheckBox {tr ("NEW su banda"), this}}
  , ft8Check_ {new QCheckBox {QStringLiteral ("FT8"), this}}
  , ft4Check_ {new QCheckBox {QStringLiteral ("FT4"), this}}
  , showWorkedCheck_ {new QCheckBox {tr ("Mostra anche lavorati"), this}}
  , holdSeconds_ {new QSpinBox {this}}
  , statusLabel_ {new QLabel {tr ("PSK Reporter disconnesso"), this}}
  , countersLabel_ {new QLabel {this}}
  , table_ {new QTableWidget {0, 9, this}}
  , goButton_ {new QPushButton {tr ("Vai e ascolta"), this}}
  , clearButton_ {new QPushButton {tr ("Pulisci"), this}}
  , settleTimer_ {new QTimer {this}}
  , listenTimer_ {new QTimer {this}}
{
  setWindowTitle (tr ("FT8 Hunter 1.0.14 - PSK Reporter"));
  setModal (false);
  setMinimumSize (980, 520);
  setAttribute (Qt::WA_DeleteOnClose, false);

  newDxccCheck_->setChecked (true);
  newBandCheck_->setChecked (true);
  ft8Check_->setChecked (true);
  ft4Check_->setChecked (true);

  holdSeconds_->setRange (10, 300);
  holdSeconds_->setValue (45);
  holdSeconds_->setSuffix (tr (" s"));
  holdSeconds_->setToolTip (tr ("Tempo minimo tra due QSY automatiche generate da PSK Reporter."));

  autoQsyCheck_->setToolTip (
    tr ("PSK Reporter segnala solo un candidato. FT8 Hunter NON trasmette finche' "
        "il decoder locale non riceve un vero CQ della stessa stazione."));
  autoQsyCheck_->setEnabled (false);
  goButton_->setEnabled (false);

  table_->setHorizontalHeaderLabels (
    {tr ("UTC"), tr ("DX"), tr ("MHz RF"), tr ("Banda"), tr ("Modo"),
     tr ("SNR"), tr ("DXCC"), tr ("Stato"), tr ("Reporter")});
  table_->setSelectionBehavior (QAbstractItemView::SelectRows);
  table_->setSelectionMode (QAbstractItemView::SingleSelection);
  table_->setEditTriggers (QAbstractItemView::NoEditTriggers);
  table_->verticalHeader ()->setVisible (false);
  for (int i = 0; i < 8; ++i)
    table_->horizontalHeader ()->setSectionResizeMode (i, QHeaderView::ResizeToContents);
  table_->horizontalHeader ()->setSectionResizeMode (8, QHeaderView::Stretch);

  auto * row1 = new QHBoxLayout;
  row1->addWidget (connectCheck_);
  row1->addWidget (autoQsyCheck_);
  row1->addSpacing (12);
  row1->addWidget (newDxccCheck_);
  row1->addWidget (newBandCheck_);
  row1->addSpacing (12);
  row1->addWidget (ft8Check_);
  row1->addWidget (ft4Check_);
  row1->addWidget (showWorkedCheck_);
  row1->addStretch ();
  row1->addWidget (new QLabel {tr ("Min. tra QSY:"), this});
  row1->addWidget (holdSeconds_);
  row1->addWidget (goButton_);
  row1->addWidget (clearButton_);

  auto * warning = new QLabel {
    tr ("Un report PSK non equivale a CQ. Dopo la QSY FT8 Hunter ascolta localmente "
        "e passa il target al Monitor Hunter soltanto se riceve davvero il suo CQ."), this};
  warning->setWordWrap (true);
  QFont warningFont = warning->font ();
  warningFont.setBold (true);
  warning->setFont (warningFont);

  auto * root = new QVBoxLayout;
  root->addLayout (row1);
  root->addWidget (warning);
  root->addWidget (statusLabel_);
  root->addWidget (countersLabel_);
  root->addWidget (table_);
  setLayout (root);

  settleTimer_->setSingleShot (true);
  listenTimer_->setSingleShot (true);
  connect (settleTimer_, &QTimer::timeout, this, [this] { attemptAutoQsy (); });
  connect (listenTimer_, &QTimer::timeout, this, [this] { listenTimeout (); });

  if (settings_)
    {
      settings_->beginGroup (QStringLiteral ("FT8HunterPskReporter"));
      restoreGeometry (settings_->value (QStringLiteral ("geometry"), saveGeometry ()).toByteArray ());
      connectCheck_->setChecked (settings_->value (QStringLiteral ("connected"), true).toBool ());
      autoQsyCheck_->setChecked (settings_->value (QStringLiteral ("autoQsy"), false).toBool ());
      newDxccCheck_->setChecked (settings_->value (QStringLiteral ("newDxcc"), true).toBool ());
      newBandCheck_->setChecked (settings_->value (QStringLiteral ("newBand"), true).toBool ());
      ft8Check_->setChecked (settings_->value (QStringLiteral ("ft8"), true).toBool ());
      ft4Check_->setChecked (settings_->value (QStringLiteral ("ft4"), true).toBool ());
      showWorkedCheck_->setChecked (settings_->value (QStringLiteral ("showWorked"), false).toBool ());
      holdSeconds_->setValue (settings_->value (QStringLiteral ("holdSeconds"), 45).toInt ());
      settings_->endGroup ();
    }

  client_->onStatus = [this] (QString const& text) { statusLabel_->setText (text); };
  client_->onConnected = [this] (bool connected) {
      connectCheck_->setText (
        connected ? tr ("PSK Reporter MQTT connesso") : tr ("Connetti PSK Reporter MQTT"));
    };
  client_->onMessage = [this] (QString const& topic, QByteArray const& payload) {
      handleMessage (topic, payload);
    };

  connect (connectCheck_, &QCheckBox::toggled, this, [this] (bool checked) {
      saveOption (QStringLiteral ("connected"), checked);
      if (checked) client_->start ();
      else
        {
          resetListen (tr ("PSK Reporter disconnesso"));
          client_->stop ();
        }
    });
  connect (autoQsyCheck_, &QCheckBox::toggled, this, [this] (bool checked) {
      saveOption (QStringLiteral ("autoQsy"), checked);
      if (!checked && listening_)
        resetListen (tr ("AUTO QSY PSK Reporter disattivato"));
      else if (checked && pendingValid_)
        settleTimer_->start (100);
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
  connect (table_, &QTableWidget::cellDoubleClicked, this, [this] (int, int) {
      tuneSelected ();
    });

  updateCounters ();
  if (connectCheck_->isChecked ())
    QTimer::singleShot (800, this, [this] { client_->start (); });
}

void PskReporterPanel::openWindow ()
{
  show ();
  raise ();
  activateWindow ();
  if (!client_->wanted ())
    connectCheck_->setChecked (true);
}

bool PskReporterPanel::huntActive () const
{
  return listening_;
}

void PskReporterPanel::setWorkedBeforeState (bool ready, int recordCount, QString const& error)
{
  workedBeforeReady_ = ready && error.trimmed ().isEmpty ();
  workedBeforeRecords_ = recordCount;
  workedBeforeError_ = error.trimmed ();

  if (!workedBeforeReady_)
    {
      pendingValid_ = false;
      settleTimer_->stop ();
      if (listening_)
        resetListen (tr ("Worked Before/Log4OM non disponibile - nessuna TX"));
      autoQsyCheck_->setEnabled (false);
      goButton_->setEnabled (false);

      if (!workedBeforeError_.isEmpty ())
        statusLabel_->setText (
          tr ("Worked Before/Log4OM ERRORE: %1").arg (workedBeforeError_));
      else
        statusLabel_->setText (
          tr ("Attendo caricamento Worked Before / Log4OM..."));
      return;
    }

  autoQsyCheck_->setEnabled (true);
  goButton_->setEnabled (true);
  statusLabel_->setText (
    tr ("Worked Before/Log4OM pronto: %1 record - confronto PSK attivo")
      .arg (workedBeforeRecords_));
}

QJsonObject PskReporterPanel::remoteSnapshot () const
{
  QJsonObject out;
  out.insert (QStringLiteral ("active"), huntActive ());
  out.insert (QStringLiteral ("connected"), client_ && client_->connected ());
  out.insert (QStringLiteral ("workedBeforeReady"), workedBeforeReady_);
  out.insert (QStringLiteral ("workedBeforeRecords"), workedBeforeRecords_);
  out.insert (QStringLiteral ("target"), active_.request.callsign);
  out.insert (QStringLiteral ("band"), active_.request.band);
  out.insert (QStringLiteral ("mode"), active_.request.mode);
  out.insert (QStringLiteral ("status"), active_.request.status);
  out.insert (QStringLiteral ("reports"), static_cast<double> (reports_));
  out.insert (QStringLiteral ("candidates"), static_cast<double> (candidateReports_));
  return out;
}

void PskReporterPanel::yieldToDxFun ()
{
  if (!listening_) return;
  listenTimer_->stop ();
  listening_ = false;
  active_ = Candidate {};
  pendingValid_ = false;
  statusLabel_->setText (tr ("PSK Reporter in pausa: priorita' al DXFun Cluster"));
}

void PskReporterPanel::handleMessage (QString const&, QByteArray const& payload)
{
  auto const doc = QJsonDocument::fromJson (payload);
  if (!doc.isObject ()) return;

  auto const obj = doc.object ();
  PskReporterReport report;
  report.callsign = obj.value (QStringLiteral ("sc")).toString ().trimmed ().toUpper ();
  report.grid = obj.value (QStringLiteral ("sl")).toString ().trimmed ().toUpper ();
  report.reporter = obj.value (QStringLiteral ("rc")).toString ().trimmed ().toUpper ();
  report.reporterGrid = obj.value (QStringLiteral ("rl")).toString ().trimmed ().toUpper ();
  report.mode = obj.value (QStringLiteral ("md")).toString ().trimmed ().toUpper ();
  report.band = obj.value (QStringLiteral ("b")).toString ().trimmed ();
  report.frequencyHz = static_cast<quint64> (
    obj.value (QStringLiteral ("f")).toVariant ().toULongLong ());
  report.snr = obj.value (QStringLiteral ("rp")).toInt ();

  auto const epoch = obj.value (QStringLiteral ("t")).toVariant ().toLongLong ();
  report.receivedUtc =
    epoch > 0 ? QDateTime::fromSecsSinceEpoch (epoch, Qt::UTC) : QDateTime::currentDateTimeUtc ();

  if (report.callsign.isEmpty () || report.frequencyHz == 0
      || (report.mode != QStringLiteral ("FT8") && report.mode != QStringLiteral ("FT4")))
    return;

  handleReport (report);
}

void PskReporterPanel::handleReport (PskReporterReport const& report)
{
  ++reports_;

  // Non classificare mai un report come NEW finche' il database Worked Before,
  // incluso Log4OM/MySQL quando abilitato, non e' stato caricato con successo.
  if (!workedBeforeReady_)
    {
      updateCounters ();
      return;
    }

  auto const now = QDateTime::currentDateTimeUtc ();
  auto const age = report.receivedUtc.secsTo (now);
  if (age > FreshSeconds || age < -10)
    {
      updateCounters ();
      return;
    }

  auto const evaluation = evaluator_ ? evaluator_ (report) : PskReporterEvaluation {};
  if (!evaluation.digital)
    {
      updateCounters ();
      return;
    }

  ++freshDigital_;
  if ((evaluation.mode == QStringLiteral ("FT8") && !ft8Check_->isChecked ())
      || (evaluation.mode == QStringLiteral ("FT4") && !ft4Check_->isChecked ()))
    {
      updateCounters ();
      return;
    }

  auto const key =
    report.callsign + QLatin1Char ('|') + evaluation.band + QLatin1Char ('|') + evaluation.mode;
  auto const previous = seen_.value (key);
  if (previous.isValid () && previous.secsTo (report.receivedUtc) < DuplicateSeconds)
    {
      updateCounters ();
      return;
    }
  seen_.insert (key, report.receivedUtc);

  bool const relevantDxcc = evaluation.newDxcc && newDxccCheck_->isChecked ();
  bool const relevantBand = !evaluation.newDxcc && evaluation.newBand && newBandCheck_->isChecked ();
  bool const relevant = relevantDxcc || relevantBand;

  Candidate candidate;
  candidate.request.callsign = report.callsign;
  candidate.request.country = evaluation.country;
  candidate.request.mode = evaluation.mode;
  candidate.request.band = evaluation.band;
  candidate.request.signalHz = evaluation.signalHz;
  candidate.request.dialHz = evaluation.dialHz;
  candidate.request.offsetHz = evaluation.offsetHz;
  candidate.request.status = evaluation.newDxcc
    ? QStringLiteral ("NEW DXCC")
    : evaluation.newBand ? QStringLiteral ("NEW BANDA") : QStringLiteral ("LAVORATO");
  candidate.priority = evaluation.newDxcc ? 2 : evaluation.newBand ? 1 : 0;
  candidate.receivedUtc = report.receivedUtc;
  candidate.reporter = report.reporter;
  candidate.snr = report.snr;

  if (relevant) ++candidateReports_;
  if (relevant || showWorkedCheck_->isChecked ())
    appendReport (candidate, relevant);

  if (relevant && !listening_)
    queueCandidate (candidate);

  updateCounters ();
}

void PskReporterPanel::appendReport (Candidate const& candidate, bool relevant)
{
  table_->insertRow (0);
  auto * utc = new QTableWidgetItem {
    candidate.receivedUtc.toUTC ().toString (QStringLiteral ("HH:mm:ss"))};
  auto * call = new QTableWidgetItem {candidate.request.callsign};
  auto * freq = new QTableWidgetItem {frequencyText (candidate.request.signalHz)};
  auto * band = new QTableWidgetItem {candidate.request.band};
  auto * mode = new QTableWidgetItem {candidate.request.mode};
  auto * snr = new QTableWidgetItem {QString::number (candidate.snr)};
  auto * country = new QTableWidgetItem {candidate.request.country};
  auto * state = new QTableWidgetItem {candidate.request.status};
  auto * reporter = new QTableWidgetItem {candidate.reporter};

  utc->setData (CallRole, candidate.request.callsign);
  utc->setData (CountryRole, candidate.request.country);
  utc->setData (ModeRole, candidate.request.mode);
  utc->setData (BandRole, candidate.request.band);
  utc->setData (SignalHzRole, QVariant::fromValue<qulonglong> (candidate.request.signalHz));
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
  table_->setItem (0, 5, snr);
  table_->setItem (0, 6, country);
  table_->setItem (0, 7, state);
  table_->setItem (0, 8, reporter);

  while (table_->rowCount () > 300)
    table_->removeRow (table_->rowCount () - 1);
}

void PskReporterPanel::queueCandidate (Candidate const& candidate)
{
  if (!pendingValid_
      || candidate.priority > pending_.priority
      || (candidate.priority == pending_.priority && candidate.receivedUtc > pending_.receivedUtc))
    {
      pending_ = candidate;
      pendingValid_ = true;
    }

  if (autoQsyCheck_->isChecked ())
    settleTimer_->start (1200);
}

void PskReporterPanel::attemptAutoQsy ()
{
  if (!workedBeforeReady_ || !autoQsyCheck_->isChecked () || !pendingValid_ || listening_) return;

  auto const now = QDateTime::currentDateTimeUtc ();
  if (pending_.receivedUtc.secsTo (now) > FreshSeconds)
    {
      pendingValid_ = false;
      statusLabel_->setText (tr ("Candidato PSK scaduto - attendo nuovi report"));
      return;
    }

  if (lastQsyUtc_.isValid ())
    {
      auto const elapsed = lastQsyUtc_.secsTo (now);
      auto const hold = holdSeconds_->value ();
      if (elapsed < hold)
        {
          settleTimer_->start (qMax (1000, (hold - static_cast<int> (elapsed)) * 1000));
          statusLabel_->setText (
            tr ("AUTO QSY PSK: attendo %1 s; candidato %2 %3")
              .arg (hold - elapsed)
              .arg (pending_.request.callsign)
              .arg (pending_.request.status));
          return;
        }
    }

  auto candidate = pending_;
  if (tuner_ && tuner_ (candidate.request))
    {
      lastQsyUtc_ = now;
      pendingValid_ = false;
      beginListen (candidate);
    }
  else
    {
      statusLabel_->setText (
        tr ("AUTO QSY PSK sospesa: radio o altro Hunter occupato. Candidato %1")
          .arg (pending_.request.callsign));
      settleTimer_->start (3000);
    }
}

void PskReporterPanel::beginListen (Candidate const& candidate)
{
  active_ = candidate;
  listening_ = true;
  listenTimer_->start (LocalListenSeconds * 1000);
  statusLabel_->setText (
    tr ("PSK CANDIDATO -> %1 %2 %3 | ascolto locale CQ per %4 s | RF %5 MHz")
      .arg (candidate.request.callsign)
      .arg (candidate.request.band)
      .arg (candidate.request.status)
      .arg (LocalListenSeconds)
      .arg (frequencyText (candidate.request.signalHz)));
}

void PskReporterPanel::listenTimeout ()
{
  if (!listening_) return;
  resetListen (
    tr ("%1 non ricevuto in CQ dal decoder locale entro %2 s - nessuna TX")
      .arg (active_.request.callsign)
      .arg (LocalListenSeconds));
}

void PskReporterPanel::resetListen (QString const& status)
{
  listenTimer_->stop ();
  settleTimer_->stop ();
  listening_ = false;
  active_ = Candidate {};
  pendingValid_ = false;
  statusLabel_->setText (status);
}

void PskReporterPanel::observeLocalCq (QString const& rawText,
                                       QString const& cqCall,
                                       QString const& grid,
                                       QString const& country,
                                       QString const& mode,
                                       QString const& band,
                                       int audioOffset,
                                       int snr,
                                       bool eligibleCq)
{
  if (!listening_) return;

  auto const call = cqCall.trimmed ().toUpper ();
  if (call.isEmpty () || call != active_.request.callsign.trimmed ().toUpper ()) return;
  if (mode != active_.request.mode || band != active_.request.band) return;

  if (!eligibleCq)
    {
      statusLabel_->setText (
        tr ("%1 ricevuto in CQ, ma il filtro Monitor Hunter non lo considera piu' eleggibile")
          .arg (call));
      return;
    }

  if (handoff_ && handoff_ (rawText, call, grid, country, mode, band, audioOffset, snr))
    {
      listenTimer_->stop ();
      listening_ = false;
      active_ = Candidate {};
      statusLabel_->setText (
        tr ("CQ locale confermato - target passato al Monitor Hunter"));
    }
  else
    {
      statusLabel_->setText (
        tr ("CQ locale confermato, ma Monitor Hunter non e' disponibile"));
    }
}

void PskReporterPanel::tuneSelected ()
{
  if (!workedBeforeReady_)
    {
      statusLabel_->setText (tr ("Attendo Worked Before / Log4OM prima della QSY"));
      return;
    }

  auto const row = table_->currentRow ();
  if (row < 0 || !table_->item (row, 0)) return;

  auto * item = table_->item (row, 0);
  Candidate candidate;
  candidate.request.callsign = item->data (CallRole).toString ();
  candidate.request.country = item->data (CountryRole).toString ();
  candidate.request.mode = item->data (ModeRole).toString ();
  candidate.request.band = item->data (BandRole).toString ();
  candidate.request.signalHz = item->data (SignalHzRole).toULongLong ();
  candidate.request.dialHz = item->data (DialHzRole).toULongLong ();
  candidate.request.offsetHz = item->data (OffsetRole).toInt ();
  candidate.request.status = item->data (StatusRole).toString ();
  candidate.receivedUtc = QDateTime::currentDateTimeUtc ();

  if (tuner_ && tuner_ (candidate.request))
    {
      lastQsyUtc_ = QDateTime::currentDateTimeUtc ();
      beginListen (candidate);
    }
}

void PskReporterPanel::updateCounters ()
{
  countersLabel_->setText (
    tr ("Report ricevuti: %1   freschi FT8/FT4 HF: %2   candidati NEW: %3   |   Log: %4")
      .arg (reports_)
      .arg (freshDigital_)
      .arg (candidateReports_)
      .arg (workedBeforeReady_
              ? tr ("%1 record").arg (workedBeforeRecords_)
              : workedBeforeError_.isEmpty () ? tr ("caricamento") : tr ("ERRORE")));
}

void PskReporterPanel::saveOption (QString const& key, QVariant const& value)
{
  if (!settings_) return;
  settings_->beginGroup (QStringLiteral ("FT8HunterPskReporter"));
  settings_->setValue (key, value);
  settings_->endGroup ();
  settings_->sync ();
}

void PskReporterPanel::closeEvent (QCloseEvent * event)
{
  if (autoQsyCheck_->isChecked ())
    autoQsyCheck_->setChecked (false);
  if (connectCheck_->isChecked ())
    connectCheck_->setChecked (false);

  if (settings_)
    {
      settings_->beginGroup (QStringLiteral ("FT8HunterPskReporter"));
      settings_->setValue (QStringLiteral ("geometry"), saveGeometry ());
      settings_->endGroup ();
      settings_->sync ();
    }

  QDialog::closeEvent (event);
}
