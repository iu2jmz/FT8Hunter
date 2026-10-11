#pragma once

#include <QDialog>
#include <QHash>
#include <QString>
#include <QStringList>
#include <functional>

class QCheckBox;
class QCloseEvent;
class QLabel;
class QSettings;
class QTimer;

class BandHunterPanel final : public QDialog
{
public:
  static constexpr int QsyUnavailable = -1;
  static constexpr int QsyBusy = 0;
  static constexpr int QsySuccess = 1;

  using QsyHandler = std::function<int (QString const& band)>;
  using BoolGetter = std::function<bool ()>;
  using MonitorSetter = std::function<bool (bool)>;

  BandHunterPanel (QsyHandler qsyHandler,
                   BoolGetter monitorEnabledGetter,
                   MonitorSetter monitorSetter,
                   BoolGetter monitorBusyGetter,
                   BoolGetter externalBusyGetter,
                   QSettings * settings,
                   QWidget * parent = nullptr);

  void openWindow ();
  bool active () const;
  void pauseForExternalHunter ();

protected:
  void closeEvent (QCloseEvent * event) override;

private:
  void startCycle ();
  void stopCycle (QString const& detail = QString {});
  void tick ();
  void tuneCurrentBand (bool resetElapsed);
  void stabilizationComplete ();
  void advanceBand ();
  QStringList selectedBands () const;
  QString nextSelectedBand () const;
  int dwellSeconds () const;
  bool nightHighBand () const;
  void updateLabels (QString const& state);
  void saveSettings ();

  QsyHandler qsyHandler_;
  BoolGetter monitorEnabledGetter_;
  MonitorSetter monitorSetter_;
  BoolGetter monitorBusyGetter_;
  BoolGetter externalBusyGetter_;
  QSettings * settings_;

  QCheckBox * enableCheck_;
  QLabel * stateLabel_;
  QLabel * bandLabel_;
  QLabel * timeLabel_;
  QLabel * nextLabel_;
  QHash<QString, QCheckBox *> bandChecks_;
  QTimer * tickTimer_;
  QTimer * stabilizeTimer_;

  QString currentBand_;
  int elapsedSeconds_ {0};
  bool monitorWasEnabled_ {false};
  bool stabilizing_ {false};
  bool externalPaused_ {false};
  bool stopping_ {false};
};
