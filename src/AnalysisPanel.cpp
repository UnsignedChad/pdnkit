#include "AnalysisPanel.h"

#include <set>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

// Sentinel meaning "let the mesher auto-pick".
constexpr const char* kAuto = "(auto)";

}  // namespace

AnalysisPanel::AnalysisPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    auto* header = new QLabel("Analysis");
    QFont f = header->font();
    f.setBold(true);
    header->setFont(f);
    outer->addWidget(header);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(4);

    net_combo_ = new QComboBox();
    layer_combo_ = new QComboBox();
    source_combo_ = new QComboBox();
    sink_combo_ = new QComboBox();

    current_spin_ = new QDoubleSpinBox();
    current_spin_->setRange(0.001, 100.0);
    current_spin_->setDecimals(3);
    current_spin_->setSingleStep(0.1);
    current_spin_->setValue(1.0);
    current_spin_->setSuffix(" A");

    cell_size_spin_ = new QDoubleSpinBox();
    cell_size_spin_->setRange(0.05, 5.0);
    cell_size_spin_->setDecimals(2);
    cell_size_spin_->setSingleStep(0.1);
    cell_size_spin_->setValue(0.5);
    cell_size_spin_->setSuffix(" mm");

    form->addRow("Net:",     net_combo_);
    form->addRow("Layer:",   layer_combo_);
    form->addRow("Source:",  source_combo_);
    form->addRow("Sink:",    sink_combo_);
    form->addRow("Current:", current_spin_);
    form->addRow("Cell:",    cell_size_spin_);
    outer->addLayout(form);

    auto* btn_row = new QHBoxLayout();
    run_btn_ = new QPushButton("Run");
    clear_btn_ = new QPushButton("Clear");
    btn_row->addWidget(run_btn_);
    btn_row->addWidget(clear_btn_);
    outer->addLayout(btn_row);
    outer->addStretch();

    connect(net_combo_,   QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AnalysisPanel::onNetOrLayerChanged);
    connect(layer_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AnalysisPanel::onNetOrLayerChanged);
    connect(run_btn_,   &QPushButton::clicked, this, &AnalysisPanel::runRequested);
    connect(clear_btn_, &QPushButton::clicked, this, &AnalysisPanel::clearRequested);
}

void AnalysisPanel::setBoard(const pdnkit::model::Board* board) {
    board_ = board;

    QSignalBlocker bn(net_combo_);
    QSignalBlocker bl(layer_combo_);
    net_combo_->clear();
    layer_combo_->clear();

    if (!board_) {
        refreshPadCombos();
        return;
    }

    // Layer combo: every copper layer in the stackup.
    for (const auto& L : board_->stackup.layers) {
        if (!L.is_copper()) continue;
        layer_combo_->addItem(QString::fromStdString(L.name), L.ordinal);
    }

    // Net combo: nets that have at least one filled zone somewhere.
    std::set<int> nets_with_copper;
    for (const auto& z : board_->zones) {
        if (!z.filled.empty()) nets_with_copper.insert(z.net_id);
    }
    for (int id : nets_with_copper) {
        const auto* n = board_->find_net(id);
        const QString name = (n && !n->name.empty())
            ? QString::fromStdString(n->name)
            : QString("net %1").arg(id);
        net_combo_->addItem(QString("%1 (#%2)").arg(name).arg(id), id);
    }

    refreshPadCombos();
}

void AnalysisPanel::onNetOrLayerChanged() {
    refreshPadCombos();
}

void AnalysisPanel::refreshPadCombos() {
    QSignalBlocker bs(source_combo_);
    QSignalBlocker bk(sink_combo_);
    source_combo_->clear();
    sink_combo_->clear();
    source_combo_->addItem(kAuto);
    sink_combo_->addItem(kAuto);

    if (!board_ || net_combo_->count() == 0 || layer_combo_->count() == 0) {
        return;
    }
    const int net = net_combo_->currentData().toInt();
    const int layer = layer_combo_->currentData().toInt();

    for (const auto& p : board_->pads) {
        if (p.net_id != net) continue;
        bool on_layer = false;
        for (int o : p.layer_ordinals) {
            if (o == layer) { on_layer = true; break; }
        }
        if (!on_layer) continue;
        const QString label = QString("%1   (%2, %3)")
            .arg(p.name.empty() ? QString("(unnamed)")
                                 : QString::fromStdString(p.name))
            .arg(p.at.x * 1000.0, 0, 'f', 2)
            .arg(p.at.y * 1000.0, 0, 'f', 2);
        source_combo_->addItem(label, QString::fromStdString(p.name));
        sink_combo_->addItem(label, QString::fromStdString(p.name));
    }
}

pdnkit::pi::MeshConfig AnalysisPanel::currentConfig() const {
    pdnkit::pi::MeshConfig cfg;
    cfg.cell_size = cell_size_spin_->value() * 1.0e-3;  // mm → m
    cfg.net_id = (net_combo_->count() > 0)
        ? net_combo_->currentData().toInt() : -1;
    cfg.layer_ordinal = (layer_combo_->count() > 0)
        ? layer_combo_->currentData().toInt() : 0;

    if (source_combo_->currentText() != kAuto) {
        cfg.source_pad_names.push_back(
            source_combo_->currentData().toString().toStdString());
    }
    if (sink_combo_->currentText() != kAuto) {
        cfg.sink_pad_names.push_back(
            sink_combo_->currentData().toString().toStdString());
    }
    return cfg;
}

double AnalysisPanel::currentTotalCurrent() const {
    return current_spin_->value();
}
