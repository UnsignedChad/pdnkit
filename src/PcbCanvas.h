#pragma once

#include <unordered_map>
#include <vector>

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>

#include "model/Board.h"
#include "render/Camera2D.h"
#include "render/IrResultMesh.h"
#include "render/SegmentMesher.h"

class PcbCanvas : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit PcbCanvas(QWidget* parent = nullptr);

    void setBoard(const pdnkit::model::Board* board);
    void setLayerVisibility(int ordinal, bool visible);
    void fitToBoard();

    // Persist / restore the cameras center and zoom across launches.
    // QSettings is passed by reference so MainWindow can group with its own
    // geometry/state save under a common organisation/app namespace.
    void saveSettings(class QSettings& settings) const;
    void restoreSettings(class QSettings& settings);

    // Attach (or clear, with an empty mesh) an IR-drop heat-map overlay.
    // Uploaded lazily on the next paintGL.
    void setIrResult(pdnkit::render::IrResultMesh result);

signals:
    void hoverInfo(const QString& info);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    void buildGrid();
    void uploadBoardMeshes();
    void uploadIrResult();

    struct LayerRange {
        int ordinal = 0;
        int index_start = 0;
        int index_count = 0;
    };

    pdnkit::render::Camera2D camera_;
    const pdnkit::model::Board* board_ = nullptr;

    QOpenGLShaderProgram flat_prog_;  // grid + board layer fills
    QOpenGLShaderProgram heat_prog_;  // IR-drop overlay (viridis)

    QOpenGLBuffer grid_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject grid_vao_;
    int grid_vertex_count_ = 0;

    QOpenGLBuffer board_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer board_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject board_vao_;
    std::vector<LayerRange> layer_ranges_;
    std::vector<pdnkit::render::LayerMesh> pending_meshes_;
    bool meshes_dirty_ = false;

    QOpenGLBuffer heat_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer heat_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject heat_vao_;
    pdnkit::render::IrResultMesh pending_heat_;
    std::vector<pdnkit::render::IrResultMesh::LayerRange> heat_layer_ranges_;
    int heat_index_count_ = 0;
    bool heat_dirty_ = false;

    std::unordered_map<int, bool> layer_visible_;

    bool panning_ = false;
    QPoint last_mouse_;
};
