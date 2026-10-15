#pragma once
#include <QAbstractItemView>
#include <QEvent>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <algorithm>
#include <cmath>

namespace dracoved {
// Shared look for the Transits to Return tables. Colors derive from the table's
// palette, which the app theme stylesheet sets (Light, Dark, Creme), so rows,
// hover and selection follow the active theme instead of fixed colors.
struct SolarTableColors {
    QColor base, ink, muted, rule, hover, selected, selectedInk, accent, band;
    bool dark = false;
};

inline QColor solarBlend(const QColor& from, const QColor& to, double amount) {
    return QColor::fromRgbF(from.redF() + (to.redF() - from.redF()) * amount,
        from.greenF() + (to.greenF() - from.greenF()) * amount,
        from.blueF() + (to.blueF() - from.blueF()) * amount);
}

inline SolarTableColors solarTableColors(const QPalette& palette) {
    SolarTableColors colors;
    colors.base = palette.color(QPalette::Base);
    colors.ink = palette.color(QPalette::Text);
    colors.dark = colors.base.lightness() < 128;
    colors.muted = solarBlend(colors.base, colors.ink, colors.dark ? 0.62 : 0.58);
    colors.rule = solarBlend(colors.base, colors.ink, colors.dark ? 0.09 : 0.075);
    colors.selected = palette.color(QPalette::Highlight);
    colors.selectedInk = palette.color(QPalette::HighlightedText);
    // A saturated stripe in the selection's hue (blue, slate-teal or terracotta).
    const int hue = colors.selected.hslHue() < 0 ? 210 : colors.selected.hslHue();
    colors.accent = QColor::fromHsl(hue, colors.dark ? 120 : 130, colors.dark ? 150 : 100);
    if (std::abs(colors.selected.lightness() - colors.base.lightness()) < 6) colors.selected = solarBlend(colors.base, colors.accent, 0.22);
    colors.hover = solarBlend(colors.base, colors.selected, 0.45);
    colors.band = solarBlend(colors.base, colors.ink, colors.dark ? 0.065 : 0.04);
    return colors;
}

// Tracks the row under the mouse for the whole-row hover tint.
class SolarTableHover final : public QObject {
public:
    explicit SolarTableHover(QAbstractItemView* view) : QObject(view), view_(view) {}
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::MouseMove) setRow(view_->indexAt(static_cast<QMouseEvent*>(event)->position().toPoint()).row());
        else if (event->type() == QEvent::Leave) setRow(-1);
        return false;
    }
private:
    void setRow(int row) {
        if (view_->property("solarHoverRow").toInt() == row) return;
        view_->setProperty("solarHoverRow", row);
        view_->viewport()->update();
    }
    QAbstractItemView* view_;
};

inline void paintSolarRowBackground(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index, const SolarTableColors& colors) {
    const bool selected = option.state & QStyle::State_Selected;
    const auto* view = qobject_cast<const QTableView*>(option.widget);
    const QVariant hover = view ? view->property("solarHoverRow") : QVariant();
    const bool hovered = hover.isValid() && hover.toInt() == index.row();
    painter->fillRect(option.rect, selected ? colors.selected : hovered ? colors.hover : colors.base);
    painter->fillRect(QRect(option.rect.left(), option.rect.bottom(), option.rect.width(), 1), colors.rule);
    if (selected && view) {
        const auto* header = view->horizontalHeader();
        int firstVisible = -1;
        for (int visual = 0; visual < header->count() && firstVisible < 0; ++visual)
            if (!header->isSectionHidden(header->logicalIndex(visual))) firstVisible = header->logicalIndex(visual);
        if (index.column() == firstVisible) painter->fillRect(QRect(option.rect.left(), option.rect.top(), 3, option.rect.height() - 1), colors.accent);
    }
}

// Plain-text cells on the shared row background; honours alignment and foreground roles.
class SolarTableDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const auto colors = solarTableColors(opt.palette);
        painter->save();
        paintSolarRowBackground(painter, opt, index, colors);
        const bool selected = opt.state & QStyle::State_Selected;
        QColor ink = selected ? colors.selectedInk : colors.ink;
        const QVariant foreground = index.data(Qt::ForegroundRole);
        if (foreground.isValid()) {
            const QColor color = foreground.value<QBrush>().color();
            ink = colors.dark ? color.lighter(125) : color;
        }
        const QVariant alignment = index.data(Qt::TextAlignmentRole);
        Qt::Alignment align = alignment.isValid() ? Qt::Alignment(alignment.toInt()) : Qt::Alignment(Qt::AlignLeft | Qt::AlignVCenter);
        const auto type = index.data(Qt::DisplayRole).typeId();
        if (!alignment.isValid() && (type == QMetaType::Int || type == QMetaType::Double || type == QMetaType::LongLong)) align = Qt::AlignCenter;
        if (!(align & Qt::AlignVertical_Mask)) align |= Qt::AlignVCenter;
        const QRect rect = opt.rect.adjusted(9, 0, -8, 0);
        painter->setFont(opt.font);
        painter->setPen(ink);
        painter->drawText(rect, align, opt.fontMetrics.elidedText(opt.text, Qt::ElideRight, rect.width()));
        painter->restore();
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        return {size.width() + 12, std::max(size.height(), option.fontMetrics.height() + 12)};
    }
};

inline void applySolarTableStyle(QTableView* table, QAbstractItemDelegate* delegate = nullptr) {
    table->setShowGrid(false);
    table->setAlternatingRowColors(false);
    table->setWordWrap(false);
    table->setMouseTracking(true);
    table->setProperty("solarHoverRow", -1);
    table->viewport()->installEventFilter(new SolarTableHover(table));
    table->setItemDelegate(delegate ? delegate : new SolarTableDelegate(table));
    table->verticalHeader()->setDefaultSectionSize(table->fontMetrics().height() + 12);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setMinimumHeight(table->fontMetrics().height() + 14);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
}
} // namespace dracoved
