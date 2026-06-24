#pragma once

#include <QDialog>

struct AppConfig;

class PoeLoginWindow : public QDialog
{
    Q_OBJECT
public:
    explicit PoeLoginWindow(const AppConfig &config, QWidget *parent = nullptr);

signals:
    void sessionCaptured(const QString &poesessid);
};
