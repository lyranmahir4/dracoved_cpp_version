// Standalone integration check for the Solar Return Technique view: day cards,
// restyled details and the directed-point marker on the real MainWindow wheel.
// Run with solar_technique_view_check.ps1 (never builds/packages dracoved_app.exe).
#include "main_window.h"
#include "chart_wheel_widget.h"
#include "solar_technique_table_style.h"
#include <QApplication>
#include <QDateEdit>
#include <QDockWidget>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLineEdit>
#include <QRadioButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QThread>
#include <iostream>
#include <stdexcept>

namespace dracoved {
struct SolarTechniqueViewChecks {
    static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
    static void run(const QString& outDir) {
        MainWindow w; w.resize(1900, 1050); w.show(); QApplication::processEvents();
        auto settle = [] { for (int i = 0; i < 5; ++i) { QApplication::processEvents(); QThread::msleep(2); } };
        auto go = [&](MainWindow::AppTab tab) {
            for (int i = 0; i < w.mainTabBar_->count(); ++i) if (w.mainTabBar_->tabData(i).toInt() == int(tab)) {
                w.mainTabBar_->setCurrentIndex(i); settle(); return;
            }
            throw std::runtime_error("Missing main tab");
        };
        require(w.swe_.isLoaded(), "Ephemeris loaded");
        w.currentInput_.name = "Technique test"; w.currentInput_.date = QDate(1999, 1, 4); w.currentInput_.time = QTime(16, 1);
        w.currentInput_.timezone = "Asia/Dhaka"; w.currentInput_.latitude = 23.764; w.currentInput_.longitude = 90.389;
        w.currentInput_.houseSystem = HouseSystem::Placidus; w.currentLocation_ = "Dhaka";
        QString error; TropicalComputeOptions options;
        options.includeFixedStars = false; options.includeArabicLots = false;
        require(w.engine_.compute(w.currentInput_, options, &w.currentChart_, &error), "Natal chart computes");
        w.hasCurrentChart_ = true; w.natalPlacidusCusps_ = w.currentChart_.cusps;
        w.solarUseNatalRadio_->setChecked(true); w.solarTimezoneEdit_->setText("Asia/Dhaka");
        w.solarYearSpin_->setValue(2026);
        if (!w.applySolarReturnYear(2026, &error)) throw std::runtime_error(error.toStdString());
        go(MainWindow::AppTab::SolarReturn);
        w.tabs_->setCurrentWidget(w.solarTechniquePanel_); settle();
        require(w.isSolarTechniqueTabActive(), "Technique sub-tab active");

        auto* hits = w.rightTopTable_;
        auto* details = w.rightBottomTable_;
        require(hits->property("solarTechniqueLook").toString() == "cards", "Hits table uses day cards");
        require(details->property("solarTechniqueLook").toString() == "details", "Details use technique look");
        require(!hits->horizontalHeader()->isVisible(), "Card header hidden");
        int monthRows = 0, dayRows = 0, quiet = 0, withHits = 0;
        for (int row = 0; row < hits->rowCount(); ++row) {
            const QString kind = hits->item(row, 0)->data(SolarTechniqueRoles::Kind).toString();
            if (kind == "month") ++monthRows;
            if (kind == "day") {
                ++dayRows;
                const int count = hits->item(row, 0)->data(SolarTechniqueRoles::Hits).toList().size();
                const int plus = hits->item(row, 2)->text().toInt(), minus = hits->item(row, 3)->text().toInt(), zero = hits->item(row, 4)->text().toInt();
                require(count == plus + minus + zero, "Chips match support/challenge/neutral counts");
                require(hits->item(row, 0)->data(SolarTechniqueRoles::Net).toInt() == plus - minus, "Net badge equals support minus challenge");
                (count ? withHits : quiet)++;
            }
        }
        require(monthRows >= 12 && dayRows >= 365, "Months and days present");
        std::cout << "cards: " << monthRows << " months, " << dayRows << " days (" << withHits << " with hits, " << quiet << " quiet)\n";

        // Pick the busiest day so the wheel shows several lines.
        QDate busiest; int most = -1;
        for (int row = 0; row < hits->rowCount(); ++row) {
            const auto* item = hits->item(row, 0);
            const int count = item->data(SolarTechniqueRoles::Hits).toList().size();
            if (item->data(SolarTechniqueRoles::Kind).toString() == "day" && count > most) { most = count; busiest = item->data(Qt::UserRole).toDate(); }
        }
        w.solarTechniqueDateEdit_->setDate(busiest); settle();
        require(w.chartWheel_->hasDirectedPoint(), "Selected day draws the directed point");
        require(w.solarTechniqueMarkerHits_.size() == most, "Wheel marker carries every hit of the day");
        require(hits->currentRow() >= 0 && hits->item(hits->currentRow(), 0)->data(Qt::UserRole).toDate() == busiest, "Selected card follows the date");
        require(details->item(0, 0)->data(SolarTechniqueRoles::Kind).toString() == "header" && details->columnSpan(0, 0) == 2, "Details header card spans");
        // Clicking a card selects that date.
        int target = -1;
        for (int row = hits->rowCount() - 1; row >= 0; --row)
            if (!hits->isRowHidden(row) && hits->item(row, 0)->data(SolarTechniqueRoles::Kind).toString() == "day"
                && hits->item(row, 0)->data(Qt::UserRole).toDate() != busiest) { target = row; break; }
        const QDate clickedDate = hits->item(target, 0)->data(Qt::UserRole).toDate();
        hits->setCurrentCell(target, 0); settle(); QThread::msleep(20); settle();
        require(w.solarTechniqueDateEdit_->date() == clickedDate, "Card click drives technique date");
        w.solarTechniqueDateEdit_->setDate(busiest); settle();

        auto save = [&](QWidget* widget, const QString& name) {
            require(widget->grab().save(outDir + "/" + name), "Save screenshot");
        };
        save(&w, "technique_window_light.png");
        save(w.chartWheel_, "technique_wheel_light.png");
        save(w.rightTopDock_, "technique_hits_light.png");
        save(w.rightBottomDock_, "technique_details_light.png");

        // Refreshing the SR wheel (theme, orb, recalculation) keeps the marker.
        w.refreshSolarReturnView(); settle();
        require(w.chartWheel_->hasDirectedPoint(), "Marker survives SR wheel rebuild");

        // Leaving the sub-tab restores the plain shared docks and clears the wheel.
        w.tabs_->setCurrentIndex(0); settle();
        require(!w.chartWheel_->hasDirectedPoint(), "Other SR sub-tabs have no marker");
        require(hits->property("solarTechniqueLook").toString().isEmpty() && hits->horizontalHeader()->isVisible(), "Shared hits dock restored");
        require(details->property("solarTechniqueLook").toString().isEmpty() && details->columnSpan(0, 0) == 1, "Shared details dock restored");
        bool anyHidden = false;
        for (int col = 0; col < hits->columnCount(); ++col) anyHidden |= hits->isColumnHidden(col);
        require(!anyHidden, "No hidden columns leak to other views");
        w.tabs_->setCurrentWidget(w.solarTechniquePanel_); settle();
        require(w.chartWheel_->hasDirectedPoint(), "Returning to Technique restores the marker");
        go(MainWindow::AppTab::Natal);
        require(!w.chartWheel_->hasDirectedPoint(), "Natal wheel has no technique marker");
        go(MainWindow::AppTab::SolarReturn);
        w.tabs_->setCurrentWidget(w.solarTechniquePanel_); settle();

        for (auto theme : {MainWindow::ThemeMode::Dark, MainWindow::ThemeMode::Creme}) {
            w.applyTheme(theme); settle();
            w.refreshSolarTechniqueView(); settle();
            const QString tag = theme == MainWindow::ThemeMode::Dark ? "dark" : "creme";
            save(&w, "technique_window_" + tag + ".png");
            save(w.chartWheel_, "technique_wheel_" + tag + ".png");
            save(w.rightTopDock_, "technique_hits_" + tag + ".png");
            save(w.rightBottomDock_, "technique_details_" + tag + ".png");
        }
        std::cout << "PASS: technique cards, details, wheel marker, click-to-select, sub-tab/tab restoration, themes\n";
    }
};
} // namespace dracoved

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("DracoVedTechniqueChecks");
    QCoreApplication::setApplicationName("IsolatedChecks");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::applicationDirPath() + "/settings");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/seguisym.ttf");
    app.setFont(QFont("Segoe UI", 9));
    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QCoreApplication::applicationDirPath();
    try { dracoved::SolarTechniqueViewChecks::run(outDir); }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
    return 0;
}
