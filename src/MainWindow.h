#pragma once

#include <QMainWindow>

class PcbCanvas;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    PcbCanvas* canvas_;
};
