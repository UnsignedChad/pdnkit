#include "MainWindow.h"

#include <algorithm>

#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <spdlog/spdlog.h>

#include "AnalysisPanel.h"
#include "ColorLegend.h"
#include "LayerPanel.h"
#include "PcbCanvas.h"
#include "parser/KicadPcbParser.h"
#include "pi/IrMesher.h"
#include "pi/IrSolver.h"
#include "render/IrResultMesh.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("pdnkit");
    resize(1280, 800);

    canvas_ = new PcbCanvas(this);
    legend_ = new ColorLegend(this);

    auto* central = new QWidget(this);
    auto* central_layout = new QHBoxLayout(central);
    central_layout->setContentsMargins(0, 0, 0, 0);
    central_layout->setSpacing(0);
    central_layout->addWidget(canvas_, 1);
    central_layout->addWidget(legend_);
    setCentralWidget(central);

    // Layer-visibility dock panel on the right.
    layer_panel_ = new LayerPanel(this);
    auto* dock = new QDockWidget("Layers", this);
    dock->setWidget(layer_panel_);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    connect(layer_panel_, &LayerPanel::visibility_changed,
            canvas_, &PcbCanvas::setLayerVisibility);

    // Analysis dock under Layers.
    analysis_panel_ = new AnalysisPanel(this);
    auto* an_dock = new QDockWidget("Analysis", this);
    an_dock->setWidget(analysis_panel_);
    an_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, an_dock);
    connect(analysis_panel_, &AnalysisPanel::runRequested,
            this, &MainWindow::onAnalyzeStaticIrDrop);
    connect(analysis_panel_, &AnalysisPanel::clearRequested,
            canvas_, [this]() { canvas_->setIrResult({}); legend_->setRange(0, 0); });

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* openAct = fileMenu->addAction("&Open KiCad PCB...");
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenKicadPcb);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu("&View");
    auto* fitAct = viewMenu->addAction("&Fit to Board");
    fitAct->setShortcut(QKeySequence(Qt::Key_Home));
    connect(fitAct, &QAction::triggered, canvas_, &PcbCanvas::fitToBoard);
    viewMenu->addAction(dock->toggleViewAction());
    viewMenu->addAction(an_dock->toggleViewAction());

    auto* analyzeMenu = menuBar()->addMenu("&Analyze");
    auto* irAct = analyzeMenu->addAction("Static &IR drop on F.Cu");
    irAct->setShortcut(QKeySequence("Ctrl+I"));
    connect(irAct, &QAction::triggered, this, &MainWindow::onAnalyzeStaticIrDrop);
    auto* clearAct = analyzeMenu->addAction("&Clear overlay");
    connect(clearAct, &QAction::triggered, canvas_, [this]() {
        canvas_->setIrResult({});
        legend_->setRange(0, 0);
    });

    // Permanent label on the right of the status bar for hover info.
    hover_label_ = new QLabel(this);
    hover_label_->setMinimumWidth(300);
    statusBar()->addPermanentWidget(hover_label_);
    connect(canvas_, &PcbCanvas::hoverInfo, hover_label_, &QLabel::setText);

    statusBar()->showMessage("Ready");
}

void MainWindow::onOpenKicadPcb() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open KiCad PCB", QString(),
        "KiCad PCB (*.kicad_pcb);;All files (*)");
    if (path.isEmpty()) return;
    loadKicadPcb(path);
}

void MainWindow::onAnalyzeStaticIrDrop() {
    if (!board_) {
        QMessageBox::information(this, "Static IR drop",
                                 "Open a KiCad PCB first.");
        return;
    }

    // Net, layer, source/sink, current, and cell size all come from the
    // AnalysisPanel — see currentConfig() / currentTotalCurrent().
    auto mc = analysis_panel_->currentConfig();
    const double total_current = analysis_panel_->currentTotalCurrent();
    if (mc.net_id < 0) {
        QMessageBox::warning(this, "Static IR drop",
                             "No net with copper zones available.");
        return;
    }
    auto mesh = pdnkit::pi::IrMesher::build(*board_, mc);
    if (mesh.nodes.empty()) {
        QMessageBox::warning(this, "Static IR drop",
                             "Mesher produced no nodes for the selected net "
                             "(check cell_size vs. zone size).");
        return;
    }
    if (mesh.source_node_ids.empty() || mesh.sink_node_ids.empty()) {
        QMessageBox::warning(this, "Static IR drop",
                             "Need at least two pads on the target net to set "
                             "source/sink. Found insufficient pads on F.Cu.");
        return;
    }

    auto sol = pdnkit::pi::IrSolver::solve(mesh, {total_current});
    if (!sol.ok) {
        QMessageBox::critical(this, "Static IR drop",
                              QString("Solver failed: %1")
                                  .arg(QString::fromStdString(sol.error)));
        return;
    }

    auto result_mesh = pdnkit::render::build_ir_result_mesh(mesh, sol,
                                                             mc.cell_size);
    canvas_->setIrResult(std::move(result_mesh));
    legend_->setRange(sol.min_v, sol.max_v);

    const auto* net = board_->find_net(mc.net_id);
    const QString net_name = (net && !net->name.empty())
        ? QString::fromStdString(net->name)
        : QString("net %1").arg(mc.net_id);
    const double v_drop_mv = (sol.max_v - sol.min_v) * 1000.0;
    const auto* layer = board_->find_layer(mc.layer_ordinal);
    const QString layer_name = layer
        ? QString::fromStdString(layer->name)
        : QString("layer %1").arg(mc.layer_ordinal);
    statusBar()->showMessage(
        QString("IR drop on %1 (%2, %3A): %4 nodes, %5 resistors, "
                "Vmax = %6 mV, Vmin = %7 mV  (drop %8 mV)")
            .arg(net_name)
            .arg(layer_name)
            .arg(total_current, 0, 'f', 3)
            .arg(mesh.nodes.size())
            .arg(mesh.resistors.size())
            .arg(sol.max_v * 1000.0, 0, 'f', 4)
            .arg(sol.min_v * 1000.0, 0, 'f', 4)
            .arg(v_drop_mv, 0, 'f', 4));
    spdlog::info("IR drop on net {} ({}) layer {}: {} nodes, {} resistors, "
                 "Vmax={:.6f}V, Vmin={:.6f}V",
                 mc.net_id, net_name.toStdString(), mc.layer_ordinal,
                 mesh.nodes.size(), mesh.resistors.size(),
                 sol.max_v, sol.min_v);
}

void MainWindow::populateLayerPanel() {
    if (!board_) {
        layer_panel_->setLayers({});
        return;
    }
    std::vector<LayerPanel::Entry> entries;
    for (const auto& L : board_->stackup.layers) {
        if (!L.is_copper()) continue;
        entries.push_back({L.ordinal, QString::fromStdString(L.name)});
    }
    layer_panel_->setLayers(entries);
}

bool MainWindow::loadKicadPcb(const QString& path) {
    try {
        auto board = std::make_unique<pdnkit::model::Board>(
            pdnkit::parser::KicadPcbParser::parse_file(path.toStdString()));

        const auto net_count   = board->nets.size();
        const auto seg_count   = board->segments.size();
        const auto via_count   = board->vias.size();
        const auto pad_count   = board->pads.size();
        const auto zone_count  = board->zones.size();
        const auto layer_count = board->stackup.layers.size();
        const auto copper_layers = std::count_if(
            board->stackup.layers.begin(), board->stackup.layers.end(),
            [](const auto& l) { return l.is_copper(); });

        board_ = std::move(board);
        canvas_->setBoard(board_.get());
        populateLayerPanel();
        analysis_panel_->setBoard(board_.get());

        spdlog::info("loaded {}: {} layers ({} copper), {} nets, {} segments, "
                     "{} vias, {} pads, {} zones",
                     path.toStdString(), layer_count, copper_layers,
                     net_count, seg_count, via_count, pad_count, zone_count);

        const QString summary = QString(
            "Loaded %1  —  %2 nets, %3 segments, %4 vias, %5 pads, %6 zones (%7 copper layers)")
            .arg(QFileInfo(path).fileName())
            .arg(net_count).arg(seg_count).arg(via_count)
            .arg(pad_count).arg(zone_count).arg(copper_layers);
        statusBar()->showMessage(summary);
        setWindowTitle(QString("pdnkit — %1").arg(QFileInfo(path).fileName()));
        return true;
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Open KiCad PCB failed", e.what());
        spdlog::error("failed to load {}: {}", path.toStdString(), e.what());
        return false;
    }
}
