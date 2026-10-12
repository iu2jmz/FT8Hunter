#pragma once

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QString>
#include <QTextStream>

namespace HunterStartupTrace
{
  inline QString path ()
  {
    auto dir = QStandardPaths::writableLocation (QStandardPaths::AppLocalDataLocation);
    if (dir.trimmed ().isEmpty ())
      dir = QStandardPaths::writableLocation (QStandardPaths::TempLocation);
    QDir {}.mkpath (dir);
    return QDir {dir}.absoluteFilePath (QStringLiteral ("ft8hunter-startup.log"));
  }

  inline void clear ()
  {
    QFile f {path ()};
    if (f.open (QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
      {
        QTextStream s {&f};
        s << QDateTime::currentDateTime ().toString (Qt::ISODateWithMs)
          << "  FT8 Hunter startup trace begin\n";
      }
  }

  inline void write (QString const& message)
  {
    QFile f {path ()};
    if (f.open (QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
      {
        QTextStream s {&f};
        s << QDateTime::currentDateTime ().toString (Qt::ISODateWithMs)
          << "  " << message << "\n";
      }
  }
}
