#pragma once
#include <QLayout>
#include <QWidget>

namespace dracoved {
// Keep related controls together while allowing a split pane to become narrow.
class CompactControls final : public QLayout {
public:
    CompactControls() { setContentsMargins(0, 0, 0, 0); setSpacing(6); }
    ~CompactControls() override { while (auto* item = takeAt(0)) delete item; }
    void addItem(QLayoutItem* item) override { items_.append(item); }
    int count() const override { return items_.size(); }
    QLayoutItem* itemAt(int index) const override { return items_.value(index); }
    QLayoutItem* takeAt(int index) override { return index >= 0 && index < items_.size() ? items_.takeAt(index) : nullptr; }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize minimumSize() const override {
        QSize size;
        for (auto* item : items_) size = size.expandedTo(item->minimumSize());
        return size;
    }
    QSize sizeHint() const override { return minimumSize(); }
    void setGeometry(const QRect& rect) override { QLayout::setGeometry(rect); arrange(rect, true); }
private:
    int arrange(const QRect& rect, bool place) const {
        int x = rect.x(), y = rect.y(), height = 0;
        for (auto* item : items_) {
            const QSize size = item->sizeHint();
            if (x > rect.x() && x + size.width() > rect.x() + rect.width()) {
                x = rect.x(); y += height + spacing(); height = 0;
            }
            if (place) item->setGeometry(QRect(QPoint(x, y), size));
            x += size.width() + spacing(); height = qMax(height, size.height());
        }
        return y + height - rect.y();
    }
    QList<QLayoutItem*> items_;
};
} // namespace dracoved
