// Offscreen standalone smoke test; does not launch/rebuild dracoved_app.exe.
#include "solar_transit_panel.h"
#include "chart_wheel_widget.h"
#include "return_calculation_service.h"
#include "../core/swiss_eph.h"
#include "../core/tropical_natal.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QThread>
#include <QClipboard>
#include <QTimeZone>
#include <QFontDatabase>
#include <QDialog>
#include <QScrollArea>
#include <QScrollBar>
#include <QMenu>
#include <QAction>
#include <QComboBox>
#include <QDateEdit>
#include <QHeaderView>
#include <iostream>
#include <stdexcept>
using namespace dracoved;
static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/seguisym.ttf");
    app.setFont(QFont("Segoe UI", 9));
    try {
        SwissEph swe; QString error;
        SolarTransitSource source;
        source.dllPath = QDir::current().absoluteFilePath("swedll64.dll"); source.ephePath = QDir::current().absoluteFilePath("ephe");
        require(swe.load({source.dllPath}, &error), "Load ephemeris"); swe.setEphePath(source.ephePath);
        source.natalInput.name = "Research test"; source.natalInput.date = QDate(1999, 1, 4); source.natalInput.time = QTime(16, 1);
        source.natalInput.timezone = "Asia/Dhaka"; source.natalInput.latitude = 23.764; source.natalInput.longitude = 90.389;
        const auto birth = QDateTime(source.natalInput.date, source.natalInput.time, QTimeZone("Asia/Dhaka")).toUTC();
        const double jd = swe.julianDay(1999, 1, 4, birth.time().msecsSinceStartOfDay()/3600000.0, SE_GREG_CAL);
        require(swe.calcUt(jd, SE_SUN, 0, &source.natalSunLongitude, &error), "Natal target");
        source.returnYear = 2026;
        QDateTime utc, local;
        require(returncalc::solarReturnTimeUtc(swe, source.natalInput, 2026, "Asia/Dhaka", source.natalSunLongitude, &utc, &local, &error), "Return time");
        source.returnInput = source.natalInput; source.returnInput.name = "Solar Return 2026";
        source.returnInput.date = local.date(); source.returnInput.time = local.time(); source.location = "Dhaka";
        TropicalNatalEngine engine(&swe, source.ephePath); TropicalComputeOptions options;
        options.includeArabicLots = false; options.includeFixedStars = false;
        require(engine.compute(source.returnInput, options, &source.returnChart, &error), "Return chart");
        bool pending = false;
        SolarTransitPanel panel([&](SolarTransitSource* out, QString* err) {
            if (pending) { *err = "Recalculate the return first."; return false; }
            *out = source; return true;
        });
        ChartWheelWidget mainWheel;
        mainWheel.resize(950, 850);
        int selectedUpdates = 0;
        panel.selectionChanged = [&]() {
            ++selectedUpdates;
            panel.renderSelectedChart(&mainWheel, source.returnInput.aspectOrbs, 3.0);
        };
        panel.resize(1100, 820); panel.show(); panel.refreshSource(); app.processEvents();
        auto* bodies = panel.findChild<QTreeWidget*>("solarTransitBodies");
        auto* targets = panel.findChild<QTreeWidget*>("solarTransitTargets");
        require(bodies && targets, "Body and target controls exist");
        const bool allMajor = app.arguments().contains("--all-major");
        auto* searchAspects = panel.findChild<QComboBox*>("solarTransitSearchAspects");
        require(searchAspects, "Generation aspect control exists");
        require(searchAspects->currentData().toInt() == -1, "Generation defaults to all major aspects");
        searchAspects->setCurrentIndex(searchAspects->findData(allMajor ? -1 : 0));
        const QStringList movingPlanets{"Sun", "Mercury", "Venus", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune", "Pluto"};
        for (auto* tree : {bodies, targets}) for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto* group = tree->topLevelItem(i);
            for (int j = 0; j < group->childCount(); ++j) {
                auto* item = group->child(j); const auto name = item->data(0, Qt::UserRole).toString();
                const bool selected = tree == bodies ? (allMajor ? movingPlanets.contains(name) : name == "Mercury" || name == "Pluto")
                    : (name == "Venus" || name == "Pluto" || name == "Ascendant");
                item->setCheckState(0, selected ? Qt::Checked : Qt::Unchecked);
            }
        }
        QPushButton* run = nullptr; QPushButton* stop = nullptr; QPushButton* copy = nullptr;
        for (auto* button : panel.findChildren<QPushButton*>()) {
            if (button->text() == "Generate return year") run = button;
            if (button->text() == "Stop") stop = button;
            if (button->text() == "Copy results") copy = button;
        }
        require(run && stop && copy, "Actions exist"); run->click();
        QElapsedTimer timer; timer.start();
        while (stop->isEnabled() && timer.elapsed() < 120000) { app.processEvents(); QThread::msleep(5); }
        require(!stop->isEnabled(), "Worker completed without UI deadlock");
        const auto status = panel.findChild<QLabel*>("solarTransitStatus")->text();
        if (!status.startsWith("Done")) throw std::runtime_error(status.toStdString());
        auto* dates = panel.findChild<QTableWidget*>("solarTransitDates");
        require(dates && dates->rowCount() > 0, "Dates populated");
        auto checkNumericOrder = [&](QTableWidget* table, int column, int role, Qt::SortOrder order) {
            table->sortItems(column, order);
            for (int row = 1; row < table->rowCount(); ++row) {
                const double before = table->item(row - 1, column)->data(role).toDouble();
                const double after = table->item(row, column)->data(role).toDouble();
                require(order == Qt::AscendingOrder ? before <= after : before >= after, "Numeric table order");
            }
        };
        require(dates->isSortingEnabled(), "Date headers support sorting");
        for (int column : {0, 1, 4, 6, 8}) for (auto order : {Qt::AscendingOrder, Qt::DescendingOrder})
            checkNumericOrder(dates, column, Qt::UserRole + 2, order);
        dates->sortItems(2, Qt::DescendingOrder);
        dates->selectRow(std::min(2, dates->rowCount()-1)); app.processEvents();
        auto* selectedDate = dates->item(dates->currentRow(), 0);
        const auto selectedUtc = panel.selectedTransitChart()->utcDateTime;
        dates->sortItems(0, Qt::DescendingOrder);
        require(dates->item(dates->currentRow(), 0) == selectedDate && panel.selectedTransitChart()->utcDateTime == selectedUtc,
            "Date sorting retains selected contact and chart");
        dates->selectRow(0);
        require(std::abs(panel.selectedTransitChart()->utcDateTime.toMSecsSinceEpoch() - dates->item(0, 0)->data(Qt::UserRole + 2).toDouble()) < 1000,
            "Sorted date selection resolves the actual event");
        copy->click(); require(QApplication::clipboard()->text().contains("Orb periods"), "Export includes active windows");
        require(panel.grab().save("dracoved_app/build/solar_transit_ui_dates.png"), "Save date view");
        auto* views = panel.findChild<QTabWidget*>();
        views->setCurrentIndex(1); app.processEvents(); require(panel.grab().save("dracoved_app/build/solar_transit_ui_timeline.png"), "Save timeline view");
        require(views->count() == 3 && views->tabText(2) == "Activity", "Dates, timeline and activity views");
        require(panel.findChildren<ChartWheelWidget*>().isEmpty(), "No embedded chart remains");
        require(selectedUpdates >= 2, "Row selections notify the main chart owner");
        require(panel.renderSelectedChart(&mainWheel, source.returnInput.aspectOrbs, 3.0), "Selected overlay can be restored on returning to the view");
        require(mainWheel.grab().save("dracoved_app/build/solar_transit_ui_chart.png"), "Save full size selected return overlay");
        const int allRows = dates->rowCount();
        int exactRows = 0;
        for (int row = 0; row < allRows; ++row) if (dates->item(row, 5)->text().startsWith("Exact")) ++exactRows;
        auto* eventMenu = panel.findChild<QMenu*>("solarTransitEventMenu"); require(eventMenu, "Event checklist exists");
        auto preset = [&](const QString& text) {
            for (auto* action : eventMenu->actions()) if (action->text() == text) { action->trigger(); return; }
            throw std::runtime_error("Missing event preset");
        };
        preset("Exact only");
        require(dates->rowCount() == exactRows && exactRows > 0, "Exact preset removes entry/exit clutter");
        auto* entryAction = panel.findChild<QAction*>("solarTransitEventType1"); require(entryAction, "Entry checkbox exists");
        entryAction->setChecked(true); require(dates->rowCount() > exactRows, "Independent categories combine");
        for (int row = 0; row < dates->rowCount(); ++row) if (dates->item(row, 5)->text() == "Orb entry") { dates->selectRow(row); break; }
        entryAction->setChecked(false);
        require(!panel.renderSelectedChart(&mainWheel, source.returnInput.aspectOrbs, 3.0), "Hidden selected event clears the overlay request");
        preset("Copy visible dates"); require(QApplication::clipboard()->text().count('\n') == exactRows, "Visible export matches checklist");
        auto* activity = panel.findChild<QTableWidget*>("solarActivityPeriods");
        auto* activityDetails = panel.findChild<QTableWidget*>("solarActivityContacts");
        auto* activityMode = panel.findChild<QComboBox*>("solarActivityMode");
        auto* activityAspect = panel.findChild<QComboBox*>("solarActivityAspect");
        auto* activityGroup = panel.findChild<QComboBox*>("solarActivityGroup");
        auto* activityOrder = panel.findChild<QComboBox*>("solarActivityOrder");
        require(activity && activityDetails && activityMode && activityAspect && activityGroup, "Activity controls exist");
        require(activityAspect->itemText(activityAspect->findData(-1)) == "All generated aspects", "Activity clearly refers to the saved search");
        int exactSum = 0, previousCount = 100000;
        for (int row = 0; row < activity->rowCount(); ++row) {
            const int count = activity->item(row, 1)->data(Qt::DisplayRole).toInt();
            require(count <= previousCount, "Most contacts sort descending"); previousCount = count; exactSum += count;
        }
        require(exactSum == exactRows, "Activity exact totals match saved hits independently of checklist");
        if (allMajor) for (int column = 2; column < 7; ++column) {
            int aspectSum = 0;
            for (int row = 0; row < activity->rowCount(); ++row) {
                require(activity->item(row, column)->data(Qt::DisplayRole).metaType().id() == QMetaType::Int, "Generated aspect has numeric counts");
                aspectSum += activity->item(row, column)->text().toInt();
            }
            require(aspectSum > 0, "Every generated major aspect has contacts");
        }
        require(activity->isSortingEnabled() && activityDetails->isSortingEnabled(), "Activity headers support sorting");
        activityOrder->setCurrentIndex(activityOrder->findText("Fewest contacts first"));
        require(activity->horizontalHeader()->sortIndicatorSection() == 1 && activity->horizontalHeader()->sortIndicatorOrder() == Qt::AscendingOrder,
            "Fewest preset sorts total ascending");
        for (int column = 1; column < (allMajor ? 7 : 3); ++column) for (auto order : {Qt::AscendingOrder, Qt::DescendingOrder})
            checkNumericOrder(activity, column, Qt::DisplayRole, order);
        for (auto order : {Qt::AscendingOrder, Qt::DescendingOrder}) checkNumericOrder(activity, 0, Qt::UserRole + 2, order);
        activityGroup->setCurrentIndex(0);
        activityOrder->setCurrentIndex(activityOrder->findText("Fewest contacts first"));
        require(activity->item(0, 1)->text() == "0", "Quietest days include zero-contact periods");
        activity->selectRow(0); require(activityDetails->rowCount() == 0, "Quiet day drilldown stays empty");
        activityGroup->setCurrentIndex(2);
        activityOrder->setCurrentIndex(activityOrder->findText("Most contacts first"));
        views->setCurrentIndex(2); activity->selectRow(0);
        require(activityDetails->rowCount() > 0, "Top period reveals exact contacts");
        activityDetails->selectRow(0);
        require(panel.renderSelectedChart(&mainWheel, source.returnInput.aspectOrbs, 3.0), "Activity exact selection updates main chart");
        auto* selectedPeriod = activity->item(activity->currentRow(), 0);
        auto* selectedDetail = activityDetails->item(activityDetails->currentRow(), 0);
        activity->sortItems(0, Qt::AscendingOrder);
        require(activity->item(activity->currentRow(), 0) == selectedPeriod && activityDetails->item(activityDetails->currentRow(), 0) == selectedDetail,
            "Period sorting preserves selected period and its contact");
        activityDetails->sortItems(0, Qt::DescendingOrder); activityDetails->selectRow(0);
        require(std::abs(panel.selectedTransitChart()->utcDateTime.toMSecsSinceEpoch() - activityDetails->item(0, 0)->data(Qt::UserRole + 2).toDouble()) < 1000,
            "Sorted detail selection resolves the actual event");
        activity->sortItems(2, Qt::DescendingOrder);
        for (int row = 0; row < activity->rowCount(); ++row) {
            activity->selectRow(row);
            require(activityDetails->rowCount() == activity->item(row, 1)->text().toInt(), "Sorted period resolves its own contacts");
        }
        for (auto* button : panel.findChildren<QPushButton*>()) if (button->text() == "Copy activity") button->click();
        const auto activityLines = QApplication::clipboard()->text().split('\n');
        for (int row = 0; row < activity->rowCount(); ++row) {
            const auto fields = activityLines[row + 2].split('\t');
            require(fields[2] == activity->item(row, 1)->text(), "Activity export preserves visible ranking");
            const auto start = QDateTime::fromMSecsSinceEpoch(activity->item(row, 0)->data(Qt::UserRole + 2).toLongLong(), QTimeZone("Asia/Dhaka"));
            require(fields[0] == start.toString("yyyy-MM-dd HH:mm:ss"), "Activity export keeps period identity");
        }
        activityAspect->setCurrentIndex(activityAspect->findData(90));
        if (allMajor) {
            require(activity->rowCount() > 0, "Generated squares remain visible when selected");
            int squares = 0;
            for (int row = 0; row < activity->rowCount(); ++row) {
                require(activity->item(row, 1)->text() == activity->item(row, 4)->text(), "Square filter agrees with square count");
                squares += activity->item(row, 4)->text().toInt();
            }
            require(squares > 0, "Square filter contains real hits");
        } else require(activity->rowCount() == 0, "Ungenerated aspect is not reported as zero activity");
        activityAspect->setCurrentIndex(0);
        activityOrder->setCurrentIndex(activityOrder->findText("Most contacts first"));
        activityMode->setCurrentIndex(1); activity->selectRow(0);
        require(activityDetails->rowCount() > 0, "Active contacts drilldown exists");
        activityDetails->selectRow(0);
        require(panel.renderSelectedChart(&mainWheel, source.returnInput.aspectOrbs, 3.0), "Active period selection updates chart even without exact hit");
        activity->sortItems(0, Qt::AscendingOrder);
        activityDetails->sortItems(1, Qt::DescendingOrder); activityDetails->selectRow(0);
        require(std::abs(panel.selectedTransitChart()->utcDateTime.toMSecsSinceEpoch() - activityDetails->item(0, 0)->data(Qt::UserRole + 2).toDouble()) < 1000,
            "Sorted active contact uses the selected period boundary");
        for (auto* button : panel.findChildren<QPushButton*>()) if (button->text() == "Copy selected") button->click();
        require(QApplication::clipboard()->text().contains("Active within orb"), "Active selection has copy parity");
        app.processEvents(); require(panel.grab().save("dracoved_app/build/solar_transit_ui_activity.png"), "Save activity view");
        activityGroup->setCurrentIndex(3);
        require(activity->rowCount() >= 12, "Return month grouping available");
        auto* activityFrom = panel.findChild<QDateEdit*>("solarActivityFrom");
        auto* activityThrough = panel.findChild<QDateEdit*>("solarActivityThrough");
        activityFrom->setDate(activityThrough->date().addDays(1)); require(activity->rowCount() == 0, "Invalid custom range clears ranking");
        panel.findChild<QPushButton*>("solarActivityWholeYear")->click(); require(activity->rowCount() > 0, "Entire year restores range");
        preset("Show all"); require(dates->rowCount() == allRows, "Checklist restores all dates without recalculation");
        bool expanded = false; panel.expandResultsChanged = [&](bool value) { expanded = value; };
        auto* expand = panel.findChild<QPushButton*>("solarTransitExpandResults"); expand->click(); require(expanded, "Expand results request");
        expand->click(); require(!expanded, "Restore results request");
        views->setCurrentIndex(0);
        panel.resize(650, 310); app.processEvents();
        auto* contentScroll = panel.findChild<QScrollArea*>("solarTransitContentScroll");
        require(contentScroll && contentScroll->verticalScrollBar()->maximum() > 0, "Short embedded panel scrolls");
        require(dates->height() >= 200 && dates->width() >= 500, "Compact results retain usable dimensions");
        require(contentScroll->horizontalScrollBar()->maximum() == 0, "Results fit the reported panel width");
        contentScroll->verticalScrollBar()->setValue(contentScroll->verticalScrollBar()->maximum());
        app.processEvents();
        require(panel.grab().save("dracoved_app/build/solar_transit_ui_compact.png"), "Save compact dates");
        auto* editFilters = panel.findChild<QPushButton*>("solarTransitEditFilters");
        require(editFilters, "Filter action exists"); editFilters->click(); app.processEvents();
        auto* filterDialog = panel.findChild<QDialog*>("solarTransitFilterDialog");
        require(filterDialog && filterDialog->isVisible() && !filterDialog->isModal(), "Filters open without blocking results");
        require(bodies->width() >= 280 && targets->width() >= 280, "Filter trees remain readable");
        require(filterDialog->grab().save("dracoved_app/build/solar_transit_ui_filters.png"), "Save filters");
        filterDialog->hide();
        pending = true; panel.refreshSource(); require(!run->isEnabled(), "Pending source prevents a new calculation");
        require(!panel.renderSelectedChart(&mainWheel, source.returnInput.aspectOrbs, 3.0), "Pending source cannot display stale overlay");
        require(dates->rowCount() > 0, "Prior snapshot remains reviewable");
        const int dateCount = dates->rowCount();
        pending = false; panel.refreshSource(); require(run->isEnabled(), "Recalculated source enables generation");
        run->click(); stop->click(); timer.restart();
        while (stop->isEnabled() && timer.elapsed() < 120000) { app.processEvents(); QThread::msleep(5); }
        require(!stop->isEnabled(), "Cancellation completes without deadlock");
        require(panel.findChild<QLabel*>("solarTransitStatus")->text().startsWith("Cancelled"), "Stop reports cancellation");
        require(dates->rowCount() == dateCount, "Cancellation preserves previous complete snapshot");
        source.returnYear = 2027;
        source.returnChart.utcDateTime = source.returnChart.utcDateTime.addYears(1);
        panel.refreshSource();
        require(dates->rowCount() == 0, "Changed return invalidates previous dates");
        require(!panel.renderSelectedChart(&mainWheel, source.returnInput.aspectOrbs, 3.0), "Changed return invalidates previous overlay");
        require(run->isEnabled(), "Changed valid source permits new generation");
        std::cout << "PASS: async UI, results, selection chart, timeline, export, pending-source guard, Stop, source switching; " << dateCount << " dates\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
