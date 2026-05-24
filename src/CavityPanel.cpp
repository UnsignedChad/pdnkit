#include "CavityPanel.h"

#include <cmath>
#include <numbers>
#include <set>
#include <vector>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "ZfPlotWidget.h"
#include "pi/CavityModel.h"

namespace {

// Bounding box of the filled zones on (net, primary copper layer 0). Empty
// returned when there is no zone fill matching the filter — caller decides.
struct Bbox {
    bool ok = false;
    double lo_x = 0.0, lo_y = 0.0, hi_x = 0.0, hi_y = 0.0;
};

Bbox zone_bbox(const pdnkit::model::Board& b, int net_id, int layer) {
    Bbox box;
    for (const auto& z : b.zones) {
        if (z.net_id != net_id || z.layer_ordinal != layer) continue;
        for (const auto& fp : z.filled) {
            for (const auto& p : fp.outline) {
                if (!box.ok) {
                    box.lo_x = box.hi_x = p.x;
                    box.lo_y = box.hi_y = p.y;
                    box.ok = true;
                } else {
                    if (p.x < box.lo_x) box.lo_x = p.x;
                    if (p.x > box.hi_x) box.hi_x = p.x;
                    if (p.y < box.lo_y) box.lo_y = p.y;
                    if (p.y > box.hi_y) box.hi_y = p.y;
                }
            }
        }
    }
    return box;
}

}  // namespace

CavityPanel::CavityPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    auto* header = new QLabel("Plane Z(f)  (cavity model)");
    QFont f = header->font();
    f.setBold(true);
    header->setFont(f);
    outer->addWidget(header);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(4);

    net_combo_ = new QComboBox();

    eps_r_spin_ = new QDoubleSpinBox();
    eps_r_spin_->setRange(1.0, 20.0);
    eps_r_spin_->setDecimals(2);
    eps_r_spin_->setValue(4.3);

    tan_delta_spin_ = new QDoubleSpinBox();
    tan_delta_spin_->setRange(0.0, 0.5);
    tan_delta_spin_->setDecimals(4);
    tan_delta_spin_->setSingleStep(0.001);
    tan_delta_spin_->setValue(0.020);

    thickness_spin_ = new QDoubleSpinBox();
    thickness_spin_->setRange(0.05, 10.0);
    thickness_spin_->setDecimals(3);
    thickness_spin_->setValue(1.6);
    thickness_spin_->setSuffix(" mm");

    port1_x_ = new QDoubleSpinBox(); port1_x_->setRange(0.0, 1000.0); port1_x_->setDecimals(2); port1_x_->setSuffix(" mm");
    port1_y_ = new QDoubleSpinBox(); port1_y_->setRange(0.0, 1000.0); port1_y_->setDecimals(2); port1_y_->setSuffix(" mm");
    port2_x_ = new QDoubleSpinBox(); port2_x_->setRange(0.0, 1000.0); port2_x_->setDecimals(2); port2_x_->setSuffix(" mm");
    port2_y_ = new QDoubleSpinBox(); port2_y_->setRange(0.0, 1000.0); port2_y_->setDecimals(2); port2_y_->setSuffix(" mm");

    f_min_spin_ = new QDoubleSpinBox();
    f_min_spin_->setRange(1.0, 1.0e10);
    f_min_spin_->setDecimals(0);
    f_min_spin_->setValue(1.0e6);
    f_min_spin_->setSuffix(" Hz");

    f_max_spin_ = new QDoubleSpinBox();
    f_max_spin_->setRange(1.0e3, 1.0e12);
    f_max_spin_->setDecimals(0);
    f_max_spin_->setValue(5.0e9);
    f_max_spin_->setSuffix(" Hz");

    points_spin_ = new QSpinBox();
    points_spin_->setRange(10, 5000);
    points_spin_->setValue(300);

    modes_spin_ = new QSpinBox();
    modes_spin_->setRange(5, 200);
    modes_spin_->setValue(30);

    form->addRow("Net:",       net_combo_);
    form->addRow("eps_r:",     eps_r_spin_);
    form->addRow("tan delta:", tan_delta_spin_);
    form->addRow("d:",         thickness_spin_);
    form->addRow("Port1 X:",   port1_x_);
    form->addRow("Port1 Y:",   port1_y_);
    form->addRow("Port2 X:",   port2_x_);
    form->addRow("Port2 Y:",   port2_y_);
    form->addRow("f_min:",     f_min_spin_);
    form->addRow("f_max:",     f_max_spin_);
    form->addRow("Points:",    points_spin_);
    form->addRow("Modes:",     modes_spin_);
    outer->addLayout(form);

    auto* btn_row = new QHBoxLayout();
    run_btn_   = new QPushButton("Run sweep");
    clear_btn_ = new QPushButton("Clear");
    btn_row->addWidget(run_btn_);
    btn_row->addWidget(clear_btn_);
    outer->addLayout(btn_row);

    plot_ = new ZfPlotWidget(this);
    outer->addWidget(plot_, 1);

    connect(run_btn_,   &QPushButton::clicked, this, &CavityPanel::onRun);
    connect(clear_btn_, &QPushButton::clicked, this, &CavityPanel::onClear);
}

void CavityPanel::setBoard(const pdnkit::model::Board* board) {
    board_ = board;
    rebuildNetCombo();
    plot_->clear();
}

void CavityPanel::rebuildNetCombo() {
    net_combo_->clear();
    if (!board_) return;
    std::set<int> nets;
    for (const auto& z : board_->zones) {
        if (!z.filled.empty()) nets.insert(z.net_id);
    }
    for (int id : nets) {
        const auto* n = board_->find_net(id);
        const QString name = (n && !n->name.empty())
            ? QString::fromStdString(n->name)
            : QString("net %1").arg(id);
        net_combo_->addItem(QString("%1 (#%2)").arg(name).arg(id), id);
    }
}

void CavityPanel::onRun() {
    if (!board_ || net_combo_->count() == 0) return;
    const int net = net_combo_->currentData().toInt();
    constexpr int kPrimaryLayer = 0;  // F.Cu for v0

    const Bbox bb = zone_bbox(*board_, net, kPrimaryLayer);
    if (!bb.ok) return;

    pdnkit::pi::CavityConfig cfg;
    cfg.a = bb.hi_x - bb.lo_x;
    cfg.b = bb.hi_y - bb.lo_y;
    cfg.d = thickness_spin_->value() * 1.0e-3;
    cfg.eps_r = eps_r_spin_->value();
    cfg.tan_delta = tan_delta_spin_->value();
    cfg.max_modes = modes_spin_->value();

    // Port positions are panel inputs in mm, relative to the plane corner
    // (which sits at world (bb.lo_x, bb.lo_y) — but the cavity formula uses
    // local coordinates so we just use them directly).
    const double x1 = port1_x_->value() * 1.0e-3;
    const double y1 = port1_y_->value() * 1.0e-3;
    const double x2 = port2_x_->value() * 1.0e-3;
    const double y2 = port2_y_->value() * 1.0e-3;

    const int N = points_spin_->value();
    const double f_lo = f_min_spin_->value();
    const double f_hi = f_max_spin_->value();
    std::vector<double> freqs;
    freqs.reserve(N);
    const double log_lo = std::log10(f_lo);
    const double log_hi = std::log10(f_hi);
    for (int i = 0; i < N; ++i) {
        const double t = (N == 1) ? 0.0 : static_cast<double>(i) / (N - 1);
        freqs.push_back(std::pow(10.0, log_lo + t * (log_hi - log_lo)));
    }
    auto mags = pdnkit::pi::cavity_impedance_magnitude_sweep(cfg, x1, y1, x2, y2, freqs);
    plot_->setData(std::move(freqs), std::move(mags));
}

void CavityPanel::onClear() {
    plot_->clear();
}
