#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>

class QTcpSocket;
class QTimer;

class PskReporterMqttClient final : public QObject
{
public:
  using StatusHandler = std::function<void (QString const&)>;
  using ConnectedHandler = std::function<void (bool)>;
  using MessageHandler = std::function<void (QString const&, QByteArray const&)>;

  explicit PskReporterMqttClient (QObject * parent = nullptr);

  void start ();
  void stop ();
  bool wanted () const { return wanted_; }
  bool connected () const;

  StatusHandler onStatus;
  ConnectedHandler onConnected;
  MessageHandler onMessage;

private:
  void connectNow ();
  void sendConnect ();
  void sendSubscribe ();
  void sendPacket (quint8 header, QByteArray const& body);
  void readyRead ();
  void parsePackets ();
  void scheduleReconnect ();
  static QByteArray encodeRemainingLength (int length);
  static void appendUtf8 (QByteArray& out, QByteArray const& text);

  QTcpSocket * socket_;
  QTimer * reconnectTimer_;
  QTimer * pingTimer_;
  QByteArray buffer_;
  bool wanted_ {false};
  bool mqttConnected_ {false};
  quint16 packetId_ {1};
};
