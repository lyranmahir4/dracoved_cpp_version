// Standalone integration check with isolated settings and synthetic chart data.
#include "main_window.h"
#include "solar_transit_panel.h"
#include "chart_wheel_widget.h"
#include <QApplication>
#include <QDateEdit>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QFrame>
#include <QLineEdit>
#include <QRadioButton>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabBar>
#include <QTableWidget>
#include <QTabWidget>
#include <QThread>
#include <QToolButton>
#include <QTreeWidget>
#include <iostream>
#include <stdexcept>

namespace dracoved {
struct SolarTransitWorkspaceChecks {
    static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
    static void run() {
        MainWindow w; w.resize(1800, 1050); w.show(); QApplication::processEvents();
        auto go = [&](MainWindow::AppTab tab) {
            for (int i = 0; i < w.mainTabBar_->count(); ++i) if (w.mainTabBar_->tabData(i).toInt() == int(tab)) {
                w.mainTabBar_->setCurrentIndex(i);
                for (int settle = 0; settle < 5; ++settle) { QApplication::processEvents(); QThread::msleep(2); }
                return;
            }
            throw std::runtime_error("Missing main tab");
        };
        require(w.swe_.isLoaded(), "Ephemeris loaded");
        w.currentInput_.name = "Workspace test"; w.currentInput_.date = QDate(1999,1,4); w.currentInput_.time = QTime(16,1);
        w.currentInput_.timezone = "Asia/Dhaka"; w.currentInput_.latitude = 23.764; w.currentInput_.longitude = 90.389;
        w.currentInput_.houseSystem = HouseSystem::Placidus; w.currentLocation_ = "Dhaka";
        QString error; TropicalComputeOptions options;
        options.includeFixedStars = false; options.includeArabicLots = false;
        require(w.engine_.compute(w.currentInput_, options, &w.currentChart_, &error), "Synthetic natal chart computes");
        w.hasCurrentChart_ = true; w.natalPlacidusCusps_ = w.currentChart_.cusps;
        w.solarUseNatalRadio_->setChecked(true); w.solarTimezoneEdit_->setText("Asia/Dhaka");
        w.solarYearSpin_->setValue(2026);
        if (!w.applySolarReturnYear(2026, &error)) throw std::runtime_error(error.toStdString());
        go(MainWindow::AppTab::SolarReturn);
        require(w.tabs_->indexOf(w.solarTransitPanel_) == -1, "Old nested tab removed");
        const int oldWidth = w.dataDock_->width();
        std::cout << "Initial dock width=" << oldWidth << " minimum=" << w.dataDock_->minimumWidth() << '\n';
        const bool priorMatrix = w.aspectsPanel_->isVisible();
        go(MainWindow::AppTab::SolarTransits);
        require(w.dataStack_->currentWidget() == w.solarTransitPanel_, "Dedicated full-height results workspace");
        require(!w.solarControls_->isVisible(), "Return setup no longer consumes result height");
        require(!w.aspectsPanel_->isVisible() && !w.solarTransitGridVisible_, "Matrix hidden by default");
        require(w.transitAspectGridToggleButton_->isVisible(), "Matrix can be explicitly enabled");
        auto* expand = w.solarTransitPanel_->findChild<QPushButton*>("solarTransitExpandResults");
        const int baseWidth = w.dataDock_->width(); expand->click(); QApplication::processEvents();
        require(w.dataDock_->width() > baseWidth, "Expand gives results more width"); expand->click(); QApplication::processEvents();
        const bool ordinaryTransitGrid = w.transitAspectGridVisible_;
        w.transitAspectGridToggleButton_->setChecked(true);
        require(w.aspectsPanel_->isVisible() && w.transitAspectGridVisible_ == ordinaryTransitGrid, "Matrix preference isolated from ordinary Transits");
        w.transitAspectGridToggleButton_->setChecked(false);
        auto* bodies = w.solarTransitPanel_->findChild<QTreeWidget*>("solarTransitBodies");
        auto* targets = w.solarTransitPanel_->findChild<QTreeWidget*>("solarTransitTargets");
        for (auto* tree : {bodies, targets}) for (int i = 0; i < tree->topLevelItemCount(); ++i)
            for (int j = 0; j < tree->topLevelItem(i)->childCount(); ++j) {
                auto* item = tree->topLevelItem(i)->child(j); const auto name = item->data(0, Qt::UserRole).toString();
                item->setCheckState(0, (tree == bodies ? name == "Mercury" : name == "Venus") ? Qt::Checked : Qt::Unchecked);
            }
        QPushButton* start = nullptr; QPushButton* stop = nullptr;
        for (auto* button : w.solarTransitPanel_->findChildren<QPushButton*>()) {
            if (button->text() == "Generate return year") start = button;
            if (button->text() == "Stop") stop = button;
        }
        require(start && stop, "Generation actions available"); start->click();
        QElapsedTimer timer; timer.start();
        while (stop->isEnabled() && timer.elapsed() < 120000) { QApplication::processEvents(); QThread::msleep(5); }
        require(!stop->isEnabled(), "Year generation finishes");
        auto* dates = w.solarTransitPanel_->findChild<QTableWidget*>("solarTransitDates");
        require(dates->rowCount() > 0 && w.solarTransitOverlayActive_, "Selection drives real main wheel");
        require(dates->height() > 350, "Results have usable height inside main window");
        const auto selectedMoment = w.solarTransitPanel_->selectedTransitChart()->utcDateTime;
        w.transitAspectGridToggleButton_->setChecked(true);
        require(w.solarTransitOverlayActive_ && w.aspectsTable_->rowCount() > 0, "Optional matrix retains selected overlay");
        w.transitAspectGridToggleButton_->setChecked(false);
        require(w.grab().save(QCoreApplication::applicationDirPath() + "/workspace.png"), "Save integrated workspace");
        go(MainWindow::AppTab::SolarReturn);
        require(!w.solarTransitOverlayActive_ && w.aspectsPanel_->isVisible() == priorMatrix, "Solar view and matrix restore");
        if (std::abs(w.dataDock_->width() - oldWidth) >= 25) {
            std::cerr << "Dock widths: original=" << oldWidth << " restored=" << w.dataDock_->width()
                      << " saved=" << w.solarTransitPreviousDockWidth_ << " min=" << w.dataDock_->minimumWidth()
                      << " hint=" << w.dataDock_->minimumSizeHint().width() << " stack=" << w.dataStack_->minimumSizeHint().width() << '\n';
            w.resizeDocks({w.dataDock_}, {oldWidth}, Qt::Horizontal); QApplication::processEvents();
            std::cerr << "After settled resize=" << w.dataDock_->width() << '\n';
            for (int page = 0; page < w.dataStack_->count(); ++page) {
                auto* widget = w.dataStack_->widget(page);
                std::cerr << "Page " << page << " min=" << widget->minimumWidth() << " hint=" << widget->minimumSizeHint().width()
                          << " policy=" << widget->sizePolicy().horizontalPolicy() << " visible=" << widget->isVisible() << '\n';
            }
            throw std::runtime_error("Previous dock width restores");
        }
        go(MainWindow::AppTab::SolarTransits);
        require(w.solarTransitOverlayActive_ && w.solarTransitPanel_->selectedTransitChart()->utcDateTime == selectedMoment, "Returning restores selected moment");
        go(MainWindow::AppTab::Natal); require(!w.solarTransitOverlayActive_, "Natal clears return overlay");
        go(MainWindow::AppTab::Transits);
        require(w.dataStack_->currentIndex() == 1 && w.transitAspectGridVisible_ == ordinaryTransitGrid, "Ordinary Transit setup index and matrix preference preserved");
        go(MainWindow::AppTab::SolarTransits);
        w.markSolarPending(); require(!w.solarTransitOverlayActive_, "Pending settings clear main overlay");
        require(w.applySolarReturnYear(2027, &error), "Changing return year computes");
        require(dates->rowCount() == 0 && !w.solarTransitOverlayActive_, "Changed return clears dates and overlay");
        go(MainWindow::AppTab::ReturnFinder); require(!w.solarTransitOverlayActive_, "Return Finder stays separate");
        std::cout << "PASS: actual MainWindow workspace, resize, matrix isolation, generation/selection, tab restoration, pending/year invalidation\n";
    }
};
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("DracoVedWorkspaceChecks");
    QCoreApplication::setApplicationName("IsolatedChecks");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::applicationDirPath() + "/settings");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    app.setFont(QFont("Segoe UI", 9));
    try { dracoved::SolarTransitWorkspaceChecks::run(); }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
