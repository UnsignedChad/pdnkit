#pragma once

#include <vector>

#include <QWidget>

// Custom log-log plot for |Z(f)|. No QtCharts dependency — paints axes,
// gridlines, and the data curve in paintEvent. Frequency on X (Hz log10),
// |Z| on Y (ohms log10).
class ZfPlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit ZfPlotWidget(QWidget* parent = nullptr);

    // Replace the curve. Empty data → empty plot.
    void setData(std::vector<double> freqs_hz, std::vector<double> z_mag_ohm);
    void clear();

protected:
    QSize sizeHint() const override;
    void paintEvent(QPaintEvent* e) override;

private:
    std::vector<double> freqs_;
    std::vector<double> mags_;
};
