#include "HunterTheme.hpp"
#include <QColor>

#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>

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
  settings->setValue (QStringLiteral ("theme"), mode == Mode::Dark ? QStringLiteral ("dark") : QStringLiteral ("light"));
  settings->endGroup ();
  settings->sync ();
}

QString HunterTheme::name (Mode mode)
{
  return mode == Mode::Dark ? QStringLiteral ("Dark") : QStringLiteral ("Light");
}

void HunterTheme::apply (QApplication * app, Mode mode)
{
  if (!app) return;
  if (auto * fusion = QStyleFactory::create (QStringLiteral ("Fusion")))
    app->setStyle (fusion);

  if (mode == Mode::Dark)
    {
      QPalette p;
      p.setColor (QPalette::Window, QColor (10, 18, 30));
      p.setColor (QPalette::WindowText, QColor (226, 240, 255));
      p.setColor (QPalette::Base, QColor (6, 13, 23));
      p.setColor (QPalette::AlternateBase, QColor (14, 26, 43));
      p.setColor (QPalette::ToolTipBase, QColor (12, 25, 43));
      p.setColor (QPalette::ToolTipText, Qt::white);
      p.setColor (QPalette::Text, QColor (226, 240, 255));
      p.setColor (QPalette::Button, QColor (17, 31, 50));
      p.setColor (QPalette::ButtonText, QColor (226, 240, 255));
      p.setColor (QPalette::BrightText, QColor (255, 75, 90));
      p.setColor (QPalette::Highlight, QColor (0, 177, 255));
      p.setColor (QPalette::HighlightedText, Qt::white);
      p.setColor (QPalette::Link, QColor (0, 220, 255));
      app->setPalette (p);
      app->setStyleSheet (QStringLiteral (R"QSS(
QMainWindow,QDialog,QDockWidget{background:#0a121e;color:#e2f0ff;}
QMenuBar,QMenu{background:#0d1a2b;color:#e2f0ff;border:1px solid #18324f;}
QMenu::item:selected{background:#0b7fb8;color:white;}
QGroupBox{border:1px solid #22476c;border-radius:9px;margin-top:10px;padding-top:8px;font-weight:600;}
QGroupBox::title{subcontrol-origin:margin;left:10px;padding:0 4px;color:#56dcff;}
QPushButton,QToolButton{background:#112840;color:#edf8ff;border:1px solid #2d5d87;border-radius:7px;padding:5px 10px;}
QPushButton:hover,QToolButton:hover{background:#16486c;border-color:#25cfff;}
QPushButton:pressed,QToolButton:pressed{background:#0b79ad;}
QPushButton:checked,QToolButton:checked{background:#008ec9;border-color:#4be7ff;color:white;}
QLineEdit,QSpinBox,QDoubleSpinBox,QComboBox,QPlainTextEdit,QTextEdit,QTableWidget,QTreeWidget,QListWidget{
 background:#071321;color:#e6f4ff;border:1px solid #244c70;border-radius:5px;selection-background-color:#007db7;selection-color:white;}
QHeaderView::section{background:#10263d;color:#8eeaff;border:0;border-right:1px solid #274b6a;padding:5px;font-weight:600;}
QTabWidget::pane{border:1px solid #234867;border-radius:6px;}
QTabBar::tab{background:#10243a;color:#a8cce6;padding:7px 12px;border:1px solid #1f4565;}
QTabBar::tab:selected{background:#007fb8;color:white;}
QCheckBox::indicator{width:17px;height:17px;}
QProgressBar{background:#06111d;border:1px solid #204765;border-radius:6px;text-align:center;}
QProgressBar::chunk{background:#00aeea;border-radius:5px;}
QStatusBar{background:#071321;color:#9edfff;}
QScrollBar:vertical{background:#081725;width:12px;margin:0;}
QScrollBar::handle:vertical{background:#24577e;min-height:28px;border-radius:5px;}
)QSS"));
    }
  else
    {
      QPalette p;
      p.setColor (QPalette::Window, QColor (242, 247, 252));
      p.setColor (QPalette::WindowText, QColor (20, 35, 50));
      p.setColor (QPalette::Base, Qt::white);
      p.setColor (QPalette::AlternateBase, QColor (232, 242, 250));
      p.setColor (QPalette::Text, QColor (20, 35, 50));
      p.setColor (QPalette::Button, QColor (228, 239, 248));
      p.setColor (QPalette::ButtonText, QColor (20, 35, 50));
      p.setColor (QPalette::Highlight, QColor (0, 132, 192));
      p.setColor (QPalette::HighlightedText, Qt::white);
      p.setColor (QPalette::Link, QColor (0, 105, 170));
      app->setPalette (p);
      app->setStyleSheet (QStringLiteral (R"QSS(
QMainWindow,QDialog,QDockWidget{background:#f2f7fc;color:#142332;}
QMenuBar,QMenu{background:white;color:#142332;border:1px solid #c9dbe9;}
QMenu::item:selected{background:#d6effc;color:#064e75;}
QGroupBox{border:1px solid #abc9de;border-radius:9px;margin-top:10px;padding-top:8px;font-weight:600;}
QGroupBox::title{subcontrol-origin:margin;left:10px;padding:0 4px;color:#006b9e;}
QPushButton,QToolButton{background:#e5f1f9;color:#123047;border:1px solid #9fc5dd;border-radius:7px;padding:5px 10px;}
QPushButton:hover,QToolButton:hover{background:#d0ecfa;border-color:#008fd0;}
QPushButton:checked,QToolButton:checked{background:#008bc5;border-color:#006d9d;color:white;}
QLineEdit,QSpinBox,QDoubleSpinBox,QComboBox,QPlainTextEdit,QTextEdit,QTableWidget,QTreeWidget,QListWidget{
 background:white;color:#142332;border:1px solid #a9c8dd;border-radius:5px;selection-background-color:#058bc5;selection-color:white;}
QHeaderView::section{background:#dcecf7;color:#154a68;border:0;border-right:1px solid #bad1e2;padding:5px;font-weight:600;}
QTabWidget::pane{border:1px solid #b8d2e4;border-radius:6px;}
QTabBar::tab{background:#e2eff8;color:#27546f;padding:7px 12px;border:1px solid #bdd4e5;}
QTabBar::tab:selected{background:#008bc5;color:white;}
QStatusBar{background:#e3f0f8;color:#19445e;}
)QSS"));
    }
}
