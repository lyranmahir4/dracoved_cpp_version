#pragma once

#include <QDialog>
#include <QString>
#include <QVector>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace dracoved {

struct SavedChartEntry {
    QString profileName;
    QString personName;
    QString date;
    QString location;
    QString zodiac;
    QString houseSystem;
};

class ChartManagerDialog : public QDialog {
public:
    enum class Action {
        None,
        NewChart,
        LoadChart,
        EditChart,
        RenameChart,
        DeleteChart,
    };

    explicit ChartManagerDialog(const QVector<SavedChartEntry>& entries, QWidget* parent = nullptr);

    Action selectedAction() const;
    QString selectedProfileName() const;

private:
    void populate(const QVector<SavedChartEntry>& entries);
    void applyFilter(const QString& text);
    void updateActionAvailability();
    void finishWithAction(Action action);

    QLineEdit* searchEdit_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* countLabel_ = nullptr;
    QPushButton* loadButton_ = nullptr;
    QPushButton* editButton_ = nullptr;
    QPushButton* renameButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    Action selectedAction_ = Action::None;
};

}  // namespace dracoved
