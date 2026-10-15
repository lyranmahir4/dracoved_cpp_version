#pragma once
#include "solar_transit_table_style.h"
#include <QAbstractTextDocumentLayout>
#include <QTextDocument>
#include <QVariantList>

namespace dracoved {
// Solar Return "Technique" look for the shared right-dock tables. The delegate
// wraps the dock's normal delegate and only takes over while the table's
// "solarTechniqueLook" property is "cards" (SR Technique Hits) or "details"
// (SR Technique Details); every other tab keeps its usual rendering.
namespace SolarTechniqueRoles {
constexpr int Kind = Qt::UserRole + 20;      // "month" or "day"
constexpr int Title = Qt::UserRole + 21;     // "Thu 8 Oct" / "October 2026"
constexpr int Subtitle = Qt::UserRole + 22;  // degree text / month day counts
constexpr int Net = Qt::UserRole + 23;       // int
constexpr int Hits = Qt::UserRole + 24;      // QVariantList of QVariantMap (aspect, symbol, glyph, scope, orb, tone)
constexpr int Collapsed = Qt::UserRole + 25; // bool, month rows
constexpr int Html = Qt::UserRole + 30;      // details value cells rendered as rich text
}

inline QColor solarToneColor(int tone, bool dark) {
    if (tone > 0) return dark ? QColor("#4fc39a") : QColor("#147a67");
    if (tone < 0) return dark ? QColor("#ff8a73") : QColor("#c4543b");
    return dark ? QColor("#a8a29a") : QColor("#7b725f");
}

inline QColor solarAspectColor(const QString& aspect) {
    if (aspect == "Conjunction") return QColor("#C99A2E");
    if (aspect == "Sextile") return QColor("#3FA7D6");
    if (aspect == "Square") return QColor("#E0533D");
    if (aspect == "Trine") return QColor("#3FA66A");
    if (aspect == "Opposition") return QColor("#9B59B6");
    return QColor("#90A4AE");
}

inline QString solarSignedLabel(int value) {
    return value > 0 ? QString("+%1").arg(value) : value < 0 ? QString::fromUtf8("−%1").arg(-value) : QString("0");
}

class SolarTechniqueDelegate final : public QStyledItemDelegate {
public:
    SolarTechniqueDelegate(QTableView* view, QAbstractItemDelegate* fallback)
        : QStyledItemDelegate(view), view_(view), fallback_(fallback) {
        if (fallback_) fallback_->setParent(this);
        if (view_->property("solarHoverRow").isNull()) {
            view_->setProperty("solarHoverRow", -1);
            view_->setMouseTracking(true);
            view_->viewport()->installEventFilter(new SolarTableHover(view_));
        }
    }
    static int dayRowHeight(const QFontMetrics& metrics) { return metrics.height() * 2 + 20; }
    static int monthRowHeight(const QFontMetrics& metrics) { return metrics.height() + 18; }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const QString look = view_->property("solarTechniqueLook").toString();
        if (look == "cards") { paintCard(painter, option, index); return; }
        if (look == "details") { paintDetail(painter, option, index); return; }
        if (fallback_) fallback_->paint(painter, option, index);
        else QStyledItemDelegate::paint(painter, option, index);
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        const QString look = view_->property("solarTechniqueLook").toString();
        if (look == "cards") {
            const bool month = index.data(SolarTechniqueRoles::Kind).toString() == "month";
            return {option.rect.width(), month ? monthRowHeight(option.fontMetrics) : dayRowHeight(option.fontMetrics)};
        }
        if (look == "details") {
            const QString html = index.data(SolarTechniqueRoles::Html).toString();
            if (html.isEmpty()) {
                QSize size = QStyledItemDelegate::sizeHint(option, index);
                return {size.width() + 16, std::max(size.height(), option.fontMetrics.height() + 12)};
            }
            QTextDocument doc;
            doc.setDefaultFont(option.font);
            doc.setDocumentMargin(0);
            doc.setHtml(html);
            const int width = std::max(60, cellWidth(index) - 18);
            doc.setTextWidth(width);
            return {static_cast<int>(doc.idealWidth()) + 18, static_cast<int>(std::ceil(doc.size().height())) + 14};
        }
        return fallback_ ? fallback_->sizeHint(option, index) : QStyledItemDelegate::sizeHint(option, index);
    }

private:
    int cellWidth(const QModelIndex& index) const {
        int width = view_->columnWidth(index.column());
        const int span = view_->columnSpan(index.row(), index.column());
        for (int col = index.column() + 1; col < index.column() + span; ++col) width += view_->columnWidth(col);
        return width;
    }

    void paintCard(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const auto colors = solarTableColors(opt.palette);
        const bool selected = opt.state & QStyle::State_Selected;
        const QVariant hover = view_->property("solarHoverRow");
        const bool hovered = hover.isValid() && hover.toInt() == index.row();
        const QRect rect = opt.rect;
        const QFontMetrics metrics(opt.font);
        QFont bold = opt.font; bold.setBold(true);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::TextAntialiasing);
        const int net = index.data(SolarTechniqueRoles::Net).toInt();

        if (index.data(SolarTechniqueRoles::Kind).toString() == "month") {
            painter->fillRect(rect, hovered ? solarBlend(colors.band, colors.ink, 0.04) : colors.band);
            painter->fillRect(QRect(rect.left(), rect.bottom(), rect.width(), 1), colors.rule);
            painter->setPen(Qt::NoPen); painter->setBrush(colors.accent);
            painter->drawRoundedRect(QRectF(rect.left() + 6, rect.top() + 7, 3, rect.height() - 14), 1.5, 1.5);
            const bool collapsed = index.data(SolarTechniqueRoles::Collapsed).toBool();
            const QRect inner = rect.adjusted(16, 0, -12, 0);
            QFont title = bold; title.setLetterSpacing(QFont::PercentageSpacing, 104);
            painter->setFont(title); painter->setPen(colors.ink);
            const QString caret = collapsed ? QString::fromUtf8("▸  ") : QString::fromUtf8("▾  ");
            const QString heading = caret + index.data(SolarTechniqueRoles::Title).toString().toUpper();
            painter->drawText(inner, Qt::AlignLeft | Qt::AlignVCenter, heading);
            const int headingRight = inner.left() + QFontMetrics(title).horizontalAdvance(heading) + 12;
            // Right: "net −4" badge, then muted good/hard day counts before it.
            const QString netText = "net " + solarSignedLabel(net);
            const QFontMetrics boldMetrics(bold);
            const int badgeWidth = boldMetrics.horizontalAdvance(netText) + 16;
            const int badgeHeight = metrics.height() + 4;
            const QRect badge(inner.right() - badgeWidth, rect.center().y() - badgeHeight / 2 + 1, badgeWidth, badgeHeight);
            const QColor tone = solarToneColor(net, colors.dark);
            QColor fill = tone; fill.setAlpha(colors.dark ? 70 : 32);
            painter->setBrush(fill); painter->setPen(Qt::NoPen);
            painter->drawRoundedRect(badge, badgeHeight / 2.0, badgeHeight / 2.0);
            painter->setFont(bold); painter->setPen(tone);
            painter->drawText(badge, Qt::AlignCenter, netText);
            const QString counts = index.data(SolarTechniqueRoles::Subtitle).toString();
            const QRect countsRect(headingRight, rect.top(), badge.left() - 10 - headingRight, rect.height());
            if (countsRect.width() > 30) {
                painter->setFont(opt.font); painter->setPen(colors.muted);
                painter->drawText(countsRect, Qt::AlignRight | Qt::AlignVCenter, metrics.elidedText(counts, Qt::ElideRight, countsRect.width()));
            }
            painter->restore();
            return;
        }

        // Day card.
        painter->fillRect(rect, selected ? colors.selected : hovered ? colors.hover : colors.base);
        painter->fillRect(QRect(rect.left(), rect.bottom(), rect.width(), 1), colors.rule);
        const QColor tone = solarToneColor(net, colors.dark);
        QColor stripe = net == 0 ? colors.rule : tone;
        if (net != 0 && !selected) stripe.setAlpha(colors.dark ? 200 : 170);
        painter->fillRect(QRect(rect.left(), rect.top(), selected ? 4 : 3, rect.height() - 1), selected ? colors.accent : stripe);
        const QColor ink = selected ? colors.selectedInk : colors.ink;
        const QColor muted = solarBlend(selected ? colors.selected : colors.base, ink, 0.6);
        const int lineHeight = metrics.height();
        const QRect line1(rect.left() + 14, rect.top() + 7, rect.width() - 26, lineHeight + 2);
        const QRect line2(rect.left() + 14, line1.bottom() + 3, rect.width() - 26, lineHeight + 2);

        // Line 1: date, degree, net badge.
        painter->setFont(bold); painter->setPen(ink);
        const QString title = index.data(SolarTechniqueRoles::Title).toString();
        painter->drawText(line1, Qt::AlignLeft | Qt::AlignVCenter, title);
        const int dateColumn = std::max(QFontMetrics(bold).horizontalAdvance("Wed 30 Sep") + 14, QFontMetrics(bold).horizontalAdvance(title) + 10);
        painter->setFont(opt.font); painter->setPen(muted);
        painter->drawText(line1.adjusted(dateColumn, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, index.data(SolarTechniqueRoles::Subtitle).toString());
        const QString netText = solarSignedLabel(net);
        const int badgeWidth = std::max(QFontMetrics(bold).horizontalAdvance(netText) + 16, lineHeight + 12);
        const QRect badge(line1.right() - badgeWidth, line1.top(), badgeWidth, line1.height());
        QColor badgeFill = tone; badgeFill.setAlpha(net == 0 ? (colors.dark ? 40 : 22) : (colors.dark ? 90 : 38));
        painter->setBrush(badgeFill); painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(badge, badge.height() / 2.0, badge.height() / 2.0);
        painter->setFont(bold); painter->setPen(net == 0 ? muted : tone);
        painter->drawText(badge, Qt::AlignCenter, netText);

        // Line 2: hit chips, tightest first; overflow collapses into "+N".
        const QVariantList hits = index.data(SolarTechniqueRoles::Hits).toList();
        if (hits.isEmpty()) {
            QFont quiet = opt.font; quiet.setItalic(true);
            painter->setFont(quiet); painter->setPen(solarBlend(selected ? colors.selected : colors.base, ink, 0.42));
            painter->drawText(line2, Qt::AlignLeft | Qt::AlignVCenter, "quiet day");
            painter->restore();
            return;
        }
        QFont small = opt.font; small.setPointSizeF(small.pointSizeF() * 0.86);
        QFont symbolFont = opt.font; symbolFont.setBold(true);
        const QFontMetrics smallMetrics(small), symbolMetrics(symbolFont);
        const int chipHeight = line2.height();
        int x = line2.left();
        for (int i = 0; i < hits.size(); ++i) {
            const QVariantMap hit = hits[i].toMap();
            const QString symbol = hit.value("symbol").toString();
            const QString glyph = hit.value("glyph").toString();
            const QString tail = hit.value("scope").toString() + " " + QString::number(hit.value("orb").toDouble(), 'f', 1) + QString::fromUtf8("°");
            const int width = symbolMetrics.horizontalAdvance(symbol) + metrics.horizontalAdvance(glyph) + smallMetrics.horizontalAdvance(tail) + 26;
            const int remaining = hits.size() - i;
            const QString more = QString("+%1 more").arg(remaining);
            const int moreWidth = smallMetrics.horizontalAdvance(more) + 14;
            if (x + width > line2.right() - (remaining > 1 ? moreWidth + 6 : 0)) {
                const QRect chip(x, line2.top(), moreWidth, chipHeight);
                painter->setPen(QPen(colors.rule, 1)); painter->setBrush(Qt::NoBrush);
                painter->drawRoundedRect(QRectF(chip).adjusted(0.5, 0.5, -0.5, -0.5), chipHeight / 2.0, chipHeight / 2.0);
                painter->setFont(small); painter->setPen(muted);
                painter->drawText(chip, Qt::AlignCenter, more);
                break;
            }
            const int hitTone = hit.value("tone").toInt();
            const QColor chipTone = solarToneColor(hitTone, colors.dark);
            QColor chipFill = chipTone; chipFill.setAlpha(colors.dark ? 55 : 24);
            const QRect chip(x, line2.top(), width, chipHeight);
            painter->setPen(Qt::NoPen); painter->setBrush(chipFill);
            painter->drawRoundedRect(chip, chipHeight / 2.0, chipHeight / 2.0);
            int cx = chip.left() + 9;
            painter->setFont(symbolFont); painter->setPen(solarAspectColor(hit.value("aspect").toString()));
            painter->drawText(QRect(cx, chip.top(), chip.width(), chip.height()), Qt::AlignLeft | Qt::AlignVCenter, symbol);
            cx += symbolMetrics.horizontalAdvance(symbol) + 3;
            painter->setFont(opt.font); painter->setPen(ink);
            painter->drawText(QRect(cx, chip.top(), chip.width(), chip.height()), Qt::AlignLeft | Qt::AlignVCenter, glyph);
            cx += metrics.horizontalAdvance(glyph) + 5;
            painter->setFont(small); painter->setPen(muted);
            painter->drawText(QRect(cx, chip.top(), chip.width(), chip.height()), Qt::AlignLeft | Qt::AlignVCenter, tail);
            x += width + 6;
        }
        painter->restore();
    }

    void paintDetail(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const auto colors = solarTableColors(opt.palette);
        painter->save();
        const bool header = index.data(SolarTechniqueRoles::Kind).toString() == "header";
        const bool section = index.data(SolarTechniqueRoles::Kind).toString() == "section";
        painter->fillRect(opt.rect, header ? solarBlend(colors.base, colors.accent, colors.dark ? 0.14 : 0.07)
            : section ? colors.band : colors.base);
        painter->fillRect(QRect(opt.rect.left(), opt.rect.bottom(), opt.rect.width(), 1), colors.rule);
        if (header && index.column() == 0) {
            painter->fillRect(QRect(opt.rect.left(), opt.rect.top(), 3, opt.rect.height() - 1), colors.accent);
        }
        const QString html = index.data(SolarTechniqueRoles::Html).toString();
        const QRect inner = opt.rect.adjusted(10, 7, -8, -7);
        if (!html.isEmpty()) {
            QTextDocument doc;
            doc.setDefaultFont(opt.font);
            doc.setDocumentMargin(0);
            doc.setDefaultStyleSheet(QString("body { color: %1; }").arg(colors.ink.name()));
            doc.setHtml(html);
            doc.setTextWidth(inner.width());
            painter->translate(inner.topLeft());
            QAbstractTextDocumentLayout::PaintContext context;
            context.palette = opt.palette;
            context.palette.setColor(QPalette::Text, colors.ink);
            context.clip = QRectF(0, 0, inner.width(), inner.height());
            doc.documentLayout()->draw(painter, context);
        } else {
            QFont font = opt.font;
            if (section) { font.setBold(true); font.setLetterSpacing(QFont::PercentageSpacing, 105); }
            painter->setFont(font);
            painter->setPen(section ? colors.ink : colors.muted);
            painter->drawText(inner, Qt::AlignLeft | Qt::AlignTop, section ? opt.text.toUpper() : opt.text);
        }
        painter->restore();
    }

    QTableView* view_;
    QAbstractItemDelegate* fallback_;
};
} // namespace dracoved
