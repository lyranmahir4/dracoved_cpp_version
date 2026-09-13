#pragma once
#include <QTableWidgetItem>

namespace dracoved {
// Sort formatted dates, angles and orbs by their values, keeping display text intact.
class SolarTransitTableItem final : public QTableWidgetItem {
public:
    SolarTransitTableItem(const QString& text, double sortValue) : QTableWidgetItem(text) {
        setData(Qt::UserRole + 2, sortValue);
    }
    bool operator<(const QTableWidgetItem& other) const override {
        return data(Qt::UserRole + 2).toDouble() < other.data(Qt::UserRole + 2).toDouble();
    }
};
} // namespace dracoved
