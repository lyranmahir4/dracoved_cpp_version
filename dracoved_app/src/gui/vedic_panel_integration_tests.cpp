// Focused offscreen integration check for the real VedicPanel and MainWindow
// routing. It intentionally lives outside the production CMake target.
#include "main_window.h"
#include "vedic_panel.h"
#include "dasha_panel.h"
#include "moorthi_graph_panel.h"
#include "south_indian_chart.h"

#include "../core/chart_types.h"
#include "../core/tropical_natal.h"

#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
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
#include <QMouseEvent>
#include <QPushButton>
#include <QStackedWidget>
#include <QSettings>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTimeEdit>
#include <QSplitter>
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
        require(table->columnCount() == VedicPanel::ColumnCount, "Dense Vedic columns are present");
        const QStringList headers = VedicPanel::columnHeaders();
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
                    && table->item(row, VedicPanel::ColNakshatra)->text() == item.nakshatra
                    && table->item(row, VedicPanel::ColPada)->text() == QString("%1/4").arg(item.pada)
                    && table->item(row, VedicPanel::ColStarLord)->text() == item.lord,
                    "Reference Vedic placement mismatch");
            require(table->item(row, 2)->text() != "N/A", "Reference degree is available");
            for (int column = 0; column < table->columnCount(); ++column) {
                require(!(table->item(row, column)->flags() & Qt::ItemIsEditable),
                        "Vedic table item is editable");
            }
        }
    }

    // South Indian D1/D9 charts must agree with the placements table they sit
    // beside: every body in its table sign (D1) and table D9 sign (D9), the
    // Lagna box marked, vargottama flagged exactly where the table shows '*'.
    static void assertSouthIndianCharts(VedicPanel& panel) {
        SouthIndianChart* d1 = panel.rasiChart();
        SouthIndianChart* d9 = panel.navamsaChart();
        require(d1 && d9 && d1->isVisible() && d9->isVisible(), "D1 and D9 charts are shown beside the table");
        // Fixed South Indian frame: Pisces top-left, clockwise.
        require(SouthIndianChart::gridCell(11) == QPoint(0, 0) && SouthIndianChart::gridCell(0) == QPoint(1, 0)
                && SouthIndianChart::gridCell(3) == QPoint(3, 1) && SouthIndianChart::gridCell(5) == QPoint(3, 3)
                && SouthIndianChart::gridCell(8) == QPoint(0, 3) && SouthIndianChart::gridCell(10) == QPoint(0, 1),
                "South Indian sign frame");
        QTableWidget* table = panel.table();
        require(d1->entries().size() == table->rowCount() && d9->entries().size() == table->rowCount(),
                "Every placement appears in both charts");
        const QStringList signs = {"Aries", "Taurus", "Gemini", "Cancer", "Leo", "Virgo",
                                   "Libra", "Scorpio", "Sagittarius", "Capricorn", "Aquarius", "Pisces"};
        for (int i = 0; i < d1->entries().size(); ++i) {
            const SouthIndianEntry& rasi = d1->entries()[i];
            const SouthIndianEntry& navamsa = d9->entries()[i];
            const int row = rowFor(table, rasi.key);
            require(row >= 0 && navamsa.key == rasi.key, "Chart entry matches a table row");
            require(signs.value(rasi.sign) == table->item(row, 1)->text(), "D1 box matches the table sign");
            const QString d9Text = table->item(row, VedicPanel::ColNavamsa)->text();
            require(signs.value(navamsa.sign) == d9Text.section(' ', 0, 0), "D9 box matches the table D9 sign");
            require(rasi.vargottama == d9Text.endsWith('*') && navamsa.vargottama == rasi.vargottama,
                    "Vargottama marks match the table");
            require(rasi.lagna == (rasi.key == "Ascendant"), "Only the Ascendant marks the Lagna box");
            require(navamsa.degree >= 0.0 && navamsa.degree < 30.0 && !rasi.tooltip.isEmpty(), "D9 degree and tooltip");
        }
        require(d1->entries().first().label == "As" && d1->entries().first().sign == 2, "Gemini Lagna in D1");
        // Clicking a planet in the chart selects its table row; selecting a row
        // highlights it in both charts.
        d1->grab();
        const QRectF moonRect = d1->entryRect("Moon");
        require(!moonRect.isEmpty() && d1->signBox(3).contains(moonRect.center()), "Moon drawn inside the Cancer box");
        QMouseEvent press(QEvent::MouseButtonPress, moonRect.center(), d1->mapToGlobal(moonRect.center()),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(d1, &press);
        require(table->item(table->currentRow(), 0)->text() == "Moon"
                && d9->highlightedKey() == "Moon", "Chart click selects the table row and both charts");
        table->selectRow(rowFor(table, "Saturn"));
        require(d1->highlightedKey() == "Saturn" && d9->highlightedKey() == "Saturn", "Table selection highlights both charts");
        table->clearSelection();
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
        auto* placements = window.vedicPanel_->table();
        const int mars = rowFor(placements, "Mars");
        const int moon = rowFor(placements, "Moon");
        const QStringList expectedLords = {"Mercury", "Jupiter", "Moon", "Mercury", "Jupiter", "Saturn", "Saturn", "Mars", "Sun", "Saturn"};
        for (int row = 0; row < placements->rowCount(); ++row) {
            if (placements->item(row, VedicPanel::ColSignLord)->text() != expectedLords[row])
                throw std::runtime_error(("Incorrect sign lord for " + placements->item(row, 0)->text()
                    + ": " + placements->item(row, VedicPanel::ColSignLord)->text()).toStdString());
        }
        require(placements->item(mars, VedicPanel::ColHouse)->text() == "4"
                && placements->item(mars, VedicPanel::ColSignLord)->text() == "Mercury"
                && placements->item(mars, VedicPanel::ColNavamsa)->text() == "Leo",
                "Reference whole-sign house, sign lord and D9 sign");
        require(placements->item(moon, VedicPanel::ColDignity)->text() == "Ruler"
                && placements->item(mars, VedicPanel::ColFromSun)->data(Qt::UserRole).toDouble() > 80
                && placements->item(mars, VedicPanel::ColFromSun)->data(Qt::UserRole).toDouble() < 90,
                "Reference dignity and solar distance");
        require(placements->item(mars, VedicPanel::ColMotion)->text().startsWith("D ")
                && placements->item(mars, VedicPanel::ColDegree)->toolTip().contains("Virgo"),
                "Motion and precise position tooltip");
        assertSouthIndianCharts(*window.vedicPanel_);
        auto* tara = window.vedicPanel_->findChild<QWidget*>("taraPanel");
        require(tara && tara->isVisible(), "Tara is visible alongside Moorthi");
        tara->findChild<QDateEdit*>("taraDate")->setDate(QDate(2026, 1, 1));
        tara->findChild<QTimeEdit*>("taraTime")->setTime(QTime(6, 0));
        tara->findChild<QPushButton*>("taraCalculate")->click();
        require(tara->findChild<QTableWidget*>("taraTable")->rowCount() == 9, "Integrated Tara calculates");
        tara->findChild<QPushButton*>("taraCopy")->click();
        require(QApplication::clipboard()->text().contains("Lahiri:")
                && QApplication::clipboard()->text().contains("UTC:"), "Transit copy includes shared birth facts");

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
        for (int row = 0; row < placements->rowCount(); ++row)
            require(placements->item(row, VedicPanel::ColSignLord)->text() == expectedLords[row],
                    "Sign lords remain correct after transit calculations and resizing");
        require(placements->columnWidth(VedicPanel::ColNakshatra) >= placements->fontMetrics().horizontalAdvance("Purva Bhadrapada") + 4,
                "Compact layout preserves readable nakshatra column width");
        require(window.grab().save(QCoreApplication::applicationDirPath() + "/vedic_compact.png"), "Compact workspace screenshot");
        window.resize(1800, 1050); settle();

        QTableWidget* table = window.vedicPanel_->table();
        table->sortItems(VedicPanel::ColLongitude, Qt::AscendingOrder);
        assertAscendingNumberRole(table, VedicPanel::ColLongitude);
        window.vedicPanel_->findChild<QToolButton*>("vedicNaturalOrder")->click();
        require(table->item(0, 0)->text() == "Ascendant" && table->item(3, 0)->text() == "Mars"
                && tara->findChild<QTableWidget*>("taraTable")->rowCount() == 9,
                "Natural order restores canonical rows without clearing Tara");
        table->sortItems(1, Qt::AscendingOrder);
        assertAscendingNumberRole(table, 1);
        assertCanonicalOrder(table, 1, {"Aries", "Taurus", "Gemini", "Cancer", "Leo", "Virgo",
                                        "Libra", "Scorpio", "Sagittarius", "Capricorn", "Aquarius", "Pisces"});
        table->sortItems(VedicPanel::ColNakshatra, Qt::AscendingOrder);
        assertAscendingNumberRole(table, VedicPanel::ColNakshatra);
        assertCanonicalOrder(table, VedicPanel::ColNakshatra, {"Ashwini", "Bharani", "Krittika", "Rohini", "Mrigashira",
                                        "Ardra", "Punarvasu", "Pushya", "Ashlesha", "Magha",
                                        "Purva Phalguni", "Uttara Phalguni", "Hasta", "Chitra", "Swati",
                                        "Vishakha", "Anuradha", "Jyeshtha", "Mula", "Purva Ashadha",
                                        "Uttara Ashadha", "Shravana", "Dhanishtha", "Shatabhisha",
                                        "Purva Bhadrapada", "Uttara Bhadrapada", "Revati"});
        table->sortItems(VedicPanel::ColStarLord, Qt::AscendingOrder);
        assertAscendingText(table, VedicPanel::ColStarLord);
        table->sortItems(2, Qt::AscendingOrder);
        assertAscendingNumberRole(table, 2);
        table->sortItems(VedicPanel::ColPada, Qt::AscendingOrder);
        assertAscendingNumberRole(table, VedicPanel::ColPada);

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
        const int copiedHeader = copiedLines.indexOf(VedicPanel::columnHeaders().join('\t'));
        require(copiedHeader >= 0 && copiedHeader + 1 < copiedLines.size()
                && copiedLines.at(copiedHeader + 1).section('\t', 0, 0) == copiedBody,
                "Copy follows displayed table order and includes context");

        auto* dasha = static_cast<DashaPanel*>(window.vedicPanel_->findChild<QWidget*>("dashaPanel"));
        auto* views = window.vedicPanel_->findChild<QTabWidget*>("vedicViews");
        require(dasha && views && views->count() == 4
                && views->tabText(2) == "Vedic transit graph" && views->tabText(3) == "Ashtakavarga",
                "Dashas, Vedic transit graph and Ashtakavarga preserve the research workspace");
        const auto inspectMoment = QDateTime(QDate(2026, 9, 13), QTime(16, 0, 0, 123), QTimeZone("Asia/Dhaka"));
        dasha->setInspectionTime(inspectMoment.toMSecsSinceEpoch());
        views->setCurrentIndex(1); settle();
        auto* lookup = dasha->findChild<QTimer*>(); QElapsedTimer lookupWait; lookupWait.start();
        while (lookup->isActive() && lookupWait.elapsed() < 60000) QApplication::processEvents();
        require(!lookup->isActive() && dasha->findChild<QTableWidget*>("dashaActive")->rowCount() == 5,
                "Five active levels and ingress lookups complete in MainWindow");
        require(tara->findChild<QDateEdit*>("taraDate")->date() == inspectMoment.date()
                && tara->findChild<QTimeEdit*>("taraTime")->time() == inspectMoment.time(), "Shared inspection instant retains milliseconds");
        auto* activeTransits = dasha->findChild<QTableWidget*>("dashaTransits");
        require(activeTransits->rowCount() == dasha->activeLords().size(), "Unique active transit lords under one node model");
        require(window.grab().save(QCoreApplication::applicationDirPath() + "/vedic_dashas.png"), "Integrated Dashas preview");
        window.resize(1200, 850); settle();
        require(window.grab().save(QCoreApplication::applicationDirPath() + "/vedic_dashas_compact.png"), "Compact integrated Dashas preview");
        window.resize(1800, 1050); settle();
        const QString selectedTransit = activeTransits->item(0, 0)->text(); activeTransits->selectRow(0);
        dasha->findChild<QPushButton*>("dashaOpenTransit")->click();
        auto* taraPlanet = tara->findChild<QComboBox*>("taraPlanet");
        auto* taraTable = tara->findChild<QTableWidget*>("taraTable");
        require(views->currentIndex() == 0 && taraPlanet->currentText() == selectedTransit && taraTable->rowCount() == 9,
                "Open Tara selects the active planet and calculates the same instant");
        taraPlanet->setCurrentIndex(0);
        // The earlier ayanamsa change correctly invalidated the Lahiri search.
        // Generate fresh results under Raman before checking filter restoration.
        moorthi->findChild<QPushButton*>("moorthiRun")->click(); searchWait.restart();
        while (searchTimer->isActive() && searchWait.elapsed() < 60000) QApplication::processEvents();
        require(!searchTimer->isActive() && moorthi->findChild<QTableWidget*>("moorthiTable")->rowCount() == 12,
                "Fresh search after ayanamsa change supplies filter baseline");
        auto* activeOnly = window.vedicPanel_->findChild<QCheckBox*>("vedicActiveOnly"); activeOnly->setChecked(true);
        int visibleActive = 0;
        for (int row = 0; row < taraTable->rowCount(); ++row) if (!taraTable->isRowHidden(row)) {
            require(dasha->activeLords().contains(taraTable->item(row, 0)->text().section(" (", 0, 0)), "Tara active filter matches base lord names");
            ++visibleActive;
        }
        require(visibleActive == dasha->activeLords().size(), "Active-lord filter includes each active planet");
        auto* moorthiTable = moorthi->findChild<QTableWidget*>("moorthiTable");
        require(moorthiTable->rowCount() == 12, "Explicit Sun selection takes priority over the shared dasha filter");
        tara->findChild<QPushButton*>("taraCopy")->click();
        require(QApplication::clipboard()->text().contains("Active lords only") && QApplication::clipboard()->text().contains("365.25-day year"), "Filtered copy includes dasha context");
        auto* graph = static_cast<MoorthiGraphPanel*>(window.vedicPanel_->findChild<QWidget*>("moorthiGraphPanel"));
        require(graph != nullptr, "Graph is wired into the loaded Vedic chart");
        views->setCurrentIndex(2); settle();
        require(!activeOnly->isVisible() && !copy->isVisible(), "Graph hides controls belonging to other views");
        graph->findChild<QDateEdit*>("moorthiGraphFrom")->setDate(QDate(2016, 1, 1));
        graph->findChild<QDateEdit*>("moorthiGraphThrough")->setDate(QDate(2032, 12, 31));
        const QStringList graphPlanets = {"Jupiter", "Saturn", "Rahu (Mean)", "Ketu (Mean)"};
        for (auto* choice : graph->findChildren<QCheckBox*>("moorthiGraphChoice"))
            choice->setChecked(graphPlanets.contains(choice->text()));
        graph->findChild<QPushButton*>("moorthiGraphCalculate")->click();
        auto* graphTimer = graph->findChild<QTimer*>("moorthiGraphTimer");
        QElapsedTimer graphWait; graphWait.start();
        while (graphTimer->isActive() && graphWait.elapsed() < 90000) QApplication::processEvents();
        require(!graphTimer->isActive() && graph->series().size() == 4
                && graph->findChild<QLabel*>("moorthiGraphStatus")->text().startsWith("Complete"),
                "Graph calculates all selected planets regardless of inspection-time active filter");
        require(window.grab().save(QCoreApplication::applicationDirPath() + "/vedic_moorthi_graph.png"), "Integrated graph preview");
        window.resize(1100, 800); settle();
        require(window.grab().save(QCoreApplication::applicationDirPath() + "/vedic_moorthi_graph_compact.png"), "Compact integrated graph preview");
        window.resize(1800, 1050); views->setCurrentIndex(0); settle();
        require(activeOnly->isVisible() && activeOnly->isChecked() && moorthiTable->rowCount() == 12,
                "Returning from graph preserves existing filter and table");
        activeOnly->setChecked(false);
        require(moorthiTable->rowCount() == 12 && taraTable->rowCount() == 9, "Turning off active filter restores existing results without rerunning");
        tara->findChild<QDateEdit*>("taraDate")->setDate(QDate(2027, 2, 1)); tara->findChild<QTimeEdit*>("taraTime")->setTime(QTime(8, 0));
        tara->findChild<QPushButton*>("taraCalculate")->click();
        require(dasha->inspectionMs() == QDateTime(QDate(2027, 2, 1), QTime(8, 0), QTimeZone("Asia/Dhaka")).toMSecsSinceEpoch()
                && taraTable->rowCount() == 9, "Calculating Tara updates the shared dasha instant without clearing fresh Tara results");

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
