#include <QApplication>
#include "MainWindow.h"
#include "Theme.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Learn to Play PoE1");
    app.setApplicationVersion("0.1.0");
    app.setQuitOnLastWindowClosed(false);

    Theme::apply(app);

    MainWindow window;
    if (!window.startMinimized())
        window.show();

    return app.exec();
}
