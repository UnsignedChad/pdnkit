#include "ZfPlotWidget.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>

ZfPlotWidget::ZfPlotWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(360, 200);
}

QSize ZfPlotWidget::sizeHint() const { return {480, 280}; }

void ZfPlotWidget::setData(std::vector<double> freqs_hz,
                            std::vector<double> z_mag_ohm) {
    freqs_ = std::move(freqs_hz);
    mags_  = std::move(z_mag_ohm);
    update();
}

void ZfPlotWidget::clear() {
    freqs_.clear();
    mags_.clear();
    update();
}

namespace {

QString fmt_freq(double f) {
    if (f >= 1.0e9) return QString::number(f / 1.0e9, 'g', 3) + " GHz";
    if (f >= 1.0e6) return QString::number(f / 1.0e6, 'g', 3) + " MHz";
    if (f >= 1.0e3) return QString::number(f / 1.0e3, 'g', 3) + " kHz";
    return QString::number(f, 'g', 3) + " Hz";
}

QString fmt_z(double z) {
    if (z >= 1.0)    return QString::number(z, 'g', 3) + " \xCE\xA9";       // Ω
    if (z >= 1.0e-3) return QString::number(z * 1.0e3, 'g', 3) + " m\xCE\xA9";
    if (z >= 1.0e-6) return QString::number(z * 1.0e6, 'g', 3) + " \xC2\xB5\xCE\xA9";
    return QString::number(z, 'g', 3) + " \xCE\xA9";
}

}  // namespace

void ZfPlotWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(15, 15, 18));

    constexpr int margin_left   = 70;
    constexpr int margin_right  = 18;
    constexpr int margin_top    = 18;
    constexpr int margin_bottom = 28;
    const int plot_w = width() - margin_left - margin_right;
    const int plot_h = height() - margin_top - margin_bottom;
    if (plot_w < 50 || plot_h < 40) return;

    if (freqs_.size() < 2 || freqs_.size() != mags_.size()) {
        p.setPen(QColor(140, 140, 140));
        p.drawText(rect(), Qt::AlignCenter, "no Z(f) data");
        return;
    }

    // Log-log axis ranges. Pad y a little so the curve doesn't touch the
    // axes. Skip any non-positive data point silently.
    double f_min = std::numeric_limits<double>::infinity();
    double f_max = -std::numeric_limits<double>::infinity();
    double z_min = std::numeric_limits<double>::infinity();
    double z_max = -std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < freqs_.size(); ++i) {
        const double f = freqs_[i], z = mags_[i];
        if (f <= 0.0 || z <= 0.0) continue;
        f_min = std::min(f_min, f);
        f_max = std::max(f_max, f);
        z_min = std::min(z_min, z);
        z_max = std::max(z_max, z);
    }
    if (!(f_max > f_min) || !(z_max > z_min)) return;

    const double lf_min = std::log10(f_min);
    const double lf_max = std::log10(f_max);
    const double lz_min = std::log10(z_min) - 0.05 * (std::log10(z_max) - std::log10(z_min) + 1e-12);
    const double lz_max = std::log10(z_max) + 0.05 * (std::log10(z_max) - std::log10(z_min) + 1e-12);

    auto map_x = [&](double f) {
        return margin_left + plot_w * (std::log10(f) - lf_min) / (lf_max - lf_min);
    };
    auto map_y = [&](double z) {
        return margin_top + plot_h * (1.0 - (std::log10(z) - lz_min) / (lz_max - lz_min));
    };

    // Plot frame.
    p.setPen(QColor(80, 80, 90));
    p.drawRect(QRect(margin_left, margin_top, plot_w, plot_h));

    // Decade gridlines.
    p.setPen(QColor(40, 40, 48));
    for (int e = static_cast<int>(std::floor(lf_min));
         e <= static_cast<int>(std::ceil(lf_max)); ++e) {
        const double x = map_x(std::pow(10.0, e));
        if (x >= margin_left - 1 && x <= margin_left + plot_w + 1) {
            p.drawLine(QPointF(x, margin_top), QPointF(x, margin_top + plot_h));
        }
    }
    for (int e = static_cast<int>(std::floor(lz_min));
         e <= static_cast<int>(std::ceil(lz_max)); ++e) {
        const double y = map_y(std::pow(10.0, e));
        if (y >= margin_top - 1 && y <= margin_top + plot_h + 1) {
            p.drawLine(QPointF(margin_left, y), QPointF(margin_left + plot_w, y));
        }
    }

    // Axis tick labels (just decade endpoints).
    p.setPen(QColor(210, 210, 215));
    QFont f = p.font();
    f.setPointSizeF(f.pointSizeF() - 1.0);
    p.setFont(f);
    p.drawText(QRect(margin_left - 60, margin_top - 12, 56, 16),
               Qt::AlignRight | Qt::AlignVCenter, fmt_z(std::pow(10.0, lz_max)));
    p.drawText(QRect(margin_left - 60, margin_top + plot_h - 4, 56, 16),
               Qt::AlignRight | Qt::AlignVCenter, fmt_z(std::pow(10.0, lz_min)));
    p.drawText(QRect(margin_left - 30, margin_top + plot_h + 4, 80, 16),
               Qt::AlignLeft, fmt_freq(f_min));
    p.drawText(QRect(margin_left + plot_w - 80, margin_top + plot_h + 4, 80, 16),
               Qt::AlignRight, fmt_freq(f_max));

    // Curve.
    p.setPen(QPen(QColor(0xfd, 0xe7, 0x25), 1.5));  // viridis-yellow
    QPainterPath path;
    bool started = false;
    for (std::size_t i = 0; i < freqs_.size(); ++i) {
        const double freq = freqs_[i], z = mags_[i];
        if (freq <= 0.0 || z <= 0.0) continue;
        const QPointF pt(map_x(freq), map_y(z));
        if (!started) { path.moveTo(pt); started = true; }
        else            path.lineTo(pt);
    }
    p.drawPath(path);
}
