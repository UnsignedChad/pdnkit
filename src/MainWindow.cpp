#include "MainWindow.h"

#include <QMenuBar>
#include <QStatusBar>

#include "PcbCanvas.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("pdnkit");
    resize(1280, 800);

    canvas_ = new PcbCanvas(this);
    setCentralWidget(canvas_);

    auto* fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&Open KiCad PCB...")->setEnabled(false);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close);

    statusBar()->showMessage("Ready");
}
