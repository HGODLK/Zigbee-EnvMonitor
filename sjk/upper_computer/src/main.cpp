#include <QApplication>
#include <QTimer>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("EnvMonitor");
    app.setApplicationVersion("1.0");

    MainWindow w;
    w.show();
    if (argc > 1) {
        const QString host = QString::fromLocal8Bit(argv[1]);
        QTimer::singleShot(0, &w, [&w, host]() {
            w.connectToDevice(host);
        });
    }

    return app.exec();
}
