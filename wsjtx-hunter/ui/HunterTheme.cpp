#include "HunterTheme.hpp"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QRegularExpression>
#include <QSettings>
#include <QStyleFactory>
#include <QWidget>

namespace
{
  void setWidgetStyle (QString const& objectName, QString const& style)
  {
    for (auto * widget : QApplication::allWidgets ())
      {
        if (widget && widget->objectName () == objectName)
          widget->setStyleSheet (style);
      }
  }

  void applySpecialWidgets (bool dark)
  {
    auto const monitor = dark
      ? QStringLiteral (R"QSS(
QPushButton{background:#102838;color:#dff7ff;border:1px solid #1f5a70;border-radius:8px;padding:6px 12px;font-weight:600;}
QPushButton:hover{background:#12384b;border-color:#27d7ff;}
QPushButton:checked{background:#13d98b;color:#031b13;border:1px solid #78ffc2;}
)QSS")
      : QStringLiteral (R"QSS(
QPushButton{background:#eaf5fa;color:#12364a;border:1px solid #9ccbdc;border-radius:8px;padding:6px 12px;font-weight:600;}
QPushButton:hover{background:#d9f1f8;border-color:#00a5ce;}
QPushButton:checked{background:#17c98a;color:#06251b;border:1px solid #0da26f;}
)QSS");

    auto const autoTx = dark
      ? QStringLiteral (R"QSS(
QPushButton{background:#2a1720;color:#ffdce3;border:1px solid #783147;border-radius:8px;padding:6px 12px;font-weight:700;}
QPushButton:hover{background:#3b1a27;border-color:#ff5577;}
QPushButton:checked{background:#ff315a;color:white;border:1px solid #ff7892;}
)QSS")
      : QStringLiteral (R"QSS(
QPushButton{background:#fff0f3;color:#7b2035;border:1px solid #e5a4b4;border-radius:8px;padding:6px 12px;font-weight:700;}
QPushButton:hover{background:#ffe3e9;border-color:#e24868;}
QPushButton:checked{background:#e93459;color:white;border:1px solid #be2343;}
)QSS");

    auto const stopTx = dark
      ? QStringLiteral (R"QSS(
QPushButton{background:#21151b;color:#ff9caf;border:1px solid #643143;border-radius:8px;padding:6px 12px;font-weight:700;}
QPushButton:hover{background:#421b28;color:#fff;border-color:#ff5b7b;}
QPushButton:pressed{background:#ff315a;color:white;}
)QSS")
      : QStringLiteral (R"QSS(
QPushButton{background:#fff4f6;color:#a12641;border:1px solid #e7afbb;border-radius:8px;padding:6px 12px;font-weight:700;}
QPushButton:hover{background:#ffe5eb;border-color:#dd4967;}
QPushButton:pressed{background:#e93459;color:white;}
)QSS");

    auto const tune = dark
      ? QStringLiteral (R"QSS(
QPushButton{background:#2b2414;color:#ffd980;border:1px solid #6f5a25;border-radius:8px;padding:6px 12px;font-weight:600;}
QPushButton:hover{background:#3a3018;border-color:#ffc94d;}
QPushButton:checked{background:#ffb020;color:#261900;border:1px solid #ffd071;}
)QSS")
      : QStringLiteral (R"QSS(
QPushButton{background:#fff8e8;color:#75520c;border:1px solid #e6ca8b;border-radius:8px;padding:6px 12px;font-weight:600;}
QPushButton:hover{background:#fff0c8;border-color:#d99a17;}
QPushButton:checked{background:#ffb020;color:#2a1b00;border:1px solid #d18a00;}
)QSS");

    auto const decode = dark
      ? QStringLiteral (R"QSS(
QPushButton{background:#102536;color:#d8f6ff;border:1px solid #20586f;border-radius:8px;padding:6px 12px;font-weight:600;}
QPushButton:hover{background:#12384d;border-color:#23cfff;}
QPushButton:checked{background:#00b7e8;color:#00171f;border:1px solid #6ceaff;}
)QSS")
      : QStringLiteral (R"QSS(
QPushButton{background:#eaf6fb;color:#17465c;border:1px solid #a2cede;border-radius:8px;padding:6px 12px;font-weight:600;}
QPushButton:hover{background:#d9f2fa;border-color:#009fc9;}
QPushButton:checked{background:#00a9d7;color:white;border:1px solid #0084aa;}
)QSS");

    auto const rig = dark
      ? QStringLiteral (R"QSS(
QPushButton{font-weight:800;background:#142331;color:#7ee7ff;border:1px solid #2d6277;border-radius:10px;min-width:20px;min-height:20px;max-width:20px;max-height:20px;padding:0;}
QPushButton[state="error"]{background:#ff315a;color:white;border-color:#ff91a6;}
QPushButton[state="warning"]{background:#ffb020;color:#261900;border-color:#ffd071;}
QPushButton[state="ok"]{background:#18d38a;color:#042015;border-color:#78ffc2;}
)QSS")
      : QStringLiteral (R"QSS(
QPushButton{font-weight:800;background:#edf7fb;color:#0f718c;border:1px solid #9bcbd9;border-radius:10px;min-width:20px;min-height:20px;max-width:20px;max-height:20px;padding:0;}
QPushButton[state="error"]{background:#e93459;color:white;border-color:#bd2846;}
QPushButton[state="warning"]{background:#ffb020;color:#2a1b00;border-color:#d18a00;}
QPushButton[state="ok"]{background:#16c985;color:#052217;border-color:#0b9c66;}
)QSS");

    auto const decodeTitle = dark
      ? QStringLiteral ("QLabel{background:#0b1c29;color:#76dfff;border:1px solid #17475c;border-radius:6px;padding:4px 8px;font-weight:700;}")
      : QStringLiteral ("QLabel{background:#e7f5fa;color:#10617b;border:1px solid #b3d7e3;border-radius:6px;padding:4px 8px;font-weight:700;}");

    auto const decodeHeading = dark
      ? QStringLiteral ("QLabel{background:#102737;color:#a9d8e9;border:1px solid #1e4b60;border-radius:4px;padding:3px 6px;font-family:Consolas,monospace;font-weight:600;}")
      : QStringLiteral ("QLabel{background:#edf7fb;color:#41697a;border:1px solid #bfd9e3;border-radius:4px;padding:3px 6px;font-family:Consolas,monospace;font-weight:600;}");

    setWidgetStyle (QStringLiteral ("monitorButton"), monitor);
    setWidgetStyle (QStringLiteral ("autoButton"), autoTx);
    setWidgetStyle (QStringLiteral ("stopTxButton"), stopTx);
    setWidgetStyle (QStringLiteral ("tuneButton"), tune);
    setWidgetStyle (QStringLiteral ("DecodeButton"), decode);
    setWidgetStyle (QStringLiteral ("readFreq"), rig);

    setWidgetStyle (QStringLiteral ("lh_decodes_title_label"), decodeTitle);
    setWidgetStyle (QStringLiteral ("rh_decodes_title_label"), decodeTitle);
    setWidgetStyle (QStringLiteral ("lh_decodes_headings_label"), decodeHeading);
    setWidgetStyle (QStringLiteral ("rh_decodes_headings_label"), decodeHeading);
  }
}

HunterTheme::Mode HunterTheme::load (QSettings * settings)
{
  if (!settings) return Mode::Dark;
  settings->beginGroup (QStringLiteral ("FT8HunterAppearance"));
  auto const value = settings->value (QStringLiteral ("theme"), QStringLiteral ("dark")).toString ();
  settings->endGroup ();
  return value.compare (QStringLiteral ("light"), Qt::CaseInsensitive) == 0
    ? Mode::Light : Mode::Dark;
}

void HunterTheme::save (QSettings * settings, Mode mode)
{
  if (!settings) return;
  settings->beginGroup (QStringLiteral ("FT8HunterAppearance"));
  settings->setValue (
    QStringLiteral ("theme"),
    mode == Mode::Dark ? QStringLiteral ("dark") : QStringLiteral ("light"));
  settings->endGroup ();
  settings->sync ();
}

QString HunterTheme::name (Mode mode)
{
  return mode == Mode::Dark
    ? QStringLiteral ("Dark Futuristic")
    : QStringLiteral ("Light Futuristic");
}

void HunterTheme::apply (QApplication * app, Mode mode)
{
  if (!app) return;

  if (auto * fusion = QStyleFactory::create (QStringLiteral ("Fusion")))
    app->setStyle (fusion);

  if (mode == Mode::Dark)
    {
      QPalette p;
      p.setColor (QPalette::Window, QColor (5, 13, 21));
      p.setColor (QPalette::WindowText, QColor (221, 242, 248));
      p.setColor (QPalette::Base, QColor (4, 12, 19));
      p.setColor (QPalette::AlternateBase, QColor (8, 25, 36));
      p.setColor (QPalette::ToolTipBase, QColor (9, 27, 38));
      p.setColor (QPalette::ToolTipText, QColor (229, 249, 255));
      p.setColor (QPalette::Text, QColor (221, 242, 248));
      p.setColor (QPalette::Button, QColor (12, 33, 46));
      p.setColor (QPalette::ButtonText, QColor (221, 242, 248));
      p.setColor (QPalette::BrightText, QColor (255, 74, 107));
      p.setColor (QPalette::Highlight, QColor (0, 183, 232));
      p.setColor (QPalette::HighlightedText, QColor (2, 20, 27));
      p.setColor (QPalette::Link, QColor (57, 218, 255));
      p.setColor (QPalette::Disabled, QPalette::Text, QColor (92, 119, 132));
      p.setColor (QPalette::Disabled, QPalette::ButtonText, QColor (92, 119, 132));
      app->setPalette (p);

      app->setStyleSheet (QStringLiteral (R"QSS(
*{font-family:"Segoe UI","Arial";font-size:9pt;}
QMainWindow,QDialog{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #050d15,stop:0.58 #081722,stop:1 #06111a);color:#ddf2f8;}
QDockWidget{background:#071622;color:#ddf2f8;border:1px solid #13384b;}
QDockWidget::title{background:#0b2130;color:#63dcff;border-bottom:1px solid #16485f;padding:7px 10px;font-weight:700;}
QMenuBar{background:#071521;color:#cae9f2;border-bottom:1px solid #123c50;padding:2px;}
QMenuBar::item{background:transparent;padding:5px 9px;border-radius:5px;}
QMenuBar::item:selected{background:#0e3345;color:#66e4ff;}
QMenu{background:#081925;color:#dff5fb;border:1px solid #1d4e63;border-radius:7px;padding:5px;}
QMenu::item{padding:6px 24px 6px 12px;border-radius:5px;}
QMenu::item:selected{background:#0f5169;color:#ffffff;}
QMenu::separator{height:1px;background:#173c4d;margin:5px 8px;}
QToolTip{background:#0d2735;color:#eaffff;border:1px solid #2c6f88;padding:5px;border-radius:4px;}

QGroupBox{background:#071722;border:1px solid #173c4f;border-radius:11px;margin-top:13px;padding:12px 8px 8px 8px;font-weight:700;}
QGroupBox::title{subcontrol-origin:margin;left:13px;padding:0 6px;color:#55d9ff;background:#071722;}

QPushButton,QToolButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #123043,stop:1 #0c2433);color:#e2f7fc;border:1px solid #23586e;border-radius:8px;padding:6px 11px;min-height:20px;}
QPushButton:hover,QToolButton:hover{background:#123c50;border-color:#2fd9ff;color:white;}
QPushButton:pressed,QToolButton:pressed{background:#0b637d;border-color:#62e7ff;}
QPushButton:checked,QToolButton:checked{background:#087fa0;border-color:#59e4ff;color:white;}
QPushButton:disabled,QToolButton:disabled{background:#0b1720;color:#536d79;border-color:#1a303b;}

QLineEdit,QSpinBox,QDoubleSpinBox,QComboBox,QDateEdit,QTimeEdit{background:#071722;color:#e6f7fb;border:1px solid #28566a;border-radius:7px;padding:4px 7px;min-height:20px;selection-background-color:#0baed7;selection-color:#00171e;}
QLineEdit:focus,QSpinBox:focus,QDoubleSpinBox:focus,QComboBox:focus{border:1px solid #39d9ff;background:#081d2a;}
QComboBox::drop-down{border:0;width:22px;}
QComboBox QAbstractItemView{background:#071722;color:#e5f7fb;border:1px solid #23566b;selection-background-color:#0b6f8b;selection-color:white;}

QTextEdit,QPlainTextEdit,QTextBrowser,QTableView,QTableWidget,QTreeView,QTreeWidget,QListView,QListWidget{background:#050f18;color:#dceff5;border:1px solid #1b4659;border-radius:7px;gridline-color:#173747;selection-background-color:#0b5f79;selection-color:white;alternate-background-color:#09202d;}
QTableView::item,QTableWidget::item{padding:4px;border-bottom:1px solid #0e2936;}
QHeaderView::section{background:#0d2938;color:#72daf6;border:0;border-right:1px solid #1c485a;border-bottom:1px solid #1c485a;padding:6px;font-weight:700;}
QHeaderView::section:first{border-top-left-radius:6px;}
QHeaderView::section:last{border-top-right-radius:6px;}

QTabWidget::pane{background:#071722;border:1px solid #1b4659;border-radius:8px;top:-1px;}
QTabBar::tab{background:#0b2130;color:#8fb8c6;border:1px solid #1c4558;padding:7px 13px;margin-right:2px;border-top-left-radius:7px;border-top-right-radius:7px;}
QTabBar::tab:hover{color:#69e0ff;border-color:#2b7088;}
QTabBar::tab:selected{background:#0d5068;color:white;border-color:#3acff0;}

QCheckBox,QRadioButton{color:#d5edf4;spacing:6px;}
QCheckBox:disabled,QRadioButton:disabled{color:#607b86;}
QProgressBar{background:#06141e;color:#dff7ff;border:1px solid #23536a;border-radius:7px;text-align:center;min-height:15px;}
QProgressBar::chunk{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #00a9d7,stop:0.58 #16d9c2,stop:1 #5af2b7);border-radius:6px;}

QStatusBar{background:#06131d;color:#7fdcf4;border-top:1px solid #153b4d;}
QStatusBar::item{border:0;}
QScrollBar:vertical{background:#06141e;width:12px;margin:1px;border-radius:6px;}
QScrollBar::handle:vertical{background:#245c72;min-height:28px;border-radius:5px;}
QScrollBar::handle:vertical:hover{background:#2f7b94;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
QScrollBar:horizontal{background:#06141e;height:12px;margin:1px;border-radius:6px;}
QScrollBar::handle:horizontal{background:#245c72;min-width:28px;border-radius:5px;}
QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}
QSplitter::handle{background:#123343;}

QLabel#lh_decodes_title_label,QLabel#rh_decodes_title_label{color:#66ddff;font-weight:700;}
QTextBrowser#decodedTextBrowser,QTextBrowser#decodedTextBrowser2{background:#040d14;border:1px solid #16455a;border-radius:7px;}

QLabel#HunterStateBanner{font-size:14pt;font-weight:800;border-radius:10px;padding:10px 14px;}
QLabel[hunterState="off"]{background:#111d26;color:#7895a2;border:1px solid #2a4350;}
QLabel[hunterState="listening"]{background:#073a31;color:#74ffd0;border:1px solid #139b75;}
QLabel[hunterState="calling"]{background:#4a3410;color:#ffde78;border:1px solid #c7932b;}
QLabel[hunterState="busy"]{background:#482816;color:#ffc27a;border:1px solid #b66b2d;}
QLabel[hunterState="qso"]{background:#44202a;color:#ff9ab0;border:1px solid #ba4765;}
QLabel[hunterState="active"]{background:#073a31;color:#74ffd0;border:1px solid #139b75;}
QLabel[hunterState="waiting"]{background:#482816;color:#ffc27a;border:1px solid #b66b2d;}
QLabel#HunterTargetCard,QLabel#HunterTxCard,QLabel#HunterProgressCard,QLabel#BandHunterBandCard,QLabel#BandHunterTimeCard,QLabel#BandHunterNextCard{background:#081d2a;color:#d8f4fb;border:1px solid #1c4d61;border-radius:8px;padding:7px 10px;font-weight:700;}
QLabel#HunterStatusLine{background:#071b27;color:#8fdaf0;border:1px solid #183f50;border-radius:7px;padding:6px 9px;}
QLabel#HunterCountersLine{color:#91b9c7;padding:3px 6px;}
)QSS"));

      applySpecialWidgets (true);
    }
  else
    {
      QPalette p;
      p.setColor (QPalette::Window, QColor (238, 246, 249));
      p.setColor (QPalette::WindowText, QColor (17, 45, 57));
      p.setColor (QPalette::Base, Qt::white);
      p.setColor (QPalette::AlternateBase, QColor (235, 246, 249));
      p.setColor (QPalette::ToolTipBase, QColor (245, 252, 254));
      p.setColor (QPalette::ToolTipText, QColor (17, 45, 57));
      p.setColor (QPalette::Text, QColor (17, 45, 57));
      p.setColor (QPalette::Button, QColor (229, 242, 247));
      p.setColor (QPalette::ButtonText, QColor (17, 45, 57));
      p.setColor (QPalette::BrightText, QColor (194, 38, 69));
      p.setColor (QPalette::Highlight, QColor (0, 156, 196));
      p.setColor (QPalette::HighlightedText, Qt::white);
      p.setColor (QPalette::Link, QColor (0, 119, 157));
      p.setColor (QPalette::Disabled, QPalette::Text, QColor (138, 157, 166));
      p.setColor (QPalette::Disabled, QPalette::ButtonText, QColor (138, 157, 166));
      app->setPalette (p);

      app->setStyleSheet (QStringLiteral (R"QSS(
*{font-family:"Segoe UI","Arial";font-size:9pt;}
QMainWindow,QDialog{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #f5fbfd,stop:0.62 #edf7fa,stop:1 #e7f2f6);color:#112d39;}
QDockWidget{background:#f5fbfd;color:#112d39;border:1px solid #b7d5df;}
QDockWidget::title{background:#e3f3f8;color:#087293;border-bottom:1px solid #b2d4df;padding:7px 10px;font-weight:700;}
QMenuBar{background:#f7fbfc;color:#1f4757;border-bottom:1px solid #c6dde5;padding:2px;}
QMenuBar::item{background:transparent;padding:5px 9px;border-radius:5px;}
QMenuBar::item:selected{background:#dff2f7;color:#007a9f;}
QMenu{background:white;color:#173c4b;border:1px solid #b8d5df;border-radius:7px;padding:5px;}
QMenu::item{padding:6px 24px 6px 12px;border-radius:5px;}
QMenu::item:selected{background:#d8f1f7;color:#075e78;}
QMenu::separator{height:1px;background:#d2e5eb;margin:5px 8px;}
QToolTip{background:#f7fdff;color:#163b49;border:1px solid #a8cfdb;padding:5px;border-radius:4px;}

QGroupBox{background:#f8fcfd;border:1px solid #bfd9e2;border-radius:11px;margin-top:13px;padding:12px 8px 8px 8px;font-weight:700;}
QGroupBox::title{subcontrol-origin:margin;left:13px;padding:0 6px;color:#087a9c;background:#f8fcfd;}

QPushButton,QToolButton{background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #f7fcfd,stop:1 #e1f1f6);color:#173b4a;border:1px solid #afd1dc;border-radius:8px;padding:6px 11px;min-height:20px;}
QPushButton:hover,QToolButton:hover{background:#dff4f9;border-color:#17a6cb;color:#075d77;}
QPushButton:pressed,QToolButton:pressed{background:#c6ebf3;border-color:#008db5;}
QPushButton:checked,QToolButton:checked{background:#08a3cd;border-color:#007f9f;color:white;}
QPushButton:disabled,QToolButton:disabled{background:#edf3f5;color:#9badb5;border-color:#d3e0e5;}

QLineEdit,QSpinBox,QDoubleSpinBox,QComboBox,QDateEdit,QTimeEdit{background:white;color:#173b49;border:1px solid #b4d1dc;border-radius:7px;padding:4px 7px;min-height:20px;selection-background-color:#11a8cf;selection-color:white;}
QLineEdit:focus,QSpinBox:focus,QDoubleSpinBox:focus,QComboBox:focus{border:1px solid #00a1ca;background:#fbfeff;}
QComboBox::drop-down{border:0;width:22px;}
QComboBox QAbstractItemView{background:white;color:#173b49;border:1px solid #acd0dc;selection-background-color:#d5f0f6;selection-color:#075c73;}

QTextEdit,QPlainTextEdit,QTextBrowser,QTableView,QTableWidget,QTreeView,QTreeWidget,QListView,QListWidget{background:white;color:#183b49;border:1px solid #b9d5df;border-radius:7px;gridline-color:#d4e5eb;selection-background-color:#ccecf4;selection-color:#104456;alternate-background-color:#f2f9fb;}
QTableView::item,QTableWidget::item{padding:4px;border-bottom:1px solid #e3eef2;}
QHeaderView::section{background:#e3f3f7;color:#236177;border:0;border-right:1px solid #c5dfe7;border-bottom:1px solid #c5dfe7;padding:6px;font-weight:700;}

QTabWidget::pane{background:#f8fcfd;border:1px solid #bdd9e2;border-radius:8px;top:-1px;}
QTabBar::tab{background:#eaf5f8;color:#577681;border:1px solid #c5dce4;padding:7px 13px;margin-right:2px;border-top-left-radius:7px;border-top-right-radius:7px;}
QTabBar::tab:hover{color:#007c9f;border-color:#80bfce;}
QTabBar::tab:selected{background:#10a3ca;color:white;border-color:#0082a3;}

QCheckBox,QRadioButton{color:#244956;spacing:6px;}
QCheckBox:disabled,QRadioButton:disabled{color:#9bacb3;}
QProgressBar{background:#e5f2f6;color:#214c5c;border:1px solid #b2d4df;border-radius:7px;text-align:center;min-height:15px;}
QProgressBar::chunk{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #00a3cc,stop:0.58 #12cbb5,stop:1 #50dfaa);border-radius:6px;}

QStatusBar{background:#e6f3f7;color:#28667c;border-top:1px solid #bdd8e1;}
QScrollBar:vertical{background:#edf6f8;width:12px;margin:1px;border-radius:6px;}
QScrollBar::handle:vertical{background:#9ac5d1;min-height:28px;border-radius:5px;}
QScrollBar::handle:vertical:hover{background:#73afbe;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
QScrollBar:horizontal{background:#edf6f8;height:12px;margin:1px;border-radius:6px;}
QScrollBar::handle:horizontal{background:#9ac5d1;min-width:28px;border-radius:5px;}
QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;}
QSplitter::handle{background:#c5dfe6;}

QLabel#lh_decodes_title_label,QLabel#rh_decodes_title_label{color:#087b9d;font-weight:700;}
QTextBrowser#decodedTextBrowser,QTextBrowser#decodedTextBrowser2{background:#ffffff;border:1px solid #b8d5df;border-radius:7px;}

QLabel#HunterStateBanner{font-size:14pt;font-weight:800;border-radius:10px;padding:10px 14px;}
QLabel[hunterState="off"]{background:#edf2f4;color:#607985;border:1px solid #c8d7dc;}
QLabel[hunterState="listening"]{background:#e1f8ef;color:#087452;border:1px solid #7ad1b4;}
QLabel[hunterState="calling"]{background:#fff2cf;color:#785807;border:1px solid #e4c66b;}
QLabel[hunterState="busy"]{background:#ffead9;color:#8d4c10;border:1px solid #e5b37c;}
QLabel[hunterState="qso"]{background:#ffe4ea;color:#9e2945;border:1px solid #e89bad;}
QLabel[hunterState="active"]{background:#e1f8ef;color:#087452;border:1px solid #7ad1b4;}
QLabel[hunterState="waiting"]{background:#ffead9;color:#8d4c10;border:1px solid #e5b37c;}
QLabel#HunterTargetCard,QLabel#HunterTxCard,QLabel#HunterProgressCard,QLabel#BandHunterBandCard,QLabel#BandHunterTimeCard,QLabel#BandHunterNextCard{background:#f4fbfd;color:#214958;border:1px solid #bedbe4;border-radius:8px;padding:7px 10px;font-weight:700;}
QLabel#HunterStatusLine{background:#f0f8fa;color:#26728b;border:1px solid #c4dde5;border-radius:7px;padding:6px 9px;}
QLabel#HunterCountersLine{color:#66828c;padding:3px 6px;}
)QSS"));

      applySpecialWidgets (false);
    }
}
