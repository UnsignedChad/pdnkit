#include "PcbCanvas.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QVector4D>
#include <QWheelEvent>

namespace {

// Minimal flat-color 2D shader. Used for grid lines and per-layer zone fills.
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

QVector4D color_for_layer(int ord) {
    switch (ord) {
        case 0:  return {0.82f, 0.20f, 0.20f, 0.80f};  // F.Cu — warm red
        case 31: return {0.20f, 0.55f, 0.85f, 0.80f};  // B.Cu — cool blue
        default: break;
    }
    // Inner copper (1..30): rotate through a palette.
    static const QVector4D palette[] = {
        {0.30f, 0.75f, 0.30f, 0.70f},  // green
        {0.78f, 0.55f, 0.20f, 0.70f},  // orange
        {0.65f, 0.30f, 0.78f, 0.70f},  // purple
        {0.30f, 0.72f, 0.78f, 0.70f},  // cyan
        {0.78f, 0.78f, 0.30f, 0.70f},  // yellow
    };
    const int n = static_cast<int>(sizeof(palette) / sizeof(palette[0]));
    return palette[((ord - 1) % n + n) % n];
}

// Render priority: bigger = drawn later (on top). KiCad convention is F.Cu (0)
// on top of the board, so we paint inner layers first, B.Cu next, F.Cu last.
int render_priority(int ord) {
    if (ord == 0) return 1000;
    if (ord == 31) return 500;
    return 100 - ord;  // lower-numbered inner copper drawn later
}

}  // namespace

PcbCanvas::PcbCanvas(QWidget* parent) : QOpenGLWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void PcbCanvas::setBoard(const pdnkit::model::Board* board) {
    board_ = board;
    pending_meshes_.clear();

    if (board_) {
        pending_meshes_ = pdnkit::render::build_all_meshes(*board_);
        meshes_dirty_ = true;

        // Fit camera to board bounding box (zones, segments, pads).
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
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    prog_.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexSrc);
    prog_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentSrc);
    prog_.link();

    grid_vao_.create();
    grid_vbo_.create();
    buildGrid();

    board_vao_.create();
    board_vbo_.create();
    board_ibo_.create();
    // Attribute layout for board VAO (matches grid layout: 2 floats per vertex).
    board_vao_.bind();
    board_vbo_.bind();
    board_ibo_.bind();
    prog_.enableAttributeArray(0);
    prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    board_vao_.release();
    board_vbo_.release();
    board_ibo_.release();
}

void PcbCanvas::buildGrid() {
    std::vector<float> verts;
    const float lo = -0.5f, hi = 0.5f;
    const float step = 0.010f;  // 10mm
    for (float v = lo; v <= hi + 1e-6f; v += step) {
        verts.insert(verts.end(), {v, lo, v, hi});
        verts.insert(verts.end(), {lo, v, hi, v});
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

void PcbCanvas::uploadBoardMeshes() {
    // Sort meshes by render priority (back-to-front).
    std::sort(pending_meshes_.begin(), pending_meshes_.end(),
              [](const auto& a, const auto& b) {
                  return render_priority(a.layer_ordinal) <
                         render_priority(b.layer_ordinal);
              });

    // Flatten into one VBO + IBO and remember each layer's index range.
    std::vector<float> all_verts;
    std::vector<std::uint32_t> all_indices;
    layer_ranges_.clear();
    layer_ranges_.reserve(pending_meshes_.size());

    std::uint32_t vbase = 0;
    int ibase = 0;
    for (const auto& m : pending_meshes_) {
        LayerRange r;
        r.ordinal = m.layer_ordinal;
        r.index_start = ibase;
        r.index_count = static_cast<int>(m.indices.size());
        layer_ranges_.push_back(r);

        all_verts.insert(all_verts.end(), m.vertices.begin(), m.vertices.end());
        for (auto idx : m.indices) all_indices.push_back(vbase + idx);
        vbase += static_cast<std::uint32_t>(m.vertex_count());
        ibase += static_cast<int>(m.indices.size());
    }

    board_vao_.bind();
    board_vbo_.bind();
    board_vbo_.allocate(all_verts.data(),
                        static_cast<int>(all_verts.size() * sizeof(float)));
    board_ibo_.bind();
    board_ibo_.allocate(all_indices.data(),
                        static_cast<int>(all_indices.size() * sizeof(std::uint32_t)));
    // Re-bind the attribute pointer in case the VBO storage was reallocated.
    prog_.enableAttributeArray(0);
    prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    board_vao_.release();
    board_vbo_.release();
    board_ibo_.release();

    meshes_dirty_ = false;
}

void PcbCanvas::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void PcbCanvas::paintGL() {
    if (meshes_dirty_) uploadBoardMeshes();

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

    // 1. Grid (cool dark gray, drawn behind everything).
    prog_.setUniformValue("u_color", QVector4D(0.22f, 0.22f, 0.28f, 1.0f));
    grid_vao_.bind();
    glDrawArrays(GL_LINES, 0, grid_vertex_count_);
    grid_vao_.release();

    // 2. Board zones per layer.
    if (!layer_ranges_.empty()) {
        board_vao_.bind();
        for (const auto& r : layer_ranges_) {
            prog_.setUniformValue("u_color", color_for_layer(r.ordinal));
            glDrawElements(GL_TRIANGLES, r.index_count, GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(
                               static_cast<std::uintptr_t>(r.index_start * sizeof(std::uint32_t))));
        }
        board_vao_.release();
    }

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
