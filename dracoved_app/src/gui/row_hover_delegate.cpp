#include "row_hover_delegate.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>

namespace dracoved {

RowHoverDelegate::RowHoverDelegate(QAbstractItemView* view, QObject* parent)
    : QStyledItemDelegate(parent), view_(view) {
    if (view_) {
        view_->setMouseTracking(true);
        if (view_->viewport()) {
            view_->viewport()->setMouseTracking(true);
            view_->viewport()->installEventFilter(this);
        }
    }
}

void RowHoverDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    if (index.row() == hoveredRow_ && !(option.state & QStyle::State_Selected)) {
        painter->fillRect(option.rect, hoverColor_);
    }
    QStyledItemDelegate::paint(painter, option, index);
}

bool RowHoverDelegate::eventFilter(QObject* obj, QEvent* event) {
    if (view_ && view_->viewport() == obj) {
        if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            const QModelIndex idx = view_->indexAt(me->position().toPoint());
            const int row = idx.isValid() ? idx.row() : -1;
            if (row != hoveredRow_) {
                hoveredRow_ = row;
                view_->viewport()->update();
            }
        } else if (event->type() == QEvent::Leave) {
            if (hoveredRow_ != -1) {
                hoveredRow_ = -1;
                view_->viewport()->update();
            }
        }
    }
    return QStyledItemDelegate::eventFilter(obj, event);
}

}  // namespace dracoved
