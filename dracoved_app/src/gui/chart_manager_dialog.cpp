#include "chart_manager_dialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace dracoved {

ChartManagerDialog::ChartManagerDialog(const QVector<SavedChartEntry>& entries, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Chart Manager");
    setModal(true);
    resize(820, 430);
    setMinimumSize(700, 360);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    auto* introLabel = new QLabel(
        "Organize saved charts, or select one to load or edit.", this);
    layout->addWidget(introLabel);

    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText("Search saved charts...");
    searchEdit_->setClearButtonEnabled(true);
    layout->addWidget(searchEdit_);

    table_ = new QTableWidget(this);
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels(
        {"Chart", "Name", "Date", "Location", "Zodiac", "House System"});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    table_->setSortingEnabled(false);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    layout->addWidget(table_, 1);

    countLabel_ = new QLabel(this);
    layout->addWidget(countLabel_);

    auto* actionsLayout = new QHBoxLayout();
    actionsLayout->setSpacing(6);
    auto* newButton = new QPushButton("New Chart...", this);
    loadButton_ = new QPushButton("Load Selected", this);
    editButton_ = new QPushButton("Edit Data...", this);
    renameButton_ = new QPushButton("Rename...", this);
    deleteButton_ = new QPushButton("Delete", this);
    deleteButton_->setToolTip("Permanently delete the selected saved chart after confirmation.");

    actionsLayout->addWidget(newButton);
    actionsLayout->addWidget(loadButton_);
    actionsLayout->addWidget(editButton_);
    actionsLayout->addWidget(renameButton_);
    actionsLayout->addWidget(deleteButton_);
    actionsLayout->addStretch(1);

    auto* closeBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    actionsLayout->addWidget(closeBox);
    layout->addLayout(actionsLayout);

    populate(entries);
    updateActionAvailability();

    connect(searchEdit_, &QLineEdit::textChanged, this, [this](const QString& text) {
        applyFilter(text);
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this]() {
        updateActionAvailability();
    });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && !table_->isRowHidden(row)) {
            table_->selectRow(row);
            finishWithAction(Action::LoadChart);
        }
    });
    connect(newButton, &QPushButton::clicked, this, [this]() {
        finishWithAction(Action::NewChart);
    });
    connect(loadButton_, &QPushButton::clicked, this, [this]() {
        finishWithAction(Action::LoadChart);
    });
    connect(editButton_, &QPushButton::clicked, this, [this]() {
        finishWithAction(Action::EditChart);
    });
    connect(renameButton_, &QPushButton::clicked, this, [this]() {
        finishWithAction(Action::RenameChart);
    });
    connect(deleteButton_, &QPushButton::clicked, this, [this]() {
        finishWithAction(Action::DeleteChart);
    });
    connect(closeBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

ChartManagerDialog::Action ChartManagerDialog::selectedAction() const {
    return selectedAction_;
}

QString ChartManagerDialog::selectedProfileName() const {
    if (!table_) {
        return QString();
    }
    const int row = table_->currentRow();
    if (row < 0) {
        return QString();
    }
    auto* item = table_->item(row, 0);
    return item ? item->data(Qt::UserRole).toString().trimmed() : QString();
}

void ChartManagerDialog::populate(const QVector<SavedChartEntry>& entries) {
    table_->setSortingEnabled(false);
    table_->setRowCount(entries.size());
    for (int row = 0; row < entries.size(); ++row) {
        const auto& entry = entries[row];
        auto* chartItem = new QTableWidgetItem(entry.profileName);
        chartItem->setData(Qt::UserRole, entry.profileName);
        table_->setItem(row, 0, chartItem);
        table_->setItem(row, 1, new QTableWidgetItem(entry.personName));
        table_->setItem(row, 2, new QTableWidgetItem(entry.date));
        table_->setItem(row, 3, new QTableWidgetItem(entry.location));
        table_->setItem(row, 4, new QTableWidgetItem(entry.zodiac));
        table_->setItem(row, 5, new QTableWidgetItem(entry.houseSystem));
    }
    table_->setSortingEnabled(true);
    table_->sortItems(0, Qt::AscendingOrder);
    if (table_->rowCount() > 0) {
        table_->selectRow(0);
    }
    countLabel_->setText(QString("%1 saved chart%2")
                             .arg(entries.size())
                             .arg(entries.size() == 1 ? QString() : QString("s")));
}

void ChartManagerDialog::applyFilter(const QString& text) {
    const QString needle = text.trimmed();
    int visibleCount = 0;
    for (int row = 0; row < table_->rowCount(); ++row) {
        bool matches = needle.isEmpty();
        for (int column = 0; !matches && column < table_->columnCount(); ++column) {
            const auto* item = table_->item(row, column);
            matches = item && item->text().contains(needle, Qt::CaseInsensitive);
        }
        table_->setRowHidden(row, !matches);
        if (matches) {
            ++visibleCount;
        }
    }
    countLabel_->setText(needle.isEmpty()
        ? QString("%1 saved chart%2")
              .arg(table_->rowCount())
              .arg(table_->rowCount() == 1 ? QString() : QString("s"))
        : QString("%1 matching chart%2")
              .arg(visibleCount)
              .arg(visibleCount == 1 ? QString() : QString("s")));
    updateActionAvailability();
}

void ChartManagerDialog::updateActionAvailability() {
    const int row = table_ ? table_->currentRow() : -1;
    const bool hasSelection = row >= 0 && !table_->isRowHidden(row)
        && !selectedProfileName().isEmpty();
    loadButton_->setEnabled(hasSelection);
    editButton_->setEnabled(hasSelection);
    renameButton_->setEnabled(hasSelection);
    deleteButton_->setEnabled(hasSelection);
}

void ChartManagerDialog::finishWithAction(Action action) {
    if (action != Action::NewChart && selectedProfileName().isEmpty()) {
        return;
    }
    selectedAction_ = action;
    accept();
}

}  // namespace dracoved
