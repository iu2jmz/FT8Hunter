#pragma once

#include <QDockWidget>
#include <QJsonObject>
#include <functional>

class QLabel;
class QTimer;

class HunterDashboard final : public QDockWidget
{
  Q_OBJECT

public:
  using StateProvider = std::function<QJsonObject ()>;

  explicit HunterDashboard (StateProvider provider, QWidget * parent = nullptr);

private:
  void refresh ();
  void pulse ();
  static QString frequencyText (double hz);
  static void repolish (QWidget * widget);

  StateProvider provider_;
  QLabel * call_;
  QLabel * activity_;
  QLabel * frequency_;
  QLabel * modeBand_;
  QLabel * rx_;
  QLabel * tx_;
  QLabel * power_;
  QLabel * target_;
  QLabel * monitor_;
  QLabel * band_;
  QLabel * dxfun_;
  QLabel * psk_;
  QTimer * refreshTimer_;
  QTimer * pulseTimer_;
  bool pulseOn_ {false};
};
