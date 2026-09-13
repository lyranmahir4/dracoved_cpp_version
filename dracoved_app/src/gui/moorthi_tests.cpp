#include "moorthi_panel.h"
#include "../core/formatting.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateEdit>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <iostream>
#include <stdexcept>

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
                    require(e.count == count && e.moorthi == expected[count - 1], "Entry Moon determines metal");
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
        std::cout << "PASS: book mapping, exact crossings, retrograde, wrap, entry Moon, UI search/filter/copy/invalidation\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
