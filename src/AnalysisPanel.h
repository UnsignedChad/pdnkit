#pragma once

#include <QWidget>

#include "model/Board.h"
#include "pi/IrMesher.h"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QTableWidget;

// Drives an IR-drop analysis run. User picks the target net + layer + mesh
// cell size, then sets per-pad current (mA) in a table — positive injects,
// negative draws. The default-current spinbox seeds new pad rows whenever
// net/layer changes (first pad = +default, last = −default, rest = 0).
class AnalysisPanel : public QWidget {
    Q_OBJECT
public:
    explicit AnalysisPanel(QWidget* parent = nullptr);

    void setBoard(const pdnkit::model::Board* board);

    // Construct a MeshConfig (including per-pad current map) from current
    // panel state. cfg.net_id == -1 means "no net selected".
    pdnkit::pi::MeshConfig currentConfig() const;

signals:
    void runRequested();
    void clearRequested();

private slots:
    void onNetOrLayerChanged();
    void onAutoBalance();

private:
    void rebuildPadTable();
    void updateSumLabel();
    void rebuildExtraLayers();

    const pdnkit::model::Board* board_ = nullptr;

    QComboBox* net_combo_;
    QComboBox* layer_combo_;
    class QListWidget* extra_layers_;
    QDoubleSpinBox* default_current_spin_;
    QDoubleSpinBox* cell_size_spin_;
    QTableWidget* pad_table_;
    class QLabel* sum_label_;
    QPushButton* auto_btn_;
    QPushButton* run_btn_;
    QPushButton* clear_btn_;
};
