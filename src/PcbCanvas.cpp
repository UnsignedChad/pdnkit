#include "PcbCanvas.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QVector4D>
#include <QSettings>
#include <QWheelEvent>

#include "model/HitTest.h"
#include "render/LayerColors.h"

namespace {

// Flat-color shader: position only, single color uniform.
constexpr auto kFlatVertSrc = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
uniform mat4 u_proj;
void main() {
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
}
)";

constexpr auto kFlatFragSrc = R"(
#version 330 core
uniform vec4 u_color;
out vec4 frag_color;
void main() {
    frag_color = u_color;
}
)";

// Heat-map shader: per-vertex scalar t in [0,1] → viridis-style colormap.
constexpr auto kHeatVertSrc = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in float a_t;
out float v_t;
uniform mat4 u_proj;
void main() {
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
    v_t = a_t;
}
)";

constexpr auto kHeatFragSrc = R"(
#version 330 core
in float v_t;
out vec4 frag_color;
vec3 viridis(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.267, 0.005, 0.329);
    vec3 c1 = vec3(0.231, 0.318, 0.545);
    vec3 c2 = vec3(0.127, 0.567, 0.550);
    vec3 c3 = vec3(0.369, 0.789, 0.382);
    vec3 c4 = vec3(0.992, 0.906, 0.144);
    if (t < 0.25) return mix(c0, c1, t * 4.0);
    if (t < 0.50) return mix(c1, c2, (t - 0.25) * 4.0);
    if (t < 0.75) return mix(c2, c3, (t - 0.50) * 4.0);
    return mix(c3, c4, (t - 0.75) * 4.0);
}
void main() {
    frag_color = vec4(viridis(v_t), 0.90);
}
)";

int render_priority(int ord) {
    if (ord == 0) return 1000;
    if (ord == 31) return 500;
    return 100 - ord;
}

QVector4D toQVec(const std::array<float, 4>& c) {
    return {c[0], c[1], c[2], c[3]};
}

}  // namespace

PcbCanvas::PcbCanvas(QWidget* parent) : QOpenGLWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void PcbCanvas::setBoard(const pdnkit::model::Board* board) {
    board_ = board;
    pending_meshes_.clear();
    layer_visible_.clear();
    setIrResult({});  // clear any heat-map from a previous board

    if (board_) {
        pending_meshes_ = pdnkit::render::build_all_meshes(*board_);
        meshes_dirty_ = true;
        fitToBoard();
    }
    update();
}

void PcbCanvas::setLayerVisibility(int ordinal, bool visible) {
    layer_visible_[ordinal] = visible;
    update();
}

void PcbCanvas::fitToBoard() {
    if (!board_) return;
    bool have_any = false;
    double lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;
    auto include = [&](double x, double y) {
        if (!have_any) { lo_x = hi_x = x; lo_y = hi_y = y; have_any = true; }
        else {
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
        update();
    }
}

void PcbCanvas::setIrResult(pdnkit::render::IrResultMesh result) {
    pending_heat_ = std::move(result);
    heat_dirty_ = true;
    update();
}

void PcbCanvas::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    flat_prog_.addShaderFromSourceCode(QOpenGLShader::Vertex,   kFlatVertSrc);
    flat_prog_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFlatFragSrc);
    flat_prog_.link();

    heat_prog_.addShaderFromSourceCode(QOpenGLShader::Vertex,   kHeatVertSrc);
    heat_prog_.addShaderFromSourceCode(QOpenGLShader::Fragment, kHeatFragSrc);
    heat_prog_.link();

    grid_vao_.create();
    grid_vbo_.create();
    buildGrid();

    board_vao_.create();
    board_vbo_.create();
    board_ibo_.create();
    board_vao_.bind();
    board_vbo_.bind();
    board_ibo_.bind();
    flat_prog_.enableAttributeArray(0);
    flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    board_vao_.release();
    board_vbo_.release();
    board_ibo_.release();

    heat_vao_.create();
    heat_vbo_.create();
    heat_ibo_.create();
    heat_vao_.bind();
    heat_vbo_.bind();
    heat_ibo_.bind();
    // Stride = 3 floats (x, y, t).
    heat_prog_.enableAttributeArray(0);
    heat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2, 3 * sizeof(float));
    heat_prog_.enableAttributeArray(1);
    heat_prog_.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 1,
                                  3 * sizeof(float));
    heat_vao_.release();
    heat_vbo_.release();
    heat_ibo_.release();
}

void PcbCanvas::buildGrid() {
    std::vector<float> verts;
    const float lo = -0.5f, hi = 0.5f;
    const float step = 0.010f;
    for (float v = lo; v <= hi + 1e-6f; v += step) {
        verts.insert(verts.end(), {v, lo, v, hi});
        verts.insert(verts.end(), {lo, v, hi, v});
    }
    grid_vertex_count_ = static_cast<int>(verts.size() / 2);

    grid_vao_.bind();
    grid_vbo_.bind();
    grid_vbo_.allocate(verts.data(),
                       static_cast<int>(verts.size() * sizeof(float)));
    flat_prog_.enableAttributeArray(0);
    flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    grid_vbo_.release();
    grid_vao_.release();
}

void PcbCanvas::uploadBoardMeshes() {
    std::sort(pending_meshes_.begin(), pending_meshes_.end(),
              [](const auto& a, const auto& b) {
                  return render_priority(a.layer_ordinal) <
                         render_priority(b.layer_ordinal);
              });

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
    flat_prog_.enableAttributeArray(0);
    flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    board_vao_.release();
    board_vbo_.release();
    board_ibo_.release();

    meshes_dirty_ = false;
}

void PcbCanvas::uploadIrResult() {
    heat_index_count_ = static_cast<int>(pending_heat_.indices.size());
    heat_layer_ranges_ = pending_heat_.layer_ranges;

    heat_vao_.bind();
    heat_vbo_.bind();
    heat_vbo_.allocate(pending_heat_.vertices.data(),
                       static_cast<int>(pending_heat_.vertices.size() *
                                        sizeof(float)));
    heat_ibo_.bind();
    heat_ibo_.allocate(pending_heat_.indices.data(),
                       static_cast<int>(pending_heat_.indices.size() *
                                        sizeof(std::uint32_t)));
    heat_prog_.enableAttributeArray(0);
    heat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2, 3 * sizeof(float));
    heat_prog_.enableAttributeArray(1);
    heat_prog_.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 1,
                                  3 * sizeof(float));
    heat_vao_.release();
    heat_vbo_.release();
    heat_ibo_.release();

    heat_dirty_ = false;
}

void PcbCanvas::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void PcbCanvas::paintGL() {
    if (meshes_dirty_) uploadBoardMeshes();
    if (heat_dirty_) uploadIrResult();

    glClear(GL_COLOR_BUFFER_BIT);

    const auto m = camera_.ortho_matrix(width(), height());
    const QMatrix4x4 proj(
        m[0], m[4], m[8],  m[12],
        m[1], m[5], m[9],  m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15]);

    flat_prog_.bind();
    flat_prog_.setUniformValue("u_proj", proj);

    flat_prog_.setUniformValue("u_color", QVector4D(0.22f, 0.22f, 0.28f, 1.0f));
    grid_vao_.bind();
    glDrawArrays(GL_LINES, 0, grid_vertex_count_);
    grid_vao_.release();

    if (!layer_ranges_.empty()) {
        board_vao_.bind();
        for (const auto& r : layer_ranges_) {
            auto vis_it = layer_visible_.find(r.ordinal);
            const bool visible = (vis_it == layer_visible_.end()) || vis_it->second;
            if (!visible) continue;
            flat_prog_.setUniformValue("u_color",
                                       toQVec(pdnkit::render::layer_color(r.ordinal)));
            glDrawElements(GL_TRIANGLES, r.index_count, GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(
                               static_cast<std::uintptr_t>(r.index_start *
                                                            sizeof(std::uint32_t))));
        }
        board_vao_.release();
    }
    flat_prog_.release();

    // IR-drop heat-map overlay, on top of the board. Per-layer ranges so
    // we honor the same layer visibility map that filters board geometry.
    if (heat_index_count_ > 0) {
        heat_prog_.bind();
        heat_prog_.setUniformValue("u_proj", proj);
        heat_vao_.bind();
        if (heat_layer_ranges_.empty()) {
            // Single-layer or pre-grouped legacy result — draw the whole batch.
            glDrawElements(GL_TRIANGLES, heat_index_count_, GL_UNSIGNED_INT, nullptr);
        } else {
            for (const auto& r : heat_layer_ranges_) {
                auto vis_it = layer_visible_.find(r.ordinal);
                const bool visible = (vis_it == layer_visible_.end()) || vis_it->second;
                if (!visible) continue;
                glDrawElements(GL_TRIANGLES, r.index_count, GL_UNSIGNED_INT,
                               reinterpret_cast<const void*>(
                                   static_cast<std::uintptr_t>(
                                       r.index_start * sizeof(std::uint32_t))));
            }
        }
        heat_vao_.release();
        heat_prog_.release();
    }
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

    if (board_) {
        const auto world = camera_.screen_to_world(
            e->pos().x(), e->pos().y(), width(), height());
        const double tol = 4.0 / camera_.pixels_per_meter;
        const auto hit = pdnkit::hittest::at_point(*board_, world, tol);

        QString info;
        if (hit.kind != pdnkit::hittest::Hit::Kind::None) {
            const auto* net = board_->find_net(hit.net_id);
            const auto* layer = board_->find_layer(hit.layer_ordinal);
            const QString net_name = (net && !net->name.empty())
                ? QString::fromStdString(net->name)
                : QString("(unnamed)");
            const QString layer_name = layer
                ? QString::fromStdString(layer->name)
                : QString("?");
            info = QString("%1   net %2 (%3)   layer %4")
                       .arg(pdnkit::hittest::name(hit.kind))
                       .arg(net_name)
                       .arg(hit.net_id)
                       .arg(layer_name);
        }
        emit hoverInfo(info);
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

void PcbCanvas::saveSettings(QSettings& settings) const {
    settings.setValue("canvas/center_x", camera_.center.x);
    settings.setValue("canvas/center_y", camera_.center.y);
    settings.setValue("canvas/pixels_per_meter", camera_.pixels_per_meter);
}

void PcbCanvas::restoreSettings(QSettings& settings) {
    bool ok_x = false, ok_y = false, ok_z = false;
    const double cx = settings.value("canvas/center_x").toDouble(&ok_x);
    const double cy = settings.value("canvas/center_y").toDouble(&ok_y);
    const double ppm = settings.value("canvas/pixels_per_meter").toDouble(&ok_z);
    if (ok_x && ok_y && ok_z && ppm > 0.0) {
        camera_.center = {cx, cy};
        camera_.pixels_per_meter = ppm;
        update();
    }
}
