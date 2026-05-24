#pragma once

#include <QMainWindow>
#include <memory>

#include "model/Board.h"

class PcbCanvas;
class LayerPanel;
class AnalysisPanel;
class ColorLegend;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Load a KiCad .kicad_pcb file. Shows a message box on error.
    // Returns true on success.
    bool loadKicadPcb(const QString& path);

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onOpenKicadPcb();
    void onAnalyzeStaticIrDrop();

private:
    void populateLayerPanel();

    PcbCanvas* canvas_;
    LayerPanel* layer_panel_;
    AnalysisPanel* analysis_panel_;
    ColorLegend* legend_;
    QLabel* hover_label_;
    std::unique_ptr<pdnkit::model::Board> board_;
};
