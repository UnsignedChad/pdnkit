#include <QApplication>
#include <QSurfaceFormat>
#include <spdlog/spdlog.h>

#include "MainWindow.h"

int main(int argc, char** argv) {
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    QApplication::setApplicationName("pdnkit");
    QApplication::setApplicationVersion("0.0.1");

    spdlog::info("pdnkit starting");

    MainWindow w;
    w.show();
    return app.exec();
}
