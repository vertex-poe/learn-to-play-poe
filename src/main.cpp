#include <QApplication>
#include <QLabel>
#include <QMainWindow>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("Learn to Play PoE1");
    app.setApplicationVersion("0.1.0");

    QMainWindow window;
    window.setWindowTitle("Learn to Play PoE1");
    window.setCentralWidget(new QLabel("Phase 0: toolchain works.", &window));
    window.resize(640, 400);
    window.show();

    return app.exec();
}
