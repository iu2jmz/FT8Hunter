#include "HunterRemoteDialog.hpp"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QUuid>
#include <QVBoxLayout>

HunterRemoteDialog::HunterRemoteDialog (HunterRemoteSettings settings, QWidget * parent)
  : QDialog {parent}
  , enabled_ {new QCheckBox {tr ("Attiva FT8 Hunter Remote"), this}}
  , lanAccess_ {new QCheckBox {tr ("Consenti accesso dalla LAN / VPN"), this}}
  , allowTransmit_ {new QCheckBox {tr ("Consenti comandi che possono avviare la TX"), this}}
  , httpPort_ {new QSpinBox {this}}
  , webSocketPort_ {new QSpinBox {this}}
  , token_ {new QLineEdit {this}}
  , url_ {new QLabel {this}}
  , regenerate_ {new QPushButton {tr ("Rigenera token"), this}}
{
  setWindowTitle (tr ("FT8 Hunter Remote 2.0.0"));
  setModal (true);
  setMinimumWidth (620);

  httpPort_->setRange (1024, 65535);
  webSocketPort_->setRange (1024, 65535);
  httpPort_->setValue (settings.httpPort);
  webSocketPort_->setValue (settings.webSocketPort);
  enabled_->setChecked (settings.enabled);
  lanAccess_->setChecked (settings.lanAccess);
  allowTransmit_->setChecked (settings.allowTransmit);

  if (settings.token.trimmed ().isEmpty ())
    settings.token = QUuid::createUuid ().toString (QUuid::WithoutBraces).remove (QLatin1Char ('-'));
  token_->setText (settings.token);
  token_->setReadOnly (true);

  auto * tokenRow = new QHBoxLayout;
  tokenRow->addWidget (token_, 1);
  tokenRow->addWidget (regenerate_);

  auto * form = new QFormLayout;
  form->addRow (enabled_);
  form->addRow (lanAccess_);
  form->addRow (allowTransmit_);
  form->addRow (tr ("Porta Web App:"), httpPort_);
  form->addRow (tr ("Porta WebSocket:"), webSocketPort_);
  form->addRow (tr ("Token di accesso:"), tokenRow);
  form->addRow (tr ("Indirizzo:"), url_);

  auto * note = new QLabel {
    tr ("Sicurezza: per l'accesso fuori casa usa una VPN (es. Tailscale/WireGuard). "
        "Non esporre direttamente le porte di FT8 Hunter su Internet. "
        "La TX remota resta disabilitata finche' non la autorizzi esplicitamente."), this};
  note->setWordWrap (true);

  auto * buttons = new QDialogButtonBox {
    QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this};

  auto * root = new QVBoxLayout;
  root->addLayout (form);
  root->addWidget (note);
  root->addWidget (buttons);
  setLayout (root);

  connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect (regenerate_, &QPushButton::clicked, this, [this] { regenerateToken (); });
  connect (lanAccess_, &QCheckBox::toggled, this, [this] { refreshSummary (); });
  connect (httpPort_, QOverload<int>::of (&QSpinBox::valueChanged), this, [this] { refreshSummary (); });
  connect (webSocketPort_, QOverload<int>::of (&QSpinBox::valueChanged), this, [this] { refreshSummary (); });

  refreshSummary ();
}

HunterRemoteSettings HunterRemoteDialog::settings () const
{
  HunterRemoteSettings out;
  out.enabled = enabled_->isChecked ();
  out.lanAccess = lanAccess_->isChecked ();
  out.allowTransmit = allowTransmit_->isChecked ();
  out.httpPort = static_cast<quint16> (httpPort_->value ());
  out.webSocketPort = static_cast<quint16> (webSocketPort_->value ());
  out.token = token_->text ().trimmed ();
  return out;
}

void HunterRemoteDialog::refreshSummary ()
{
  HunterRemoteSettings s = settings ();
  url_->setText (
    QStringLiteral ("%1   |   WS: %2:%3")
      .arg (HunterRemoteServer::appUrl (s))
      .arg (s.lanAccess ? QStringLiteral ("IP-DEL-PC") : QStringLiteral ("127.0.0.1"))
      .arg (s.webSocketPort));
}

void HunterRemoteDialog::regenerateToken ()
{
  token_->setText (
    QUuid::createUuid ().toString (QUuid::WithoutBraces).remove (QLatin1Char ('-')));
}
