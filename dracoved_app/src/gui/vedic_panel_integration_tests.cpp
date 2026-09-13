// Focused offscreen integration check for the real VedicPanel and MainWindow
// routing. It intentionally lives outside the production CMake target.
#include "main_window.h"
#include "vedic_panel.h"

#include "../core/chart_types.h"
#include "../core/tropical_natal.h"

#include <QApplication>
#include <QComboBox>
#include <QClipboard>
#include <QDate>
#include <QDateEdit>
#include <QElapsedTimer>
#include <QTimer>
#include <QDockWidget>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QSettings>
#include <QTabBar>
#include <QTableWidget>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace dracoved {
struct VedicPanelIntegrationChecks {
    static void require(bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    static void settle() {
        for (int i = 0; i < 5; ++i) {
            QApplication::processEvents();
            QThread::msleep(2);
        }
    }

    static NatalInput fixture() {
        NatalInput input;
        input.name = "Vedic integration fixture";
        input.date = QDate(1999, 1, 4);
        input.time = QTime(16, 1);
        input.timezone = "Asia/Dhaka";
        input.latitude = 23.764;
        input.longitude = 90.389;
        input.zodiacSystem = ZodiacSystem::Tropical;
        input.siderealAyanamsa = SiderealAyanamsa::Lahiri;
        input.lunarNodePolicy.mode = LunarNodeMode::MeanOnly;
        input.lunarNodePolicy.primary = LunarNodeType::Mean;
        input.useDefaultLunarNodePolicy = false;
        input.houseSystem = HouseSystem::Placidus;
        return input;
    }

    static int tabIndex(MainWindow& window, MainWindow::AppTab tab) {
        for (int i = 0; i < window.mainTabBar_->count(); ++i) {
            if (window.mainTabBar_->tabData(i).toInt() == static_cast<int>(tab)) {
                return i;
            }
        }
        return -1;
    }

    static void go(MainWindow& window, MainWindow::AppTab tab) {
        const int index = tabIndex(window, tab);
        require(index >= 0, "Requested main tab is missing");
        window.mainTabBar_->setCurrentIndex(index);
        settle();
    }

    static int rowFor(const QTableWidget* table, const QString& body) {
        for (int row = 0; row < table->rowCount(); ++row) {
            if (table->item(row, 0) && table->item(row, 0)->text() == body) {
                return row;
            }
        }
        return -1;
    }

    static QComboBox* ayanamsaCombo(VedicPanel& panel) {
        auto* combo = panel.findChild<QComboBox*>("vedicAyanamsa");
        require(combo != nullptr, "Vedic ayanamsa selector exists");
        return combo;
    }

    static QPushButton* copyButton(VedicPanel& panel) {
        for (auto* button : panel.findChildren<QPushButton*>()) {
            if (button->text() == "Copy table") {
                return button;
            }
        }
        return nullptr;
    }

    static void assertAscendingText(const QTableWidget* table, int column) {
        for (int row = 1; row < table->rowCount(); ++row) {
            require(QString::localeAwareCompare(table->item(row - 1, column)->text(),
                                                table->item(row, column)->text()) <= 0,
                    "Text column is not alphabetically sorted");
        }
    }

    static void assertAscendingNumberRole(const QTableWidget* table, int column) {
        for (int row = 1; row < table->rowCount(); ++row) {
            bool leftOk = false;
            bool rightOk = false;
            const double left = table->item(row - 1, column)->data(Qt::UserRole).toDouble(&leftOk);
            const double right = table->item(row, column)->data(Qt::UserRole).toDouble(&rightOk);
            require(leftOk && rightOk && left <= right, "Numeric column is not numerically sorted");
        }
    }

    static void assertCanonicalOrder(const QTableWidget* table, int column,
                                     const QStringList& expectedOrder) {
        QHash<QString, int> orderIndex;
        for (int i = 0; i < expectedOrder.size(); ++i) {
            orderIndex.insert(expectedOrder.at(i), i);
        }
        int previous = -1;
        for (int row = 0; row < table->rowCount(); ++row) {
            const int current = orderIndex.value(table->item(row, column)->text(), -1);
            require(current >= 0 && current >= previous,
                    "Canonical table order mismatch");
            previous = current;
        }
    }

    static void assertReferenceTable(VedicPanel& panel) {
        QTableWidget* table = panel.table();
        require(table->rowCount() == 10, "Mean-node Vedic table has ten rows");
        require(table->columnCount() == 6, "Vedic table has six columns");
        const QStringList headers = {"Body", "Sign", "Degree in sign", "Nakshatra", "Pada", "Nakshatra lord"};
        for (int column = 0; column < headers.size(); ++column) {
            require(table->horizontalHeaderItem(column)->text() == headers.at(column),
                    "Vedic table header mismatch");
        }
        require(table->item(0, 0)->text() == "Ascendant", "Default Vedic order starts with Ascendant");
        require(table->editTriggers() == QAbstractItemView::NoEditTriggers,
                "Vedic cells are read-only");

        struct Expected {
            const char* body;
            const char* sign;
            const char* nakshatra;
            int pada;
            const char* lord;
        };
        const Expected expected[] = {
            {"Ascendant", "Gemini", "Mrigashira", 3, "Mars"},
            {"Sun", "Sagittarius", "Purva Ashadha", 2, "Venus"},
            {"Moon", "Cancer", "Ashlesha", 1, "Mercury"},
            {"Mercury", "Sagittarius", "Mula", 1, "Ketu"},
            {"Venus", "Capricorn", "Uttara Ashadha", 3, "Sun"},
            {"Mars", "Virgo", "Chitra", 1, "Mars"},
            {"Jupiter", "Aquarius", "Purva Bhadrapada", 3, "Jupiter"},
            {"Saturn", "Aries", "Ashwini", 1, "Ketu"},
            {"Rahu", "Leo", "Magha", 1, "Ketu"},
            {"Ketu", "Aquarius", "Dhanishtha", 3, "Mars"},
        };
        for (const Expected& item : expected) {
            const int row = rowFor(table, item.body);
            require(row >= 0, "Reference body is missing");
            require(table->item(row, 1)->text() == item.sign
                    && table->item(row, 3)->text() == item.nakshatra
                    && table->item(row, 4)->text() == QString::number(item.pada)
                    && table->item(row, 5)->text() == item.lord,
                    "Reference Vedic placement mismatch");
            require(table->item(row, 2)->text() != "N/A", "Reference degree is available");
            for (int column = 0; column < table->columnCount(); ++column) {
                require(!(table->item(row, column)->flags() & Qt::ItemIsEditable),
                        "Vedic table item is editable");
            }
        }
    }

    static void assertNodeMode(MainWindow& window, VedicPanel& panel,
                               LunarNodeMode mode, LunarNodeType primary,
                               int expectedRows, const QStringList& nodeLabels) {
        NatalInput input = fixture();
        input.lunarNodePolicy.mode = mode;
        input.lunarNodePolicy.primary = primary;
        NatalChart chart;
        QString error;
        TropicalComputeOptions options;
        options.includeArabicLots = false;
        options.includeFixedStars = false;
        options.includeAspectGrid = false;
        require(window.engine_.compute(input, options, &chart, &error), "Node-mode fixture computes");
        panel.setNatalContext(input, chart, "Dhaka");
        require(panel.table()->rowCount() == expectedRows, "Node-mode row count mismatch");
        for (const QString& label : nodeLabels) {
            require(rowFor(panel.table(), label) >= 0, "Node-mode label is missing");
        }
    }

    static void run() {
        MainWindow window;
        window.resize(1800, 1050);

        const bool originalData = !window.dataDock_->isHidden();
        const bool originalRightTop = !window.rightTopDock_->isHidden();
        const bool originalAspects = !window.transitAspectsDock_->isHidden();
        const bool originalRightBottom = !window.rightBottomDock_->isHidden();
        require(tabIndex(window, MainWindow::AppTab::Vedic) >= 0, "Vedic main tab is wired");

        // Enter before show so dock preservation is explicitly based on
        // isHidden(), not isVisible() inherited from the still-hidden window.
        go(window, MainWindow::AppTab::Vedic);
        require(window.vedicWorkspaceActive_ && window.zodiacToolbarRow_->isHidden(),
                "Vedic workspace activates and hides global zodiac controls");
        require(window.vedicDataDockWasVisible_ == originalData
                && window.vedicRightTopDockWasVisible_ == originalRightTop
                && window.vedicTransitAspectsDockWasVisible_ == originalAspects
                && window.vedicRightBottomDockWasVisible_ == originalRightBottom,
                "Vedic captures dock visibility before MainWindow is shown");
        require(window.dataDock_->isHidden() && window.rightTopDock_->isHidden()
                && window.transitAspectsDock_->isHidden() && window.rightBottomDock_->isHidden(),
                "Vedic hides unrelated docks");

        const NatalInput input = fixture();
        require(window.computeChart(input, "Dhaka"), "Reference natal chart computes");
        const ZodiacSystem originalZodiac = window.currentInput_.zodiacSystem;
        const SiderealAyanamsa originalAyanamsa = window.currentInput_.siderealAyanamsa;
        const double originalSun = window.currentChart_.bodies.at(0).longitude;
        settle();
        window.show();
        settle();
        require(window.centerStack_->currentWidget() == window.vedicPanel_,
                "Vedic tab selects the Vedic panel in the center stack");
        require(window.vedicPanel_->table()->rowCount() == 10, "Vedic panel is populated through MainWindow");
        assertReferenceTable(*window.vedicPanel_);

        auto* moorthi = window.vedicPanel_->findChild<QWidget*>("moorthiPanel");
        require(moorthi != nullptr, "Moorthi calculator is integrated");
        moorthi->findChild<QDateEdit*>("moorthiFrom")->setDate(QDate(2026, 1, 1));
        moorthi->findChild<QDateEdit*>("moorthiThrough")->setDate(QDate(2026, 12, 31));
        auto* moorthiPlanet = moorthi->findChild<QComboBox*>("moorthiPlanet");
        moorthiPlanet->setCurrentIndex(moorthiPlanet->findText("Sun"));
        moorthi->findChild<QPushButton*>("moorthiRun")->click();
        auto* searchTimer = moorthi->findChild<QTimer*>();
        QElapsedTimer searchWait; searchWait.start();
        while (searchTimer->isActive() && searchWait.elapsed() < 60000) QApplication::processEvents();
        require(!searchTimer->isActive() && moorthi->findChild<QTableWidget*>("moorthiTable")->rowCount() == 12,
                "Moorthi search completes inside main Vedic workspace");

        const QString screenshot = QCoreApplication::applicationDirPath() + "/vedic_d1.png";
        require(window.grab().save(screenshot), "Vedic integration screenshot saves");
        window.resize(1200, 850); settle();
        require(window.grab().save(QCoreApplication::applicationDirPath() + "/vedic_compact.png"), "Compact workspace screenshot");
        window.resize(1800, 1050); settle();

        QTableWidget* table = window.vedicPanel_->table();
        table->sortItems(1, Qt::AscendingOrder);
        assertAscendingNumberRole(table, 1);
        assertCanonicalOrder(table, 1, {"Aries", "Taurus", "Gemini", "Cancer", "Leo", "Virgo",
                                        "Libra", "Scorpio", "Sagittarius", "Capricorn", "Aquarius", "Pisces"});
        table->sortItems(3, Qt::AscendingOrder);
        assertAscendingNumberRole(table, 3);
        assertCanonicalOrder(table, 3, {"Ashwini", "Bharani", "Krittika", "Rohini", "Mrigashira",
                                        "Ardra", "Punarvasu", "Pushya", "Ashlesha", "Magha",
                                        "Purva Phalguni", "Uttara Phalguni", "Hasta", "Chitra", "Swati",
                                        "Vishakha", "Anuradha", "Jyeshtha", "Mula", "Purva Ashadha",
                                        "Uttara Ashadha", "Shravana", "Dhanishtha", "Shatabhisha",
                                        "Purva Bhadrapada", "Uttara Bhadrapada", "Revati"});
        table->sortItems(5, Qt::AscendingOrder);
        assertAscendingText(table, 5);
        table->sortItems(2, Qt::AscendingOrder);
        assertAscendingNumberRole(table, 2);
        table->sortItems(4, Qt::AscendingOrder);
        assertAscendingNumberRole(table, 4);

        table->sortItems(2, Qt::DescendingOrder);
        const int selectedBefore = rowFor(table, "Jupiter");
        require(selectedBefore >= 0, "Jupiter is selectable");
        table->selectRow(selectedBefore);
        QComboBox* combo = ayanamsaCombo(*window.vedicPanel_);
        combo->setCurrentIndex(combo->findData(static_cast<int>(SiderealAyanamsa::Raman)));
        settle();
        require(table->horizontalHeader()->sortIndicatorSection() == 2
                && table->horizontalHeader()->sortIndicatorOrder() == Qt::DescendingOrder,
                "Ayanamsa refresh preserves active sort");
        require(!table->selectionModel()->selectedRows().isEmpty()
                && table->item(table->selectionModel()->selectedRows().first().row(), 0)->text() == "Jupiter",
                "Ayanamsa refresh preserves selected body");
        require(QSettings().value("vedic/ayanamsa").toInt() == static_cast<int>(SiderealAyanamsa::Raman),
                "Vedic ayanamsa persists independently");
        require(window.currentInput_.zodiacSystem == originalZodiac
                && window.currentInput_.siderealAyanamsa == originalAyanamsa
                && std::abs(window.currentChart_.bodies.at(0).longitude - originalSun) < 1e-9,
                "Vedic calculation leaves global tropical natal state unchanged");

        const QString copiedBody = table->item(0, 0)->text();
        QPushButton* copy = copyButton(*window.vedicPanel_);
        require(copy != nullptr, "Vedic copy button is present");
        copy->click();
        const QStringList copiedLines = QApplication::clipboard()->text().split('\n');
        const int copiedHeader = copiedLines.indexOf("Body\tSign\tDegree in sign\tNakshatra\tPada\tNakshatra lord");
        require(copiedHeader >= 0 && copiedHeader + 1 < copiedLines.size()
                && copiedLines.at(copiedHeader + 1).section('\t', 0, 0) == copiedBody,
                "Copy follows displayed table order and includes context");

        VedicPanel persistedPanel(&window.engine_, &window.swe_);
        persistedPanel.setNatalContext(input, window.currentChart_, "Dhaka");
        require(persistedPanel.ayanamsa() == SiderealAyanamsa::Raman,
                "A fresh Vedic panel restores its independent ayanamsa");
        assertNodeMode(window, persistedPanel, LunarNodeMode::TrueOnly, LunarNodeType::True,
                       10, {"Rahu", "Ketu"});
        assertNodeMode(window, persistedPanel, LunarNodeMode::Both, LunarNodeType::Mean,
                       12, {"Rahu (Mean)", "Rahu (True)", "Ketu (Mean)", "Ketu (True)"});

        NatalInput refreshedInput = fixture();
        refreshedInput.name = "Refreshed Vedic input";
        refreshedInput.date = refreshedInput.date.addDays(1);
        NatalChart refreshedChart;
        QString refreshError;
        TropicalComputeOptions refreshOptions;
        refreshOptions.includeArabicLots = false;
        refreshOptions.includeFixedStars = false;
        refreshOptions.includeAspectGrid = false;
        require(window.engine_.compute(refreshedInput, refreshOptions, &refreshedChart, &refreshError),
                "Refreshed input computes");
        persistedPanel.setNatalContext(refreshedInput, refreshedChart, "Dhaka");
        require(persistedPanel.table()->rowCount() == 10, "Input refresh repopulates Vedic table");
        bool refreshedContext = false;
        for (auto* label : persistedPanel.findChildren<QLabel*>()) {
            refreshedContext = refreshedContext || label->text().contains("Refreshed Vedic input");
        }
        require(refreshedContext, "Input refresh updates Vedic context");

        go(window, MainWindow::AppTab::Natal);
        require(window.centerStack_->currentWidget() == window.chartViewPanel_
                && !window.zodiacToolbarRow_->isHidden()
                && ((!window.dataDock_->isHidden()) == originalData),
                "Returning to Natal restores the natal workspace");
        go(window, MainWindow::AppTab::Transits);
        require(window.dataStack_->currentIndex() == 1, "Returning to Transits restores transit setup");
        go(window, MainWindow::AppTab::SolarReturn);
        require(window.centerStack_->currentWidget() == window.chartViewPanel_,
                "Returning to Solar Return restores the shared chart workspace");

        go(window, MainWindow::AppTab::Vedic);
        const QString profileName = "vedic_panel_integration_profile";
        window.currentProfileName_.clear();
        require(window.saveProfileByName(profileName, false), "Profile saves while Vedic is active");
        window.currentInput_.name = "Mutated after save";
        require(window.loadProfileByName(profileName), "Profile reopens while Vedic is active");
        require(window.currentInput_.name == input.name && window.vedicPanel_->table()->rowCount() == 10,
                "Profile reopen restores natal input and Vedic table");
        window.saveUiState();
        QSettings().sync();

        MainWindow reopened;
        reopened.resize(1800, 1050);
        settle();
        require(tabIndex(reopened, MainWindow::AppTab::Vedic) == reopened.mainTabBar_->currentIndex()
                && reopened.vedicWorkspaceActive_,
                "Reopen restores Vedic as the active tab");
        require(reopened.dataDock_->isHidden() && reopened.rightTopDock_->isHidden()
                && reopened.transitAspectsDock_->isHidden() && reopened.rightBottomDock_->isHidden(),
                "Reopen keeps unrelated docks hidden while Vedic is active");
        require(reopened.loadProfileByName(profileName), "Reopened window loads saved profile");
        reopened.resetDockLayout();
        require(reopened.dataDock_->isHidden() && reopened.rightTopDock_->isHidden()
                && reopened.transitAspectsDock_->isHidden() && reopened.rightBottomDock_->isHidden(),
                "Reset layout keeps Vedic workspace isolated");
        go(reopened, MainWindow::AppTab::Natal);
        require((!reopened.dataDock_->isHidden()) == originalData
                && (!reopened.rightTopDock_->isHidden()) == originalRightTop
                && (!reopened.transitAspectsDock_->isHidden()) == originalAspects
                && (!reopened.rightBottomDock_->isHidden()) == originalRightBottom,
                "Reset layout restores original dock visibility after Vedic");

        QFile::remove(window.profileFilePath(profileName));
        std::cout << "PASS: Vedic D1 reference table, sorting, copy, node modes, persistence, refresh, tabs, docks, profile reopen\n"
                  << "SCREENSHOT: " << screenshot.toStdString() << '\n';
    }
};
}  // namespace dracoved

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("DracoVedVedicPanelChecks");
    QCoreApplication::setApplicationName("IsolatedChecks");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QCoreApplication::applicationDirPath() + "/settings");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    app.setFont(QFont("Segoe UI", 9));
    QSettings settings;
    settings.clear();
    settings.sync();
    try {
        dracoved::VedicPanelIntegrationChecks::run();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
