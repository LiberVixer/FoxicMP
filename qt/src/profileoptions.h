#pragma once
#include <functional>
#include <QString>
class Profile;
class QWidget;
void showProfileManagement(Profile &, QWidget *,
                           std::function<void()> checkpoint,
                           std::function<void(bool)> reset = {},
                           const QString &programDirectory = {});
