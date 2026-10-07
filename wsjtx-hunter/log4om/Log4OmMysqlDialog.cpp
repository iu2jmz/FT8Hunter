#include "Log4OmMysqlDialog.hpp"
#include "Log4OmMysql.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

Log4OmMysqlDialog::Log4OmMysqlDialog (QWidget * parent)
  : QDialog {parent}
  , enabled_ {new QCheckBox {tr ("Usa il database MySQL di Log4OM come log worked-before"), this}}
  , host_ {new QLineEdit {this}}
  , port_ {new QSpinBox {this}}
  , database_ {new QLineEdit {this}}
  , username_ {new QLineEdit {this}}
  , password_ {new QLineEdit {this}}
  , station_ {new QLineEdit {this}}
{
  setWindowTitle (tr ("FT8 Hunter - Log4OM MySQL"));
  setMinimumWidth (520);

  port_->setRange (1, 65535);
  password_->setEchoMode (QLineEdit::Password);
  station_->setPlaceholderText (tr ("opzionale, es. IU2JMZ"));

  auto * form = new QFormLayout;
  form->addRow (tr ("Server / IP:"), host_);
  form->addRow (tr ("Porta:"), port_);
  form->addRow (tr ("Database:"), database_);
  form->addRow (tr ("Utente:"), username_);
  form->addRow (tr ("Password:"), password_);
  form->addRow (tr ("Station callsign:"), station_);

  auto * note = new QLabel {
    tr ("FT8 Hunter accede al database in sola lettura. La tabella Log4OM attesa e' 'log'.\n"
        "Per un database su un altro PC, MySQL deve accettare connessioni dalla rete locale "
        "e il firewall deve consentire la porta configurata."), this};
  note->setWordWrap (true);

  auto * testButton = new QPushButton {tr ("Test connessione e lettura"), this};
  connect (testButton, &QPushButton::clicked, this, &Log4OmMysqlDialog::test);

  auto * buttons = new QDialogButtonBox {QDialogButtonBox::Save | QDialogButtonBox::Cancel, this};
  connect (buttons, &QDialogButtonBox::accepted, [this] {
      save ();
      accept ();
    });
  connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto * layout = new QVBoxLayout {this};
  layout->addWidget (enabled_);
  layout->addLayout (form);
  layout->addWidget (note);
  layout->addWidget (testButton);
  layout->addWidget (buttons);

  load ();
}

void Log4OmMysqlDialog::load ()
{
  auto const s = Log4OmMysql::loadSettings ();
  enabled_->setChecked (s.enabled);
  host_->setText (s.host);
  port_->setValue (s.port);
  database_->setText (s.database);
  username_->setText (s.username);
  password_->setText (s.password);
  station_->setText (s.stationCallsign);
}

void Log4OmMysqlDialog::save ()
{
  Log4OmMysqlSettings s;
  s.enabled = enabled_->isChecked ();
  s.host = host_->text ().trimmed ();
  s.port = port_->value ();
  s.database = database_->text ().trimmed ();
  s.username = username_->text ().trimmed ();
  s.password = password_->text ();
  s.stationCallsign = station_->text ().trimmed ().toUpper ();
  Log4OmMysql::saveSettings (s);
}

void Log4OmMysqlDialog::test ()
{
  Log4OmMysqlSettings s;
  s.enabled = enabled_->isChecked ();
  s.host = host_->text ().trimmed ();
  s.port = port_->value ();
  s.database = database_->text ().trimmed ();
  s.username = username_->text ().trimmed ();
  s.password = password_->text ();
  s.stationCallsign = station_->text ().trimmed ().toUpper ();

  auto const r = Log4OmMysql::testConnection (s);
  if (r.ok)
    {
      QMessageBox::information (
        this, tr ("Log4OM MySQL"),
        tr ("Connessione riuscita.\nQSO letti: %1\nServer: %2")
          .arg (r.qsoCount)
          .arg (r.serverVersion));
    }
  else
    {
      QMessageBox::critical (
        this, tr ("Log4OM MySQL"),
        tr ("Connessione o lettura fallita:\n%1").arg (r.error));
    }
}
