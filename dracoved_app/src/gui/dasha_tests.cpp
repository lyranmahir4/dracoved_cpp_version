// Focused arithmetic, boundary, real-ephemeris and widget checks. No production EXE.
#include "dasha_panel.h"
#include "../core/vedic_nakshatra.h"
#include "../core/formatting.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateEdit>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTimeEdit>
#include <QTimer>
#include <QTreeWidget>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace dracoved;
static void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
static void partitions(const Vimshottari& engine, const DashaPeriod& parent) {
    const auto children = Vimshottari::children(parent);
    if (parent.level == 4) { require(children.isEmpty(), "Prana is the final supported level"); return; }
    require(children.size() == 9 && children.first().startMs == parent.startMs
        && children.last().endMs == parent.endMs, "Nine children exactly span their parent");
    qint64 previous = parent.startMs;
    for (const auto& child : children) {
        require(child.startMs == previous && child.endMs > child.startMs, "No gaps, overlaps or zero periods");
        const auto at = engine.activeAt(child.startMs), beforeEnd = engine.activeAt(child.endMs - 1);
        require(at.size() == 5 && at[child.level].startMs == child.startMs
            && beforeEnd[child.level].startMs == child.startMs, "Start inclusive and end exclusive at every level");
        previous = child.endMs; partitions(engine, child);
    }
}
static void arithmetic() {
    const auto birth = QDateTime(QDate(2000, 1, 1), QTime(0, 0), QTimeZone::UTC).toMSecsSinceEpoch();
    for (double year : {365.25, 360.0}) {
        Vimshottari engine;
        require(engine.initialize(birth, 0, year), "Initialize at Ashwini start");
        const qint64 yearMs = qRound64(year * 86400000.0);
        require(engine.birthMajor().startMs == birth && engine.birthMajor().endMs == birth + 7 * yearMs, "Ketu is seven years");
        const auto ketuAd = Vimshottari::children(engine.birthMajor()).first();
        require(ketuAd.endMs - ketuAd.startMs == std::llround(49.0L / 120 * yearMs), "Independent Ketu/Ketu duration");
        const auto fullCycle = engine.majorCycle(birth);
        require(fullCycle.last().endMs - fullCycle.first().startMs == 120 * yearMs, "120-year cycle");
        for (const auto& major : fullCycle) partitions(engine, major);
        require(engine.activeAt(fullCycle.last().endMs).first().lord == 0, "Next cycle boundary");
        require(engine.activeAt(birth - 1).first().lord == 8, "Floor division before cycle anchor");
        require(engine.initialize(birth, 20.0 / 3.0, year), "Initialize midway through Ashwini");
        require(engine.birthMajor().startMs == birth - std::llround(3.5L * yearMs), "Reconstruct full MD before birth");
        // Hand-worked proportional sequence at 3.5/7 of Ketu MD. This detects
        // the common error of treating the remaining balance as a new period.
        const int expected[] = {0, 5, 8, 6, 3}; // Ketu / Rahu / Mercury / Jupiter / Moon.
        const auto active = engine.activeAt(birth);
        for (int level = 0; level < 5; ++level) require(active[level].lord == expected[level], "Independent five-level birth-balance fixture");
        for (int star = 0; star < 27; ++star) {
            const double lon = star * 40.0 / 3.0;
            require(engine.initialize(birth, lon, year) && engine.birthMajor().lord == star % 9
                && engine.birthMajor().startMs == birth, "All 27 exact nakshatra starts");
            if (star) require(engine.initialize(birth, std::nextafter(lon, 0.0), year)
                && engine.birthMajor().lord == (star - 1) % 9 && engine.birthMajor().contains(birth), "Representable instant before star boundary");
        }
    }
    Vimshottari invalid;
    require(!invalid.initialize(birth, std::numeric_limits<double>::quiet_NaN()) && invalid.activeAt(birth).isEmpty(), "Invalid Moon has no schedule");
    require(!invalid.initialize(birth, 0, 366), "Reject unspecified year conventions");
    require(invalid.initialize(birth, -360) && invalid.birthMajor().lord == 0, "Longitude wraparound");
    require(!invalid.initialize(std::numeric_limits<qint64>::max(), 0), "Epoch overflow guard");
    std::cout << "PASS: independent balance fixture, both year conventions, all five-level partitions and boundaries\n";
}
static void settle() { for (int i = 0; i < 4; ++i) QApplication::processEvents(); }
static void finish(DashaPanel& panel) {
    auto* timer = panel.findChild<QTimer*>(); QElapsedTimer wait; wait.start();
    while (timer->isActive() && wait.elapsed() < 60000) QApplication::processEvents();
    require(!timer->isActive(), "Ingress lookup completes within the test limit");
}
static void widgets() {
    SwissEph swe; QString error;
    require(swe.load({"swedll64.dll"}, &error), "Swiss DLL available"); swe.setEphePath("ephe");
    NatalInput input; input.name = "Dasha boundary fixture"; input.date = QDate(2026, 7, 8); input.time = QTime(12, 0);
    input.timezone = "Asia/Dhaka"; input.siderealAyanamsa = SiderealAyanamsa::Raman;
    input.lunarNodePolicy.mode = LunarNodeMode::Both;
    NatalChart chart; chart.siderealAyanamsa = SiderealAyanamsa::Lahiri;
    chart.utcDateTime = QDateTime(input.date, input.time, QTimeZone("Asia/Dhaka")).toUTC();
    BodyPosition moon; moon.name = "Moon"; moon.longitude = 320.0 / 3.0; chart.bodies << moon;
    DashaPanel panel(&swe); panel.resize(1780, 830);
    panel.setContext(input, chart, "Synthetic birth balance fixture · Lahiri · Both node models");
    panel.setInspectionTime(chart.utcDateTime.toMSecsSinceEpoch()); panel.show(); settle(); finish(panel);
    auto* active = panel.findChild<QTableWidget*>("dashaActive");
    auto* transits = panel.findChild<QTableWidget*>("dashaTransits");
    auto* schedule = panel.findChild<QTreeWidget*>("dashaSchedule");
    auto* year = panel.findChild<QComboBox*>("dashaYear");
    require(active->rowCount() == 5 && panel.activeLords() == QStringList{"Mercury"}, "Repeated active lord deduplicated");
    require(transits->rowCount() == 1 && transits->item(0, 1)->text() == "MD / AD / PD / SD / PrD", "All five roles retained");
    require(transits->item(0, 2)->text() == "Gemini" && transits->item(0, 7)->text().startsWith("07 Jul 2026"), "Latest Mercury retrograde re-entry supersedes direct entry");
    require(transits->item(0, 6)->text() != "Unavailable", "Current Moorthi resolves");
    transits->selectRow(0); settle();
    require(panel.findChild<QLabel*>("dashaDetail")->text().contains("Retrograde sign entry"), "Selection exposes entry provenance");
    double actual = 0, expected = 0;
    const double jd = 2440587.5 + chart.utcDateTime.toMSecsSinceEpoch() / 86400000.0;
    require(swe.calcUt(jd, SE_MOON, SEFLG_SIDEREAL, &actual, &error), "Read restored ephemeris");
    swe.setSidMode(siderealAyanamsaSwissMode(input.siderealAyanamsa));
    require(swe.calcUt(jd, SE_MOON, SEFLG_SIDEREAL, &expected, &error) && actual == expected, "Each lookup batch restores global ayanamsa");
    auto* level = panel.findChild<QComboBox*>("dashaChangeLevel"); level->setCurrentIndex(4);
    const qint64 next = active->item(4, 3)->data(Qt::UserRole + 2).toLongLong();
    panel.setInspectionTime((chart.utcDateTime.toMSecsSinceEpoch() + next) / 2);
    panel.findChild<QPushButton*>("dashaPrevious")->click();
    require(panel.inspectionMs() == chart.utcDateTime.toMSecsSinceEpoch(), "Previous change within a period visits the nearest boundary without skipping it");
    panel.findChild<QPushButton*>("dashaNext")->click();
    require(panel.inspectionMs() == next && active->item(4, 1)->text() == "Ketu", "Next Prana navigates to exact exclusive boundary");
    panel.findChild<QPushButton*>("dashaPrevious")->click();
    require(panel.inspectionMs() == chart.utcDateTime.toMSecsSinceEpoch(), "Previous change returns to prior period start");
    panel.findChild<QPushButton*>("dashaCopy")->click();
    require(QApplication::clipboard()->text().contains("365.25-day year") && QApplication::clipboard()->text().contains("Prana\tMercury")
        && QApplication::clipboard()->text().contains("UTC:"), "Copy includes convention, all levels and UTC");
    panel.findChild<QPushButton*>("dashaCopySchedule")->click();
    require(QApplication::clipboard()->text().contains("Expanded schedule") && QApplication::clipboard()->text().contains("PrD · Mercury"), "Copy expanded schedule");
    require(schedule->topLevelItemCount() == 9, "Lazy schedule has nine MD roots");
    year->setCurrentIndex(1);
    require(panel.activeSummary().contains("360-day year") && QSettings().value("vedic/dashaYearDays").toDouble() == 360, "Year choice recalculates and persists");
    panel.findChild<QPushButton*>("dashaStop")->click();
    require(!panel.findChild<QTimer*>()->isActive() && transits->item(0, 6)->text() == "Unavailable", "Stopped lookup never supplies a stale metal");
    chart.bodies[0].longitude = 0; panel.setContext(input, chart, "Node-model fixture");
    panel.setInspectionTime(chart.utcDateTime.toMSecsSinceEpoch()); finish(panel);
    require(panel.activeLords() == QStringList{"Ketu"} && transits->rowCount() == 2
        && transits->item(0, 0)->text() == "Ketu (Mean)" && transits->item(1, 0)->text() == "Ketu (True)", "One Ketu dasha lord, two explicitly labeled transit models");
    // A real Lahiri birth Moon gives a mixed five-level chain for visual review.
    chart.utcDateTime = QDateTime(QDate(1999, 1, 4), QTime(10, 1, 23), QTimeZone::UTC);
    input.date = QDate(1999, 1, 4); input.time = QTime(16, 1, 23); input.name = "Mahir 1999";
    swe.setSidMode(siderealAyanamsaSwissMode(chart.siderealAyanamsa));
    require(swe.calcUt(2440587.5 + chart.utcDateTime.toMSecsSinceEpoch() / 86400000.0, SE_MOON,
        SEFLG_SIDEREAL, &chart.bodies[0].longitude, &error), "Real birth Moon");
    year->setCurrentIndex(0); panel.setContext(input, chart, "Mahir 1999 · 04 Jan 1999 16:01:23 · Asia/Dhaka · Lahiri");
    panel.setInspectionTime(QDateTime(QDate(2026, 9, 13), QTime(16, 0), QTimeZone("Asia/Dhaka")).toMSecsSinceEpoch()); finish(panel); settle();
    require(panel.grab().save(QCoreApplication::applicationDirPath() + "/dashas.png"), "Wide preview");
    panel.resize(1180, 790); settle();
    require(panel.width() == 1180, "Controls allow compact panel width");
    require(panel.grab().save(QCoreApplication::applicationDirPath() + "/dashas_compact.png"), "Compact preview");
    QPalette dark = panel.palette(); dark.setColor(QPalette::Base, QColor("#20242c")); dark.setColor(QPalette::AlternateBase, QColor("#282d37"));
    dark.setColor(QPalette::Window, QColor("#252a32")); dark.setColor(QPalette::Text, Qt::white); dark.setColor(QPalette::WindowText, Qt::white);
    dark.setColor(QPalette::Button, QColor("#353b45")); dark.setColor(QPalette::ButtonText, Qt::white); panel.setPalette(dark); settle();
    require(panel.grab().save(QCoreApplication::applicationDirPath() + "/dashas_dark.png"), "Dark palette preview");
    input.timezone = "America/New_York"; panel.setContext(input, chart, "DST check");
    auto* date = panel.findChild<QDateEdit*>("dashaDate"); auto* time = panel.findChild<QTimeEdit*>("dashaTime");
    require(date->minimumDate() == QDate(1, 1, 1) && date->maximumDate() == QDate(9999, 12, 31), "Calendar controls do not impose an unrelated 1800–2399 restriction");
    date->setDate(QDate(2026, 3, 8)); time->setTime(QTime(2, 30)); panel.findChild<QPushButton*>("dashaCalculate")->click();
    require(active->rowCount() == 0, "Reject manually entered DST gap");
    date->setDate(QDate(2026, 11, 1)); time->setTime(QTime(1, 30)); panel.findChild<QPushButton*>("dashaCalculate")->click();
    require(active->rowCount() == 0, "Reject manually entered DST overlap");
    const qint64 repeated = QDateTime(QDate(2026, 11, 1), QTime(6, 30), QTimeZone::UTC).toMSecsSinceEpoch();
    panel.setInspectionTime(repeated);
    require(panel.inspectionMs() == repeated && active->rowCount() == 5 && time->time() == QTime(1, 30), "UTC navigation retains exact occurrence of repeated hour");
    panel.setContext(input, {}, {});
    require(active->rowCount() == 0 && transits->rowCount() == 0 && !panel.findChild<QTimer*>()->isActive(), "Missing context cancels lookups and clears all results");
    std::cout << "PASS: current ingress, role deduplication, node models, navigation, copy, cancellation, timezone, shared-state and UI checks\n";
}
int main(int argc, char** argv) {
    QApplication app(argc, argv); QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf"); app.setFont(QFont("Segoe UI", 9));
    QCoreApplication::setOrganizationName("DracoVedDashaChecks"); QCoreApplication::setApplicationName("IsolatedChecks");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QCoreApplication::applicationDirPath() + "/settings");
    QSettings().clear();
    try { arithmetic(); widgets(); } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
    return 0;
}
