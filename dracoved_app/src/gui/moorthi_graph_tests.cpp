#include "moorthi_graph_panel.h"
#include "moorthi_panel.h"
#include "../core/formatting.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace dracoved;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    app.setFont(QFont("Segoe UI", 9));
    try {
        SwissEph swe; QString error;
        require(swe.load({"swedll64.dll"}, &error), "Load ephemeris");
        swe.setEphePath("ephe");
        NatalInput input; input.name = "Mahir 1999"; input.date = QDate(1999, 1, 4);
        input.time = QTime(16, 1, 23); input.timezone = "Asia/Dhaka";
        input.lunarNodePolicy.mode = LunarNodeMode::TrueOnly;
        input.siderealAyanamsa = SiderealAyanamsa::Raman;
        NatalChart chart; chart.siderealAyanamsa = SiderealAyanamsa::Lahiri;
        BodyPosition moon; moon.name = "Moon"; moon.longitude = 109.6409818; chart.bodies << moon;
        MoorthiGraphPanel panel(&swe); panel.resize(1600, 730); panel.setContext(input, chart); panel.show();
        auto* from = panel.findChild<QDateEdit*>("moorthiGraphFrom");
        auto* through = panel.findChild<QDateEdit*>("moorthiGraphThrough");
        auto* run = panel.findChild<QPushButton*>("moorthiGraphCalculate");
        auto* stop = panel.findChild<QPushButton*>("moorthiGraphStop");
        auto* timer = panel.findChild<QTimer*>("moorthiGraphTimer");
        auto* status = panel.findChild<QLabel*>("moorthiGraphStatus");
        auto select = [&](const QStringList& names) {
            for (auto* choice : panel.findChildren<QCheckBox*>("moorthiGraphChoice"))
                choice->setChecked(names.isEmpty() || names.contains(choice->text()));
        };
        auto wait = [&] {
            QElapsedTimer elapsed; elapsed.start();
            while (timer->isActive() && elapsed.elapsed() < 90000) app.processEvents();
            require(!timer->isActive(), "Graph calculation yields and completes");
        };
        auto query = [&](QDate first, QDate last, const QStringList& names) {
            from->setDate(first); through->setDate(last); select(names); run->click(); wait();
            require(status->text().startsWith("Complete"), "Graph search succeeds");
        };
        const QTimeZone zone("Asia/Dhaka");
        auto jd = [&](QDate date) { return 2440587.5 + date.startOfDay(zone).toMSecsSinceEpoch() / 86400000.0; };
        query(QDate(2026, 1, 1), QDate(2026, 12, 31), {"Sun"});
        require(panel.series().size() == 1 && panel.series()[0].entries.size() == 13,
                "One initial Sun entry plus twelve entries in the year");
        require(panel.series()[0].entries.first().jd < jd(QDate(2026, 1, 1))
                && panel.series()[0].calculatedThrough == jd(QDate(2027, 1, 1)),
                "Initial metal and inclusive Through day cover the full range");
        MoorthiPanel tablePanel(&swe); tablePanel.setContext(input, chart);
        tablePanel.findChild<QDateEdit*>("moorthiFrom")->setDate(QDate(2026, 1, 1));
        tablePanel.findChild<QDateEdit*>("moorthiThrough")->setDate(QDate(2026, 12, 31));
        auto* planet = tablePanel.findChild<QComboBox*>("moorthiPlanet");
        planet->setCurrentIndex(planet->findText("Sun"));
        tablePanel.findChild<QPushButton*>("moorthiRun")->click();
        QElapsedTimer tableWait; tableWait.start();
        while (tablePanel.findChild<QTimer*>()->isActive() && tableWait.elapsed() < 30000) app.processEvents();
        auto* table = tablePanel.findChild<QTableWidget*>("moorthiTable");
        require(table->rowCount() == 12, "Reference table has twelve Sun entries");
        for (int i = 0; i < 12; ++i) {
            const auto& entry = panel.series()[0].entries[i + 1];
            require(std::abs(table->item(i, 1)->data(Qt::UserRole).toDouble() - entry.jd) < 0.1 / 86400.0
                    && table->item(i, 6)->text() == moorthiName(entry.moorthi), "Graph and table agree on exact entry and metal");
        }
        double actual = 0, expected = 0;
        require(swe.calcUt(jd(QDate(2026, 1, 1)), SE_MOON, SEFLG_SIDEREAL, &actual, &error), "Read shared ephemeris state");
        swe.setSidMode(siderealAyanamsaSwissMode(input.siderealAyanamsa));
        require(swe.calcUt(jd(QDate(2026, 1, 1)), SE_MOON, SEFLG_SIDEREAL, &expected, &error)
                && std::abs(actual - expected) < 1e-9, "Calculation restores global ayanamsa");

        query(QDate(2026, 1, 1), QDate(2026, 1, 1), {"Saturn"});
        require(panel.series()[0].entries.size() == 1 && panel.series()[0].entries.first().jd < jd(QDate(2026, 1, 1)),
                "A slow planet with no entries in range retains its previous metal");
        query(QDate(2016, 1, 1), QDate(2032, 12, 31), {"Jupiter", "Saturn", "Rahu (True)", "Ketu (True)"});
        require(panel.series().size() == 4, "Multiple checked planets are calculated beyond ten years");
        for (const auto& series : panel.series()) {
            require(series.calculatedThrough == jd(QDate(2033, 1, 1)) && series.entries.size() > 1,
                    "All selected planets complete long range");
            for (int i = 1; i < series.entries.size(); ++i)
                require(series.entries[i].jd > series.entries[i - 1].jd
                        && series.entries[i].jd < jd(QDate(2033, 1, 1)), "Entries are chronological with exclusive end");
        }
        app.processEvents();
        const QString out = QCoreApplication::applicationDirPath();
        require(panel.grab().save(out + "/moorthi_graph.png"), "Save actual graph preview");
        panel.resize(700, 480); app.processEvents();
        require(panel.grab().save(out + "/moorthi_graph_compact.png"), "Save compact graph preview");
        QPalette dark = app.palette();
        dark.setColor(QPalette::Base, QColor("#202329")); dark.setColor(QPalette::Window, QColor("#292d34"));
        dark.setColor(QPalette::Text, QColor("#eeeeee")); dark.setColor(QPalette::WindowText, QColor("#eeeeee"));
        dark.setColor(QPalette::Button, QColor("#343940")); dark.setColor(QPalette::ButtonText, QColor("#eeeeee"));
        panel.setPalette(dark); panel.resize(1400, 720); app.processEvents();
        require(panel.grab().save(out + "/moorthi_graph_dark.png"), "Save dark graph preview");
        panel.setPalette(app.palette());

        query(QDate(2026, 1, 1), QDate(2026, 1, 31), {});
        require(panel.series().size() == 9, "All planets includes both node axes");
        auto* all = panel.findChild<QCheckBox*>("moorthiGraphAll"); all->click();
        require(!run->isEnabled() && panel.series().isEmpty(), "Unchecking All clears graph and requires selection");
        all->click(); require(run->isEnabled(), "All can be restored");
        input.lunarNodePolicy.mode = LunarNodeMode::Both; panel.setContext(input, chart);
        query(QDate(2026, 1, 1), QDate(2026, 1, 1), {});
        require(panel.series().size() == 11, "Both node models remain separate series");

        input.lunarNodePolicy.mode = LunarNodeMode::TrueOnly;
        chart.siderealAyanamsa = SiderealAyanamsa::TrueCitra; chart.bodies[0].longitude = 334.6009;
        panel.setContext(input, chart);
        query(QDate(2026, 1, 1), QDate(2029, 12, 31), {"Rahu (True)"});
        require(panel.series().size() == 1 && panel.series()[0].entries.size() == 3,
                "Reported Rahu range has initial metal and two new entries");
        for (int i = 1; i < 3; ++i) require(panel.series()[0].entries[i].moorthi == Moorthi::Tamra, "Reported Rahu metals match");
        from->setDate(QDate(2030, 1, 1)); run->click();
        require(status->text().contains("From must") && panel.series().isEmpty(), "Reject reversed range without stale graph");
        from->setDate(QDate(1800, 1, 1)); through->setDate(QDate(2200, 12, 31)); select({"Sun"}); run->click();
        QElapsedTimer stopWait; stopWait.start();
        while (timer->isActive() && panel.series().first().entries.size() < 4 && stopWait.elapsed() < 10000) app.processEvents();
        require(timer->isActive() && panel.series().first().entries.size() >= 4, "Long search remains cancellable");
        stop->click();
        require(!timer->isActive() && status->text().startsWith("Stopped")
                && panel.series().first().calculatedThrough < jd(QDate(2201, 1, 1)), "Stop preserves only calculated portion");
        run->click(); input.name = "Changed natal"; panel.setContext(input, chart);
        require(!timer->isActive() && panel.series().isEmpty(), "Chart change cancels pending work");
        from->setDate(QDate(9999, 1, 1)); through->setDate(QDate(9999, 12, 31)); select({"Sun"}); run->click(); wait();
        require(status->text().startsWith("Unavailable:"), "Ephemeris failure is explicit, not a metal");
        chart.bodies[0].longitude = std::numeric_limits<double>::quiet_NaN(); panel.setContext(input, chart);
        require(!run->isEnabled() && panel.series().isEmpty(), "Invalid natal Moon cannot produce graph");
        std::cout << "Moorthi graph checks passed: table parity, initial metal, long ranges, selection, nodes, cancellation and errors.\n";
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n'; return 1;
    }
}
