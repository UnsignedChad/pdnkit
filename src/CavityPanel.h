#pragma once

#include <QWidget>

#include <vector>

#include "model/Board.h"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class QTableWidget;
class ZfPlotWidget;

// Dockable panel that runs a cavity-model Z(f) sweep on the loaded board.
// Plane dimensions are auto-fit to the bounding box of the selected net's
// filled-zone copper on the selected layer. Ports are given as (x, y) in mm
// relative to that bbox. Plot is a log-log |Z(f)| curve.
class CavityPanel : public QWidget {
    Q_OBJECT
public:
    explicit CavityPanel(QWidget* parent = nullptr);

    void setBoard(const pdnkit::model::Board* board);

signals:
    // Fires whenever the decap list changes (add / remove / cell edit).
    // Positions are world coords in meters relative to the plane corner
    // — for now treat them as canvas world coords too (the cavity panel
    // does not yet know the plane bbox offset in board coords).
    void decapsChanged(const std::vector<pdnkit::model::Point2>& positions);

private slots:
    void onRun();
    void onClear();
    void onSaveCsv();
    void onAddDecap();
    void onRemoveDecap();

private:
    void rebuildNetCombo();

    const pdnkit::model::Board* board_ = nullptr;

    QComboBox* net_combo_;
    QDoubleSpinBox* eps_r_spin_;
    QDoubleSpinBox* tan_delta_spin_;
    QDoubleSpinBox* thickness_spin_;
    QDoubleSpinBox* port1_x_, * port1_y_;
    QDoubleSpinBox* port2_x_, * port2_y_;
    QDoubleSpinBox* f_min_spin_;
    QDoubleSpinBox* f_max_spin_;
    QSpinBox* points_spin_;
    QSpinBox* modes_spin_;
    QDoubleSpinBox* target_z_spin_;
    class QCheckBox* overlay_bare_check_;
    QPushButton* run_btn_;
    QPushButton* clear_btn_;
    QPushButton* save_btn_;
    QPushButton* add_decap_btn_;
    QPushButton* remove_decap_btn_;
    QTableWidget* decap_table_;
    ZfPlotWidget* plot_;

    // Cache of the latest sweep for CSV export.
    std::vector<double> last_freqs_;
    std::vector<double> last_mags_;
};
