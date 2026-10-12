#pragma once

#include <QString>

class QApplication;
class QSettings;

class HunterTheme final
{
public:
  enum class Mode { Dark, Light };

  static Mode load (QSettings * settings);
  static void save (QSettings * settings, Mode mode);
  static void apply (QApplication * app, Mode mode);
  static QString name (Mode mode);
};
