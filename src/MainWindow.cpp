#include "MainWindow.h"

#include <algorithm>

#include <QDockWidget>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QSettings>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QImage>
#include <QMessageBox>
#include <QStatusBar>
#include <QTextStream>
#include <spdlog/spdlog.h>

#include "AnalysisPanel.h"
#include "ColorLegend.h"
#include "CavityPanel.h"
#include "NetStatsPanel.h"
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

    // Net statistics dock, tabbed with Analysis on the right.
    netstats_panel_ = new NetStatsPanel(this);
    auto* nets_dock = new QDockWidget("Net Stats", this);
    nets_dock->setWidget(netstats_panel_);
    nets_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, nets_dock);
    tabifyDockWidget(an_dock, nets_dock);

    cavity_panel_ = new CavityPanel(this);
    auto* cav_dock = new QDockWidget("Plane Z(f)", this);
    cav_dock->setWidget(cavity_panel_);
    cav_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, cav_dock);
    tabifyDockWidget(an_dock, cav_dock);
    connect(cavity_panel_, &CavityPanel::decapsChanged,
            canvas_, &PcbCanvas::setDecapMarkers);
    connect(cavity_panel_, &CavityPanel::cavityChanged,
            canvas_, &PcbCanvas::setCavityHighlight);
    connect(cavity_panel_, &CavityPanel::modeShapeMesh, this,
            [this](pdnkit::render::IrResultMesh m) {
                legend_->setRange(m.v_min, m.v_max);
                canvas_->setIrResult(std::move(m));
            });

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* openAct = fileMenu->addAction("&Open KiCad PCB...");
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenKicadPcb);
    auto* reloadAct = fileMenu->addAction("&Reload");
    reloadAct->setShortcut(QKeySequence("Ctrl+R"));
    connect(reloadAct, &QAction::triggered, this, &MainWindow::onReloadBoard);
    fileMenu->addSeparator();
    auto* saveImgAct = fileMenu->addAction("&Save Canvas as Image...");
    saveImgAct->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(saveImgAct, &QAction::triggered, this, &MainWindow::onSaveCanvasImage);
    auto* exportCsvAct = fileMenu->addAction("&Export Results as CSV...");
    connect(exportCsvAct, &QAction::triggered, this, &MainWindow::onExportResultsCsv);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu("&View");
    auto* fitAct = viewMenu->addAction("&Fit to Board");
    fitAct->setShortcut(QKeySequence(Qt::Key_Home));
    connect(fitAct, &QAction::triggered, canvas_, &PcbCanvas::fitToBoard);
    viewMenu->addAction(dock->toggleViewAction());
    viewMenu->addAction(an_dock->toggleViewAction());
    viewMenu->addAction(nets_dock->toggleViewAction());
    viewMenu->addAction(cav_dock->toggleViewAction());

    auto* analyzeMenu = menuBar()->addMenu("&Analyze");
    auto* irAct = analyzeMenu->addAction("Static &IR drop on F.Cu");
    irAct->setShortcut(QKeySequence("Ctrl+I"));
    connect(irAct, &QAction::triggered, this, &MainWindow::onAnalyzeStaticIrDrop);
    auto* clearAct = analyzeMenu->addAction("&Clear overlay");
    connect(clearAct, &QAction::triggered, canvas_, [this]() {
        canvas_->setIrResult({});
        legend_->setRange(0, 0);
    });

    auto* helpMenu = menuBar()->addMenu("&Help");
    auto* aboutAct = helpMenu->addAction("&About pdnkit...");
    connect(aboutAct, &QAction::triggered, this, &MainWindow::onAboutDialog);

    // Permanent label on the right of the status bar for hover info.
    hover_label_ = new QLabel(this);
    hover_label_->setMinimumWidth(300);
    statusBar()->addPermanentWidget(hover_label_);
    connect(canvas_, &PcbCanvas::hoverInfo, hover_label_, &QLabel::setText);

    statusBar()->showMessage("Ready");

    // Restore previously-saved window geometry + dock state + camera.
    QSettings settings("pdnkit", "pdnkit");
    if (auto geom = settings.value("window/geometry").toByteArray(); !geom.isEmpty()) {
        restoreGeometry(geom);
    }
    if (auto st = settings.value("window/state").toByteArray(); !st.isEmpty()) {
        restoreState(st);
    }
    canvas_->restoreSettings(settings);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    QSettings settings("pdnkit", "pdnkit");
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/state", saveState());
    canvas_->saveSettings(settings);
    QMainWindow::closeEvent(e);
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
    if (mc.net_id < 0) {
        QMessageBox::warning(this, "Static IR drop",
                             "No net with copper zones available.");
        return;
    }
    if (mc.pad_currents.empty()) {
        QMessageBox::warning(this, "Static IR drop",
                             "Set at least one non-zero pad current. "
                             "Click Auto-balance for a default.");
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

    auto sol = pdnkit::pi::IrSolver::solve(mesh, {});
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
    last_mesh_ = std::move(mesh);
    last_solution_ = std::move(sol);

    const auto* net = board_->find_net(mc.net_id);
    const QString net_name = (net && !net->name.empty())
        ? QString::fromStdString(net->name)
        : QString("net %1").arg(mc.net_id);
    const double v_drop_mv = (sol.max_v - sol.min_v) * 1000.0;
    const int reported_layer = (mesh.primary_layer_used >= 0)
        ? mesh.primary_layer_used : mc.layer_ordinal;
    const auto* layer = board_->find_layer(reported_layer);
    const QString layer_name = layer
        ? QString::fromStdString(layer->name) +
            (reported_layer != mc.layer_ordinal ? " (auto)" : "")
        : QString("layer %1").arg(reported_layer);
    double total_injected = 0.0;
    for (const auto& [_, cur] : mc.pad_currents) {
        if (cur > 0.0) total_injected += cur;
    }
    statusBar()->showMessage(
        QString("IR drop on %1 (%2, %3 A injected): %4 nodes, %5 resistors, "
                "Vmax = %6 mV, Vmin = %7 mV  (drop %8 mV)")
            .arg(net_name)
            .arg(layer_name)
            .arg(total_injected, 0, 'f', 3)
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
        LayerPanel::Entry e;
        e.ordinal = L.ordinal;
        e.name = QString::fromStdString(L.name);
        e.thickness_um = L.thickness * 1.0e6;  // m -> um
        entries.push_back(e);
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
        current_board_path_ = path;
        analysis_panel_->setBoard(board_.get());
        netstats_panel_->setBoard(board_.get());
        cavity_panel_->setBoard(board_.get());

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

void MainWindow::onSaveCanvasImage() {
    if (!canvas_) return;
    QString path = QFileDialog::getSaveFileName(
        this, "Save canvas as image", QString(),
        "PNG (*.png);;JPEG (*.jpg);;BMP (*.bmp)");
    if (path.isEmpty()) return;
    QImage img = canvas_->grabFramebuffer();
    if (img.save(path)) {
        statusBar()->showMessage(
            QString("Saved %1  (%2 x %3)")
                .arg(QFileInfo(path).fileName())
                .arg(img.width())
                .arg(img.height()));
    } else {
        QMessageBox::warning(this, "Save failed",
                             QString("Could not write %1").arg(path));
    }
}

void MainWindow::onExportResultsCsv() {
    if (!last_solution_.ok || last_mesh_.nodes.empty() ||
        last_solution_.voltages.size() != last_mesh_.nodes.size()) {
        QMessageBox::information(this, "Export results",
            "No analysis result to export. Run Analyze first.");
        return;
    }
    QString path = QFileDialog::getSaveFileName(
        this, "Export results as CSV", QString(), "CSV (*.csv)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export failed",
                             QString("Could not open %1 for writing").arg(path));
        return;
    }
    QTextStream out(&f);
    out << "node_id,x_mm,y_mm,voltage_mV\n";
    for (std::size_t i = 0; i < last_mesh_.nodes.size(); ++i) {
        const auto& n = last_mesh_.nodes[i];
        out << n.id << ','
            << QString::number(n.x * 1000.0, 'f', 4) << ','
            << QString::number(n.y * 1000.0, 'f', 4) << ','
            << QString::number(last_solution_.voltages[i] * 1000.0, 'f', 6)
            << '\n';
    }
    statusBar()->showMessage(
        QString("Exported %1 nodes to %2")
            .arg(last_mesh_.nodes.size())
            .arg(QFileInfo(path).fileName()));
}

void MainWindow::onReloadBoard() {
    if (current_board_path_.isEmpty()) {
        QMessageBox::information(this, "Reload", "No board loaded yet.");
        return;
    }
    if (!loadKicadPcb(current_board_path_)) {
        // loadKicadPcb already showed an error box; status bar will reflect it.
    }
}

void MainWindow::onAboutDialog() {
    QMessageBox::about(this, "About pdnkit",
        "<h3>pdnkit 0.0.1</h3>"
        "<p>Open-source Power Integrity analysis for KiCad PCBs.</p>"
        "<p><b>Pillars:</b></p>"
        "<ul>"
        "<li>Static IR drop (sparse Cholesky, multi-layer mesh, via wiring)</li>"
        "<li>Frequency-domain plane Z(f) (cavity model, decap network, "
        "greedy decap optimizer)</li>"
        "</ul>"
        "<p>Built with C++20, Qt 6, Eigen + SuiteSparse / CHOLMOD, earcut.hpp.</p>"
        "<p>License: GPL-3.0<br>"
        "Source: <a href=\"https://github.com/UnsignedChad/pdnkit\">"
        "github.com/UnsignedChad/pdnkit</a></p>");
}
