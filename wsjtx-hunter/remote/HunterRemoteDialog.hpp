#pragma once

#include "HunterRemoteServer.hpp"

#include <QDialog>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

class HunterRemoteDialog final : public QDialog
{
public:
  explicit HunterRemoteDialog (HunterRemoteSettings settings, QWidget * parent = nullptr);
  HunterRemoteSettings settings () const;

private:
  void refreshSummary ();
  void regenerateToken ();

  QCheckBox * enabled_;
  QCheckBox * lanAccess_;
  QCheckBox * allowTransmit_;
  QSpinBox * httpPort_;
  QSpinBox * webSocketPort_;
  QLineEdit * token_;
  QLabel * url_;
  QPushButton * regenerate_;
};
