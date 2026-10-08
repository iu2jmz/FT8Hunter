#pragma once

#include <QDockWidget>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVariant>
#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSettings;
class QSpinBox;
class QTableWidget;
class QTimer;
class DxFunClusterClient;

struct DxFunEvaluation
{
  bool digital {false};
  QString mode;
  QString band;
  QString country;
  quint64 spotHz {0};
  quint64 dialHz {0};
  int offsetHz {0};
  bool newDxcc {false};
  bool newBand {false};
};

struct DxFunTuneRequest
{
  QString callsign;
  QString mode;
  QString band;
  QString country;
  quint64 spotHz {0};
  quint64 dialHz {0};
  int offsetHz {0};
  QString status;
};

class DxFunClusterPanel final : public QDockWidget
{
public:
  enum class DecodeAction
  {
    None,
    StartQso,
    CompleteQso
  };

  using Evaluator = std::function<DxFunEvaluation (QString const&, quint64)>;
  using Tuner = std::function<bool (DxFunTuneRequest const&)>;
  using AbortHandler = std::function<void ()>;

  DxFunClusterPanel (QString const& defaultCallsign,
                     Evaluator evaluator,
                     Tuner tuner,
                     AbortHandler abortHandler,
                     QSettings * settings,
                     QWidget * parent = nullptr);

  void openAndConnect ();

  DecodeAction observeDecode (QString const& deCall,
                              QString const& cleanText,
                              int audioOffset,
                              QString const& myCall);
  bool qsoActive () const;
  int nextTxPowerPercent ();
  void qsoLogged ();

private:
  enum class HuntState
  {
    Idle,
    Listening,
    Qso
  };

  struct Candidate
  {
    DxFunTuneRequest request;
    int priority {0};
    QDateTime receivedUtc;
  };

  void setWanted (bool wanted);
  void reconnectForCallsign ();
  void handleSpot (QString const& callsign, quint64 frequencyHz,
                   QString const& comment, QString const& spotter,
                   QDateTime const& receivedUtc);
  void appendSpot (Candidate const& candidate, QString const& comment, QString const& spotter,
                   bool relevant);
  void queueCandidate (Candidate const& candidate);
  void attemptAutoQsy ();
  void tuneSelected ();
  void beginHunt (Candidate const& candidate);
  void listenTimeout ();
  void qsoTimeout ();
  void resetHunt (QString const& status, bool abortRadio);
  void updateCounters ();
  void saveOption (QString const& key, QVariant const& value);

  QString defaultCallsign_;
  Evaluator evaluator_;
  Tuner tuner_;
  AbortHandler abortHandler_;
  QSettings * settings_;
  DxFunClusterClient * client_;

  QLineEdit * clusterCall_;
  QPushButton * connectButton_;
  QPushButton * autoQsyButton_;
  QCheckBox * newDxccCheck_;
  QCheckBox * newBandCheck_;
  QCheckBox * ft8Check_;
  QCheckBox * ft4Check_;
  QCheckBox * showWorkedCheck_;
  QSpinBox * holdSeconds_;
  QLabel * statusLabel_;
  QLabel * countersLabel_;
  QTableWidget * table_;
  QPushButton * goButton_;
  QPushButton * clearButton_;
  QTimer * settleTimer_;
  QTimer * listenTimer_;
  QTimer * qsoTimer_;

  Candidate pending_;
  bool pendingValid_ {false};
  Candidate active_;
  HuntState huntState_ {HuntState::Idle};
  bool answered_ {false};
  int txCycles_ {0};
  int lastPowerPercent_ {50};
  QDateTime lastQsyUtc_;
  QHash<QString, QDateTime> seen_;
  quint64 allSpots_ {0};
  quint64 digitalSpots_ {0};
  quint64 candidateSpots_ {0};
};
