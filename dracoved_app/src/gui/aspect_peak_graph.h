#pragma once

#include <QDateTime>
#include <QTimeZone>
#include <QVector>
#include <QWidget>
#include <functional>

namespace dracoved {

struct AspectPeakGraphPoint {
    int resultIndex = -1;
    QDateTime peakUtc, startUtc, endUtc;
    double net = 0, positive = 0, negative = 0;
    int hits = 0, natalHits = 0, transitHits = 0, solarHits = 0, solarYear = 0;
};

class AspectPeakGraph final : public QWidget {
public:
    explicit AspectPeakGraph(QWidget* parent = nullptr);
    void setResults(QVector<AspectPeakGraphPoint> points, QDateTime from, QDateTime through,
                    QTimeZone timezone, bool weighted, bool periods, int sampleMinutes, bool partial);
    void setSelectedResult(int index);
    std::function<void(int)> pointSelected;

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QRectF plotRect() const;
    QPointF pointPosition(const AspectPeakGraphPoint& point) const;
    double value(const AspectPeakGraphPoint& point) const;
    int nearestPoint(QPointF position) const;
    bool connected(int index) const;
    QString pointTooltip(const AspectPeakGraphPoint& point) const;
    QVector<AspectPeakGraphPoint> points_;
    QDateTime from_, through_;
    QTimeZone timezone_;
    bool weighted_ = false, periods_ = false, partial_ = false;
    int sampleMinutes_ = 60, selected_ = -1;
    double minimum_ = 0, maximum_ = 1, tickStep_ = 1;
};

} // namespace dracoved
