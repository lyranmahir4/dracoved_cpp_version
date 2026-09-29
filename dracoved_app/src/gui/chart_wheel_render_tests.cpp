// Offscreen chart-wheel render check. Draws the real ChartWheelWidget from real
// ephemeris charts into PNGs for visual review. Never touches the app EXEs.
#include "chart_wheel_widget.h"
#include "../core/fixed_stars.h"
#include "../core/swiss_eph.h"
#include "../core/tropical_natal.h"

#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QImage>
#include <iostream>
#include <stdexcept>

using namespace dracoved;

namespace dracoved {
// Reads the wheel's hit-test bookkeeping, which mirrors exactly what was drawn.
struct ChartWheelRenderChecks {
    static const QVector<QString>& scopedNames(const ChartWheelWidget& w) { return w.planetHitScopedNames_; }
    static const QVector<QString>& names(const ChartWheelWidget& w) { return w.planetHitNames_; }
    static const QVector<QRectF>& areas(const ChartWheelWidget& w) { return w.planetHitAreas_; }
};
}  // namespace dracoved

namespace {
void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

// Mirrors MainWindow::buildChartTheme(ThemeMode::Dark).
ChartWheelTheme darkTheme() {
    return ChartWheelTheme{
        QColor("#0f1112"), QColor("#202326"), QColor("#1f2225"), QColor("#24282b"),
        QColor("#1b1f22"), QColor("#6a6f73"), QColor("#2b2f33"), QColor("#2f3337"),
        QColor("#b14040"), QColor("#3a3f44"), QColor("#9aa0a6"), QColor("#23272b"),
        QColor(11, 12, 13, 200), QColor("#c0c0c0"), QColor("#98a9bf"), QColor("#7c7f84"),
        QColor("#e6e6e6"), QColor("#cfd3d6"), QColor("#e6e6e6"),
        QColor("#2a1a0a"), QColor("#0f1f10"), QColor("#0a1525"), QColor("#1a0a25"),
        QColor("#2b2f33"), QColor("#b14040"), QColor("#9aa0a6"), QColor("#191d21"),
        QColor("#15181a"), QColor("#0c0e10"), QColor(150, 170, 190, 46), QColor("#070809"),
        QColor("#121517"), QColor(120, 160, 200, 34), QColor("#4a5157"),
        QColor(16, 18, 20, 200), QColor(70, 78, 85, 120), QColor(90, 98, 105, 120),
        QColor("#e0a94a"), QColor(150, 160, 170, 190),
    };
}

// Mirrors MainWindow::buildChartTheme(ThemeMode::Light).
ChartWheelTheme lightTheme() {
    return ChartWheelTheme{
        QColor("#ffffff"), QColor("#d7d7d7"), QColor("#e2e2e2"), QColor("#dedede"),
        QColor("#e8e8e8"), QColor("#8a8a8a"), QColor("#d0d0d0"), QColor("#cfcfcf"),
        QColor("#5c6470"), QColor("#c0c0c0"), QColor("#6f6f6f"), QColor("#d0d0d0"),
        QColor(255, 255, 255, 235), QColor("#6f7378"), QColor("#6a7a90"), QColor("#7a7e83"),
        QColor("#2b2b2b"), QColor("#2b2b2b"), QColor("#1f1f1f"),
        QColor("#fbf3ec"), QColor("#eef4ed"), QColor("#ebf1f7"), QColor("#f2edf6"),
        QColor("#d0d0d0"), QColor("#b14040"), QColor("#6f6f6f"), QColor("#eef2f7"),
        QColor("#fbfcfd"), QColor("#eef1f4"), QColor(255, 255, 255, 180), QColor("#d2d7dd"),
        QColor("#fcfdfe"), QColor(100, 112, 126, 24), QColor("#b2b9c1"),
        QColor(255, 255, 255, 226), QColor(216, 221, 228, 120), QColor(192, 199, 207, 125),
        QColor("#c98a2c"), QColor(124, 134, 145, 190),
    };
}

NatalChart compute(TropicalNatalEngine& engine, const QDate& date, const QTime& time,
                   const QString& tz, double lat, double lon, HouseSystem houses,
                   LunarNodeMode nodes = LunarNodeMode::MeanOnly) {
    NatalInput input;
    input.name = "Wheel check";
    input.date = date;
    input.time = time;
    input.timezone = tz;
    input.latitude = lat;
    input.longitude = lon;
    input.houseSystem = houses;
    input.lunarNodePolicy.mode = nodes;
    input.fixedStars = defaultFixedStars();
    NatalChart chart;
    QString error;
    require(engine.compute(input, &chart, &error), "Chart calculation failed: " + error.toStdString());
    return chart;
}

// Mirrors MainWindow::applyChartReadabilityPreset(Standard), with Lots off as in
// the user's usual view. Part of Fortune stays on (the app default).
void applyStandardPreset(ChartWheelWidget& wheel, bool lots = false) {
    wheel.setShowAspects(true);
    wheel.setShowTicks(true);
    wheel.setTickDensity(ChartWheelWidget::TickDensity::Medium);
    wheel.setShowDegrees(true);
    wheel.setShowAspectSymbols(false);
    wheel.setAspectDisplayMaxOrb(4.0);
    wheel.setOverlayAspectScopes(true, false, false);
    wheel.setShowLots(lots);
    wheel.setShowPartOfFortune(true);
}

// A rendered wheel must contain real ink, not just background.
void render(ChartWheelWidget& wheel, const QString& path, int size) {
    wheel.resize(size, size);
    const QImage image = wheel.grab().toImage();
    require(!image.isNull(), "Grab failed: " + path.toStdString());
    const QRgb background = image.pixel(2, 2);
    int ink = 0;
    for (int y = 0; y < image.height(); y += 4)
        for (int x = 0; x < image.width(); x += 4)
            if (image.pixel(x, y) != background) ++ink;
    require(ink > 500, "Wheel rendered almost nothing: " + path.toStdString());
    require(image.save(path), "Could not save " + path.toStdString());
    std::cout << "  wrote " << path.toStdString() << "\n";
}

// The four angles must always be drawn and clickable (a standing product rule).
void requireAngles(const ChartWheelWidget& wheel, const QString& prefix, const std::string& where) {
    const auto& scoped = ChartWheelRenderChecks::scopedNames(wheel);
    for (const QString& angle : {"Ascendant", "Midheaven", "Descendant", "IC"}) {
        require(scoped.contains(prefix + angle),
                where + ": missing angle " + (prefix + angle).toStdString());
    }
}

// In the label-stack layout no two body glyphs may overlap.
void requireNoGlyphOverlap(const ChartWheelWidget& wheel, const QString& prefix, const std::string& where) {
    const auto& scoped = ChartWheelRenderChecks::scopedNames(wheel);
    const auto& areas = ChartWheelRenderChecks::areas(wheel);
    QVector<QRectF> lane;
    for (int i = 0; i < scoped.size(); ++i) {
        if (!scoped[i].isEmpty() && scoped[i].startsWith(prefix)) lane.push_back(areas[i].adjusted(3, 3, -3, -3));
    }
    for (int i = 0; i < lane.size(); ++i)
        for (int j = i + 1; j < lane.size(); ++j)
            require(!lane[i].intersects(lane[j]), where + ": two glyphs overlap");
}
}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/seguisym.ttf");
    app.setFont(QFont("Segoe UI", 9));
    try {
        SwissEph swe;
        QString error;
        require(swe.load({"swedll64.dll"}, &error), "Load Swiss Ephemeris: " + error.toStdString());
        TropicalNatalEngine engine(&swe, "ephe");

        const QString out = QDir::current().filePath(argc > 1 ? argv[1] : "dracoved_app/build/chart_wheel_checks");
        QDir().mkpath(out);

        // Ordinary spread chart, Placidus.
        const NatalChart natal = compute(engine, QDate(1996, 4, 16), QTime(21, 30), "America/New_York",
                                         25.7743, -80.1937, HouseSystem::Placidus);
        // Dense stellium: seven bodies in Aquarius (4 Feb 1962), Whole Sign.
        const NatalChart dense = compute(engine, QDate(1962, 2, 4), QTime(12, 0), "Asia/Kolkata",
                                         28.6139, 77.2090, HouseSystem::WholeSign, LunarNodeMode::Both);
        const NatalChart transit = compute(engine, QDate(2026, 9, 28), QTime(10, 0), "America/New_York",
                                           25.7743, -80.1937, HouseSystem::Placidus);

        ChartWheelWidget wheel;
        applyStandardPreset(wheel);
        wheel.setTheme(lightTheme());
        wheel.setChart(natal, HouseSystem::Placidus);
        require(wheel.mode() == ChartWheelWidget::Mode::NatalOnly, "Natal mode");
        render(wheel, out + "/natal_light.png", 900);
        requireAngles(wheel, "", "natal");
        requireNoGlyphOverlap(wheel, "", "natal");
        require(ChartWheelRenderChecks::names(wheel).contains("Sun")
                && ChartWheelRenderChecks::names(wheel).contains("Pluto"), "natal: planets drawn");

        wheel.setTheme(darkTheme());
        render(wheel, out + "/natal_dark.png", 900);

        wheel.setTheme(lightTheme());
        wheel.setChart(dense, HouseSystem::WholeSign);
        render(wheel, out + "/dense_light.png", 900);
        requireAngles(wheel, "", "dense stellium");
        requireNoGlyphOverlap(wheel, "", "dense stellium");
        require(ChartWheelRenderChecks::names(wheel).contains("Mean North Node")
                || ChartWheelRenderChecks::names(wheel).contains("True North Node"), "dense: both node models drawn");

        // Stress case: every Arabic Lot visible (the app's default setting).
        applyStandardPreset(wheel, true);
        render(wheel, out + "/dense_lots_light.png", 900);
        applyStandardPreset(wheel);

        wheel.setOverlayCharts(natal, transit, HouseSystem::Placidus, defaultAspectOrbs());
        wheel.setAspectDisplayMaxOrb(3.0);
        require(wheel.mode() == ChartWheelWidget::Mode::Overlay, "Overlay mode");
        render(wheel, out + "/overlay_light.png", 900);
        requireAngles(wheel, "Natal ", "overlay");
        requireNoGlyphOverlap(wheel, "Natal ", "overlay inner ring");
        requireNoGlyphOverlap(wheel, "Transit ", "overlay outer ring");
        require(ChartWheelRenderChecks::scopedNames(wheel).contains("Transit Moon")
                && ChartWheelRenderChecks::scopedNames(wheel).contains("Natal Moon"),
                "overlay: scope-qualified hit names kept");

        wheel.setTheme(darkTheme());
        render(wheel, out + "/overlay_dark.png", 900);

        // Synastry-style: outer angles on, partner label.
        wheel.setTheme(lightTheme());
        wheel.setOverlayCharts(natal, dense, HouseSystem::Placidus, defaultAspectOrbs());
        wheel.setBaseLabel("Anna");
        wheel.setOverlayLabel("Ben");
        wheel.setOverlayAnglesVisible(true);
        render(wheel, out + "/synastry_light.png", 900);
        requireAngles(wheel, "Anna ", "synastry inner");
        // Outer angles use the overlay label, as Lunar Return's check expects.
        requireAngles(wheel, "Ben ", "synastry outer");

        // Small embedded preview (astrocartography/relocation) must not clip.
        // Mirrors MainWindow's astrocartography preview wheel settings.
        ChartWheelWidget preview;
        preview.setShowAspects(false);
        preview.setShowLots(false);
        preview.setShowPartOfFortune(false);
        preview.setShowDerivedPoints(false);
        preview.setShowFixedStars(false);
        preview.setShowAsteroids(false);
        preview.setTickDensity(ChartWheelWidget::TickDensity::Minimal);
        preview.setFontScale(0.72);
        preview.setZoom(0.94);
        preview.setTheme(lightTheme());
        preview.setChart(natal, HouseSystem::Placidus);
        render(preview, out + "/preview_small.png", 300);
        requireAngles(preview, "", "small preview");

        // Larger text setting on the main wheel.
        ChartWheelWidget large;
        applyStandardPreset(large);
        large.setTheme(lightTheme());
        large.setChart(natal, HouseSystem::Placidus);
        large.setFontScale(1.25);
        render(large, out + "/natal_font125.png", 900);

        // Clean preset (no degrees) must keep the original glyph-only layout.
        large.setFontScale(1.0);
        large.setShowDegrees(false);
        large.setTickDensity(ChartWheelWidget::TickDensity::Minimal);
        large.setAspectDisplayMaxOrb(2.0);
        render(large, out + "/natal_clean.png", 900);
        requireAngles(large, "", "clean preset");

        std::cout << "Chart wheel render checks passed.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << "\n";
        return 1;
    }
}
