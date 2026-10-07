#include "Log4OmMysql.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>

namespace
{
  QString options (Log4OmMysqlSettings const& settings)
  {
    return QStringLiteral(
      "MYSQL_OPT_CONNECT_TIMEOUT=3;"
      "MYSQL_OPT_RECONNECT=1;"
      "MYSQL_SET_CHARSET_NAME=utf8mb4;"
      "MYSQL_OPT_SSL_VERIFY_SERVER_CERT=%1")
      .arg (settings.verifyTlsCertificate ? 1 : 0);
  }

  void configure (QSqlDatabase& db, Log4OmMysqlSettings const& settings)
  {
    db.setHostName (settings.host.trimmed ());
    db.setPort (settings.port);
    db.setDatabaseName (settings.database.trimmed ());
    db.setUserName (settings.username.trimmed ());
    db.setPassword (settings.password);
    db.setConnectOptions (options (settings));
  }

  QString filterSql (Log4OmMysqlSettings const& settings)
  {
    return settings.stationCallsign.trimmed ().isEmpty ()
      ? QString {}
      : QStringLiteral(" WHERE stationcallsign = ?");
  }
}

QString Log4OmMysql::connectionName ()
{
  return QStringLiteral("ft8hunter-log4om-%1")
    .arg (QUuid::createUuid ().toString (QUuid::WithoutBraces));
}

QString Log4OmMysql::settingsPath ()
{
  auto dir = QStandardPaths::writableLocation (QStandardPaths::AppConfigLocation);
  QDir {}.mkpath (dir);
  return QDir {dir}.absoluteFilePath (QStringLiteral("ft8hunter-log4om.ini"));
}

Log4OmMysqlSettings Log4OmMysql::loadSettings ()
{
  QSettings s {settingsPath (), QSettings::IniFormat};
  s.beginGroup (QStringLiteral("Log4OM"));
  Log4OmMysqlSettings out;
  out.enabled = s.value (QStringLiteral("enabled"), false).toBool ();
  out.host = s.value (QStringLiteral("host"), QStringLiteral("127.0.0.1")).toString ();
  out.port = s.value (QStringLiteral("port"), 3306).toInt ();
  out.database = s.value (QStringLiteral("database"), QStringLiteral("log4om2")).toString ();
  out.username = s.value (QStringLiteral("username")).toString ();
  out.password = s.value (QStringLiteral("password")).toString ();
  out.verifyTlsCertificate = s.value (QStringLiteral("verifyTlsCertificate"), false).toBool ();
  out.stationCallsign = s.value (QStringLiteral("stationCallsign")).toString ().trimmed ().toUpper ();
  s.endGroup ();
  return out;
}

void Log4OmMysql::saveSettings (Log4OmMysqlSettings const& settings)
{
  QSettings s {settingsPath (), QSettings::IniFormat};
  s.beginGroup (QStringLiteral("Log4OM"));
  s.setValue (QStringLiteral("enabled"), settings.enabled);
  s.setValue (QStringLiteral("host"), settings.host.trimmed ());
  s.setValue (QStringLiteral("port"), settings.port);
  s.setValue (QStringLiteral("database"), settings.database.trimmed ());
  s.setValue (QStringLiteral("username"), settings.username.trimmed ());
  s.setValue (QStringLiteral("password"), settings.password);
  s.setValue (QStringLiteral("verifyTlsCertificate"), settings.verifyTlsCertificate);
  s.setValue (QStringLiteral("stationCallsign"), settings.stationCallsign.trimmed ().toUpper ());
  s.endGroup ();
  s.sync ();
}

Log4OmMysqlTestResult Log4OmMysql::testConnection (Log4OmMysqlSettings const& settings)
{
  Log4OmMysqlTestResult result;
  auto const name = connectionName ();

  {
    auto db = QSqlDatabase::addDatabase (QStringLiteral("QMYSQL"), name);
    if (!db.isValid ())
      {
        result.error = QStringLiteral("Driver QMYSQL non disponibile. Driver presenti: %1")
                         .arg (QSqlDatabase::drivers ().join (QStringLiteral(", ")));
      }
    else
      {
        configure (db, settings);
        if (!db.open ())
          {
            result.error = db.lastError ().text ();
          }
        else
          {
            result.serverVersion = db.driverName ();
            QSqlQuery q {db};
            auto sql = QStringLiteral("SELECT COUNT(*) FROM log") + filterSql (settings);
            q.prepare (sql);
            if (!settings.stationCallsign.trimmed ().isEmpty ())
              q.addBindValue (settings.stationCallsign.trimmed ().toUpper ());

            if (!q.exec ())
              {
                result.error = q.lastError ().text ();
              }
            else if (!q.next ())
              {
                result.error = QStringLiteral("La query Log4OM non ha restituito risultati.");
              }
            else
              {
                result.ok = true;
                result.qsoCount = q.value (0).toLongLong ();
                QSqlQuery versionQuery {db};
                if (versionQuery.exec (QStringLiteral("SELECT VERSION()")) && versionQuery.next ())
                  result.serverVersion = versionQuery.value (0).toString ();
              }
            db.close ();
          }
      }
  }

  QSqlDatabase::removeDatabase (name);
  return result;
}

QVector<Log4OmQsoRow> Log4OmMysql::loadWorkedQsos (Log4OmMysqlSettings const& settings,
                                                    QString * error)
{
  QVector<Log4OmQsoRow> rows;
  if (error) error->clear ();

  auto const name = connectionName ();
  {
    auto db = QSqlDatabase::addDatabase (QStringLiteral("QMYSQL"), name);
    if (!db.isValid ())
      {
        if (error)
          *error = QStringLiteral("Driver QMYSQL non disponibile. Driver presenti: %1")
                     .arg (QSqlDatabase::drivers ().join (QStringLiteral(", ")));
      }
    else
      {
        configure (db, settings);
        if (!db.open ())
          {
            if (error) *error = db.lastError ().text ();
          }
        else
          {
            QSqlQuery q {db};
            q.setForwardOnly (true);
            auto sql = QStringLiteral(
              "SELECT callsign, gridsquare, band, mode FROM log")
              + filterSql (settings)
              + QStringLiteral(" ORDER BY qsodate ASC");
            q.prepare (sql);
            if (!settings.stationCallsign.trimmed ().isEmpty ())
              q.addBindValue (settings.stationCallsign.trimmed ().toUpper ());

            if (!q.exec ())
              {
                if (error) *error = q.lastError ().text ();
              }
            else
              {
                while (q.next ())
                  {
                    Log4OmQsoRow row;
                    row.callsign = q.value (0).toString ().trimmed ().toUpper ();
                    row.gridsquare = q.value (1).toString ().trimmed ().left (4).toUpper ();
                    row.band = q.value (2).toString ().trimmed ().toUpper ();
                    row.mode = q.value (3).toString ().trimmed ().toUpper ();
                    if (!row.callsign.isEmpty ())
                      rows.push_back (std::move (row));
                  }
              }
            db.close ();
          }
      }
  }

  QSqlDatabase::removeDatabase (name);
  return rows;
}
