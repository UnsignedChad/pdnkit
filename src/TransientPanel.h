#pragma once

#include <QWidget>

#include <vector>

#include "model/Board.h"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class TransientPlotWidget;

// Dockable panel that drives a time-domain transient (step response).
// Builds its own IrMesh from the selected net+layer, derives a per-node
// capacitance from the plane substrate (eps_r, thickness, cell area), and
// runs solve_step_transient. Plots V(t) at the observation port + max |V|
// across the mesh.
class TransientPanel : public QWidget {
    Q_OBJECT
public:
    explicit TransientPanel(QWidget* parent = nullptr);
    void setBoard(const pdnkit::model::Board* board);

private slots:
    void onRun();
    void onClear();
    void onSaveCsv();

private:
    void rebuildNetCombo();

    const pdnkit::model::Board* board_ = nullptr;

    QComboBox* net_combo_;
    QComboBox* layer_combo_;
    QDoubleSpinBox* current_spin_;     // A
    QDoubleSpinBox* dt_spin_;          // ns
    QSpinBox* nsteps_spin_;
    QDoubleSpinBox* cell_spin_;        // mm
    QDoubleSpinBox* epsr_spin_;
    QDoubleSpinBox* thickness_spin_;   // mm
    QPushButton* run_btn_;
    QPushButton* save_btn_;
    QPushButton* clear_btn_;

    // Cache of latest sweep for CSV export.
    std::vector<double> last_t_;
    std::vector<double> last_vobs_;
    std::vector<double> last_vmax_;
    TransientPlotWidget* plot_;
};
