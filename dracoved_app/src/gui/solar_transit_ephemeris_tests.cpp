#include "solar_transit_types.h"
#include "return_calculation_service.h"
#include "transit_calc_service.h"
#include "../core/swiss_eph.h"
#include "../core/formatting.h"
#include <QCoreApplication>
#include <QDir>
#include <QTimeZone>
#include <iostream>
#include <stdexcept>

using namespace dracoved;
static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static double jd(SwissEph& swe, const QDateTime& utc) {
    return swe.julianDay(utc.date().year(), utc.date().month(), utc.date().day(), utc.time().msecsSinceStartOfDay()/3600000.0, SE_GREG_CAL);
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        SwissEph swe; QString error;
        require(swe.load({QDir::current().absoluteFilePath("swedll64.dll")}, &error), "Load Swiss DLL from project root");
        swe.setEphePath(QDir::current().absoluteFilePath("ephe"));
        const QTimeZone zone("Asia/Dhaka");
        require(zone.isValid(), "Dhaka timezone available");
        for (int mode = 0; mode < 3; ++mode) {
            SolarTransitSource source;
            source.dllPath = swe.loadedPath(); source.ephePath = QDir::current().absoluteFilePath("ephe");
            source.tajaka = mode == 2; source.returnYear = 2026;
            source.natalInput.date = QDate(1999, 1, 4); source.natalInput.time = QTime(16, 1);
            source.natalInput.timezone = "Asia/Dhaka";
            source.natalInput.latitude = 23.764; source.natalInput.longitude = 90.389;
            source.natalInput.houseSystem = mode == 1 ? HouseSystem::Placidus : HouseSystem::WholeSign;
            source.natalInput.zodiacSystem = mode ? ZodiacSystem::Sidereal : ZodiacSystem::Tropical;
            source.natalInput.siderealAyanamsa = SiderealAyanamsa::Lahiri;
            swe.setSidMode(siderealAyanamsaSwissMode(source.natalInput.siderealAyanamsa));
            const int flags = mode ? SEFLG_SIDEREAL : 0;
            const auto natalUtc = QDateTime(source.natalInput.date, source.natalInput.time, zone).toUTC();
            require(swe.calcUt(jd(swe, natalUtc), SE_SUN, source.tajaka ? 0 : flags, &source.natalSunLongitude, &error), "Natal solar target");
            const auto returnTime = source.tajaka ? returncalc::tajakaSolarReturnTimeUtc : returncalc::solarReturnTimeUtc;
            require(returnTime(swe, source.natalInput, 2026, "Asia/Dhaka", source.natalSunLongitude,
                &source.returnChart.utcDateTime, &source.returnChart.localDateTime, &error, {}), "Compute loaded return");
            source.returnInput = source.natalInput;
            source.returnInput.date = source.returnChart.localDateTime.date(); source.returnInput.time = source.returnChart.localDateTime.time();
            source.returnChart.timezoneLabel = "Asia/Dhaka";
            source.returnChart.zodiacSystem = source.returnInput.zodiacSystem;
            source.returnChart.siderealAyanamsa = source.returnInput.siderealAyanamsa;
            swe.setSidMode(siderealAyanamsaSwissMode(source.returnInput.siderealAyanamsa));
            const double startJd = jd(swe, source.returnChart.utcDateTime);
            double cusp[13] = {}, angles[10] = {};
            require(swe.housesEx(startJd, flags, 23.764, 90.389, 'P', cusp, angles, &error), "Return houses");
            source.returnChart.angles.asc = angles[0]; source.returnChart.angles.mc = angles[1];
            for (int h = 1; h <= 12; ++h) source.returnChart.cusps.push_back({h, cusp[h], {}});
            double venus = 0, pluto = 0, sun = 0;
            require(swe.calcUt(startJd, SE_SUN, flags, &sun, &error), "Return Sun");
            require(swe.calcUt(startJd, SE_VENUS, flags, &venus, &error), "Return Venus");
            require(swe.calcUt(startJd, SE_PLUTO, flags, &pluto, &error), "Return Pluto");
            SolarTransitQuery query; query.source = source; query.bodies = {"Sun", "Mercury", "Pluto"};
            query.targets = {{"Venus", venus}, {"Pluto", pluto}, {"Sun", sun}}; query.aspects = {0, 90}; query.orb = 1; query.houses = true;
            swe.setSidMode(0);
            double beforeMode = 0, afterMode = 0;
            require(swe.calcUt(startJd, SE_MOON, SEFLG_SIDEREAL, &beforeMode, &error), "Main ephemeris baseline");
            const auto result = calculateSolarTransits(query, [] { return false; }, [](int, const QString&) {});
            require(swe.calcUt(startJd, SE_MOON, SEFLG_SIDEREAL, &afterMode, &error), "Main ephemeris after worker");
            require(std::abs(beforeMode - afterMode) < 1e-10, "Worker must not change another ephemeris instance's zodiac state");
            swe.setSidMode(siderealAyanamsaSwissMode(source.returnInput.siderealAyanamsa));
            if (!result.error.isEmpty()) throw std::runtime_error(result.error.toStdString());
            require(!result.cancelled && !result.events.isEmpty(), "Computed contacts");
            require(result.startUtc == source.returnChart.utcDateTime, "Uses exact loaded return as start");
            require(result.startUtc.daysTo(result.endUtc) >= 364 && result.startUtc.daysTo(result.endUtc) <= 367, "Return-year span");
            double endSun = 0;
            require(swe.calcUt(jd(swe, result.endUtc), SE_SUN, source.tajaka ? 0 : flags, &endSun, &error), "Next return Sun");
            require(std::abs(search_events::signedAngle(endSun - source.natalSunLongitude)) < 2e-5, "End is next exact solar return");
            int exact = 0, houses = 0, solarConjunctions = 0;
            for (const auto& event : result.events) {
                require(event.utc >= result.startUtc && event.utc <= result.endUtc, "Event respects year");
                double position[6] = {};
                require(swe.calcUtFull(jd(swe, event.utc), transitcalc::bodyIdForName(event.body), flags | SEFLG_SPEED, position, &error), "Event position");
                if (event.exact) {
                    ++exact;
                    if (event.body == "Sun" && event.target == "Sun" && event.aspect == 0) {
                        ++solarConjunctions;
                        std::cout << "Solar boundary contact: " << event.utc.toString(Qt::ISODateWithMs).toStdString()
                            << " (" << event.utc.msecsTo(result.endUtc) << "ms before end)\n";
                    }
                    require(search_events::orb(position[0] - event.targetLongitude, event.aspect) < 2e-5, "Exact contact verified independently");
                    require(event.utc < result.endUtc && event.pass >= 1 && event.pass <= event.totalPasses, "Exact pass metadata");
                }
                if (event.houseCrossing) {
                    ++houses;
                    require(std::abs(search_events::signedAngle(position[0] - event.targetLongitude)) < 2e-5, "House cusp crossing verified");
                    auto independentHouse = [&](double longitude) {
                        if (source.returnInput.houseSystem == HouseSystem::WholeSign)
                            return (int(std::floor(longitude/30)) - int(std::floor(angles[0]/30)) + 12) % 12 + 1;
                        for (int h = 1; h <= 12; ++h) {
                            const double offset = search_events::wrap(longitude - cusp[h]);
                            const double width = search_events::wrap(cusp[h == 12 ? 1 : h + 1] - cusp[h]);
                            if (offset < width) return h;
                        }
                        return 0;
                    };
                    double before = 0, after = 0;
                    require(swe.calcUt(jd(swe, event.utc.addSecs(-2)), transitcalc::bodyIdForName(event.body), flags, &before, &error), "Before house boundary");
                    require(swe.calcUt(jd(swe, event.utc.addSecs(2)), transitcalc::bodyIdForName(event.body), flags, &after, &error), "After house boundary");
                    require(event.target == QString("House %1 → %2").arg(independentHouse(before)).arg(independentHouse(after)), "Chronological house labels verified");
                }
            }
            require(exact > 2 && houses > 0, "Exact and house events found");
            require(solarConjunctions == 1, "Return Sun starts exact; next-return boundary must not duplicate it");
            for (const auto& window : result.windows) {
                const auto mid = window.startUtc.addMSecs(window.startUtc.msecsTo(window.endUtc)/2);
                double longitude = 0;
                require(swe.calcUt(jd(swe, mid), transitcalc::bodyIdForName(window.body), flags, &longitude, &error), "Orb window midpoint");
                const double target = window.target == "Venus" ? venus : window.target == "Sun" ? sun : pluto;
                require(search_events::orb(longitude - target, window.aspect) <= query.orb + 1e-5, "Window interior within orb");
            }
            const auto stopped = calculateSolarTransits(query, [] { return true; }, [](int, const QString&) {});
            require(stopped.cancelled && stopped.events.isEmpty(), "Cancelled calculation not partial");
            int checks = 0;
            const auto interrupted = calculateSolarTransits(query, [&checks] { return ++checks > 7000; }, [](int, const QString&) {});
            require(interrupted.cancelled && interrupted.events.isEmpty() && interrupted.windows.isEmpty(), "Mid-search cancellation discards partial periods");
            std::cout << "PASS mode " << mode << ": " << exact << " exact hits, " << houses << " house crossings, " << result.windows.size() << " windows\n";
        }
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
