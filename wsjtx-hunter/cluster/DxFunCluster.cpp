#include "DxFunCluster.hpp"

#include <QTcpSocket>
#include <QAbstractSocket>
#include <QRegularExpression>
#include <QtMath>

namespace
{
  constexpr auto host = "dxfun.com";
  constexpr quint16 port = 8000;
}

DxFunClusterClient::DxFunClusterClient (QObject * parent)
  : QObject {parent}
  , socket_ {new QTcpSocket {this}}
{
  reconnectTimer_.setInterval (10000);
  reconnectTimer_.setSingleShot (true);

  connect (&reconnectTimer_, &QTimer::timeout, this, [this] {
      if (wanted_) connectNow ();
    });

  connect (socket_, &QTcpSocket::connected, this, [this] {
      loginSent_ = false;
      buffer_.clear ();
      if (onConnected) onConnected (true);
      if (onStatus) onStatus (QStringLiteral ("Connesso a dxfun.com:8000 - login..."));
      QTimer::singleShot (1200, this, [this] {
          if (wanted_ && socket_->state () == QAbstractSocket::ConnectedState && !loginSent_)
            sendLogin ();
        });
    });

  connect (socket_, &QTcpSocket::readyRead, this, [this] { readyRead (); });

  connect (socket_, &QTcpSocket::disconnected, this, [this] {
      if (onConnected) onConnected (false);
      if (wanted_)
        {
          if (onStatus) onStatus (QStringLiteral ("DXFun disconnesso - riconnessione tra 10 s"));
          reconnectTimer_.start ();
        }
      else if (onStatus)
        {
          onStatus (QStringLiteral ("DXFun disconnesso"));
        }
    });

#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  connect (socket_, &QAbstractSocket::errorOccurred, this, [this] (QAbstractSocket::SocketError) {
      if (onStatus) onStatus (QStringLiteral ("DXFun: %1").arg (socket_->errorString ()));
    });
#else
  connect (socket_, QOverload<QAbstractSocket::SocketError>::of (&QAbstractSocket::error),
           this, [this] (QAbstractSocket::SocketError) {
      if (onStatus) onStatus (QStringLiteral ("DXFun: %1").arg (socket_->errorString ()));
    });
#endif
}

bool DxFunClusterClient::connected () const
{
  return socket_->state () == QAbstractSocket::ConnectedState;
}

void DxFunClusterClient::start (QString const& callsign)
{
  callsign_ = callsign.trimmed ().toUpper ();
  wanted_ = true;
  reconnectTimer_.stop ();
  if (connected ())
    {
      if (!loginSent_) sendLogin ();
      return;
    }
  connectNow ();
}

void DxFunClusterClient::stop ()
{
  wanted_ = false;
  reconnectTimer_.stop ();
  loginSent_ = false;
  buffer_.clear ();
  socket_->abort ();
  if (onConnected) onConnected (false);
  if (onStatus) onStatus (QStringLiteral ("DXFun disconnesso"));
}

void DxFunClusterClient::connectNow ()
{
  if (!wanted_) return;
  if (callsign_.isEmpty ())
    {
      if (onStatus) onStatus (QStringLiteral ("DXFun: nominativo locale non configurato"));
      return;
    }
  if (socket_->state () != QAbstractSocket::UnconnectedState)
    socket_->abort ();
  if (onStatus) onStatus (QStringLiteral ("Connessione a dxfun.com:8000..."));
  socket_->connectToHost (QString::fromLatin1 (host), port);
}

void DxFunClusterClient::sendLogin ()
{
  if (loginSent_ || callsign_.isEmpty () || !connected ()) return;
  socket_->write (callsign_.toLatin1 () + "\r\n");
  socket_->flush ();
  loginSent_ = true;
  if (onStatus) onStatus (QStringLiteral ("DXFun connesso come %1").arg (callsign_));
}

QByteArray DxFunClusterClient::cleanTelnet (QByteArray const& input)
{
  QByteArray out;
  out.reserve (input.size ());

  for (int i = 0; i < input.size (); ++i)
    {
      auto const ch = static_cast<unsigned char> (input.at (i));
      if (ch != 255)
        {
          out.append (input.at (i));
          continue;
        }

      if (i + 1 >= input.size ()) break;
      auto const cmd = static_cast<unsigned char> (input.at (++i));

      // IAC IAC encodes a literal 0xff.
      if (cmd == 255)
        {
          out.append (char (255));
          continue;
        }

      // WILL/WONT/DO/DONT + option. Refuse optional Telnet features.
      if ((cmd == 251 || cmd == 252 || cmd == 253 || cmd == 254) && i + 1 < input.size ())
        {
          auto const option = input.at (++i);
          if (cmd == 253 || cmd == 251)
            {
              QByteArray reply;
              reply.append (char (255));
              reply.append (char (cmd == 253 ? 252 : 254));
              reply.append (option);
              socket_->write (reply);
            }
          continue;
        }

      // Sub-negotiation: skip until IAC SE.
      if (cmd == 250)
        {
          while (i + 1 < input.size ())
            {
              ++i;
              if (static_cast<unsigned char> (input.at (i)) == 255
                  && i + 1 < input.size ()
                  && static_cast<unsigned char> (input.at (i + 1)) == 240)
                {
                  ++i;
                  break;
                }
            }
        }
    }
  return out;
}

void DxFunClusterClient::readyRead ()
{
  auto data = cleanTelnet (socket_->readAll ());
  buffer_.append (data);

  auto const lower = buffer_.toLower ();
  if (!loginSent_
      && (lower.contains ("login:")
          || lower.contains ("callsign:")
          || lower.contains ("call:")))
    {
      sendLogin ();
    }

  while (true)
    {
      auto const pos = buffer_.indexOf ('\n');
      if (pos < 0) break;

      auto lineBytes = buffer_.left (pos);
      buffer_.remove (0, pos + 1);
      lineBytes.replace ("\r", "");

      auto line = QString::fromLatin1 (lineBytes);
      line.remove (QRegularExpression {QStringLiteral ("\\x1B\\[[0-?]*[ -/]*[@-~]")});
      line = line.trimmed ();
      if (line.isEmpty ()) continue;

      static QRegularExpression const spotRe {
        QStringLiteral ("^DX\\s+de\\s+([^:]+):\\s*([0-9]+(?:\\.[0-9]+)?)\\s+([A-Z0-9/<>-]+)\\s*(.*)$"),
        QRegularExpression::CaseInsensitiveOption};

      auto const m = spotRe.match (line);
      if (!m.hasMatch ()) continue;

      bool ok = false;
      auto const kHz = m.captured (2).toDouble (&ok);
      if (!ok || kHz <= 0.) continue;

      auto call = m.captured (3).trimmed ().toUpper ();
      call.remove ('<');
      call.remove ('>');
      auto const frequencyHz = static_cast<quint64> (qRound64 (kHz * 1000.0));
      auto const comment = m.captured (4).trimmed ();
      auto const spotter = m.captured (1).trimmed ().toUpper ();

      if (onSpot)
        onSpot (call, frequencyHz, comment, spotter, QDateTime::currentDateTimeUtc ());
    }
}
