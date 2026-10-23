#pragma once
#include <QApplication>
inline void applyTheme(QApplication &app) {
  app.setStyle("Fusion");
  app.setStyleSheet(
      "QWidget { background:#292e32; color:#e3e6e8; } QMenuBar,QMenu { "
      "background:#292e32; } QMenu::item:selected,QListWidget::item:selected { "
      "background:#4a535b; } QToolButton { border:0; border-radius:2px; } "
      "QWidget#controls { background:qlineargradient(x1:0,y1:0,x2:0,y2:1, "
      "stop:0 #41474c, stop:1 #292e32); border-top:1px solid #565d63; } "
      "QWidget#controls QLabel, QWidget#controls QToolButton, QWidget#controls "
      "QSlider { background:transparent; } "
      "QToolButton:hover, QWidget#controls QToolButton:hover { "
      "background:#505960; } QSlider::groove:horizontal { "
      "height:5px; background:#171a1d; } QSlider::handle:horizontal { "
      "width:7px; background:#9aa4ab; margin:-4px 0; } QDockWidget { "
      "min-width:200px; } QToolTip { color:#eee; background:#383e43; "
      "border:1px solid #677078; }");
}
