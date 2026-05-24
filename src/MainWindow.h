#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <memory>

#include "model/Board.h"
#include "pi/IrMesher.h"
#include "pi/IrSolver.h"

class PcbCanvas;
class LayerPanel;
class AnalysisPanel;
class ColorLegend;
class NetStatsPanel;
class CavityPanel;
class TransientPanel;
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
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private slots:
    void onOpenKicadPcb();
    void onReloadBoard();
    void onAnalyzeStaticIrDrop();
    void onProbeRequested(int pad_a, int pad_b, int net_id, int layer_ord);
    void onSaveCanvasImage();
    void onExportResultsCsv();
    void onAboutDialog();
    void onShortcutsDialog();

private:
    void populateLayerPanel();
    void updateRecentMenu();
    void addRecent(const QString& path);

    PcbCanvas* canvas_;
    LayerPanel* layer_panel_;
    AnalysisPanel* analysis_panel_;
    NetStatsPanel* netstats_panel_;
    CavityPanel* cavity_panel_;
    TransientPanel* transient_panel_;
    ColorLegend* legend_;
    QLabel* hover_label_;
    std::unique_ptr<pdnkit::model::Board> board_;
    pdnkit::pi::IrMesh last_mesh_;
    pdnkit::pi::Solution last_solution_;
    QString current_board_path_;
    QStringList recent_files_;
    class QMenu* recent_menu_ = nullptr;
};
