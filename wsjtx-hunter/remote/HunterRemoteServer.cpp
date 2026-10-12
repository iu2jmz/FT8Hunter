#include "HunterRemoteServer.hpp"
#include <QStringList>
#include <utility>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkInterface>
#include <QSettings>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>
#include <QWebSocket>
#include <QWebSocketServer>

namespace
{
  QString settingsPath ()
  {
    auto dir = QStandardPaths::writableLocation (QStandardPaths::AppConfigLocation);
    QDir {}.mkpath (dir);
    return QDir {dir}.absoluteFilePath (QStringLiteral ("ft8hunter-remote.ini"));
  }

  QByteArray contentTypeFor (QString const& name)
  {
    if (name.endsWith (QStringLiteral (".html"))) return QByteArrayLiteral ("text/html; charset=utf-8");
    if (name.endsWith (QStringLiteral (".js"))) return QByteArrayLiteral ("application/javascript; charset=utf-8");
    if (name.endsWith (QStringLiteral (".css"))) return QByteArrayLiteral ("text/css; charset=utf-8");
    if (name.endsWith (QStringLiteral (".json"))) return QByteArrayLiteral ("application/json; charset=utf-8");
    if (name.endsWith (QStringLiteral (".png"))) return QByteArrayLiteral ("image/png");
    if (name.endsWith (QStringLiteral (".svg"))) return QByteArrayLiteral ("image/svg+xml");
    return QByteArrayLiteral ("application/octet-stream");
  }
}

HunterRemoteServer::HunterRemoteServer (StateProvider stateProvider,
                                        SpectrumProvider spectrumProvider,
                                        CommandHandler commandHandler,
                                        QObject * parent)
  : QObject {parent}
  , stateProvider_ {std::move (stateProvider)}
  , spectrumProvider_ {std::move (spectrumProvider)}
  , commandHandler_ {std::move (commandHandler)}
  , httpServer_ {new QTcpServer {this}}
  , webSocketServer_ {new QWebSocketServer {
      QStringLiteral ("FT8 Hunter Remote"),
      QWebSocketServer::NonSecureMode,
      this}}
  , stateTimer_ {new QTimer {this}}
  , spectrumTimer_ {new QTimer {this}}
{
  connect (httpServer_, &QTcpServer::newConnection, this, [this] { acceptHttp (); });
  connect (webSocketServer_, &QWebSocketServer::newConnection, this, [this] { acceptWebSocket (); });

  stateTimer_->setInterval (500);
  spectrumTimer_->setInterval (850);
  connect (stateTimer_, &QTimer::timeout, this, [this] { broadcastState (); });
  connect (spectrumTimer_, &QTimer::timeout, this, [this] { broadcastSpectrum (); });
}

HunterRemoteServer::~HunterRemoteServer ()
{
  stop ();
}

HunterRemoteSettings HunterRemoteServer::loadSettings ()
{
  QSettings s {settingsPath (), QSettings::IniFormat};
  s.beginGroup (QStringLiteral ("Remote"));
  HunterRemoteSettings out;
  out.enabled = s.value (QStringLiteral ("enabled"), false).toBool ();
  out.lanAccess = s.value (QStringLiteral ("lanAccess"), false).toBool ();
  out.allowTransmit = s.value (QStringLiteral ("allowTransmit"), false).toBool ();
  out.httpPort = static_cast<quint16> (s.value (QStringLiteral ("httpPort"), 8787).toUInt ());
  out.webSocketPort = static_cast<quint16> (s.value (QStringLiteral ("webSocketPort"), 8788).toUInt ());
  out.token = s.value (QStringLiteral ("token")).toString ().trimmed ();
  s.endGroup ();
  if (out.token.isEmpty ())
    out.token = QUuid::createUuid ().toString (QUuid::WithoutBraces).remove (QLatin1Char ('-'));
  return out;
}

void HunterRemoteServer::saveSettings (HunterRemoteSettings const& settings)
{
  QSettings s {settingsPath (), QSettings::IniFormat};
  s.beginGroup (QStringLiteral ("Remote"));
  s.setValue (QStringLiteral ("enabled"), settings.enabled);
  s.setValue (QStringLiteral ("lanAccess"), settings.lanAccess);
  s.setValue (QStringLiteral ("allowTransmit"), settings.allowTransmit);
  s.setValue (QStringLiteral ("httpPort"), settings.httpPort);
  s.setValue (QStringLiteral ("webSocketPort"), settings.webSocketPort);
  s.setValue (QStringLiteral ("token"), settings.token.trimmed ());
  s.endGroup ();
  s.sync ();
}

QString HunterRemoteServer::ensureToken (HunterRemoteSettings settings)
{
  if (settings.token.trimmed ().isEmpty ())
    settings.token = QUuid::createUuid ().toString (QUuid::WithoutBraces).remove (QLatin1Char ('-'));
  return settings.token;
}

QString HunterRemoteServer::appUrl (HunterRemoteSettings const& settings)
{
  QString host = QStringLiteral ("127.0.0.1");
  if (settings.lanAccess)
    {
      for (auto const& address : QNetworkInterface::allAddresses ())
        {
          if (address.protocol () != QAbstractSocket::IPv4Protocol
              || address.isLoopback ()
              || address.isNull ())
            continue;

          auto const candidate = address.toString ();
          if (candidate.startsWith (QStringLiteral ("169.254."))) continue;
          host = candidate;
          break;
        }
    }

  return QStringLiteral ("http://%1:%2/")
    .arg (host)
    .arg (settings.httpPort);
}

bool HunterRemoteServer::start (HunterRemoteSettings const& settings, QString * error)
{
  stop ();
  settings_ = settings;
  if (settings_.token.trimmed ().isEmpty ())
    settings_.token = QUuid::createUuid ().toString (QUuid::WithoutBraces).remove (QLatin1Char ('-'));

  auto const address = settings_.lanAccess ? QHostAddress::AnyIPv4 : QHostAddress::LocalHost;
  if (!httpServer_->listen (address, settings_.httpPort))
    {
      if (error) *error = QStringLiteral ("HTTP: %1").arg (httpServer_->errorString ());
      stop ();
      return false;
    }

  if (!webSocketServer_->listen (address, settings_.webSocketPort))
    {
      if (error) *error = QStringLiteral ("WebSocket: %1").arg (webSocketServer_->errorString ());
      stop ();
      return false;
    }

  stateTimer_->start ();
  spectrumTimer_->start ();
  return true;
}

void HunterRemoteServer::stop ()
{
  stateTimer_->stop ();
  spectrumTimer_->stop ();

  for (auto * socket : clients_.keys ())
    {
      if (socket)
        {
          socket->close (QWebSocketProtocol::CloseCodeGoingAway,
                         QStringLiteral ("FT8 Hunter Remote stopped"));
          socket->deleteLater ();
        }
    }
  clients_.clear ();

  webSocketServer_->close ();
  httpServer_->close ();
}

bool HunterRemoteServer::running () const
{
  return httpServer_->isListening () && webSocketServer_->isListening ();
}

QString HunterRemoteServer::webRoot () const
{
  QDir bin {QCoreApplication::applicationDirPath ()};
  return QDir::cleanPath (bin.absoluteFilePath (QStringLiteral ("../share/ft8hunter-remote")));
}

QByteArray HunterRemoteServer::loadWebAsset (QString const& path, QByteArray * mime) const
{
  QString safe = path;
  if (safe == QStringLiteral ("/")) safe = QStringLiteral ("/index.html");
  if (safe.contains (QStringLiteral (".."))) return {};
  while (safe.startsWith (QLatin1Char ('/'))) safe.remove (0, 1);

  static QStringList const allowed {
    QStringLiteral ("index.html"),
    QStringLiteral ("app.js"),
    QStringLiteral ("styles.css"),
    QStringLiteral ("manifest.json"),
    QStringLiteral ("service-worker.js"),
    QStringLiteral ("icon.png")
  };
  if (!allowed.contains (safe)) return {};

  QFile f {QDir {webRoot ()}.absoluteFilePath (safe)};
  if (!f.open (QIODevice::ReadOnly)) return {};
  if (mime) *mime = contentTypeFor (safe);
  return f.readAll ();
}

void HunterRemoteServer::acceptHttp ()
{
  while (httpServer_->hasPendingConnections ())
    {
      auto * socket = httpServer_->nextPendingConnection ();
      connect (socket, &QTcpSocket::readyRead, this, [this, socket] { handleHttp (socket); });
      connect (socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void HunterRemoteServer::handleHttp (QTcpSocket * socket)
{
  if (!socket || !socket->canReadLine ()) return;

  auto const request = socket->readLine ().trimmed ();
  auto const parts = request.split (' ');
  if (parts.size () < 2 || parts.at (0) != QByteArrayLiteral ("GET"))
    {
      socket->write ("HTTP/1.1 405 Method Not Allowed\r\nConnection: close\r\n\r\n");
      socket->disconnectFromHost ();
      return;
    }

  auto path = QString::fromUtf8 (parts.at (1));
  auto const queryPos = path.indexOf (QLatin1Char ('?'));
  if (queryPos >= 0) path = path.left (queryPos);

  QByteArray mime;
  QByteArray body;
  if (path == QStringLiteral ("/remote-config.json"))
    {
      QJsonObject config {
        {QStringLiteral ("version"), QStringLiteral ("2.2.2")},
        {QStringLiteral ("webSocketPort"), static_cast<int> (settings_.webSocketPort)}
      };
      body = QJsonDocument {config}.toJson (QJsonDocument::Compact);
      mime = QByteArrayLiteral ("application/json; charset=utf-8");
    }
  else
    {
      body = loadWebAsset (path, &mime);
    }

  if (body.isEmpty ())
    {
      socket->write ("HTTP/1.1 404 Not Found\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
      socket->disconnectFromHost ();
      return;
    }

  QByteArray header = "HTTP/1.1 200 OK\r\nConnection: close\r\nCache-Control: no-cache\r\n";
  header += "Content-Type: " + mime + "\r\n";
  header += "Content-Length: " + QByteArray::number (body.size ()) + "\r\n\r\n";
  socket->write (header);
  socket->write (body);
  socket->disconnectFromHost ();
}

void HunterRemoteServer::acceptWebSocket ()
{
  while (webSocketServer_->hasPendingConnections ())
    {
      auto * socket = webSocketServer_->nextPendingConnection ();
      clients_.insert (socket, false);

      connect (socket, &QWebSocket::textMessageReceived, this,
               [this, socket] (QString const& message) {
          handleWebSocketMessage (socket, message);
        });
      connect (socket, &QWebSocket::disconnected, this, [this, socket] {
          clients_.remove (socket);
          socket->deleteLater ();
        });

      QJsonObject hello;
      hello.insert (QStringLiteral ("type"), QStringLiteral ("hello"));
      hello.insert (QStringLiteral ("version"), QStringLiteral ("2.2.2"));
      hello.insert (QStringLiteral ("authRequired"), true);
      sendJson (socket, hello);
    }
}

bool HunterRemoteServer::authenticated (QWebSocket * socket) const
{
  return clients_.value (socket, false);
}

void HunterRemoteServer::handleWebSocketMessage (QWebSocket * socket, QString const& text)
{
  QJsonParseError parseError;
  auto const doc = QJsonDocument::fromJson (text.toUtf8 (), &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject ())
    {
      sendJson (socket, QJsonObject {
        {QStringLiteral ("type"), QStringLiteral ("error")},
        {QStringLiteral ("message"), QStringLiteral ("JSON non valido")}});
      return;
    }

  auto const obj = doc.object ();
  auto const type = obj.value (QStringLiteral ("type")).toString ();

  if (!authenticated (socket))
    {
      if (type != QStringLiteral ("auth")
          || obj.value (QStringLiteral ("token")).toString () != settings_.token)
        {
          sendJson (socket, QJsonObject {
            {QStringLiteral ("type"), QStringLiteral ("auth")},
            {QStringLiteral ("ok"), false},
            {QStringLiteral ("message"), QStringLiteral ("Token non valido")}});
          socket->close (QWebSocketProtocol::CloseCodePolicyViolated,
                         QStringLiteral ("Authentication failed"));
          return;
        }

      clients_[socket] = true;
      sendJson (socket, QJsonObject {
        {QStringLiteral ("type"), QStringLiteral ("auth")},
        {QStringLiteral ("ok"), true},
        {QStringLiteral ("allowTransmit"), settings_.allowTransmit}});
      if (stateProvider_)
        {
          auto state = stateProvider_ ();
          state.insert (QStringLiteral ("type"), QStringLiteral ("state"));
          sendJson (socket, state);
        }
      return;
    }

  if (type == QStringLiteral ("ping"))
    {
      sendJson (socket, QJsonObject {{QStringLiteral ("type"), QStringLiteral ("pong")}});
      return;
    }

  if (type == QStringLiteral ("command"))
    {
      QJsonObject reply;
      if (commandHandler_)
        reply = commandHandler_ (obj, settings_.allowTransmit);
      else
        reply.insert (QStringLiteral ("ok"), false);

      reply.insert (QStringLiteral ("type"), QStringLiteral ("command_result"));
      reply.insert (QStringLiteral ("id"), obj.value (QStringLiteral ("id")));
      sendJson (socket, reply);
    }
}

void HunterRemoteServer::sendJson (QWebSocket * socket, QJsonObject const& object)
{
  if (!socket) return;
  socket->sendTextMessage (QString::fromUtf8 (
    QJsonDocument {object}.toJson (QJsonDocument::Compact)));
}

void HunterRemoteServer::broadcastState ()
{
  if (!stateProvider_) return;
  auto state = stateProvider_ ();
  state.insert (QStringLiteral ("type"), QStringLiteral ("state"));

  for (auto it = clients_.cbegin (); it != clients_.cend (); ++it)
    if (it.value ())
      sendJson (it.key (), state);
}

void HunterRemoteServer::broadcastSpectrum ()
{
  if (!spectrumProvider_) return;
  auto spectrum = spectrumProvider_ ();
  spectrum.insert (QStringLiteral ("type"), QStringLiteral ("spectrum"));

  for (auto it = clients_.cbegin (); it != clients_.cend (); ++it)
    if (it.value ())
      sendJson (it.key (), spectrum);
}
