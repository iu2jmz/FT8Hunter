#pragma once

#include <QObject>
#include <QByteArray>
#include <QDateTime>
#include <QTimer>
#include <QString>
#include <functional>

class QTcpSocket;

class DxFunClusterClient final : public QObject
{
public:
  using StatusHandler = std::function<void (QString const&)>;
  using ConnectedHandler = std::function<void (bool)>;
  using SpotHandler = std::function<void (QString const&, quint64, QString const&, QString const&, QDateTime const&)>;

  explicit DxFunClusterClient (QObject * parent = nullptr);

  void start (QString const& callsign);
  void stop ();
  bool wanted () const { return wanted_; }
  bool connected () const;

  StatusHandler onStatus;
  ConnectedHandler onConnected;
  SpotHandler onSpot;

private:
  void connectNow ();
  void sendLogin ();
  void readyRead ();
  QByteArray cleanTelnet (QByteArray const& input);

  QTcpSocket * socket_;
  QTimer reconnectTimer_;
  QByteArray buffer_;
  QString callsign_;
  bool wanted_ {false};
  bool loginSent_ {false};
};
