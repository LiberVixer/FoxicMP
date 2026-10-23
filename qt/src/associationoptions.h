#pragma once
class Profile;
class QWidget;
void showAssociations(Profile &, QWidget *parent);

QWidget *createAssociationsPage(Profile &, QWidget *parent, bool deferVerification = false);
