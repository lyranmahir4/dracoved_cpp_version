#include "moorthi_panel.h"
#include "../core/formatting.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateEdit>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QLabel>
#include <QRegularExpression>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <iostream>
#include <stdexcept>
#include <limits>

using namespace dracoved;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    app.setFont(QFont("Segoe UI", 9));
    try {
        const Moorthi expected[] = {Moorthi::Swarna, Moorthi::Rajata, Moorthi::Tamra, Moorthi::Loha,
            Moorthi::Rajata, Moorthi::Swarna, Moorthi::Tamra, Moorthi::Loha, Moorthi::Rajata,
            Moorthi::Tamra, Moorthi::Swarna, Moorthi::Loha};
        for (int i = 0; i < 12; ++i) require(moorthiForCount(i + 1) == expected[i], "Book classification table");
        SwissEph swe; QString error;
        require(swe.load({"swedll64.dll"}, &error), "Load Swiss Ephemeris");
        swe.setEphePath("ephe"); swe.setSidMode(1);
        const double start = swe.julianDay(2026, 1, 1, 0, SE_GREG_CAL);
        const double end = swe.julianDay(2027, 1, 1, 0, SE_GREG_CAL);
        int sunEntries = 0, retroEntries = 0, wrapEntries = 0;
        for (int body : {SE_SUN, SE_MERCURY, SE_MOON, SE_TRUE_NODE}) {
            double last = start - 1;
            for (double jd = start; jd < end; jd += 0.25) {
                QVector<MoorthiEntry> entries;
                require(findMoorthiEntries(swe, body, 0, jd, std::min(jd + 0.25, end), 3, &entries, &error), "Search interval");
                for (const auto& e : entries) {
                    require(e.jd > last, "Ordered unique entries"); last = e.jd;
                    double before{}, after{}, moon{};
                    require(swe.calcUt(e.jd - 1.0 / 86400, body, SEFLG_SIDEREAL, &before, &error), "Pre-entry position");
                    require(swe.calcUt(e.jd + 1.0 / 86400, body, SEFLG_SIDEREAL, &after, &error), "Post-entry position");
                    require(signIndex(before) != signIndex(after) && signIndex(after) == e.newSign, "Brackets exact sign crossing");
                    require(swe.calcUt(e.jd, SE_MOON, SEFLG_SIDEREAL, &moon, &error), "Entry Moon");
                    const int moonSign = body == SE_MOON ? e.newSign : signIndex(moon);
                    const int count = (moonSign - 3 + 12) % 12 + 1;
                    if (body == SE_MOON) require(e.count == 0 && e.moorthi == Moorthi::NotApplicable, "Moon crossing is a sampling anchor, not a Moorthi");
                    else require(e.count == count && e.moorthi == expected[count - 1], "Entry Moon determines metal");
                    if (body == SE_SUN) ++sunEntries;
                    if (body == SE_MERCURY && e.retrograde) ++retroEntries;
                    if (e.newSign == 0 || signIndex(before) == 0) ++wrapEntries;
                }
            }
        }
        require(sunEntries == 12 && retroEntries > 0 && wrapEntries > 0, "Annual ingress and retrograde coverage");
        NatalInput input; input.name = "Moorthi check"; input.date = QDate(1999, 1, 4);
        input.time = QTime(16, 1); input.timezone = "Asia/Dhaka";
        NatalChart chart; chart.siderealAyanamsa = SiderealAyanamsa::Lahiri;
        BodyPosition moon; moon.name = "Moon"; moon.longitude = 109.6409818; chart.bodies << moon;
        MoorthiPanel panel(&swe); panel.resize(1200, 620); panel.setContext(input, chart); panel.show();
        auto* from = panel.findChild<QDateEdit*>("moorthiFrom");
        auto* through = panel.findChild<QDateEdit*>("moorthiThrough");
        auto* planet = panel.findChild<QComboBox*>("moorthiPlanet");
        auto* run = panel.findChild<QPushButton*>("moorthiRun");
        auto* table = panel.findChild<QTableWidget*>("moorthiTable");
        from->setDate(QDate(2026, 1, 1)); through->setDate(QDate(2026, 12, 31));
        planet->setCurrentIndex(planet->findText("Sun")); run->click();
        auto* timer = panel.findChild<QTimer*>(); QElapsedTimer wait; wait.start();
        while (timer->isActive() && wait.elapsed() < 60000) app.processEvents();
        require(!timer->isActive() && table->rowCount() == 12, "UI completes Sun search");
        require(!table->item(0, 6)->icon().isNull(), "Metal icons exist");
        table->sortItems(1, Qt::DescendingOrder);
        panel.findChild<QPushButton*>("moorthiCopy")->click();
        require(app.clipboard()->text().contains(table->item(0, 1)->text())
            && app.clipboard()->text().contains("Asia/Dhaka"), "Copy includes results and timezone");
        auto* filter = panel.findChild<QComboBox*>("moorthiFilter"); filter->setCurrentIndex(1);
        for (int row = 0; row < table->rowCount(); ++row) require(table->item(row, 6)->text() == "Swarna", "Metal filter");
        filter->setCurrentIndex(0); app.processEvents();
        require(panel.grab().save(QCoreApplication::applicationDirPath() + "/moorthi.png"), "Screenshot");
        from->setDate(QDate(2027, 1, 1)); require(table->rowCount() == 0, "Changed range clears stale results");
        from->setDate(QDate(2026, 1, 1)); run->click();
        input.name = "Changed chart"; panel.setContext(input, chart);
        require(!timer->isActive() && table->rowCount() == 0, "Source change cancels search");
        auto* status = panel.findChild<QLabel*>("moorthiStatus");
        auto* copy = panel.findChild<QPushButton*>("moorthiCopy");
        auto query = [&](QDate first, QDate last, const QString& target) {
            from->setDate(first); through->setDate(last);
            planet->setCurrentIndex(planet->findText(target)); run->click(); wait.restart();
            while (timer->isActive() && wait.elapsed() < 60000) app.processEvents();
            require(!timer->isActive() && status->text().startsWith("Complete"), "Search completes without an arbitrary duration cap");
        };
        // User regression: unrelated 2034 dasha lords must not hide the two
        // explicitly requested Rahu entries from 2026 through 2029.
        input.name = "Mahir 1998 regression"; input.date = QDate(1998, 1, 4); input.time = QTime(16, 1, 23);
        input.lunarNodePolicy.mode = LunarNodeMode::TrueOnly;
        chart.siderealAyanamsa = SiderealAyanamsa::TrueCitra; chart.bodies[0].longitude = 334.6009;
        panel.setContext(input, chart);
        const QString dashaContext = "17 Feb 2034 00:32:11.867 +06:00 · MD Ketu · AD Moon · 365.25-day year";
        panel.setActiveLords({"Ketu", "Moon"}, dashaContext, true);
        query(QDate(2026, 1, 1), QDate(2029, 12, 31), "Rahu (True)");
        require(table->rowCount() == 2 && status->text().contains("2 of 2 entries shown")
            && status->text().contains("Selected planet takes priority"), "Explicit Rahu shows both entries despite unrelated dasha lords");
        for (int row = 0; row < table->rowCount(); ++row) require(table->item(row, 0)->text() == "Rahu (True)", "True node target retained");
        table->sortItems(1, Qt::AscendingOrder);
        const double firstJd = table->item(0, 1)->data(Qt::UserRole).toDouble();
        const int firstMetal = table->item(0, 6)->data(Qt::UserRole).toInt();
        require(panel.grab().save(QCoreApplication::applicationDirPath() + "/rahu_2026_2029.png"), "Rahu regression preview");
        copy->click();
        require(app.clipboard()->text().contains("2 of 2 entries shown")
            && app.clipboard()->text().contains("selected planet takes priority")
            && !app.clipboard()->text().contains("Active lords only ·"), "Copy reports effective filtering");
        const auto entryDay = QDateTime::fromMSecsSinceEpoch(qRound64((firstJd - 2440587.5) * 86400000.0), QTimeZone("Asia/Dhaka")).date();
        query(entryDay, entryDay, "Rahu (True)");
        require(table->rowCount() == 1 && std::abs(table->item(0, 1)->data(Qt::UserRole).toDouble() - firstJd) < 0.1 / 86400,
            "Both range endpoints include the selected local day");
        filter->setCurrentIndex((firstMetal + 1) % 4 + 1);
        require(table->rowCount() == 0 && status->text().contains("0 of 1 entries shown")
            && status->text().contains("hidden by the filters"), "Empty metal filter is explained");
        filter->setCurrentIndex(0); require(table->rowCount() == 1, "Clearing metal filter restores results without recalculating");
        query(QDate(2024, 1, 1), QDate(2030, 12, 31), "Jupiter");
        require(table->rowCount() >= 7, "Jupiter query works with unrelated dasha lords");
        query(QDate(1900, 1, 1), QDate(1999, 12, 31), "Sun");
        require(table->rowCount() == 1200 && status->text().contains("1200 of 1200 entries shown"), "100-year Sun search returns all 1200 sign entries");
        copy->click(); require(app.clipboard()->text().contains("1900-01-01 through 1999-12-31"), "Long-range copy keeps full dates");
        panel.setActiveLords({"Sun"}, dashaContext, true);
        query(QDate(2026, 1, 1), QDate(2026, 12, 31), "All planets");
        require(table->rowCount() == 12, "All planets still supports active-lord filtering");
        panel.setActiveLords({}, "Choose an inspection time", true);
        require(table->rowCount() == 0 && status->text().contains("hidden by the filters"), "Empty dasha chain explains hidden results");
        panel.setActiveLords({}, {}, false);
        require(table->rowCount() > 150 && !status->text().contains("hidden by the filters"), "Turning off dasha filtering restores all found entries");
        const QString fullStatus = status->text(); filter->setCurrentIndex(1); filter->setCurrentIndex(0);
        require(status->text() == fullStatus, "Result counts follow filtering without stale status");
        from->setDate(QDate(1900, 1, 1)); through->setDate(QDate(2100, 12, 31)); run->click(); wait.restart();
        const QRegularExpression countPattern(" · ([1-9][0-9]*) entries$");
        while (timer->isActive() && !countPattern.match(status->text()).hasMatch() && wait.elapsed() < 5000) app.processEvents();
        require(timer->isActive() && countPattern.match(status->text()).hasMatch(), "Long search yields UI events and reports found entries");
        panel.findChild<QPushButton*>("moorthiStop")->click();
        require(!timer->isActive() && table->rowCount() > 0 && status->text().contains("partial results"), "Stop preserves collected entries as partial results");
        copy->click(); require(app.clipboard()->text().contains("Stopped · partial results"), "Partial copy never claims complete coverage");
        from->setDate(QDate(9999, 1, 1)); through->setDate(QDate(9999, 12, 31));
        planet->setCurrentIndex(planet->findText("Jupiter")); run->click(); wait.restart();
        while (timer->isActive() && wait.elapsed() < 5000) app.processEvents();
        require(!timer->isActive() && status->text().startsWith("Search failed for Jupiter near")
            && !status->text().contains("No sign entries found in this range"), "Ephemeris failures identify planet/date rather than claiming a successful empty result");
        from->setDate(QDate(2027, 1, 1)); through->setDate(QDate(2026, 1, 1)); run->click();
        require(!timer->isActive() && status->text().contains("From must be on or before Through"), "Reject reversed dates only, not long durations");
        SwissEph unloaded; MoorthiPanel missing(&unloaded); missing.setContext(input, chart);
        missing.findChild<QPushButton*>("moorthiRun")->click();
        require(missing.findChild<QLabel*>("moorthiStatus")->text().contains("ephemeris unavailable"), "Missing ephemeris never silently ignores Calculate");
        chart.bodies[0].longitude = std::numeric_limits<double>::quiet_NaN(); panel.setContext(input, chart);
        require(!run->isEnabled() && table->rowCount() == 0, "Invalid natal Moon cannot produce a bogus sign count");
        std::cout << "PASS: Rahu/Jupiter filter regression, 100-year range, inclusive dates, partial cancellation and failure messages\n";
        std::cout << "PASS: book mapping, exact crossings, retrograde, wrap, entry Moon, UI search/filter/copy/invalidation\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
