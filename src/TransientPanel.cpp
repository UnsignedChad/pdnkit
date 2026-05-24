#include "TransientPanel.h"

#include <set>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "TransientPlotWidget.h"
#include "pi/IrMesher.h"
#include "pi/Transient.h"

TransientPanel::TransientPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    auto* header = new QLabel("Transient (step response)");
    QFont f = header->font();
    f.setBold(true);
    header->setFont(f);
    outer->addWidget(header);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(4);

    net_combo_ = new QComboBox();
    layer_combo_ = new QComboBox();

    current_spin_ = new QDoubleSpinBox();
    current_spin_->setRange(0.001, 1000.0);
    current_spin_->setDecimals(3);
    current_spin_->setValue(1.0);
    current_spin_->setSuffix(" A");

    dt_spin_ = new QDoubleSpinBox();
    dt_spin_->setRange(0.001, 1000.0);
    dt_spin_->setDecimals(3);
    dt_spin_->setValue(10.0);
    dt_spin_->setSuffix(" ns");

    nsteps_spin_ = new QSpinBox();
    nsteps_spin_->setRange(10, 100000);
    nsteps_spin_->setValue(1000);

    cell_spin_ = new QDoubleSpinBox();
    cell_spin_->setRange(0.05, 5.0);
    cell_spin_->setDecimals(2);
    cell_spin_->setValue(0.5);
    cell_spin_->setSuffix(" mm");

    epsr_spin_ = new QDoubleSpinBox();
    epsr_spin_->setRange(1.0, 20.0);
    epsr_spin_->setDecimals(2);
    epsr_spin_->setValue(4.3);

    thickness_spin_ = new QDoubleSpinBox();
    thickness_spin_->setRange(0.05, 10.0);
    thickness_spin_->setDecimals(3);
    thickness_spin_->setValue(1.6);
    thickness_spin_->setSuffix(" mm");

    form->addRow("Net:",        net_combo_);
    form->addRow("Layer:",      layer_combo_);
    form->addRow("Step I:",     current_spin_);
    form->addRow("dt:",         dt_spin_);
    form->addRow("Steps:",      nsteps_spin_);
    form->addRow("Cell size:",  cell_spin_);
    form->addRow("eps_r:",      epsr_spin_);
    form->addRow("Substrate d:", thickness_spin_);
    outer->addLayout(form);

    auto* btn_row = new QHBoxLayout();
    run_btn_   = new QPushButton("Run transient");
    clear_btn_ = new QPushButton("Clear");
    btn_row->addWidget(run_btn_);
    btn_row->addWidget(clear_btn_);
    outer->addLayout(btn_row);

    plot_ = new TransientPlotWidget(this);
    outer->addWidget(plot_, 1);

    connect(run_btn_,   &QPushButton::clicked, this, &TransientPanel::onRun);
    connect(clear_btn_, &QPushButton::clicked, this, &TransientPanel::onClear);
}

void TransientPanel::setBoard(const pdnkit::model::Board* board) {
    board_ = board;
    rebuildNetCombo();
    layer_combo_->clear();
    if (board_) {
        for (const auto& L : board_->stackup.layers) {
            if (!L.is_copper()) continue;
            layer_combo_->addItem(QString::fromStdString(L.name), L.ordinal);
        }
    }
    plot_->clear();
}

void TransientPanel::rebuildNetCombo() {
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

void TransientPanel::onClear() {
    plot_->clear();
}

void TransientPanel::onRun() {
    if (!board_ || net_combo_->count() == 0 || layer_combo_->count() == 0) {
        QMessageBox::information(this, "Transient",
            "Open a board and select a net + layer first.");
        return;
    }

    pdnkit::pi::MeshConfig mc;
    mc.cell_size = cell_spin_->value() * 1.0e-3;
    mc.net_id = net_combo_->currentData().toInt();
    mc.layer_ordinal = layer_combo_->currentData().toInt();

    auto mesh = pdnkit::pi::IrMesher::build(*board_, mc);
    if (mesh.nodes.empty()) {
        QMessageBox::warning(this, "Transient",
            "Mesher produced no nodes -- no copper to step.");
        return;
    }
    if (mesh.source_node_ids.empty() || mesh.sink_node_ids.empty()) {
        QMessageBox::warning(this, "Transient",
            "Need at least 2 pads on the net for source/sink auto-pick.");
        return;
    }

    // Build distributed C from plane physics.
    auto c_vec = pdnkit::pi::build_distributed_capacitance(
        mesh, mc.cell_size, epsr_spin_->value(),
        thickness_spin_->value() * 1.0e-3, {});

    pdnkit::pi::TransientConfig tcfg;
    tcfg.per_node_capacitances = std::move(c_vec);
    tcfg.dt = dt_spin_->value() * 1.0e-9;
    tcfg.n_steps = nsteps_spin_->value();
    tcfg.step_current = current_spin_->value();

    auto res = pdnkit::pi::solve_step_transient(mesh, tcfg);
    if (!res.ok) {
        QMessageBox::critical(this, "Transient",
            QString("Solver failed: %1").arg(QString::fromStdString(res.error)));
        return;
    }
    plot_->setData(std::move(res.times),
                   std::move(res.obs_v),
                   std::move(res.max_v));
}
