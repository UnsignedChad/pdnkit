#pragma once

#include <QWidget>

#include "model/Board.h"
#include "pi/IrMesher.h"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;

// Dockable panel that drives an IR-drop analysis run. Lets the user pick the
// target net + layer, override the source/sink pad selection by pad name, and
// adjust the injected current and grid cell size. The Run button + the
// MainWindow's Analyze menu action both pull config from this panel.
class AnalysisPanel : public QWidget {
    Q_OBJECT
public:
    explicit AnalysisPanel(QWidget* parent = nullptr);

    // Repopulate dropdowns from the loaded board. Pass nullptr to clear.
    void setBoard(const pdnkit::model::Board* board);

    // Construct a MeshConfig from current panel state. net_id may be -1 if
    // the board has no copper nets — the caller should check.
    pdnkit::pi::MeshConfig currentConfig() const;

    // Total current (Amperes) to inject across all source nodes.
    double currentTotalCurrent() const;

signals:
    void runRequested();
    void clearRequested();

private slots:
    void onNetOrLayerChanged();

private:
    void refreshPadCombos();

    const pdnkit::model::Board* board_ = nullptr;

    QComboBox* net_combo_;
    QComboBox* layer_combo_;
    QComboBox* source_combo_;
    QComboBox* sink_combo_;
    QDoubleSpinBox* current_spin_;
    QDoubleSpinBox* cell_size_spin_;
    QPushButton* run_btn_;
    QPushButton* clear_btn_;
};
