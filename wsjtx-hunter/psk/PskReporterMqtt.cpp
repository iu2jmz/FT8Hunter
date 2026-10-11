#include "PskReporterMqtt.hpp"

#include <QAbstractSocket>
#include <QDateTime>
#include <QRandomGenerator>
#include <QTcpSocket>
#include <QTimer>

namespace
{
  constexpr auto PskHost = "mqtt.pskreporter.info";
  constexpr quint16 PskPort = 1883;
}

PskReporterMqttClient::PskReporterMqttClient (QObject * parent)
  : QObject {parent}
  , socket_ {new QTcpSocket {this}}
  , reconnectTimer_ {new QTimer {this}}
  , pingTimer_ {new QTimer {this}}
{
  reconnectTimer_->setSingleShot (true);
  reconnectTimer_->setInterval (7000);
  pingTimer_->setInterval (30000);

  connect (reconnectTimer_, &QTimer::timeout, this, [this] {
      if (wanted_) connectNow ();
    });

  connect (pingTimer_, &QTimer::timeout, this, [this] {
      if (mqttConnected_)
        sendPacket (0xC0, QByteArray {});
    });

  connect (socket_, &QTcpSocket::connected, this, [this] {
      buffer_.clear ();
      mqttConnected_ = false;
      if (onStatus) onStatus (QStringLiteral ("PSK Reporter MQTT: socket connesso, handshake..."));
      sendConnect ();
    });

  connect (socket_, &QTcpSocket::readyRead, this, [this] { readyRead (); });

  connect (socket_, &QTcpSocket::disconnected, this, [this] {
      auto const wasConnected = mqttConnected_;
      mqttConnected_ = false;
      pingTimer_->stop ();
      if (wasConnected && onConnected) onConnected (false);
      if (wanted_)
        {
          if (onStatus) onStatus (QStringLiteral ("PSK Reporter MQTT disconnesso - nuovo tentativo tra 7 s"));
          scheduleReconnect ();
        }
      else if (onStatus)
        {
          onStatus (QStringLiteral ("PSK Reporter MQTT disconnesso"));
        }
    });

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  connect (socket_, &QAbstractSocket::errorOccurred, this, [this] (QAbstractSocket::SocketError) {
      if (onStatus) onStatus (QStringLiteral ("PSK Reporter MQTT: %1").arg (socket_->errorString ()));
    });
#else
  connect (socket_, QOverload<QAbstractSocket::SocketError>::of (&QAbstractSocket::error),
           this, [this] (QAbstractSocket::SocketError) {
      if (onStatus) onStatus (QStringLiteral ("PSK Reporter MQTT: %1").arg (socket_->errorString ()));
    });
#endif
}

bool PskReporterMqttClient::connected () const
{
  return mqttConnected_ && socket_->state () == QAbstractSocket::ConnectedState;
}

void PskReporterMqttClient::start ()
{
  wanted_ = true;
  reconnectTimer_->stop ();
  if (socket_->state () == QAbstractSocket::ConnectedState)
    {
      if (!mqttConnected_) sendConnect ();
      return;
    }
  connectNow ();
}

void PskReporterMqttClient::stop ()
{
  wanted_ = false;
  reconnectTimer_->stop ();
  pingTimer_->stop ();
  mqttConnected_ = false;
  buffer_.clear ();
  socket_->abort ();
  if (onConnected) onConnected (false);
  if (onStatus) onStatus (QStringLiteral ("PSK Reporter MQTT disconnesso"));
}

void PskReporterMqttClient::connectNow ()
{
  if (!wanted_) return;
  if (socket_->state () != QAbstractSocket::UnconnectedState)
    socket_->abort ();
  if (onStatus) onStatus (QStringLiteral ("Connessione a mqtt.pskreporter.info:1883..."));
  socket_->connectToHost (QString::fromLatin1 (PskHost), PskPort);
}

void PskReporterMqttClient::scheduleReconnect ()
{
  if (wanted_ && !reconnectTimer_->isActive ())
    reconnectTimer_->start ();
}

void PskReporterMqttClient::appendUtf8 (QByteArray& out, QByteArray const& text)
{
  auto const n = text.size ();
  out.append (static_cast<char> ((n >> 8) & 0xff));
  out.append (static_cast<char> (n & 0xff));
  out.append (text);
}

QByteArray PskReporterMqttClient::encodeRemainingLength (int length)
{
  QByteArray out;
  do
    {
      auto digit = length % 128;
      length /= 128;
      if (length > 0) digit |= 0x80;
      out.append (static_cast<char> (digit));
    }
  while (length > 0);
  return out;
}

void PskReporterMqttClient::sendPacket (quint8 header, QByteArray const& body)
{
  if (socket_->state () != QAbstractSocket::ConnectedState) return;

  QByteArray packet;
  packet.append (static_cast<char> (header));
  packet.append (encodeRemainingLength (body.size ()));
  packet.append (body);
  socket_->write (packet);
  socket_->flush ();
}

void PskReporterMqttClient::sendConnect ()
{
  QByteArray body;
  appendUtf8 (body, QByteArrayLiteral ("MQTT"));
  body.append (char (0x04)); // MQTT 3.1.1
  body.append (char (0x02)); // Clean Session
  body.append (char (0x00));
  body.append (char (0x3c)); // Keep alive 60 s

  auto const suffix = QString::number (
    static_cast<quint32> (QDateTime::currentMSecsSinceEpoch () & 0xffffffffu), 16);
  auto const clientId = QStringLiteral ("FT8Hunter-IU2JMZ-%1").arg (suffix).toLatin1 ();
  appendUtf8 (body, clientId);

  sendPacket (0x10, body);
}

void PskReporterMqttClient::sendSubscribe ()
{
  static QStringList const bands {
    QStringLiteral ("80m"), QStringLiteral ("60m"), QStringLiteral ("40m"),
    QStringLiteral ("30m"), QStringLiteral ("20m"), QStringLiteral ("17m"),
    QStringLiteral ("15m"), QStringLiteral ("12m"), QStringLiteral ("10m")};

  QByteArray body;
  auto const id = packetId_++;
  body.append (static_cast<char> ((id >> 8) & 0xff));
  body.append (static_cast<char> (id & 0xff));

  for (auto const& band : bands)
    {
      for (auto const& mode : {QStringLiteral ("FT8"), QStringLiteral ("FT4")})
        {
          auto const topic =
            QStringLiteral ("pskr/filter/v2/%1/%2/#").arg (band, mode).toUtf8 ();
          appendUtf8 (body, topic);
          body.append (char (0x00)); // QoS 0
        }
    }

  sendPacket (0x82, body);
  if (onStatus) onStatus (QStringLiteral ("PSK Reporter MQTT connesso - ascolto FT8/FT4 HF 80-10 m"));
}

void PskReporterMqttClient::readyRead ()
{
  buffer_.append (socket_->readAll ());
  parsePackets ();
}

void PskReporterMqttClient::parsePackets ()
{
  while (buffer_.size () >= 2)
    {
      auto const header = static_cast<quint8> (buffer_.at (0));

      int multiplier = 1;
      int remaining = 0;
      int index = 1;
      int digit = 0;
      int bytes = 0;
      do
        {
          if (index >= buffer_.size ()) return;
          digit = static_cast<quint8> (buffer_.at (index++));
          remaining += (digit & 127) * multiplier;
          multiplier *= 128;
          ++bytes;
          if (bytes > 4)
            {
              buffer_.clear ();
              return;
            }
        }
      while ((digit & 128) != 0);

      if (buffer_.size () < index + remaining) return;

      auto const body = buffer_.mid (index, remaining);
      buffer_.remove (0, index + remaining);

      auto const type = header >> 4;
      if (type == 2) // CONNACK
        {
          if (body.size () < 2 || static_cast<quint8> (body.at (1)) != 0)
            {
              if (onStatus) onStatus (QStringLiteral ("PSK Reporter MQTT: CONNACK rifiutato"));
              socket_->abort ();
              continue;
            }

          mqttConnected_ = true;
          if (onConnected) onConnected (true);
          pingTimer_->start ();
          sendSubscribe ();
          continue;
        }

      if (type == 3) // PUBLISH
        {
          if (body.size () < 2) continue;
          auto const topicLength =
            (static_cast<quint8> (body.at (0)) << 8)
            | static_cast<quint8> (body.at (1));
          if (topicLength <= 0 || body.size () < 2 + topicLength) continue;

          auto pos = 2;
          auto const topic = QString::fromUtf8 (body.mid (pos, topicLength));
          pos += topicLength;

          auto const qos = (header >> 1) & 0x03;
          if (qos > 0)
            {
              if (body.size () < pos + 2) continue;
              pos += 2;
            }

          if (onMessage) onMessage (topic, body.mid (pos));
        }
    }
}
