#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>

#include "model/Board.h"
#include "render/Camera2D.h"

class PcbCanvas : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit PcbCanvas(QWidget* parent = nullptr);

    // Attach a board (non-owning). Canvas does not yet tessellate geometry;
    // for now it just keeps a pointer for future passes and resets the
    // camera to fit any board content.
    void setBoard(const pdnkit::model::Board* board);

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

    pdnkit::render::Camera2D camera_;
    const pdnkit::model::Board* board_ = nullptr;

    QOpenGLShaderProgram prog_;
    QOpenGLBuffer grid_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject grid_vao_;
    int grid_vertex_count_ = 0;

    bool panning_ = false;
    QPoint last_mouse_;
};
