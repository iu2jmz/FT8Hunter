#pragma once

#include <QString>
#include <QVector>

struct Log4OmMysqlSettings
{
  bool enabled {false};
  QString host {"127.0.0.1"};
  int port {3306};
  QString database {"log4om2"};
  QString username;
  QString password;
  bool verifyTlsCertificate {false};
  QString stationCallsign;
};

struct Log4OmQsoRow
{
  QString callsign;
  QString gridsquare;
  QString band;
  QString mode;
};

struct Log4OmMysqlTestResult
{
  bool ok {false};
  qint64 qsoCount {0};
  QString serverVersion;
  QString error;
};

class Log4OmMysql final
{
public:
  static QString settingsPath ();
  static Log4OmMysqlSettings loadSettings ();
  static void saveSettings (Log4OmMysqlSettings const& settings);

  static Log4OmMysqlTestResult testConnection (Log4OmMysqlSettings const& settings);
  static QVector<Log4OmQsoRow> loadWorkedQsos (Log4OmMysqlSettings const& settings,
                                                QString * error = nullptr);

private:
  static QString connectionName ();
};
