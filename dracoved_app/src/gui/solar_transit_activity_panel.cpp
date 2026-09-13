#include "solar_transit_panel.h"
#include "solar_transit_table_item.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateEdit>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace dracoved {
namespace {
void prepareTable(QTableWidget* table, const QStringList& headers) {
    table->setColumnCount(headers.size()); table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true); table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->setMinimumHeight(130);
}
QString stamp(const QDateTime& utc, const QTimeZone& zone) {
    return utc.toTimeZone(zone).toString("yyyy-MM-dd HH:mm:ss");
}
QString periodCaption(const SolarActivityPeriod& period, SolarActivityGrouping group, const QTimeZone& zone) {
    const auto start = period.startUtc.toTimeZone(zone);
    if (group == SolarActivityGrouping::CalendarMonth) return start.toString("MMMM yyyy");
    if (group == SolarActivityGrouping::Day) return start.toString("ddd d MMM yyyy");
    if (group == SolarActivityGrouping::Week)
        return "Week of " + start.date().addDays(1 - start.date().dayOfWeek()).toString("d MMM yyyy");
    return start.toString("d MMM yyyy") + " → " + period.endUtc.addMSecs(-1).toTimeZone(zone).toString("d MMM yyyy");
}
}

QWidget* SolarTransitPanel::createActivityPage() {
    auto* page = new QWidget(views_);
    auto* layout = new QVBoxLayout(page);
    auto* controls = new QGridLayout();
    activityGroup_ = new QComboBox(page); activityGroup_->setObjectName("solarActivityGroup");
    activityGroup_->addItems({"Day", "Week (Monday start)", "Calendar month", "Return month"});
    activityGroup_->setCurrentIndex(2);
    activityAspect_ = new QComboBox(page); activityAspect_->setObjectName("solarActivityAspect");
    activityAspect_->addItem("All generated aspects", -1);
    activityAspect_->setToolTip("Filter the saved results. To calculate additional aspects, change Generate aspects in Filters and generate again.");
    for (int angle : {0, 60, 90, 120, 180}) activityAspect_->addItem(solarTransitAspectLabel(angle), angle);
    activityMode_ = new QComboBox(page); activityMode_->setObjectName("solarActivityMode");
    activityMode_->addItems({"Exact contacts", "Contacts active within orb"});
    activityOrder_ = new QComboBox(page); activityOrder_->setObjectName("solarActivityOrder");
    activityOrder_->addItems({"Most contacts first", "Fewest contacts first", "Chronological"});
    activityOrder_->setPlaceholderText("Column heading");
    activityFrom_ = new QDateEdit(QDate::currentDate(), page); activityFrom_->setObjectName("solarActivityFrom");
    activityThrough_ = new QDateEdit(QDate::currentDate(), page); activityThrough_->setObjectName("solarActivityThrough");
    for (auto* date : {activityFrom_, activityThrough_}) {
        date->setCalendarPopup(true); date->setDisplayFormat("dd MMM yyyy"); date->setDateRange(QDate(1, 1, 1), QDate(9999, 12, 31));
    }
    controls->addWidget(new QLabel("Group by", page), 0, 0); controls->addWidget(activityGroup_, 0, 1);
    controls->addWidget(new QLabel("Aspects", page), 0, 2); controls->addWidget(activityAspect_, 0, 3);
    controls->addWidget(new QLabel("Count", page), 1, 0); controls->addWidget(activityMode_, 1, 1);
    controls->addWidget(new QLabel("Order", page), 1, 2); controls->addWidget(activityOrder_, 1, 3);
    controls->addWidget(new QLabel("From", page), 2, 0); controls->addWidget(activityFrom_, 2, 1);
    controls->addWidget(new QLabel("Through", page), 2, 2); controls->addWidget(activityThrough_, 2, 3);
    controls->setColumnStretch(1, 1); controls->setColumnStretch(3, 1);
    layout->addLayout(controls);
    auto* actions = new QHBoxLayout();
    auto* wholeYear = new QPushButton("Entire return year", page); wholeYear->setObjectName("solarActivityWholeYear");
    auto* copy = new QPushButton("Copy activity", page);
    actions->addWidget(wholeYear); actions->addStretch(); actions->addWidget(copy); layout->addLayout(actions);
    activityLabel_ = new QLabel("Generate a return year to compare periods.", page); activityLabel_->setWordWrap(true);
    layout->addWidget(activityLabel_);
    auto* split = new QSplitter(Qt::Vertical, page); split->setChildrenCollapsible(false);
    activityTable_ = new QTableWidget(split); activityTable_->setObjectName("solarActivityPeriods");
    prepareTable(activityTable_, {"Period", "Total", "Conjunctions", "Sextiles", "Squares", "Trines", "Oppositions"});
    activityTable_->horizontalHeader()->setStretchLastSection(false);
    activityTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    activityTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    activityTable_->horizontalHeader()->setSortIndicator(1, Qt::DescendingOrder);
    activityTable_->setSortingEnabled(true);
    activityTable_->horizontalHeader()->setToolTip("Click a heading to sort; click again to reverse. Period restores date order.");
    activityDetails_ = new QTableWidget(split); activityDetails_->setObjectName("solarActivityContacts");
    prepareTable(activityDetails_, {"Local time", "Transit", "SR target", "Aspect", "Event / period"});
    activityDetails_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    activityDetails_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
    activityDetails_->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
    activityDetails_->setSortingEnabled(true);
    split->setSizes({240, 200}); layout->addWidget(split, 1);
    for (auto* combo : {activityGroup_, activityAspect_, activityMode_})
        connect(combo, &QComboBox::currentIndexChanged, this, [this](int) { renderActivity(); });
    connect(activityOrder_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0) activityTable_->sortItems(index == 2 ? 0 : 1, index == 0 ? Qt::DescendingOrder : Qt::AscendingOrder);
    });
    connect(activityTable_->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this, [this](int column, Qt::SortOrder order) {
        const QSignalBlocker block(activityOrder_);
        activityOrder_->setCurrentIndex(column == 1 ? (order == Qt::DescendingOrder ? 0 : 1)
            : column == 0 && order == Qt::AscendingOrder ? 2 : -1);
    });
    for (auto* date : {activityFrom_, activityThrough_})
        connect(date, &QDateEdit::dateChanged, this, [this](const QDate&) { renderActivity(); });
    connect(wholeYear, &QPushButton::clicked, this, [this]() {
        if (!result_) return;
        const auto zone = result_->query.source.returnChart.localDateTime.timeZone();
        const QSignalBlocker first(activityFrom_), last(activityThrough_);
        activityFrom_->setDate(result_->startUtc.toTimeZone(zone).date());
        activityThrough_->setDate(result_->endUtc.addMSecs(-1).toTimeZone(zone).date());
        renderActivity();
    });
    connect(activityTable_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) { showActivityPeriod(row); });
    connect(activityDetails_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) { selectActivityContact(row); });
    connect(copy, &QPushButton::clicked, this, [this]() {
        if (!result_) return;
        const auto zone = result_->query.source.returnChart.localDateTime.timeZone();
        QStringList lines{QString("%1 · %2 · %3 · %4 · %5 through %6 (%7)")
            .arg(result_->query.source.returnInput.name, activityGroup_->currentText(), activityMode_->currentText(), activityAspect_->currentText(),
                activityFrom_->date().toString(Qt::ISODate), activityThrough_->date().toString(Qt::ISODate), result_->query.source.returnChart.timezoneLabel),
            "Start local\tEnd local (exclusive)\tTotal\tConjunctions\tSextiles\tSquares\tTrines\tOppositions"};
        for (int row = 0; row < activityTable_->rowCount(); ++row) {
            const auto& period = activityPeriods_[activityTable_->item(row, 0)->data(Qt::UserRole).toInt()];
            QStringList fields{stamp(period.startUtc, zone), stamp(period.endUtc, zone), QString::number(period.total())};
            const std::array<int, 5> angles{0, 60, 90, 120, 180};
            for (int i = 0; i < 5; ++i) fields << (result_->query.aspects.contains(angles[i]) ? QString::number(period.counts[i]) : "Not generated");
            lines << fields.join('\t');
        }
        QApplication::clipboard()->setText(lines.join('\n'));
        statusLabel_->setText("Activity counts copied with period boundaries and counting mode.");
    });
    return page;
}

void SolarTransitPanel::renderActivity() {
    if (!activityTable_) return;
    const QSignalBlocker top(activityTable_), bottom(activityDetails_);
    activityPeriods_.clear(); activitySelectedPeriod_ = -1;
    activityTable_->setRowCount(0); activityDetails_->setRowCount(0);
    if (!result_) { activityLabel_->setText("Generate a return year to compare periods."); return; }
    if (activityFrom_->date() > activityThrough_->date()) {
        activityLabel_->setText("From must be on or before Through."); return;
    }
    const int selectedAspect = activityAspect_->currentData().toInt();
    if (selectedAspect >= 0 && !result_->query.aspects.contains(selectedAspect)) {
        activityLabel_->setText("This aspect was not generated. In Filters, change Generate aspects, then click Generate return year. Activity only filters saved results."); return;
    }
    activityPeriods_ = solarActivityPeriods(*result_, SolarActivityGrouping(activityGroup_->currentIndex()),
        activityMode_->currentIndex() == 1, activityAspect_->currentData().toInt(), activityFrom_->date(), activityThrough_->date());
    activityTable_->setSortingEnabled(false);
    const auto zone = result_->query.source.returnChart.localDateTime.timeZone();
    for (int index = 0; index < activityPeriods_.size(); ++index) {
        const auto& period = activityPeriods_[index];
        const int row = activityTable_->rowCount(); activityTable_->insertRow(row);
        auto* start = new SolarTransitTableItem(periodCaption(period, SolarActivityGrouping(activityGroup_->currentIndex()), zone), period.startUtc.toMSecsSinceEpoch());
        start->setData(Qt::UserRole, index);
        start->setToolTip("From " + stamp(period.startUtc, zone) + " until " + stamp(period.endUtc, zone) + " (exclusive)");
        activityTable_->setItem(row, 0, start);
        auto* total = new QTableWidgetItem(); total->setData(Qt::DisplayRole, period.total()); activityTable_->setItem(row, 1, total);
        for (int col = 0; col < 5; ++col) {
            const std::array<int, 5> angles{0, 60, 90, 120, 180};
            auto* item = new QTableWidgetItem();
            if (result_->query.aspects.contains(angles[col])) item->setData(Qt::DisplayRole, period.counts[col]);
            else { item->setText("—"); item->setToolTip("This aspect was not generated."); }
            activityTable_->setItem(row, col + 2, item);
        }
    }
    activityTable_->setSortingEnabled(true);
    QStringList generatedAspects;
    for (int angle : result_->query.aspects) generatedAspects << solarTransitAspectLabel(angle);
    activityLabel_->setText(QString("%1 periods · %2. Generated: %3. Click headings to sort or reverse. Zero-contact periods are included; — means not generated. Dates checkboxes do not alter these counts.")
        .arg(activityPeriods_.size()).arg(activityMode_->currentIndex() == 0
            ? "Exact hits only; repeated passes count separately"
            : "Each transit/target/aspect counts once per period; contacts may occur in multiple periods")
        .arg(generatedAspects.join(", ")));
    if (result_->query.aspects.size() < 5)
        activityLabel_->setText(activityLabel_->text() + " For all five aspects, choose Filters → Generate aspects → All major aspects, then Generate return year.");
    if (!activityPeriods_.isEmpty()) { activityTable_->selectRow(0); showActivityPeriod(0); }
}

void SolarTransitPanel::showActivityPeriod(int row) {
    const int index = row >= 0 && activityTable_->item(row, 0) ? activityTable_->item(row, 0)->data(Qt::UserRole).toInt() : -1;
    if (index == activitySelectedPeriod_) return;
    const QSignalBlocker block(activityDetails_);
    activityDetails_->setRowCount(0); activitySelectedPeriod_ = index;
    if (!result_ || index < 0 || index >= activityPeriods_.size()) return;
    activityDetails_->setSortingEnabled(false);
    const auto& period = activityPeriods_[index];
    const auto zone = result_->query.source.returnChart.localDateTime.timeZone();
    auto add = [&](const QStringList& fields, const QDateTime& utc, int aspect, int event, int window) {
        const int index = activityDetails_->rowCount(); activityDetails_->insertRow(index);
        for (int col = 0; col < fields.size(); ++col) {
            QTableWidgetItem* item = col == 0 ? new SolarTransitTableItem(fields[col], utc.toMSecsSinceEpoch())
                : col == 3 ? new SolarTransitTableItem(fields[col], aspect) : new QTableWidgetItem(fields[col]);
            activityDetails_->setItem(index, col, item);
        }
        activityDetails_->item(index, 0)->setData(Qt::UserRole, event);
        activityDetails_->item(index, 0)->setData(Qt::UserRole + 1, window);
    };
    for (int index : period.eventIndices) {
        const auto& event = result_->events[index];
        add({stamp(event.utc, zone), event.body, event.target, solarTransitAspectLabel(event.aspect), event.kind}, event.utc, event.aspect, index, -1);
    }
    for (int index : period.windowIndices) {
        const auto& window = result_->windows[index];
        add({stamp(std::max(period.startUtc, window.startUtc), zone), window.body, window.target, solarTransitAspectLabel(window.aspect),
            "Active interval until " + stamp(std::min(period.endUtc, window.endUtc), zone)}, std::max(period.startUtc, window.startUtc), window.aspect, -1, index);
    }
    activityDetails_->setSortingEnabled(true);
}

void SolarTransitPanel::selectActivityContact(int row) {
    if (!result_ || row < 0 || !activityDetails_->item(row, 0) || activitySelectedPeriod_ < 0 || activitySelectedPeriod_ >= activityPeriods_.size()) return;
    const int eventIndex = activityDetails_->item(row, 0)->data(Qt::UserRole).toInt();
    if (eventIndex >= 0) { selectEvent(eventIndex); return; }
    const int windowIndex = activityDetails_->item(row, 0)->data(Qt::UserRole + 1).toInt();
    if (windowIndex < 0 || windowIndex >= result_->windows.size()) return;
    const auto& window = result_->windows[windowIndex];
    const auto& period = activityPeriods_[activitySelectedPeriod_];
    SolarTransitEvent event;
    event.utc = std::max(period.startUtc, window.startUtc);
    event.untilUtc = std::min(period.endUtc, window.endUtc);
    event.body = window.body; event.target = window.target; event.aspect = window.aspect;
    event.kind = "Active within orb";
    event.note = "Representative active moment in this period; not an exact-hit event. Repeated intervals count once per period.";
    bool found = false;
    for (const auto& target : result_->query.targets) if (target.name == event.target) { event.targetLongitude = target.longitude; found = true; break; }
    if (found) selectContact(event);
}
} // namespace dracoved
