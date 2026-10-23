#pragma once
#include <QString>
class QDialog;
class QTabWidget;
class QWidget;
namespace ClassicPreferences {
void beginLoading(QDialog *);
bool loadingStep(QDialog *);
void finishLoading(QDialog *);
void organize(QDialog *, QTabWidget *, const QString &initialPath = {});
void selectPage(QTabWidget *, QWidget *);
}
