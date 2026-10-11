#pragma once

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
  static constexpr int StopTransmission = -2;

  using Starter = std::function<bool (MonitorHunterCandidate const&)>;
  using AbortHandler = std::function<void ()>;

  MonitorHunterPanel (QString const& myCall,
                      Starter starter,
                      AbortHandler abortHandler,
                      QSettings * settings,
                      QWidget * parent = nullptr);

  void openWindow ();
  void setFilterActive (bool active);

  void observeDecode (QString const& rawText,
                      QString const& cleanText,
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
  int nextTxPowerPercent ();
  void callingExhausted ();
  void qsoLogged (QString const& call);

protected:
  void closeEvent (QCloseEvent * event) override;

private:
  enum class HuntState
  {
    Idle,
    Calling,
    Qso
  };

  void enqueueCandidate (MonitorHunterCandidate const& candidate);
  void attemptStart ();
  void pruneQueue ();
  void resetActive (QString const& status, bool abortRadio, bool cooldown);
  void appendEvent (MonitorHunterCandidate const& candidate, QString const& state);
  void updateSummary ();
  bool inCooldown (QString const& call, QDateTime const& now) const;

  QString myCall_;
  Starter starter_;
  AbortHandler abortHandler_;
  QSettings * settings_;

  QCheckBox * autoCheck_;
  QLabel * filterLabel_;
  QLabel * stateLabel_;
  QLabel * activeLabel_;
  QLabel * queueLabel_;
  QTableWidget * table_;
  QPushButton * stopButton_;
  QPushButton * clearButton_;
  QTimer * retryTimer_;

  bool filterActive_ {false};
  HuntState state_ {HuntState::Idle};
  MonitorHunterCandidate active_;
  QList<MonitorHunterCandidate> queue_;
  QHash<QString, QDateTime> cooldownUntil_;
  bool answered_ {false};
  int txCycles_ {0};
  int lastPowerPercent_ {50};
  quint64 completedQsos_ {0};
  quint64 failedCalls_ {0};
};
