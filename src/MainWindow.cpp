#include "MainWindow.h"

#include <algorithm>

#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <spdlog/spdlog.h>

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
    setCentralWidget(canvas_);

    // Layer-visibility dock panel on the right.
    layer_panel_ = new LayerPanel(this);
    auto* dock = new QDockWidget("Layers", this);
    dock->setWidget(layer_panel_);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    connect(layer_panel_, &LayerPanel::visibility_changed,
            canvas_, &PcbCanvas::setLayerVisibility);

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

    auto* analyzeMenu = menuBar()->addMenu("&Analyze");
    auto* irAct = analyzeMenu->addAction("Static &IR drop on F.Cu");
    irAct->setShortcut(QKeySequence("Ctrl+I"));
    connect(irAct, &QAction::triggered, this, &MainWindow::onAnalyzeStaticIrDrop);
    auto* clearAct = analyzeMenu->addAction("&Clear overlay");
    connect(clearAct, &QAction::triggered, canvas_, [this]() {
        canvas_->setIrResult({});
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

    // v0 net selection: first net that has at least one zone on F.Cu (ord 0).
    // Future commit replaces this with a proper net-selector UI.
    int target_net = -1;
    for (const auto& z : board_->zones) {
        if (z.layer_ordinal == 0 && !z.filled.empty()) {
            target_net = z.net_id;
            break;
        }
    }
    if (target_net < 0) {
        QMessageBox::warning(this, "Static IR drop",
                             "No zone fill found on F.Cu — nothing to analyze.");
        return;
    }

    pdnkit::pi::MeshConfig mc;
    mc.cell_size = 0.5e-3;  // 0.5 mm default; ~thousands of nodes per cm²
    mc.net_id = target_net;
    mc.layer_ordinal = 0;
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

    auto sol = pdnkit::pi::IrSolver::solve(mesh, {1.0});
    if (!sol.ok) {
        QMessageBox::critical(this, "Static IR drop",
                              QString("Solver failed: %1")
                                  .arg(QString::fromStdString(sol.error)));
        return;
    }

    auto result_mesh = pdnkit::render::build_ir_result_mesh(mesh, sol,
                                                             mc.cell_size);
    canvas_->setIrResult(std::move(result_mesh));

    const auto* net = board_->find_net(target_net);
    const QString net_name = (net && !net->name.empty())
        ? QString::fromStdString(net->name)
        : QString("net %1").arg(target_net);
    const double v_drop_mv = (sol.max_v - sol.min_v) * 1000.0;
    statusBar()->showMessage(
        QString("IR drop on %1 (F.Cu, 1A): %2 nodes, %3 resistors, "
                "Vmax = %4 mV, Vmin = %5 mV  (drop %6 mV)")
            .arg(net_name)
            .arg(mesh.nodes.size())
            .arg(mesh.resistors.size())
            .arg(sol.max_v * 1000.0, 0, 'f', 4)
            .arg(sol.min_v * 1000.0, 0, 'f', 4)
            .arg(v_drop_mv, 0, 'f', 4));
    spdlog::info("IR drop on net {} ({}): {} nodes, {} resistors, "
                 "Vmax={:.6f}V, Vmin={:.6f}V",
                 target_net, net_name.toStdString(),
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
