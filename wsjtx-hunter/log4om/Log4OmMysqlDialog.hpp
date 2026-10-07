#pragma once

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QSpinBox;

class Log4OmMysqlDialog final : public QDialog
{
  Q_OBJECT

public:
  explicit Log4OmMysqlDialog (QWidget * parent = nullptr);

private:
  void load ();
  void save ();
  void test ();

  QCheckBox * enabled_;
  QLineEdit * host_;
  QSpinBox * port_;
  QLineEdit * database_;
  QLineEdit * username_;
  QLineEdit * password_;
  QCheckBox * verifyTlsCertificate_;
  QLineEdit * station_;
};
