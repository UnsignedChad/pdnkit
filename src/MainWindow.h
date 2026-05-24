#pragma once

#include <QMainWindow>
#include <memory>

#include "model/Board.h"

class PcbCanvas;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Load a KiCad .kicad_pcb file. Shows a message box on error.
    // Returns true on success.
    bool loadKicadPcb(const QString& path);

private slots:
    void onOpenKicadPcb();

private:
    PcbCanvas* canvas_;
    std::unique_ptr<pdnkit::model::Board> board_;
};
