#pragma once

#include <QDockWidget>
#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVariant>
#include <functional>

class QCheckBox;
class QLabel;
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
  using Evaluator = std::function<DxFunEvaluation (QString const&, quint64)>;
  using Tuner = std::function<bool (DxFunTuneRequest const&)>;

  DxFunClusterPanel (QString const& callsign,
                     Evaluator evaluator,
                     Tuner tuner,
                     QSettings * settings,
                     QWidget * parent = nullptr);

  void openAndConnect ();

private:
  struct Candidate
  {
    DxFunTuneRequest request;
    int priority {0};
    QDateTime receivedUtc;
  };

  void setWanted (bool wanted);
  void handleSpot (QString const& callsign, quint64 frequencyHz,
                   QString const& comment, QString const& spotter,
                   QDateTime const& receivedUtc);
  void appendSpot (Candidate const& candidate, QString const& comment, QString const& spotter,
                   bool relevant);
  void queueCandidate (Candidate const& candidate);
  void attemptAutoQsy ();
  void tuneSelected ();
  void updateCounters ();
  void saveOption (QString const& key, QVariant const& value);

  QString callsign_;
  Evaluator evaluator_;
  Tuner tuner_;
  QSettings * settings_;
  DxFunClusterClient * client_;

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

  Candidate pending_;
  bool pendingValid_ {false};
  QDateTime lastQsyUtc_;
  QHash<QString, QDateTime> seen_;
  quint64 allSpots_ {0};
  quint64 digitalSpots_ {0};
  quint64 candidateSpots_ {0};
};
