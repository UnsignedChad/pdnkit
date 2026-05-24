#pragma once

#include <QWidget>

#include "model/Board.h"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
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

private slots:
    void onRun();
    void onClear();

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
    QPushButton* run_btn_;
    QPushButton* clear_btn_;
    ZfPlotWidget* plot_;
};
