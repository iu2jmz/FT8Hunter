#pragma once

#include "HunterQsoWatchdog.hpp"

#include <QDateTime>
#include <QDialog>
#include <QHash>
#include <QList>
#include <QString>
#include <functional>

class QCheckBox;
class QCloseEvent;
class QLabel;
class QPushButton;
class QSettings;
class QTableWidget;
class QTimer;

struct MonitorHunterCandidate
{
  QString callsign;
  QString grid;
  QString country;
  QString mode;
  QString band;
  QString rawText;
  int audioOffset {0};
  int snr {0};
  QDateTime receivedUtc;
};

class MonitorHunterPanel final : public QDialog
{
public:
  static constexpr int StartUnavailable = -1;
  static constexpr int StopTransmission = -2;

  using Starter = std::function<int (MonitorHunterCandidate const&, bool autoTxSlot)>;
  using PauseHandler = std::function<void ()>;
  using ResumeHandler = std::function<bool (QString const& rawText, int audioOffset)>;
  using AbortHandler = std::function<void ()>;
  using ReleaseHandler = std::function<void ()>;

  MonitorHunterPanel (QString const& myCall,
                      Starter starter,
                      PauseHandler pauseHandler,
                      ResumeHandler resumeHandler,
                      AbortHandler abortHandler,
                      ReleaseHandler releaseHandler,
                      QSettings * settings,
                      QWidget * parent = nullptr);

  void openWindow ();
  void setFilterActive (bool active);

  void observeDecode (QString const& rawText,
                      QString const& cleanText,
                      QString const& firstCall,
                      QString const& cqCall,
                      QString const& deCall,
                      QString const& grid,
                      int audioOffset,
                      int snr,
                      QString const& mode,
                      QString const& band,
                      QString const& country,
                      bool eligibleCq);

  bool qsoActive () const;
  bool huntActive () const;
  int nextTxPowerPercent (QString const& txMessage);
  void transmissionExhausted ();
  void qsoLogged (QString const& call);

protected:
  void closeEvent (QCloseEvent * event) override;

private:
  enum class HuntState
  {
    Idle,
    Calling,
    Busy,
    Qso
  };

  void enqueueCandidate (MonitorHunterCandidate const& candidate);
  void attemptStart ();
  void pruneQueue ();
  void enterBusy (QString const& otherCall);
  void busyTimeout ();
  void resetActive (QString const& status, bool abortRadio, bool cooldown);
  void appendEvent (MonitorHunterCandidate const& candidate, QString const& state);
  void updateSummary ();
  bool inCooldown (QString const& call, QDateTime const& now) const;
  void showOffState (QString const& detail);
  void showListeningState (QString const& detail);
  void showCallingState (QString const& detail);
  void showBusyState (QString const& detail);
  void showQsoState (QString const& detail);
  void updateActiveLabels ();

  QString myCall_;
  Starter starter_;
  PauseHandler pauseHandler_;
  ResumeHandler resumeHandler_;
  AbortHandler abortHandler_;
  ReleaseHandler releaseHandler_;
  QSettings * settings_;

  QCheckBox * autoCheck_;
  QCheckBox * autoTxSlotCheck_;
  QLabel * filterLabel_;
  QLabel * stateLabel_;
  QLabel * targetLabel_;
  QLabel * txSlotLabel_;
  QLabel * progressLabel_;
  QLabel * queueLabel_;
  QTableWidget * table_;
  QPushButton * stopButton_;
  QPushButton * clearButton_;
  QTimer * retryTimer_;
  QTimer * busyTimer_;

  bool filterActive_ {false};
  HuntState state_ {HuntState::Idle};
  MonitorHunterCandidate active_;
  QList<MonitorHunterCandidate> queue_;
  QHash<QString, QDateTime> cooldownUntil_;
  HunterQsoWatchdog watchdog_;
  HunterQsoWatchdog::StopReason pendingStopReason_ {HunterQsoWatchdog::StopReason::None};
  int lastPowerPercent_ {50};
  int lockedTxOffset_ {-1};
  bool lockedTxWasAutomatic_ {false};
  quint64 completedQsos_ {0};
  quint64 failedCalls_ {0};
};
