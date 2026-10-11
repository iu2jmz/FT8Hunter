#pragma once

#include <QDateTime>
#include <QDialog>
#include <QHash>
#include <QString>
#include <functional>

class QByteArray;
class QCheckBox;
class QCloseEvent;
class QLabel;
class QPushButton;
class QSettings;
class QSpinBox;
class QTableWidget;
class QTimer;
class QVariant;
class PskReporterMqttClient;

struct PskReporterReport
{
  QString callsign;
  QString grid;
  QString reporter;
  QString reporterGrid;
  QString mode;
  QString band;
  quint64 frequencyHz {0};
  int snr {0};
  QDateTime receivedUtc;
};

struct PskReporterEvaluation
{
  bool digital {false};
  QString country;
  QString mode;
  QString band;
  quint64 signalHz {0};
  quint64 dialHz {0};
  int offsetHz {0};
  bool newDxcc {false};
  bool newBand {false};
};

struct PskReporterTuneRequest
{
  QString callsign;
  QString country;
  QString mode;
  QString band;
  quint64 signalHz {0};
  quint64 dialHz {0};
  int offsetHz {0};
  QString status;
};

class PskReporterPanel final : public QDialog
{
public:
  using Evaluator = std::function<PskReporterEvaluation (PskReporterReport const&)>;
  using Tuner = std::function<bool (PskReporterTuneRequest const&)>;
  using Handoff = std::function<bool (QString const& rawText,
                                      QString const& callsign,
                                      QString const& grid,
                                      QString const& country,
                                      QString const& mode,
                                      QString const& band,
                                      int audioOffset,
                                      int snr)>;

  PskReporterPanel (Evaluator evaluator,
                    Tuner tuner,
                    Handoff handoff,
                    QSettings * settings,
                    QWidget * parent = nullptr);

  void openWindow ();
  bool huntActive () const;
  void yieldToDxFun ();

  void observeLocalCq (QString const& rawText,
                       QString const& cqCall,
                       QString const& grid,
                       QString const& country,
                       QString const& mode,
                       QString const& band,
                       int audioOffset,
                       int snr,
                       bool eligibleCq);

protected:
  void closeEvent (QCloseEvent * event) override;

private:
  struct Candidate
  {
    PskReporterTuneRequest request;
    int priority {0};
    QDateTime receivedUtc;
    QString reporter;
    int snr {0};
  };

  void handleMessage (QString const& topic, QByteArray const& payload);
  void handleReport (PskReporterReport const& report);
  void appendReport (Candidate const& candidate, bool relevant);
  void queueCandidate (Candidate const& candidate);
  void attemptAutoQsy ();
  void beginListen (Candidate const& candidate);
  void listenTimeout ();
  void resetListen (QString const& status);
  void tuneSelected ();
  void updateCounters ();
  void saveOption (QString const& key, QVariant const& value);

  Evaluator evaluator_;
  Tuner tuner_;
  Handoff handoff_;
  QSettings * settings_;
  PskReporterMqttClient * client_;

  QCheckBox * connectCheck_;
  QCheckBox * autoQsyCheck_;
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

  Candidate pending_;
  bool pendingValid_ {false};
  Candidate active_;
  bool listening_ {false};
  QDateTime lastQsyUtc_;
  QHash<QString, QDateTime> seen_;
  quint64 reports_ {0};
  quint64 freshDigital_ {0};
  quint64 candidateReports_ {0};
};
