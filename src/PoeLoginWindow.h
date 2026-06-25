#pragma once

#include <QDialog>

struct AppConfig;

class PoeLoginWindow : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Login, Browse };

    explicit PoeLoginWindow(const AppConfig &config, QWidget *parent = nullptr,
                            Mode mode = Mode::Login);

signals:
    void sessionCaptured(const QString &poesessid);
};
