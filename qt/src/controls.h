#pragma once
#include <QIcon>
#include <QString>
QIcon controlIcon(const QString &name);
bool parsePlaybackTime(const QString &text, double *seconds);
