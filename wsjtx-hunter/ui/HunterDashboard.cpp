#include "HunterDashboard.hpp"

#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QJsonValue>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
  QLabel * makeCard (QString const& title, QWidget * parent, QString const& objectName)
  {
    auto * label = new QLabel {title + QStringLiteral ("\n--"), parent};
    label->setObjectName (objectName);
    label->setAlignment (Qt::AlignCenter);
    label->setMinimumWidth (112);
    label->setMinimumHeight (54);
    return label;
  }

  QString moduleText (QString const& name, QJsonObject const& data)
  {
    auto const active = data.value (QStringLiteral ("active")).toBool ();
    auto const automatic = data.value (QStringLiteral ("automatic")).toBool ();
    auto const connected =
      data.contains (QStringLiteral ("connected"))
        ? data.value (QStringLiteral ("connected")).toBool ()
        : data.value (QStringLiteral ("connectionEnabled")).toBool ();

    QString state = data.value (QStringLiteral ("state")).toString ();
    if (state.isEmpty ())
      state = active ? QStringLiteral ("ACTIVE")
                     : automatic ? QStringLiteral ("AUTO READY")
                                 : connected ? QStringLiteral ("CONNECTED")
                                             : QStringLiteral ("IDLE");

    return name + QStringLiteral ("\n") + state;
  }

  QString firstTarget (QJsonObject const& state)
  {
    for (auto const& key : {QStringLiteral ("monitorHunter"),
                            QStringLiteral ("dxFun"),
                            QStringLiteral ("pskReporter")})
      {
        auto const obj = state.value (key).toObject ();
        auto const target = obj.value (QStringLiteral ("target")).toString ().trimmed ();
        if (!target.isEmpty ()) return target;
      }
    return QStringLiteral ("--");
  }
}

HunterDashboard::HunterDashboard (StateProvider provider, QWidget * parent)
  : QDockWidget {tr ("FT8 Hunter // Live Console 2.2"), parent}
  , provider_ {std::move (provider)}
  , call_ {new QLabel {QStringLiteral ("FT8 HUNTER"), this}}
  , activity_ {new QLabel {tr ("STANDBY"), this}}
  , frequency_ {new QLabel {QStringLiteral ("--.---.---"), this}}
  , modeBand_ {new QLabel {QStringLiteral ("--  |  --"), this}}
  , rx_ {makeCard (QStringLiteral ("RX"), this, QStringLiteral ("HunterDashboardCard"))}
  , tx_ {makeCard (QStringLiteral ("TX"), this, QStringLiteral ("HunterDashboardCard"))}
  , power_ {makeCard (QStringLiteral ("RF"), this, QStringLiteral ("HunterDashboardCard"))}
  , target_ {makeCard (QStringLiteral ("TARGET"), this, QStringLiteral ("HunterDashboardTarget"))}
  , monitor_ {makeCard (QStringLiteral ("MONITOR"), this, QStringLiteral ("HunterDashboardModule"))}
  , band_ {makeCard (QStringLiteral ("BAND"), this, QStringLiteral ("HunterDashboardModule"))}
  , dxfun_ {makeCard (QStringLiteral ("DXFUN"), this, QStringLiteral ("HunterDashboardModule"))}
  , psk_ {makeCard (QStringLiteral ("PSK"), this, QStringLiteral ("HunterDashboardModule"))}
  , refreshTimer_ {new QTimer {this}}
  , pulseTimer_ {new QTimer {this}}
{
  setObjectName (QStringLiteral ("FT8HunterDashboard"));
  setAllowedAreas (Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
  setFeatures (QDockWidget::DockWidgetClosable
               | QDockWidget::DockWidgetMovable
               | QDockWidget::DockWidgetFloatable);
  setMinimumHeight (138);
  setMaximumHeight (188);

  auto * body = new QWidget {this};
  body->setObjectName (QStringLiteral ("HunterDashboardBody"));

  call_->setObjectName (QStringLiteral ("HunterDashboardCall"));
  activity_->setObjectName (QStringLiteral ("HunterDashboardActivity"));
  frequency_->setObjectName (QStringLiteral ("HunterDashboardFrequency"));
  modeBand_->setObjectName (QStringLiteral ("HunterDashboardModeBand"));

  call_->setAlignment (Qt::AlignLeft | Qt::AlignVCenter);
  activity_->setAlignment (Qt::AlignCenter);
  frequency_->setAlignment (Qt::AlignCenter);
  modeBand_->setAlignment (Qt::AlignCenter);

  auto callFont = call_->font ();
  callFont.setBold (true);
  callFont.setPointSize (11);
  call_->setFont (callFont);

  auto freqFont = frequency_->font ();
  freqFont.setBold (true);
  freqFont.setPointSize (22);
  frequency_->setFont (freqFont);

  auto * identity = new QVBoxLayout;
  identity->setContentsMargins (0, 0, 0, 0);
  identity->setSpacing (4);
  identity->addWidget (call_);
  identity->addWidget (activity_);
  identity->addStretch ();

  auto * radio = new QVBoxLayout;
  radio->setContentsMargins (0, 0, 0, 0);
  radio->setSpacing (2);
  radio->addWidget (frequency_);
  radio->addWidget (modeBand_);

  auto * metrics = new QHBoxLayout;
  metrics->setContentsMargins (0, 0, 0, 0);
  metrics->setSpacing (6);
  metrics->addWidget (rx_);
  metrics->addWidget (tx_);
  metrics->addWidget (power_);
  metrics->addWidget (target_);

  auto * modules = new QHBoxLayout;
  modules->setContentsMargins (0, 0, 0, 0);
  modules->setSpacing (6);
  modules->addWidget (monitor_);
  modules->addWidget (band_);
  modules->addWidget (dxfun_);
  modules->addWidget (psk_);

  auto * root = new QGridLayout;
  root->setContentsMargins (10, 8, 10, 8);
  root->setHorizontalSpacing (12);
  root->setVerticalSpacing (6);
  root->addLayout (identity, 0, 0, 2, 1);
  root->addLayout (radio, 0, 1, 2, 1);
  root->addLayout (metrics, 0, 2);
  root->addLayout (modules, 1, 2);
  root->setColumnStretch (0, 1);
  root->setColumnStretch (1, 2);
  root->setColumnStretch (2, 6);
  body->setLayout (root);
  setWidget (body);

  refreshTimer_->setInterval (450);
  pulseTimer_->setInterval (650);
  connect (refreshTimer_, &QTimer::timeout, this, [this] { refresh (); });
  connect (pulseTimer_, &QTimer::timeout, this, [this] { pulse (); });
  refreshTimer_->start ();
  pulseTimer_->start ();
  refresh ();
}

QString HunterDashboard::frequencyText (double hz)
{
  auto text = QString::number (static_cast<qulonglong> (hz + 0.5));
  for (int i = text.size () - 3; i > 0; i -= 3)
    text.insert (i, QLatin1Char ('.'));
  return text;
}

void HunterDashboard::repolish (QWidget * widget)
{
  if (!widget || !widget->style ()) return;
  widget->style ()->unpolish (widget);
  widget->style ()->polish (widget);
  widget->update ();
}

void HunterDashboard::refresh ()
{
  if (!provider_) return;
  auto const state = provider_ ();

  auto const myCall = state.value (QStringLiteral ("myCall")).toString ().trimmed ();
  call_->setText (
    myCall.isEmpty ()
      ? QStringLiteral ("FT8 HUNTER // LIVE CONSOLE")
      : QStringLiteral ("FT8 HUNTER  //  %1").arg (myCall));

  frequency_->setText (frequencyText (state.value (QStringLiteral ("frequencyHz")).toDouble ()));
  modeBand_->setText (
    QStringLiteral ("%1   |   %2")
      .arg (state.value (QStringLiteral ("mode")).toString (QStringLiteral ("--")))
      .arg (state.value (QStringLiteral ("band")).toString (QStringLiteral ("--"))));

  rx_->setText (
    QStringLiteral ("RX\n%1 Hz")
      .arg (state.value (QStringLiteral ("rxOffset")).toInt ()));
  tx_->setText (
    QStringLiteral ("TX\n%1 Hz")
      .arg (state.value (QStringLiteral ("txOffset")).toInt ()));

  auto power = state.value (QStringLiteral ("rfPower")).toInt (-1);
  if (power < 0)
    power = state.value (QStringLiteral ("monitorHunter"))
      .toObject ().value (QStringLiteral ("power")).toInt (-1);
  power_->setText (
    power >= 0 ? QStringLiteral ("RF\n%1%").arg (power)
               : QStringLiteral ("RF\n--"));

  target_->setText (
    QStringLiteral ("TARGET\n%1").arg (firstTarget (state)));

  auto const monitor = state.value (QStringLiteral ("monitorHunter")).toObject ();
  auto const band = state.value (QStringLiteral ("bandHunter")).toObject ();
  auto const dxfun = state.value (QStringLiteral ("dxFun")).toObject ();
  auto const psk = state.value (QStringLiteral ("pskReporter")).toObject ();

  monitor_->setText (moduleText (QStringLiteral ("MONITOR"), monitor));
  band_->setText (
    QStringLiteral ("BAND\n%1")
      .arg (band.value (QStringLiteral ("active")).toBool ()
              ? band.value (QStringLiteral ("currentBand")).toString (QStringLiteral ("ACTIVE"))
              : QStringLiteral ("IDLE")));
  dxfun_->setText (moduleText (QStringLiteral ("DXFUN"), dxfun));
  psk_->setText (moduleText (QStringLiteral ("PSK"), psk));

  for (auto pair : {
         qMakePair (monitor_, monitor.value (QStringLiteral ("active")).toBool ()),
         qMakePair (band_, band.value (QStringLiteral ("active")).toBool ()),
         qMakePair (dxfun_, dxfun.value (QStringLiteral ("active")).toBool ()),
         qMakePair (psk_, psk.value (QStringLiteral ("active")).toBool ())
       })
    {
      pair.first->setProperty ("active", pair.second);
      repolish (pair.first);
    }

  QString stateName = QStringLiteral ("idle");
  QString text = tr ("STANDBY");
  if (state.value (QStringLiteral ("transmitting")).toBool ())
    {
      stateName = QStringLiteral ("tx");
      text = tr ("TX LIVE");
    }
  else if (state.value (QStringLiteral ("tune")).toBool ())
    {
      stateName = QStringLiteral ("tune");
      text = tr ("TUNE");
    }
  else if (state.value (QStringLiteral ("monitoring")).toBool ())
    {
      stateName = QStringLiteral ("rx");
      text = state.value (QStringLiteral ("autoTx")).toBool ()
        ? tr ("RX // AUTO TX ARMED")
        : tr ("RX MONITOR");
    }

  activity_->setText (text);
  activity_->setProperty ("dashboardState", stateName);
  repolish (activity_);
}

void HunterDashboard::pulse ()
{
  pulseOn_ = !pulseOn_;
  activity_->setProperty ("pulseOn", pulseOn_);
  repolish (activity_);

  for (auto * label : {monitor_, band_, dxfun_, psk_})
    {
      if (label->property ("active").toBool ())
        {
          label->setProperty ("pulseOn", pulseOn_);
          repolish (label);
        }
    }
}
