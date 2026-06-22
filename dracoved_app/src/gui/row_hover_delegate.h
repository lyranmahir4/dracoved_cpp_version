#pragma once

#include <QStyledItemDelegate>
#include <QColor>

class QAbstractItemView;

namespace dracoved {

// Adds a clean full-row hover highlight to a plain item view while preserving
// the view's normal content rendering (text, colours, selection). Tracks the
// hovered row itself via an event filter on the viewport, so no external wiring
// is needed beyond constructing it with the target view.
class RowHoverDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit RowHoverDelegate(QAbstractItemView* view, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QAbstractItemView* view_ = nullptr;
    int hoveredRow_ = -1;
    QColor hoverColor_{128, 128, 128, 30};
};

}  // namespace dracoved
