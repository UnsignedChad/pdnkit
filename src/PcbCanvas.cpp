#include "PcbCanvas.h"

#include <vector>

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QVector4D>
#include <QWheelEvent>

namespace {

// Minimal flat-color 2D shader. Used for grid lines for now; will be reused
// for board geometry (zones / segments / pads) as those land.
constexpr auto kVertexSrc = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
uniform mat4 u_proj;
void main() {
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
}
)";

constexpr auto kFragmentSrc = R"(
#version 330 core
uniform vec4 u_color;
out vec4 frag_color;
void main() {
    frag_color = u_color;
}
)";

}  // namespace

PcbCanvas::PcbCanvas(QWidget* parent) : QOpenGLWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void PcbCanvas::setBoard(const pdnkit::model::Board* board) {
    board_ = board;
    // Compute bounds from segments + pads + zone outlines, then fit camera.
    if (board_ && (!board_->segments.empty() || !board_->pads.empty() ||
                   !board_->zones.empty())) {
        bool have_any = false;
        double lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;
        auto include = [&](double x, double y) {
            if (!have_any) {
                lo_x = hi_x = x;
                lo_y = hi_y = y;
                have_any = true;
            } else {
                if (x < lo_x) lo_x = x;
                if (x > hi_x) hi_x = x;
                if (y < lo_y) lo_y = y;
                if (y > hi_y) hi_y = y;
            }
        };
        for (const auto& s : board_->segments) {
            include(s.start.x, s.start.y);
            include(s.end.x, s.end.y);
        }
        for (const auto& p : board_->pads) include(p.at.x, p.at.y);
        for (const auto& z : board_->zones) {
            for (const auto& pt : z.outline.outline) include(pt.x, pt.y);
            for (const auto& fp : z.filled)
                for (const auto& pt : fp.outline) include(pt.x, pt.y);
        }
        if (have_any) {
            camera_.fit_to_bounds({lo_x, lo_y}, {hi_x, hi_y},
                                   width(), height(), 0.10);
        }
    }
    update();
}

void PcbCanvas::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);

    prog_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexSrc);
    prog_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentSrc);
    prog_.link();

    grid_vao_.create();
    grid_vbo_.create();
    buildGrid();
}

void PcbCanvas::buildGrid() {
    // 10mm-spaced grid covering ±0.5 m around origin. Cheap, plenty for now.
    // Will be replaced with a view-adaptive grid once the renderer matures.
    std::vector<float> verts;
    const float lo = -0.5f, hi = 0.5f;
    const float step = 0.010f;  // 10mm
    for (float v = lo; v <= hi + 1e-6f; v += step) {
        verts.insert(verts.end(), {v, lo, v, hi});  // vertical line at x=v
        verts.insert(verts.end(), {lo, v, hi, v});  // horizontal line at y=v
    }
    grid_vertex_count_ = static_cast<int>(verts.size() / 2);

    grid_vao_.bind();
    grid_vbo_.bind();
    grid_vbo_.allocate(verts.data(),
                       static_cast<int>(verts.size() * sizeof(float)));
    prog_.enableAttributeArray(0);
    prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    grid_vbo_.release();
    grid_vao_.release();
}

void PcbCanvas::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void PcbCanvas::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);

    // Camera2D returns column-major; QMatrix4x4(float...) takes row-major,
    // so transpose at construction time.
    const auto m = camera_.ortho_matrix(width(), height());
    const QMatrix4x4 proj(
        m[0], m[4], m[8],  m[12],
        m[1], m[5], m[9],  m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15]);

    prog_.bind();
    prog_.setUniformValue("u_proj", proj);
    prog_.setUniformValue("u_color", QVector4D(0.22f, 0.22f, 0.28f, 1.0f));

    grid_vao_.bind();
    glDrawArrays(GL_LINES, 0, grid_vertex_count_);
    grid_vao_.release();

    prog_.release();
}

void PcbCanvas::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton) {
        panning_ = true;
        last_mouse_ = e->pos();
    }
}

void PcbCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (panning_) {
        const QPoint d = e->pos() - last_mouse_;
        camera_.pan_pixels(d.x(), d.y());
        last_mouse_ = e->pos();
        update();
    }
}

void PcbCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton) {
        panning_ = false;
    }
}

void PcbCanvas::wheelEvent(QWheelEvent* e) {
    const double factor = (e->angleDelta().y() > 0) ? 1.20 : 1.0 / 1.20;
    const QPointF pos = e->position();
    camera_.zoom_at(pos.x(), pos.y(), factor, width(), height());
    update();
}
