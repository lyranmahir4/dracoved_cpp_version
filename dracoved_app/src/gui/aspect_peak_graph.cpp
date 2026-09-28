#include "aspect_peak_graph.h"

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <algorithm>
#include <cmath>

namespace dracoved {

AspectPeakGraph::AspectPeakGraph(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(300, 260);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void AspectPeakGraph::setResults(QVector<AspectPeakGraphPoint> points, QDateTime from, QDateTime through,
                                QTimeZone timezone, bool weighted, bool periods, int sampleMinutes, bool partial) {
    points_ = std::move(points);
    std::sort(points_.begin(), points_.end(), [](const auto& a, const auto& b) {
        return a.peakUtc < b.peakUtc;
    });
    from_ = from; through_ = through; timezone_ = timezone;
    weighted_ = weighted; periods_ = periods; partial_ = partial;
    sampleMinutes_ = std::max(1, sampleMinutes);
    minimum_ = maximum_ = 0;
    for (const auto& point : points_) {
        minimum_ = std::min(minimum_, value(point));
        maximum_ = std::max(maximum_, value(point));
    }
    if (minimum_ == maximum_) {
        minimum_ = weighted_ ? -1 : 0;
        maximum_ = 1;
    }
    const double rawStep = (maximum_ - minimum_) / 5.0;
    const double scale = std::pow(10.0, std::floor(std::log10(rawStep)));
    tickStep_ = std::ceil(rawStep / scale) * scale;
    if (!weighted_) tickStep_ = std::max(1.0, tickStep_);
    minimum_ = std::floor(minimum_ / tickStep_) * tickStep_;
    maximum_ = std::ceil(maximum_ / tickStep_) * tickStep_;
    QToolTip::hideText();
    update();
}

void AspectPeakGraph::setSelectedResult(int index) {
    selected_ = index;
    update();
}

QRectF AspectPeakGraph::plotRect() const {
    return QRectF(58, 34, std::max(1, width() - 80), std::max(1, height() - 99));
}

double AspectPeakGraph::value(const AspectPeakGraphPoint& point) const {
    return weighted_ ? point.net : point.hits;
}

QPointF AspectPeakGraph::pointPosition(const AspectPeakGraphPoint& point) const {
    const QRectF plot = plotRect();
    const qint64 span = from_.msecsTo(through_);
    const double fraction = span > 0 ? double(from_.msecsTo(point.peakUtc)) / span : 0.5;
    return {plot.left() + fraction * plot.width(),
            plot.bottom() - (value(point) - minimum_) / (maximum_ - minimum_) * plot.height()};
}

bool AspectPeakGraph::connected(int index) const {
    if (index <= 0) return false;
    const auto& previous = points_[index - 1];
    const auto& current = points_[index];
    if (periods_) return previous.endUtc.secsTo(current.startUtc) <= qint64(sampleMinutes_) * 60;
    // Non-qualifying dates are gaps, not zero scores or interpolated peaks.
    return previous.peakUtc.toTimeZone(timezone_).date().daysTo(
        current.peakUtc.toTimeZone(timezone_).date()) == 1;
}

void AspectPeakGraph::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), palette().base());
    painter.setPen(palette().text().color());
    if (points_.isEmpty() || !from_.isValid() || !through_.isValid()) {
        painter.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap,
            "No peaks to plot. Run Find Aspect Peaks or adjust the search filters.");
        return;
    }
    const QRectF plot = plotRect();
    const QColor positive("#2E8B57"), negative("#C4473A"), counts("#397BB5");
    const auto color = [&](double y) {
        return !weighted_ ? counts : (y > 0 ? positive : (y < 0 ? negative : palette().text().color()));
    };
    QColor grid = palette().text().color(); grid.setAlpha(35);
    painter.drawText(QRectF(6, 4, width() - 12, 24), Qt::AlignVCenter,
        QString("%1 · %2 — %3")
            .arg(weighted_ ? "Net score" : "Hit count",
                 from_.toTimeZone(timezone_).toString("d MMM yyyy HH:mm"),
                 through_.toTimeZone(timezone_).toString("d MMM yyyy HH:mm")));
    for (double tick = minimum_; tick <= maximum_ + tickStep_ * 0.01; tick += tickStep_) {
        const double y = plot.bottom() - (tick - minimum_) / (maximum_ - minimum_) * plot.height();
        painter.setPen(QPen(std::abs(tick) < tickStep_ * 0.01 ? palette().mid().color() : grid,
                            std::abs(tick) < tickStep_ * 0.01 ? 1.5 : 1));
        painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        painter.setPen(palette().text().color());
        painter.drawText(QRectF(0, y - 10, plot.left() - 8, 20), Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(std::abs(tick) < 1e-9 ? 0 : tick, 'g', 4));
    }
    const qint64 span = from_.msecsTo(through_);
    const int ticks = span > 0 ? std::clamp(int(plot.width() / 115), 1, 10) : 0;
    const QString dateFormat = span / std::max(1, ticks) < 86400000LL ? "d MMM HH:mm"
        : (span > 370 * 86400000LL ? "MMM yyyy" : "d MMM");
    for (int i = 0; i <= ticks; ++i) {
        const double fraction = ticks ? double(i) / ticks : 0.5;
        const double x = plot.left() + fraction * plot.width();
        const QDateTime time = from_.addMSecs(qRound64(span * fraction)).toTimeZone(timezone_);
        painter.setPen(grid);
        painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        painter.setPen(palette().text().color());
        const double labelLeft = std::clamp(x - 57, 0.0, double(width() - 114));
        painter.drawText(QRectF(labelLeft, plot.bottom() + 5, 114, 20), Qt::AlignCenter, time.toString(dateFormat));
    }
    painter.save();
    painter.setClipRect(plot.adjusted(-5, -5, 5, 5));
    const double zeroY = plot.bottom() - (0 - minimum_) / (maximum_ - minimum_) * plot.height();
    for (int i = 0; i < points_.size(); ++i) {
        const auto& point = points_[i];
        const QPointF position = pointPosition(point);
        const double y = value(point);
        if (connected(i)) {
            const auto& previous = points_[i - 1];
            const QPointF before = pointPosition(previous);
            const double previousY = value(previous);
            if (weighted_ && previousY * y < 0) {
                const double ratio = previousY / (previousY - y);
                const QPointF crossing(before.x() + ratio * (position.x() - before.x()), zeroY);
                painter.setPen(QPen(color(previousY), 2)); painter.drawLine(before, crossing);
                painter.setPen(QPen(color(y), 2)); painter.drawLine(crossing, position);
            } else {
                painter.setPen(QPen(color(y == 0 ? previousY : y), 2));
                painter.drawLine(before, position);
            }
        }
        painter.setPen(Qt::NoPen); painter.setBrush(color(y));
        const double radius = points_.size() > plot.width() / 5 ? 1.7 : 3.2;
        painter.drawEllipse(position, radius, radius);
    }
    for (const auto& point : points_) if (point.resultIndex == selected_) {
        const QPointF position = pointPosition(point);
        painter.setPen(QPen(palette().highlight().color(), 1, Qt::DashLine));
        painter.drawLine(QPointF(position.x(), plot.top()), QPointF(position.x(), plot.bottom()));
        painter.setPen(QPen(palette().highlight().color(), 2));
        painter.setBrush(palette().base()); painter.drawEllipse(position, 6, 6);
        painter.setPen(Qt::NoPen); painter.setBrush(color(value(point))); painter.drawEllipse(position, 3, 3);
        break;
    }
    painter.restore();
    painter.setPen(palette().text().color());
    painter.drawText(QRectF(6, height() - 32, width() - 12, 28), Qt::AlignVCenter | Qt::TextWordWrap,
        QString("%1%2 %3 peaks · %4 · Click a point to inspect")
            .arg(partial_ ? "Partial scan · " : "").arg(points_.size())
            .arg(periods_ ? "period" : "daily", QString::fromUtf8(timezone_.id())));
}

int AspectPeakGraph::nearestPoint(QPointF position) const {
    int nearest = -1;
    double best = 12 * 12;
    for (int i = 0; i < points_.size(); ++i) {
        const QPointF delta = pointPosition(points_[i]) - position;
        const double distance = delta.x() * delta.x() + delta.y() * delta.y();
        if (distance < best) { best = distance; nearest = i; }
    }
    return nearest;
}

QString AspectPeakGraph::pointTooltip(const AspectPeakGraphPoint& point) const {
    const auto time = [&](const QDateTime& utc) { return utc.toTimeZone(timezone_).toString("d MMM yyyy HH:mm:ss"); };
    QStringList lines{time(point.peakUtc) + " (" + QString::fromUtf8(timezone_.id()) + ")"};
    if (periods_) lines << "Period: " + time(point.startUtc) + " — " + time(point.endUtc);
    if (weighted_) lines << QString("Positive +%1 · Negative -%2 · Net %3")
        .arg(point.positive, 0, 'g', 6).arg(point.negative, 0, 'g', 6).arg(point.net, 0, 'g', 6);
    lines << QString("Hits %1 · T-N %2 · T-T %3 · T-SR %4")
        .arg(point.hits).arg(point.natalHits).arg(point.transitHits).arg(point.solarHits);
    if (point.solarYear) lines << QString("Solar Return %1").arg(point.solarYear);
    return lines.join('\n');
}

void AspectPeakGraph::mouseMoveEvent(QMouseEvent* event) {
    const int index = nearestPoint(event->position());
    setCursor(index >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (index >= 0) QToolTip::showText(event->globalPosition().toPoint(), pointTooltip(points_[index]), this);
    else QToolTip::hideText();
}

void AspectPeakGraph::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const int index = nearestPoint(event->position());
    if (index >= 0 && pointSelected) pointSelected(points_[index].resultIndex);
}

void AspectPeakGraph::leaveEvent(QEvent* event) {
    QToolTip::hideText();
    unsetCursor();
    QWidget::leaveEvent(event);
}

} // namespace dracoved
