#include "tara_panel.h"
#include "../core/swiss_eph.h"
#include "../core/vedic_nakshatra.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateEdit>
#include <QFontDatabase>
#include <QTimeEdit>
#include <QPushButton>
#include <QTableWidget>
#include <iostream>
#include <stdexcept>

using namespace dracoved;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    app.setFont(QFont("Segoe UI", 9));
    try {
        const QStringList names = {"Janma", "Sampat", "Vipat", "Kshema", "Pratyak",
            "Daivanukula", "Naidhana", "Mitra", "Parama Maitra"};
        for (int natal = 0; natal < 27; ++natal)
            for (int offset = 0; offset < 27; ++offset) {
                const auto tara = classifyVedicTara(natal, (natal + offset) % 27);
                require(tara.count == offset + 1 && tara.number == offset % 9 + 1
                    && tara.name == names[offset % 9], "All birth stars, cycles and wraparound");
            }
        require(classifyVedicTara(-1, 0).count == 0 && classifyVedicTara(0, 27).number == 0, "Invalid indices");
        SwissEph swe; QString error;
        require(swe.load({"swedll64.dll"}, &error), "Load Swiss Ephemeris"); swe.setEphePath("ephe");
        NatalInput input; input.name = "Tara check"; input.timezone = "Asia/Dhaka";
        input.date = QDate(2026, 1, 1); input.time = QTime(6, 0);
        input.siderealAyanamsa = SiderealAyanamsa::Raman; input.lunarNodePolicy.mode = LunarNodeMode::Both;
        NatalChart chart; chart.siderealAyanamsa = SiderealAyanamsa::Lahiri;
        const double jd = swe.julianDay(2026, 1, 1, 0, SE_GREG_CAL);
        swe.setSidMode(siderealAyanamsaSwissMode(chart.siderealAyanamsa));
        BodyPosition moon; moon.name = "Moon";
        require(swe.calcUt(jd, SE_MOON, SEFLG_SIDEREAL, &moon.longitude, &error), "Reference Moon"); chart.bodies << moon;
        TaraPanel panel(&swe); panel.resize(1050, 460); panel.setContext(input, chart); panel.show();
        auto* date = panel.findChild<QDateEdit*>("taraDate");
        auto* time = panel.findChild<QTimeEdit*>("taraTime");
        auto* planet = panel.findChild<QComboBox*>("taraPlanet");
        auto* table = panel.findChild<QTableWidget*>("taraTable");
        auto* run = panel.findChild<QPushButton*>("taraCalculate");
        auto* copy = panel.findChild<QPushButton*>("taraCopy");
        date->setDate(input.date); time->setTime(input.time); run->click();
        require(table->rowCount() == 11 && copy->isEnabled(), "Seven planets plus both node pairs");
        planet->setCurrentIndex(planet->findText("Moon")); table->sortItems(4, Qt::DescendingOrder);
        int shown = 0;
        for (int row = 0; row < table->rowCount(); ++row) if (!table->isRowHidden(row)) {
            ++shown;
            require(table->item(row, 0)->text() == "Moon" && table->item(row, 4)->text() == "1"
                && table->item(row, 5)->text() == "1 · Janma", "Timezone conversion, natal reference and sorted filter");
        }
        require(shown == 1, "Single-planet filter"); copy->click();
        const QString report = QApplication::clipboard()->text();
        require(report.contains("UTC: 2026-01-01T00:00:00Z") && report.contains("1 · Janma")
            && !report.contains("\nSun\t"), "Copy context and visible rows only");
        double after{}, expected{};
        require(swe.calcUt(jd, SE_MOON, SEFLG_SIDEREAL, &after, &error), "Restored Moon");
        swe.setSidMode(siderealAyanamsaSwissMode(input.siderealAyanamsa));
        require(swe.calcUt(jd, SE_MOON, SEFLG_SIDEREAL, &expected, &error) && after == expected, "Shared ayanamsa restored");
        time->setTime(QTime(7, 0)); require(table->rowCount() == 0 && !copy->isEnabled(), "Edited moment invalidates results");
        input.timezone = "America/New_York"; panel.setContext(input, chart);
        date->setDate(QDate(2026, 3, 8)); time->setTime(QTime(2, 30)); run->click();
        require(table->rowCount() == 0, "Reject skipped daylight-saving time");
        date->setDate(QDate(2026, 11, 1)); time->setTime(QTime(1, 30)); run->click();
        require(table->rowCount() == 0, "Reject ambiguous daylight-saving time");
        input.timezone = "Asia/Dhaka"; panel.setContext(input, chart);
        date->setDate(input.date); time->setTime(input.time); run->click(); planet->setCurrentIndex(0);
        app.processEvents();
        require(panel.grab().save("dracoved_app/build/tara_checks/tara.png"), "Save UI preview");
        panel.setContext(input, NatalChart{});
        require(!run->isEnabled() && table->rowCount() == 0, "Missing natal Moon clears and disables");
        std::cout << "Tara calculation and UI checks passed.\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    return 0;
}
