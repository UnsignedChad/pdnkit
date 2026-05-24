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
#include "render/SegmentMesher.h"

class PcbCanvas : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit PcbCanvas(QWidget* parent = nullptr);

    // Attach a board (non-owning). Triggers zone tessellation and a fit-to-bounds
    // of the camera. GPU buffers are populated lazily on the next paintGL().
    void setBoard(const pdnkit::model::Board* board);

    // Toggle a single layer's visibility. Re-paints; no rebuild of GPU data.
    void setLayerVisibility(int ordinal, bool visible);

    // Re-run fit-to-bounds on whatever board is currently loaded.
    void fitToBoard();

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

    struct LayerRange {
        int ordinal = 0;
        int index_start = 0;
        int index_count = 0;
    };

    pdnkit::render::Camera2D camera_;
    const pdnkit::model::Board* board_ = nullptr;

    QOpenGLShaderProgram prog_;

    QOpenGLBuffer grid_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject grid_vao_;
    int grid_vertex_count_ = 0;

    QOpenGLBuffer board_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer board_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject board_vao_;
    std::vector<LayerRange> layer_ranges_;
    std::vector<pdnkit::render::LayerMesh> pending_meshes_;
    bool meshes_dirty_ = false;

    // Per-layer visibility. Absent = visible (default). Map only stores overrides.
    std::unordered_map<int, bool> layer_visible_;

    bool panning_ = false;
    QPoint last_mouse_;
};
