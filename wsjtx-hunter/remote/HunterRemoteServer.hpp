#pragma once

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <functional>

class QTcpServer;
class QTcpSocket;
class QTimer;
class QWebSocket;
class QWebSocketServer;

struct HunterRemoteSettings
{
  bool enabled {false};
  bool lanAccess {false};
  bool allowTransmit {false};
  quint16 httpPort {8787};
  quint16 webSocketPort {8788};
  QString token;
};

class HunterRemoteServer final : public QObject
{
public:
  using StateProvider = std::function<QJsonObject ()>;
  using SpectrumProvider = std::function<QJsonObject ()>;
  using CommandHandler = std::function<QJsonObject (QJsonObject const&, bool allowTransmit)>;

  HunterRemoteServer (StateProvider stateProvider,
                      SpectrumProvider spectrumProvider,
                      CommandHandler commandHandler,
                      QObject * parent = nullptr);
  ~HunterRemoteServer () override;

  static HunterRemoteSettings loadSettings ();
  static void saveSettings (HunterRemoteSettings const& settings);
  static QString ensureToken (HunterRemoteSettings settings);
  static QString appUrl (HunterRemoteSettings const& settings);

  bool start (HunterRemoteSettings const& settings, QString * error = nullptr);
  void stop ();
  bool running () const;
  HunterRemoteSettings settings () const { return settings_; }

private:
  void acceptHttp ();
  void handleHttp (QTcpSocket * socket);
  void acceptWebSocket ();
  void handleWebSocketMessage (QWebSocket * socket, QString const& text);
  void sendJson (QWebSocket * socket, QJsonObject const& object);
  void broadcastState ();
  void broadcastSpectrum ();
  bool authenticated (QWebSocket * socket) const;
  QByteArray loadWebAsset (QString const& path, QByteArray * mime) const;
  QString webRoot () const;

  StateProvider stateProvider_;
  SpectrumProvider spectrumProvider_;
  CommandHandler commandHandler_;

  QTcpServer * httpServer_;
  QWebSocketServer * webSocketServer_;
  QTimer * stateTimer_;
  QTimer * spectrumTimer_;
  HunterRemoteSettings settings_;
  QHash<QWebSocket *, bool> clients_;
};
