#include "south_indian_chart.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <algorithm>
#include <cmath>

namespace dracoved {
namespace {

double luminance(const QColor& c) {
    return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF();
}

QColor withAlpha(QColor c, int alpha) {
    c.setAlpha(std::clamp(alpha, 0, 255));
    return c;
}

// Traditional planet inks, lifted on dark backgrounds so each stays readable.
QColor planetInk(const QString& key, bool dark) {
    struct Ink { const char* prefix; const char* light; const char* dark; };
    static const Ink inks[] = {
        {"Ascendant", "#b03a2e", "#ef7b6c"},
        {"Sun", "#b8651b", "#f0a24f"},
        {"Moon", "#2f6db3", "#7fb0ec"},
        {"Mars", "#c0392b", "#f07565"},
        {"Mercury", "#23875a", "#5fcf97"},
        {"Jupiter", "#a87a00", "#e8bd4a"},
        {"Venus", "#a3378f", "#e183cf"},
        {"Saturn", "#46505c", "#a7b3c0"},
        {"Rahu", "#6d4a9e", "#b89ae6"},
        {"Ketu", "#7a5c3e", "#cfa981"},
    };
    for (const Ink& ink : inks) {
        if (key.startsWith(QLatin1String(ink.prefix))) {
            return QColor(dark ? ink.dark : ink.light);
        }
    }
    return {};
}

QString shortSign(int sign) {
    static const QStringList names = {"Ari", "Tau", "Gem", "Can", "Leo", "Vir",
                                      "Lib", "Sco", "Sag", "Cap", "Aqu", "Pis"};
    return names.value(sign);
}

// Truncated, like the placements table, so a body at 29°59'59" never reads as
// 30° of the sign it is still in.
QString degreeText(double degree) {
    const int totalMinutes = static_cast<int>(std::floor(std::clamp(degree, 0.0, 29.99999) * 60.0));
    return QString("%1%2%3'")
        .arg(totalMinutes / 60, 2, 10, QChar('0'))
        .arg(QChar(0x00B0))
        .arg(totalMinutes % 60, 2, 10, QChar('0'));
}

QFont pixelFont(const QFont& base, double px, bool bold) {
    QFont f = base;
    f.setPixelSize(std::max(7, static_cast<int>(std::lround(px))));
    f.setBold(bold);
    return f;
}

}  // namespace

SouthIndianChart::SouthIndianChart(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(minimumSizeHint());
}

QPoint SouthIndianChart::gridCell(int sign) {
    // Pisces top-left, then clockwise: Aries..Gemini across the top, Cancer..
    // Virgo down the right, Libra..Sagittarius back along the bottom, and
    // Capricorn/Aquarius up the left side.
    static const QPoint cells[12] = {
        {1, 0}, {2, 0}, {3, 0}, {3, 1}, {3, 2}, {3, 3},
        {2, 3}, {1, 3}, {0, 3}, {0, 2}, {0, 1}, {0, 0},
    };
    return sign >= 0 && sign < 12 ? cells[sign] : QPoint(-1, -1);
}

QRectF SouthIndianChart::chartSquare() const {
    const double margin = 4.0;
    const double side = std::max(0.0, std::min(width(), height()) - 2.0 * margin);
    return QRectF((width() - side) * 0.5, (height() - side) * 0.5, side, side);
}

QRectF SouthIndianChart::signBox(int sign) const {
    const QPoint cell = gridCell(sign);
    if (cell.x() < 0) return {};
    const QRectF square = chartSquare();
    const double size = square.width() / 4.0;
    return QRectF(square.left() + cell.x() * size, square.top() + cell.y() * size, size, size);
}

QRectF SouthIndianChart::entryRect(const QString& key) const {
    for (int i = 0; i < entries_.size() && i < entryRects_.size(); ++i) {
        if (entries_[i].key == key) return entryRects_[i];
    }
    return {};
}

void SouthIndianChart::setChart(const QString& title, const QStringList& details,
                                const QVector<SouthIndianEntry>& entries) {
    title_ = title;
    details_ = details;
    entries_ = entries;
    entryRects_ = QVector<QRectF>(entries_.size());
    hovered_ = -1;
    message_.clear();
    update();
}

void SouthIndianChart::clear(const QString& message) {
    entries_.clear();
    entryRects_.clear();
    details_.clear();
    hovered_ = -1;
    message_ = message;
    update();
}

void SouthIndianChart::setHighlightedKey(const QString& key) {
    if (key == highlightedKey_) return;
    highlightedKey_ = key;
    update();
}

int SouthIndianChart::entryAt(const QPointF& point) const {
    for (int i = 0; i < entryRects_.size(); ++i) {
        if (entryRects_[i].adjusted(-2, -1, 2, 1).contains(point)) return i;
    }
    return -1;
}

void SouthIndianChart::mouseMoveEvent(QMouseEvent* event) {
    const int index = entryAt(event->position());
    if (index != hovered_) {
        hovered_ = index;
        setCursor(index >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
    if (index >= 0 && !entries_[index].tooltip.isEmpty()) {
        QToolTip::showText(event->globalPosition().toPoint(), entries_[index].tooltip, this);
    } else {
        QToolTip::hideText();
    }
}

void SouthIndianChart::mousePressEvent(QMouseEvent* event) {
    const int index = entryAt(event->position());
    if (event->button() == Qt::LeftButton && index >= 0) {
        setHighlightedKey(entries_[index].key);
        if (onEntryClicked) onEntryClicked(entries_[index].key);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void SouthIndianChart::leaveEvent(QEvent* event) {
    if (hovered_ >= 0) {
        hovered_ = -1;
        update();
    }
    setCursor(Qt::ArrowCursor);
    QWidget::leaveEvent(event);
}

void SouthIndianChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QColor base = palette().base().color();
    const QColor ink = palette().text().color();
    const bool dark = luminance(base) < 0.45;
    const QColor accent = dark ? QColor("#e0b04a") : QColor("#b8860b");
    const QColor gridInk = withAlpha(ink, dark ? 95 : 80);
    p.fillRect(rect(), base);

    const QRectF square = chartSquare();
    const double cell = square.width() / 4.0;
    if (cell < 10.0) return;

    int lagnaSign = -1;
    for (const auto& entry : entries_) {
        if (entry.lagna) lagnaSign = entry.sign;
    }

    // Box surfaces: the Lagna box is lightly tinted so the first house is found
    // at a glance even before the diagonal is noticed.
    for (int sign = 0; sign < 12; ++sign) {
        const QRectF box = signBox(sign);
        p.fillRect(box, sign == lagnaSign ? withAlpha(accent, dark ? 38 : 26)
                                          : withAlpha(ink, dark ? 10 : 6));
    }

    // Grid: the twelve boxes, then the centre panel, then a firmer outer frame.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(gridInk, 1.0));
    for (int sign = 0; sign < 12; ++sign) p.drawRect(signBox(sign));
    const QRectF centre(square.left() + cell, square.top() + cell, 2.0 * cell, 2.0 * cell);
    p.drawRect(centre);
    p.setPen(QPen(withAlpha(ink, dark ? 150 : 135), 1.6));
    p.drawRect(square);

    // Traditional Lagna mark: a diagonal across the box's top-left corner.
    const double lagnaCut = cell * 0.26;
    if (lagnaSign >= 0) {
        const QRectF box = signBox(lagnaSign);
        p.setPen(QPen(accent, std::max(1.4, cell * 0.012), Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(box.left() + 1.0, box.top() + lagnaCut),
                   QPointF(box.left() + lagnaCut, box.top() + 1.0));
    }

    // Sign names in the bottom-right corner of every box.
    const QFont baseFont = font();
    const QFont signFont = pixelFont(baseFont, std::clamp(cell * 0.085, 8.0, 11.0), false);
    const QFontMetricsF signMetrics(signFont);
    p.setFont(signFont);
    p.setPen(withAlpha(ink, dark ? 120 : 105));
    for (int sign = 0; sign < 12; ++sign) {
        const QRectF box = signBox(sign).adjusted(3.0, 2.0, -4.0, -2.0);
        p.drawText(box, Qt::AlignRight | Qt::AlignBottom, shortSign(sign));
    }

    // Centre panel: chart title and context, or the empty-state message.
    {
        const QRectF inner = centre.adjusted(cell * 0.12, cell * 0.12, -cell * 0.12, -cell * 0.12);
        const QFont titleFont = pixelFont(baseFont, std::clamp(cell * 0.16, 10.0, 18.0), true);
        const QFont detailFont = pixelFont(baseFont, std::clamp(cell * 0.105, 8.0, 13.0), false);
        const QFontMetricsF titleMetrics(titleFont), detailMetrics(detailFont);
        QStringList lines = entries_.isEmpty() ? QStringList{message_} : details_;
        if (!entries_.isEmpty()) {
            lines << QString();
            lines << QString("R retro  %1  * vargottama").arg(QChar(0x00B7));
        }
        const double blockHeight = (entries_.isEmpty() ? 0.0 : titleMetrics.height() + 4.0)
            + lines.size() * detailMetrics.height();
        double y = inner.center().y() - blockHeight * 0.5;
        if (!entries_.isEmpty()) {
            p.setFont(titleFont);
            p.setPen(ink);
            p.drawText(QRectF(inner.left(), y, inner.width(), titleMetrics.height()),
                       Qt::AlignCenter, title_);
            y += titleMetrics.height() + 4.0;
        }
        p.setFont(detailFont);
        for (int i = 0; i < lines.size(); ++i) {
            const bool legend = !entries_.isEmpty() && i == lines.size() - 1;
            p.setPen(withAlpha(ink, legend ? 125 : 185));
            p.drawText(QRectF(inner.left(), y, inner.width(), detailMetrics.height()),
                       Qt::AlignCenter,
                       detailMetrics.elidedText(lines[i], Qt::ElideRight, inner.width()));
            y += detailMetrics.height();
        }
    }

    // Placements, box by box: Lagna first, then the caller's planet order.
    entryRects_ = QVector<QRectF>(entries_.size());
    for (int sign = 0; sign < 12; ++sign) {
        QVector<int> members;
        for (int i = 0; i < entries_.size(); ++i) {
            if (entries_[i].sign == sign) members.push_back(i);
        }
        if (members.isEmpty()) continue;
        std::stable_sort(members.begin(), members.end(), [&](int a, int b) {
            return entries_[a].lagna && !entries_[b].lagna;
        });

        const QRectF box = signBox(sign);
        const double pad = std::max(3.0, cell * 0.06);
        // The sign name owns the bottom strip; the Lagna stroke owns the corner.
        const QRectF area(box.left() + pad, box.top() + pad,
                          box.width() - 2.0 * pad,
                          box.height() - 2.0 * pad - signMetrics.height() * 0.8);

        // Largest type that fits: one column first, then two, dropping minutes
        // only as a last resort for very crowded boxes.
        int columns = 1;
        bool showMinutes = true;
        double px = std::clamp(cell * 0.155, 9.0, 17.0);
        auto lineWidth = [&](const SouthIndianEntry& e, const QFont& bold, const QFont& normal, bool minutes) {
            const QString degree = minutes ? degreeText(e.degree) : degreeText(e.degree).left(3);
            return QFontMetricsF(bold).horizontalAdvance(e.label + ' ')
                + QFontMetricsF(normal).horizontalAdvance(degree)
                + (e.retrograde ? QFontMetricsF(bold).horizontalAdvance("R") + 1.0 : 0.0)
                + (e.vargottama ? QFontMetricsF(bold).horizontalAdvance("*") + 1.0 : 0.0);
        };
        auto fits = [&](double size, int cols, bool minutes) {
            const QFont bold = pixelFont(baseFont, size, true);
            const QFont normal = pixelFont(baseFont, size - 1.0, false);
            const double lineHeight = QFontMetricsF(bold).height();
            const int rows = (members.size() + cols - 1) / cols;
            const double firstIndent = (sign == lagnaSign && cols == 1) ? lagnaCut * 0.55 : 0.0;
            if (rows * lineHeight > area.height()) return false;
            const double columnWidth = area.width() / cols;
            for (int k = 0; k < members.size(); ++k) {
                const double indent = k == 0 ? firstIndent : 0.0;
                if (lineWidth(entries_[members[k]], bold, normal, minutes) + indent > columnWidth) return false;
            }
            return true;
        };
        bool found = false;
        for (const bool minutes : {true, false}) {
            for (const int cols : {1, 2}) {
                for (double size = px; size >= 8.0; size -= 0.5) {
                    if (fits(size, cols, minutes)) {
                        px = size; columns = cols; showMinutes = minutes; found = true;
                        break;
                    }
                }
                if (found) break;
            }
            if (found) break;
        }
        if (!found) { px = 8.0; columns = 2; showMinutes = false; }

        const QFont bold = pixelFont(baseFont, px, true);
        const QFont normal = pixelFont(baseFont, px - 1.0, false);
        const QFontMetricsF boldMetrics(bold), normalMetrics(normal);
        const double lineHeight = boldMetrics.height();
        const int rows = (members.size() + columns - 1) / columns;
        const double columnWidth = area.width() / columns;
        // A lone column is centred vertically in its box, which reads calmer
        // than hugging the top edge.
        const double blockTop = area.top() + std::max(0.0, (area.height() - rows * lineHeight) * 0.35);

        for (int k = 0; k < members.size(); ++k) {
            const int index = members[k];
            const SouthIndianEntry& entry = entries_[index];
            const int column = k / rows;
            const int row = k % rows;
            double x = area.left() + column * columnWidth
                + ((k == 0 && sign == lagnaSign && columns == 1) ? lagnaCut * 0.55 : 0.0);
            const double y = blockTop + row * lineHeight;
            const double width = lineWidth(entry, bold, normal, showMinutes);
            const QRectF lineRect(x - 2.0, y, width + 4.0, lineHeight);
            entryRects_[index] = lineRect;

            const bool selected = !highlightedKey_.isEmpty() && entry.key == highlightedKey_;
            if (selected || index == hovered_) {
                p.setPen(Qt::NoPen);
                p.setBrush(withAlpha(accent, selected ? (dark ? 70 : 55) : (dark ? 40 : 30)));
                p.drawRoundedRect(lineRect, 3.0, 3.0);
                p.setBrush(Qt::NoBrush);
            }

            QColor labelInk = planetInk(entry.key, dark);
            if (!labelInk.isValid()) labelInk = ink;
            const double baseline = y + boldMetrics.ascent();
            p.setFont(bold);
            p.setPen(labelInk);
            const QString label = entry.label + ' ';
            p.drawText(QPointF(x, baseline), label);
            x += boldMetrics.horizontalAdvance(label);
            p.setFont(normal);
            p.setPen(withAlpha(ink, 215));
            const QString degree = showMinutes ? degreeText(entry.degree) : degreeText(entry.degree).left(3);
            p.drawText(QPointF(x, baseline), degree);
            x += normalMetrics.horizontalAdvance(degree);
            p.setFont(bold);
            if (entry.retrograde) {
                p.setPen(dark ? QColor("#ef6b6b") : QColor("#b14040"));
                p.drawText(QPointF(x + 1.0, baseline), "R");
                x += boldMetrics.horizontalAdvance("R") + 1.0;
            }
            if (entry.vargottama) {
                p.setPen(accent);
                p.drawText(QPointF(x + 1.0, baseline), "*");
            }
        }
    }
}

}  // namespace dracoved
