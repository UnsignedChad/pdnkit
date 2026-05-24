#include "CavityPanel.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <vector>

#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QHeaderView>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "ZfPlotWidget.h"
#include "pi/CavityModel.h"
#include "pi/DecapOptimizer.h"

namespace {

std::vector<pdnkit::model::Point2> read_decap_positions(QTableWidget* t,
                                                        double offset_x_m,
                                                        double offset_y_m) {
    std::vector<pdnkit::model::Point2> out;
    for (int r = 0; r < t->rowCount(); ++r) {
        auto* xi = t->item(r, 0);
        auto* yi = t->item(r, 1);
        if (!xi || !yi) continue;
        out.push_back({xi->text().toDouble() * 1.0e-3 + offset_x_m,
                       yi->text().toDouble() * 1.0e-3 + offset_y_m});
    }
    return out;
}

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

    target_z_spin_ = new QDoubleSpinBox();
    target_z_spin_->setRange(0.0, 1000.0);
    target_z_spin_->setDecimals(3);
    target_z_spin_->setValue(0.025);  // 25 mOhm, a common PDN target
    target_z_spin_->setSuffix(" ohm");
    target_z_spin_->setSingleStep(0.005);
    form->addRow("Target Z:", target_z_spin_);

    overlay_bare_check_ = new QCheckBox("Overlay bare plane");
    overlay_bare_check_->setChecked(true);
    form->addRow("", overlay_bare_check_);
    outer->addLayout(form);

    auto* decaps_label = new QLabel("Decoupling capacitors:");
    outer->addWidget(decaps_label);
    decap_table_ = new QTableWidget(0, 5);
    decap_table_->setHorizontalHeaderLabels(
        {"X (mm)", "Y (mm)", "C (uF)", "ESR (mOhm)", "ESL (nH)"});
    decap_table_->verticalHeader()->setVisible(false);
    decap_table_->setMaximumHeight(120);
    decap_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    outer->addWidget(decap_table_);

    auto* dec_btn_row = new QHBoxLayout();
    add_decap_btn_    = new QPushButton("Add decap");
    remove_decap_btn_ = new QPushButton("Remove selected");
    auto_decap_btn_   = new QPushButton("Auto-suggest");
    auto_decap_btn_->setToolTip(
        "Greedy decap selection from a small library. Adds capacitors near "
        "port 1 until the target impedance is met or the cap budget is hit.");
    dec_btn_row->addWidget(add_decap_btn_);
    dec_btn_row->addWidget(remove_decap_btn_);
    dec_btn_row->addWidget(auto_decap_btn_);
    outer->addLayout(dec_btn_row);

    auto* btn_row = new QHBoxLayout();
    run_btn_   = new QPushButton("Run sweep");
    save_btn_  = new QPushButton("Save CSV...");
    clear_btn_ = new QPushButton("Clear");
    btn_row->addWidget(run_btn_);
    btn_row->addWidget(save_btn_);
    btn_row->addWidget(clear_btn_);
    outer->addLayout(btn_row);

    plot_ = new ZfPlotWidget(this);
    outer->addWidget(plot_, 1);

    connect(net_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ emitCavity(); });
    connect(port1_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ emitCavity(); });
    connect(port1_y_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ emitCavity(); });
    connect(port2_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ emitCavity(); });
    connect(port2_y_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ emitCavity(); });
    connect(run_btn_,   &QPushButton::clicked, this, &CavityPanel::onRun);
    connect(save_btn_,  &QPushButton::clicked, this, &CavityPanel::onSaveCsv);
    connect(clear_btn_, &QPushButton::clicked, this, &CavityPanel::onClear);
    connect(add_decap_btn_,    &QPushButton::clicked, this, &CavityPanel::onAddDecap);
    connect(remove_decap_btn_, &QPushButton::clicked, this, &CavityPanel::onRemoveDecap);
    connect(auto_decap_btn_,   &QPushButton::clicked, this, &CavityPanel::onAutoSuggest);
    connect(decap_table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) {
        emit decapsChanged(read_decap_positions(decap_table_, 0.0, 0.0));
    });
}

void CavityPanel::onAddDecap() {
    const int row = decap_table_->rowCount();
    decap_table_->insertRow(row);
    // Defaults: at plane center, 1uF, 5 mOhm ESR, 0.5nH ESL.
    decap_table_->setItem(row, 0, new QTableWidgetItem(QString::number(0.0)));
    decap_table_->setItem(row, 1, new QTableWidgetItem(QString::number(0.0)));
    decap_table_->setItem(row, 2, new QTableWidgetItem(QString::number(1.0)));     // 1 uF
    decap_table_->setItem(row, 3, new QTableWidgetItem(QString::number(5.0)));     // 5 mOhm
    decap_table_->setItem(row, 4, new QTableWidgetItem(QString::number(0.5)));     // 0.5 nH
    emit decapsChanged(read_decap_positions(decap_table_, 0.0, 0.0));
}

void CavityPanel::onRemoveDecap() {
    auto rows = decap_table_->selectionModel()->selectedRows();
    // Remove in descending order so indices stay valid.
    std::vector<int> idx;
    for (auto& r : rows) idx.push_back(r.row());
    std::sort(idx.rbegin(), idx.rend());
    for (int r : idx) decap_table_->removeRow(r);
    emit decapsChanged(read_decap_positions(decap_table_, 0.0, 0.0));
}

namespace {

std::vector<pdnkit::model::Point2> port_positions(double p1x_mm, double p1y_mm,
                                                   double p2x_mm, double p2y_mm,
                                                   double offset_x_m, double offset_y_m) {
    return {
        {p1x_mm * 1.0e-3 + offset_x_m, p1y_mm * 1.0e-3 + offset_y_m},
        {p2x_mm * 1.0e-3 + offset_x_m, p2y_mm * 1.0e-3 + offset_y_m},
    };
}

}  // namespace

void CavityPanel::emitCavity() {
    if (!board_ || net_combo_->count() == 0) {
        emit cavityChanged(0, 0, 0, 0, {});
        return;
    }
    const int net = net_combo_->currentData().toInt();
    constexpr int kPrimaryLayer = 0;
    const Bbox bb = zone_bbox(*board_, net, kPrimaryLayer);
    if (!bb.ok) {
        emit cavityChanged(0, 0, 0, 0, {});
        return;
    }
    // Port positions are panel inputs in plane-local mm; translate to world
    // coords by adding the bbox lo corner.
    auto ports = port_positions(port1_x_->value(), port1_y_->value(),
                                port2_x_->value(), port2_y_->value(),
                                bb.lo_x, bb.lo_y);
    emit cavityChanged(bb.lo_x, bb.lo_y, bb.hi_x, bb.hi_y, ports);
}

void CavityPanel::setBoard(const pdnkit::model::Board* board) {
    board_ = board;
    rebuildNetCombo();
    plot_->clear();
    emitCavity();
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
    // Collect decaps from the table; treat blank cells as 0.
    std::vector<pdnkit::pi::Decap> decaps;
    for (int row = 0; row < decap_table_->rowCount(); ++row) {
        auto read = [&](int col) -> double {
            auto* item = decap_table_->item(row, col);
            return item ? item->text().toDouble() : 0.0;
        };
        pdnkit::pi::Decap d;
        d.x   = read(0) * 1.0e-3;   // mm -> m
        d.y   = read(1) * 1.0e-3;
        d.C   = read(2) * 1.0e-6;   // uF -> F
        d.esr = read(3) * 1.0e-3;   // mOhm -> Ohm
        d.esl = read(4) * 1.0e-9;   // nH -> H
        if (d.C > 0.0) decaps.push_back(d);
    }

    std::vector<ZfPlotWidget::Curve> curves;

    std::vector<double> mags_main;
    if (decaps.empty()) {
        mags_main = pdnkit::pi::cavity_impedance_magnitude_sweep(
            cfg, x1, y1, x2, y2, freqs);
        ZfPlotWidget::Curve c;
        c.freqs = freqs;
        c.mags  = mags_main;
        c.color = QColor(0xfd, 0xe7, 0x25);   // viridis yellow
        c.label = "Z(f)";
        curves.push_back(std::move(c));
    } else {
        mags_main = pdnkit::pi::cavity_impedance_with_decaps_magnitude_sweep(
            cfg, x1, y1, decaps, freqs);
        ZfPlotWidget::Curve c;
        c.freqs = freqs;
        c.mags  = mags_main;
        c.color = QColor(0xfd, 0xe7, 0x25);
        c.label = "with decaps";
        curves.push_back(std::move(c));

        // Overlay bare plane Z if requested.
        if (overlay_bare_check_->isChecked()) {
            auto mags_bare = pdnkit::pi::cavity_impedance_magnitude_sweep(
                cfg, x1, y1, x1, y1, freqs);
            ZfPlotWidget::Curve b;
            b.freqs = freqs;
            b.mags  = mags_bare;
            b.color = QColor(0x77, 0x88, 0xaa);  // muted blue-gray
            b.label = "bare plane";
            curves.push_back(std::move(b));
        }
    }

    last_freqs_ = freqs;
    last_mags_  = mags_main;
    plot_->setTargetImpedance(target_z_spin_->value());
    plot_->setCurves(std::move(curves));
}

void CavityPanel::onSaveCsv() {
    if (last_freqs_.empty() || last_freqs_.size() != last_mags_.size()) {
        QMessageBox::information(this, "Z(f) export",
            "No sweep result to export. Click Run sweep first.");
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, "Export Z(f) sweep as CSV",
                                                  QString(), "CSV (*.csv)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export failed",
                             QString("Could not open %1 for writing").arg(path));
        return;
    }
    QTextStream out(&f);
    out << "freq_hz,abs_z_ohm\n";
    for (std::size_t i = 0; i < last_freqs_.size(); ++i) {
        out << QString::number(last_freqs_[i], 'g', 8) << ','
            << QString::number(last_mags_[i],  'g', 8) << '\n';
    }
}

void CavityPanel::onClear() {
    last_freqs_.clear();
    last_mags_.clear();
    plot_->clear();
}

void CavityPanel::onAutoSuggest() {
    if (!board_ || net_combo_->count() == 0) {
        QMessageBox::information(this, "Auto-suggest decaps",
            "Load a board and pick a net first.");
        return;
    }
    const int net = net_combo_->currentData().toInt();
    constexpr int kPrimaryLayer = 0;
    const Bbox bb = zone_bbox(*board_, net, kPrimaryLayer);
    if (!bb.ok) {
        QMessageBox::warning(this, "Auto-suggest decaps",
            "No filled zones for the selected net on F.Cu.");
        return;
    }

    pdnkit::pi::CavityConfig cfg;
    cfg.a = bb.hi_x - bb.lo_x;
    cfg.b = bb.hi_y - bb.lo_y;
    cfg.d = thickness_spin_->value() * 1.0e-3;
    cfg.eps_r = eps_r_spin_->value();
    cfg.tan_delta = tan_delta_spin_->value();
    cfg.max_modes = modes_spin_->value();

    pdnkit::pi::DecapOptimizerConfig opt;
    opt.target_z = target_z_spin_->value();
    opt.f_min = f_min_spin_->value();
    opt.f_max = f_max_spin_->value();
    opt.n_points = std::min(points_spin_->value(), 80);  // cap for search speed
    opt.max_caps = 30;
    opt.cap_x = port1_x_->value() * 1.0e-3;
    opt.cap_y = port1_y_->value() * 1.0e-3;

    auto result = pdnkit::pi::optimize_decaps(
        cfg, port1_x_->value() * 1.0e-3, port1_y_->value() * 1.0e-3, opt);

    // Replace the table with the suggested decaps.
    decap_table_->setRowCount(0);
    for (const auto& d : result.decaps) {
        const int row = decap_table_->rowCount();
        decap_table_->insertRow(row);
        decap_table_->setItem(row, 0, new QTableWidgetItem(QString::number(d.x * 1e3, 'f', 2)));
        decap_table_->setItem(row, 1, new QTableWidgetItem(QString::number(d.y * 1e3, 'f', 2)));
        decap_table_->setItem(row, 2, new QTableWidgetItem(QString::number(d.C * 1e6, 'g', 4)));
        decap_table_->setItem(row, 3, new QTableWidgetItem(QString::number(d.esr * 1e3, 'f', 1)));
        decap_table_->setItem(row, 4, new QTableWidgetItem(QString::number(d.esl * 1e9, 'f', 2)));
    }
    emit decapsChanged(read_decap_positions(decap_table_, 0.0, 0.0));

    const QString msg = QString(
        "Suggested %1 decap%2.  Final max |Z| over sweep: %3 mOhm.  "
        "Target %4 mOhm: %5")
        .arg(result.decaps.size())
        .arg(result.decaps.size() == 1 ? "" : "s")
        .arg(result.final_max_z * 1000.0, 0, 'f', 3)
        .arg(opt.target_z * 1000.0, 0, 'f', 3)
        .arg(result.target_met ? "met" : "NOT met (cap budget exhausted)");
    QMessageBox::information(this, "Auto-suggest decaps", msg);
}
