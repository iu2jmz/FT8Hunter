#include "BandHunterPanel.hpp"

#include <QCheckBox>
#include <QCloseEvent>
#include <QFont>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <utility>

namespace
{
  QStringList const HunterBands {
    QStringLiteral ("80m"), QStringLiteral ("60m"), QStringLiteral ("40m"),
    QStringLiteral ("30m"), QStringLiteral ("20m"), QStringLiteral ("17m"),
    QStringLiteral ("15m"), QStringLiteral ("12m"), QStringLiteral ("10m")};

  QStringList const NightHighBands {
    QStringLiteral ("20m"), QStringLiteral ("17m"), QStringLiteral ("15m"),
    QStringLiteral ("12m"), QStringLiteral ("10m")};

  QString clockText (int seconds)
  {
    return QStringLiteral ("%1:%2")
      .arg (seconds / 60, 2, 10, QLatin1Char ('0'))
      .arg (seconds % 60, 2, 10, QLatin1Char ('0'));
  }
}

BandHunterPanel::BandHunterPanel (QsyHandler qsyHandler,
                                  BoolGetter monitorEnabledGetter,
                                  MonitorSetter monitorSetter,
                                  BoolGetter monitorBusyGetter,
                                  BoolGetter externalBusyGetter,
                                  QSettings * settings,
                                  QWidget * parent)
  : QDialog {parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint}
  , qsyHandler_ {std::move (qsyHandler)}
  , monitorEnabledGetter_ {std::move (monitorEnabledGetter)}
  , monitorSetter_ {std::move (monitorSetter)}
  , monitorBusyGetter_ {std::move (monitorBusyGetter)}
  , externalBusyGetter_ {std::move (externalBusyGetter)}
  , settings_ {settings}
  , enableCheck_ {new QCheckBox {tr ("Attiva Band Hunter automatico"), this}}
  , stateLabel_ {new QLabel {tr ("OFF"), this}}
  , bandLabel_ {new QLabel {tr ("BANDA: --"), this}}
  , timeLabel_ {new QLabel {tr ("TEMPO: --"), this}}
  , nextLabel_ {new QLabel {tr ("PROSSIMA: --"), this}}
  , tickTimer_ {new QTimer {this}}
  , stabilizeTimer_ {new QTimer {this}}
{
  setWindowTitle (tr ("FT8 Hunter - Band Hunter"));
  setModal (false);
  setMinimumWidth (720);
  setAttribute (Qt::WA_DeleteOnClose, false);

  stateLabel_->setAlignment (Qt::AlignCenter);
  stateLabel_->setMinimumHeight (46);
  QFont sf = stateLabel_->font ();
  sf.setBold (true);
  sf.setPointSize (13);
  stateLabel_->setFont (sf);

  auto * bands = new QGridLayout;
  for (int i = 0; i < HunterBands.size (); ++i)
    {
      auto const& band = HunterBands.at (i);
      auto * box = new QCheckBox {band, this};
      box->setChecked (true);
      bandChecks_.insert (band, box);
      bands->addWidget (box, i / 5, i % 5);
      connect (box, &QCheckBox::toggled, this, [this] { saveSettings (); });
    }

  auto * info = new QHBoxLayout;
  info->addWidget (bandLabel_, 2);
  info->addWidget (timeLabel_, 2);
  info->addWidget (nextLabel_, 2);

  auto * root = new QVBoxLayout;
  root->addWidget (enableCheck_);
  root->addLayout (bands);
  root->addWidget (new QLabel {
    tr ("Permanenza: 15 min per banda; 23:00-06:00, 20/17/15/12/10 m = 4 min. "
        "DXFun ha priorita' e il tempo QSO non viene conteggiato."), this});
  root->addWidget (stateLabel_);
  root->addLayout (info);
  setLayout (root);

  tickTimer_->setInterval (1000);
  stabilizeTimer_->setSingleShot (true);
  connect (tickTimer_, &QTimer::timeout, this, [this] { tick (); });
  connect (stabilizeTimer_, &QTimer::timeout, this, [this] { stabilizationComplete (); });

  if (settings_)
    {
      settings_->beginGroup (QStringLiteral ("FT8HunterBandHunter"));
      restoreGeometry (settings_->value (QStringLiteral ("geometry"), saveGeometry ()).toByteArray ());
      for (auto const& band : HunterBands)
        bandChecks_.value (band)->setChecked (
          settings_->value (QStringLiteral ("band_") + band, true).toBool ());
      settings_->endGroup ();
    }

  connect (enableCheck_, &QCheckBox::toggled, this, [this] (bool on) {
      if (stopping_) return;
      if (on) startCycle ();
      else stopCycle ();
    });

  updateLabels (tr ("OFF"));
}

void BandHunterPanel::openWindow ()
{
  show ();
  raise ();
  activateWindow ();
}

bool BandHunterPanel::active () const
{
  return enableCheck_->isChecked ();
}

QStringList BandHunterPanel::selectedBands () const
{
  QStringList result;
  for (auto const& band : HunterBands)
    if (bandChecks_.value (band)->isChecked ())
      result.append (band);
  return result;
}

QString BandHunterPanel::nextSelectedBand () const
{
  auto const selected = selectedBands ();
  if (selected.isEmpty ()) return QString {};
  auto const i = selected.indexOf (currentBand_);
  if (i < 0 || i + 1 >= selected.size ()) return selected.first ();
  return selected.at (i + 1);
}

bool BandHunterPanel::nightHighBand () const
{
  auto const hour = QTime::currentTime ().hour ();
  auto const night = hour >= 23 || hour < 6;
  return night && NightHighBands.contains (currentBand_);
}

int BandHunterPanel::dwellSeconds () const
{
  return nightHighBand () ? 4 * 60 : 15 * 60;
}

void BandHunterPanel::startCycle ()
{
  auto const selected = selectedBands ();
  if (selected.isEmpty ())
    {
      stopping_ = true;
      enableCheck_->setChecked (false);
      stopping_ = false;
      updateLabels (tr ("ERRORE - selezionare almeno una banda"));
      return;
    }

  monitorWasEnabled_ = monitorEnabledGetter_ ? monitorEnabledGetter_ () : false;
  if (monitorSetter_) monitorSetter_ (false);

  currentBand_ = selected.first ();
  elapsedSeconds_ = 0;
  externalPaused_ = false;
  stabilizing_ = false;
  tickTimer_->start ();
  tuneCurrentBand (true);
}

void BandHunterPanel::stopCycle (QString const& detail)
{
  tickTimer_->stop ();
  stabilizeTimer_->stop ();
  stabilizing_ = false;
  externalPaused_ = false;

  if (monitorSetter_) monitorSetter_ (monitorWasEnabled_);

  currentBand_.clear ();
  elapsedSeconds_ = 0;
  updateLabels (detail.isEmpty () ? tr ("OFF") : tr ("OFF - %1").arg (detail));
}

void BandHunterPanel::pauseForExternalHunter ()
{
  if (!active ()) return;

  externalPaused_ = true;
  stabilizing_ = false;
  stabilizeTimer_->stop ();
  if (monitorSetter_) monitorSetter_ (false);
  updateLabels (tr ("PAUSA - priorita' DXFun Cluster"));
}

void BandHunterPanel::tuneCurrentBand (bool resetElapsed)
{
  if (!active () || currentBand_.isEmpty ()) return;
  if (resetElapsed) elapsedSeconds_ = 0;

  if (monitorSetter_) monitorSetter_ (false);

  auto const rc = qsyHandler_ ? qsyHandler_ (currentBand_) : QsyUnavailable;
  if (rc == QsyBusy)
    {
      updateLabels (tr ("ATTESA - radio/Hunter occupati"));
      QTimer::singleShot (1500, this, [this, resetElapsed] {
          if (active () && !externalPaused_) tuneCurrentBand (resetElapsed);
        });
      return;
    }

  if (rc == QsyUnavailable)
    {
      updateLabels (tr ("BANDA %1 non disponibile - salto").arg (currentBand_));
      QTimer::singleShot (1200, this, [this] {
          if (active ()) advanceBand ();
        });
      return;
    }

  stabilizing_ = true;
  updateLabels (tr ("STABILIZZAZIONE %1 - 5 s").arg (currentBand_));
  stabilizeTimer_->start (5000);
}

void BandHunterPanel::stabilizationComplete ()
{
  if (!active ()) return;
  stabilizing_ = false;

  if (externalBusyGetter_ && externalBusyGetter_ ())
    {
      pauseForExternalHunter ();
      return;
    }

  if (monitorSetter_ && !monitorSetter_ (true))
    {
      stopping_ = true;
      enableCheck_->setChecked (false);
      stopping_ = false;
      stopCycle (tr ("attivare il filtro DXCC sulla banda nel Monitor Hunter"));
      return;
    }

  updateLabels (tr ("ASCOLTO CQ"));
}

void BandHunterPanel::advanceBand ()
{
  if (!active ()) return;

  auto const selected = selectedBands ();
  if (selected.isEmpty ())
    {
      stopping_ = true;
      enableCheck_->setChecked (false);
      stopping_ = false;
      stopCycle (tr ("nessuna banda selezionata"));
      return;
    }

  auto i = selected.indexOf (currentBand_);
  if (i < 0 || i + 1 >= selected.size ()) i = 0;
  else ++i;

  currentBand_ = selected.at (i);
  elapsedSeconds_ = 0;
  tuneCurrentBand (true);
}

void BandHunterPanel::tick ()
{
  if (!active ()) return;

  if (externalBusyGetter_ && externalBusyGetter_ ())
    {
      if (!externalPaused_) pauseForExternalHunter ();
      return;
    }

  if (externalPaused_)
    {
      externalPaused_ = false;
      tuneCurrentBand (false);
      return;
    }

  if (stabilizing_) return;

  if (monitorBusyGetter_ && monitorBusyGetter_ ())
    {
      updateLabels (tr ("QSO/HUNTER IN CORSO - timer banda in pausa"));
      return;
    }

  if (monitorSetter_ && !(monitorEnabledGetter_ && monitorEnabledGetter_ ()))
    {
      if (!monitorSetter_ (true))
        {
          updateLabels (tr ("ATTESA - Monitor Hunter non disponibile"));
          return;
        }
    }

  ++elapsedSeconds_;
  if (elapsedSeconds_ >= dwellSeconds ())
    {
      advanceBand ();
      return;
    }

  updateLabels (tr ("ASCOLTO CQ"));
}

void BandHunterPanel::updateLabels (QString const& state)
{
  stateLabel_->setText (state);

  if (!active ())
    stateLabel_->setStyleSheet (
      QStringLiteral ("QLabel { background:#606060; color:white; border:1px solid #404040; padding:7px; }"));
  else if (externalPaused_ || state.contains (tr ("PAUSA")) || state.contains (tr ("ATTESA")))
    stateLabel_->setStyleSheet (
      QStringLiteral ("QLabel { background:#ffb74d; color:black; border:1px solid #b26a00; padding:7px; }"));
  else
    stateLabel_->setStyleSheet (
      QStringLiteral ("QLabel { background:#55c271; color:black; border:1px solid #2f8f49; padding:7px; }"));

  if (currentBand_.isEmpty ())
    {
      bandLabel_->setText (tr ("BANDA: --"));
      timeLabel_->setText (tr ("TEMPO: --"));
      nextLabel_->setText (tr ("PROSSIMA: --"));
      return;
    }

  bandLabel_->setText (
    tr ("BANDA: %1%2")
      .arg (currentBand_)
      .arg (nightHighBand () ? tr (" (NOTTE 4 MIN)") : QString {}));

  auto const limit = dwellSeconds ();
  timeLabel_->setText (
    tr ("TEMPO: %1 / %2")
      .arg (clockText (elapsedSeconds_))
      .arg (clockText (limit)));

  nextLabel_->setText (tr ("PROSSIMA: %1").arg (nextSelectedBand ()));
}

void BandHunterPanel::saveSettings ()
{
  if (!settings_) return;
  settings_->beginGroup (QStringLiteral ("FT8HunterBandHunter"));
  for (auto const& band : HunterBands)
    settings_->setValue (QStringLiteral ("band_") + band, bandChecks_.value (band)->isChecked ());
  settings_->endGroup ();
  settings_->sync ();
}

void BandHunterPanel::closeEvent (QCloseEvent * event)
{
  if (enableCheck_->isChecked ())
    enableCheck_->setChecked (false);

  if (settings_)
    {
      settings_->beginGroup (QStringLiteral ("FT8HunterBandHunter"));
      settings_->setValue (QStringLiteral ("geometry"), saveGeometry ());
      settings_->endGroup ();
      saveSettings ();
    }

  QDialog::closeEvent (event);
}
