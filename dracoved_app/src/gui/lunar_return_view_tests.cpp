// Standalone MainWindow check with isolated settings and real ephemeris charts.
#include "main_window.h"
#include "chart_wheel_widget.h"
#include <QApplication>
#include <QClipboard>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QRadioButton>
#include <QSettings>
#include <QTabBar>
#include <QTableWidget>
#include <QThread>
#include <iostream>
#include <stdexcept>

namespace dracoved {
struct LunarReturnViewChecks {
    static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
    static void settle() {
        for (int i = 0; i < 5; ++i) { QApplication::processEvents(); QThread::msleep(2); }
    }
    static void click(QWidget* target, const QPointF& position) {
        const QPointF global = target->mapToGlobal(position.toPoint());
        QMouseEvent down(QEvent::MouseButtonPress, position, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent up(QEvent::MouseButtonRelease, position, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(target, &down); QApplication::sendEvent(target, &up); settle();
    }
    static void run() {
        MainWindow w; w.resize(1800, 1050); w.show(); settle();
        auto go = [&](MainWindow::AppTab tab) {
            for (int i = 0; i < w.mainTabBar_->count(); ++i) if (w.mainTabBar_->tabData(i).toInt() == int(tab)) {
                w.mainTabBar_->setCurrentIndex(i); settle(); return;
            }
            throw std::runtime_error("Main tab is missing");
        };
        require(w.swe_.isLoaded(), "Ephemeris loaded");
        w.currentInput_.name = "Lunar overlay test"; w.currentInput_.date = QDate(1999, 1, 4);
        w.currentInput_.time = QTime(16, 1); w.currentInput_.timezone = "Asia/Dhaka";
        w.currentInput_.latitude = 23.764; w.currentInput_.longitude = 90.389;
        w.currentInput_.houseSystem = HouseSystem::Placidus; w.currentLocation_ = "Dhaka";
        QString error; TropicalComputeOptions options;
        options.includeFixedStars = false; options.includeArabicLots = false;
        require(w.engine_.compute(w.currentInput_, options, &w.currentChart_, &error), "Natal chart computes");
        w.hasCurrentChart_ = true; w.natalPlacidusCusps_ = w.currentChart_.cusps;
        const auto natalUtc = w.currentChart_.utcDateTime;
        const auto natalAsc = w.currentChart_.angles.asc;
        w.lunarUseCustomRadio_->setChecked(true); w.lunarLocationEdit_->setText("New York");
        w.lunarLatSpin_->setValue(40.713); w.lunarLonSpin_->setValue(-74.006);
        w.lunarTimezoneEdit_->setText("America/New_York");
        w.lunarAnchorDateEdit_->setDate(QDate(2026, 5, 1));
        if (!w.applyLunarReturnAnchor(1, true, &error)) throw std::runtime_error(error.toStdString());
        const auto lunarUtc = w.currentLunarChart_.utcDateTime;
        require(std::abs(w.currentLunarChart_.angles.asc - natalAsc) > 1.0, "Fixture has distinct natal and return angles");
        auto* wheel = w.chartWheel_;
        wheel->setShowLots(false); wheel->setShowFixedStars(false); wheel->setShowAspects(true);
        const bool transitNatal = w.overlayAspectsTransitNatal_;
        const bool transitTransit = w.overlayAspectsTransitTransit_;
        const bool natalNatal = w.overlayAspectsNatalNatal_;
        w.lunarAspectView_ = MainWindow::LunarAspectView::LunarReturn;
        go(MainWindow::AppTab::LunarReturn);
        require(wheel->mode() == ChartWheelWidget::Mode::NatalOnly && wheel->chart_.utcDateTime == lunarUtc,
            "Single-return mode retains the return chart");
        require(!wheel->aspectSelectionEnabled_, "Aspect pinning is opt-in");
        require(w.aspectScopeTabs_->count() == 2, "Existing lunar scope tabs available");
        w.aspectScopeTabs_->setCurrentIndex(1); settle();
        require(wheel->mode() == ChartWheelWidget::Mode::Overlay, "Lunar-Natal switches the actual wheel to two rings");
        require(wheel->chart_.utcDateTime == natalUtc && wheel->overlayChart_.utcDateTime == lunarUtc,
            "Natal inside, return outside");
        require(wheel->baseLabel_ == "Natal" && wheel->overlayLabel_ == "Lunar Return", "Both rings labelled correctly");
        require(wheel->houseSystem_ == w.currentInput_.houseSystem && wheel->chart_.angles.asc == natalAsc,
            "Inner houses/orientation stay natal at a custom return location");
        require(wheel->overlayAnglesVisible_ && wheel->overlayTransitNatalAspects_
            && !wheel->overlayTransitTransitAspects_ && !wheel->overlayNatalNatalAspects_,
            "Return angles visible with cross-chart aspects only");
        require(w.chartLegendLabel_->text().contains("Natal (inner) / Lunar Return (outer)"), "Legend explains ring roles");

        auto* grid = w.aspectsTable_;
        QTableWidgetItem* moonContact = nullptr;
        QTableWidgetItem* empty = nullptr;
        for (int row = 0; row < grid->rowCount(); ++row) for (int col = 0; col < grid->columnCount(); ++col) {
            auto* item = grid->item(row, col);
            if (item->data(AspectRoles::Kind).toInt() == AspectEmptyBox) empty = item;
            if (item->data(AspectRoles::Kind).toInt() == AspectFilled
                && item->data(AspectRoles::BodyA).toString() == "Moon" && item->data(AspectRoles::BodyB).toString() == "Moon") moonContact = item;
        }
        require(moonContact && empty, "Cross grid exposes contact identities and empty cells");
        auto clickCell = [&](QTableWidgetItem* item) {
            grid->scrollToItem(item); settle(); click(grid->viewport(), grid->visualItemRect(item).center());
        };
        clickCell(moonContact);
        require(wheel->hasAspectHighlight() && wheel->selectedAspectBodyA_ == "Lunar Return Moon"
            && wheel->selectedAspectBodyB_ == "Natal Moon", "Grid click distinguishes the two Moons");
        wheel->grab();
        int matchingLines = 0;
        for (const auto& line : wheel->aspectLines_) if (wheel->isAspectHighlighted(line)) ++matchingLines;
        require(matchingLines == 1, "Selected grid contact resolves exactly one rendered aspect");
        require(wheel->planetHitScopedNames_.contains("Lunar Return Moon") && wheel->planetHitScopedNames_.contains("Natal Moon"),
            "Both selected endpoints have separate drawn glyphs");
        require(wheel->planetHitScopedNames_.contains("Lunar Return Ascendant"), "Return Ascendant is drawn in the outer ring");
        require(wheel->grab().save(QCoreApplication::applicationDirPath() + "/lunar_natal.png"), "Save two-ring screenshot");
        clickCell(empty); require(!wheel->hasAspectHighlight(), "Empty grid cell clears the aspect highlight");

        bool lineClicked = false;
        for (int i = 0; i < wheel->aspectLines_.size() && !lineClicked; ++i) {
            const auto line = wheel->aspectLines_[i];
            const auto point = line.path.pointAtPercent(0.5);
            bool onBody = false;
            for (int n = 0; n < wheel->planetHitAreas_.size(); ++n)
                if (!wheel->planetHitNames_[n].isEmpty() && wheel->planetHitAreas_[n].contains(point)) onBody = true;
            if (onBody || wheel->hitTestAspect(point) != i) continue;
            click(wheel, point);
            require(wheel->hasAspectHighlight() && wheel->isAspectHighlighted(line), "Wheel aspect click pins both endpoints");
            lineClicked = true;
        }
        require(lineClicked, "A rendered aspect can be clicked");
        QApplication::setActiveWindow(&w); grid->setFocus(); settle();
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(grid, &escape); settle();
        require(!wheel->hasAspectHighlight(), "Escape clears selected aspect from the grid");
        clickCell(moonContact);
        w.refreshAspectsForHeaderMode(); settle();
        require(!wheel->hasAspectHighlight() && wheel->mode() == ChartWheelWidget::Mode::Overlay,
            "Refreshing aspect filters clears selection while keeping the overlay");
        w.handleCopyAspects();
        require(QApplication::clipboard()->text().contains("Lunar"), "Aspect copy retains Lunar Return context");

        wheel->setAspectHighlight("Lunar Return Moon", "Natal Moon");
        w.handleLunarNext(); settle();
        require(w.currentLunarChart_.utcDateTime > lunarUtc && wheel->overlayChart_.utcDateTime == w.currentLunarChart_.utcDateTime,
            "Next return updates the outer chart");
        require(wheel->chart_.utcDateTime == natalUtc && !wheel->hasAspectHighlight(), "Next return preserves natal and clears old selection");
        w.handleLunarPrev(); settle();
        require(std::abs(w.currentLunarChart_.utcDateTime.msecsTo(lunarUtc)) <= 1000, "Previous returns to the original lunar cycle");
        w.aspectScopeTabs_->setCurrentIndex(0); settle();
        require(wheel->mode() == ChartWheelWidget::Mode::NatalOnly && wheel->chart_.utcDateTime == w.currentLunarChart_.utcDateTime,
            "Single-return view restores the return chart");
        require(!wheel->hasAspectHighlight() && !wheel->aspectSelectionEnabled_, "Single view clears overlay selection");
        w.aspectScopeTabs_->setCurrentIndex(1); settle();
        go(MainWindow::AppTab::Natal);
        require(wheel->mode() == ChartWheelWidget::Mode::NatalOnly && wheel->chart_.utcDateTime == natalUtc
            && !wheel->aspectSelectionEnabled_, "Leaving Lunar Return restores the natal wheel");
        go(MainWindow::AppTab::Transits);
        require(w.overlayAspectsTransitNatal_ == transitNatal && w.overlayAspectsTransitTransit_ == transitTransit
            && w.overlayAspectsNatalNatal_ == natalNatal, "Ordinary transit aspect preferences remain unchanged");
        go(MainWindow::AppTab::LunarReturn);
        require(wheel->mode() == ChartWheelWidget::Mode::Overlay && wheel->baseLabel_ == "Natal"
            && wheel->overlayLabel_ == "Lunar Return", "Returning restores the chosen two-ring view");
        require(w.currentChart_.utcDateTime == natalUtc && w.currentChart_.angles.asc == natalAsc, "Natal source was not mutated");
        std::cout << "PASS: Lunar-Natal main wheel, ring identities/houses/angles, grid and wheel selection, Escape, filters/copy, return navigation, single view, tab restoration\n";
    }
};
} // namespace dracoved

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("DracoVedLunarViewChecks");
    QCoreApplication::setApplicationName("IsolatedChecks");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::applicationDirPath() + "/settings");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    app.setFont(QFont("Segoe UI", 9));
    try { dracoved::LunarReturnViewChecks::run(); }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
