#pragma once

#include <QString>
#include <QRegularExpression>

class HunterQsoWatchdog
{
public:
  static constexpr int MaxCallingTransmissions = 8;
  static constexpr int MaxNoProgressTransmissions = 4;
  static constexpr int BusyTimeoutSeconds = 90;

  enum class DecodeEvent
  {
    None,
    TargetCq,
    TargetToMe,
    TargetToOther
  };

  enum class StopReason
  {
    None,
    CallingLimit,
    NoProgressLimit
  };

  struct TxResult
  {
    bool allow {false};
    int powerPercent {-1};
    int callingTransmission {0};
    int noProgressTransmission {0};
    StopReason stopReason {StopReason::None};
  };

  void reset (QString const& target, QString const& myCall)
  {
    target_ = normalizeCall (target);
    myCall_ = normalizeCall (myCall);
    answered_ = false;
    callingTransmissions_ = 0;
    noProgressTransmissions_ = 0;
    lockedPowerPercent_ = 50;
    lastTxStep_.clear ();
  }

  void clear ()
  {
    reset (QString {}, QString {});
  }

  DecodeEvent observe (QString const& deCall,
                       QString const& cqCall,
                       QString const& cleanText)
  {
    if (target_.isEmpty ()) return DecodeEvent::None;

    auto const cq = normalizeCall (cqCall);
    if (!cq.isEmpty () && cq == target_)
      return DecodeEvent::TargetCq;

    auto const de = normalizeCall (deCall);
    if (de != target_) return DecodeEvent::None;

    if (!myCall_.isEmpty () && containsCall (cleanText, myCall_))
      {
        answered_ = true;
        // Qualunque nuovo messaggio valido del DX verso di noi rappresenta
        // progresso/ricezione e riapre il budget di quattro TX.
        noProgressTransmissions_ = 0;
        lastTxStep_.clear ();
        return DecodeEvent::TargetToMe;
      }

    return DecodeEvent::TargetToOther;
  }

  TxResult beforeTransmit (QString const& txMessage)
  {
    TxResult out;

    if (target_.isEmpty ()) return out;

    if (!answered_)
      {
        if (callingTransmissions_ >= MaxCallingTransmissions)
          {
            out.stopReason = StopReason::CallingLimit;
            return out;
          }

        out.allow = true;
        out.callingTransmission = ++callingTransmissions_;
        lockedPowerPercent_ = 50 + 5 * (callingTransmissions_ - 1);
        out.powerPercent = lockedPowerPercent_;
        return out;
      }

    auto const step = txMessage.trimmed ().toUpper ().simplified ();
    if (step != lastTxStep_)
      {
        lastTxStep_ = step;
        noProgressTransmissions_ = 0;
      }

    if (noProgressTransmissions_ >= MaxNoProgressTransmissions)
      {
        out.stopReason = StopReason::NoProgressLimit;
        return out;
      }

    out.allow = true;
    out.powerPercent = lockedPowerPercent_;
    out.callingTransmission = callingTransmissions_;
    out.noProgressTransmission = ++noProgressTransmissions_;
    return out;
  }

  bool answered () const { return answered_; }
  int callingTransmissions () const { return callingTransmissions_; }
  int noProgressTransmissions () const { return noProgressTransmissions_; }
  int powerPercent () const { return lockedPowerPercent_; }
  QString target () const { return target_; }

private:
  static QString normalizeCall (QString call)
  {
    return call.trimmed ().remove ('<').remove ('>').toUpper ();
  }

  static bool containsCall (QString text, QString const& call)
  {
    text = text.toUpper ();
    text.remove ('<');
    text.remove ('>');
    text.replace (QRegularExpression {QStringLiteral ("[^A-Z0-9/]+")}, QStringLiteral (" "));
    text = QStringLiteral (" ") + text.simplified () + QStringLiteral (" ");
    return text.contains (QStringLiteral (" ") + call + QStringLiteral (" "));
  }

  QString target_;
  QString myCall_;
  bool answered_ {false};
  int callingTransmissions_ {0};
  int noProgressTransmissions_ {0};
  int lockedPowerPercent_ {50};
  QString lastTxStep_;
};
