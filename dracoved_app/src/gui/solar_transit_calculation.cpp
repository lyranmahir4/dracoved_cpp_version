#include "solar_transit_types.h"
#include "return_calculation_service.h"
#include "transit_calc_service.h"
#include "../core/formatting.h"
#include "../core/tropical_natal.h"
#include "../core/timezone_utils.h"
#include "../core/swiss_eph.h"
#include <QFile>
#include <QTemporaryDir>
#include <map>
#ifdef _WIN32
#include <windows.h>
#endif

namespace dracoved {
namespace {

// Swiss Ephemeris has mutable library state. A separate module image keeps
// background research independent of other tabs changing zodiac/settings.
class IsolatedEphemeris {
public:
    QTemporaryDir directory;
    SwissEph swe;
    bool load(const SolarTransitSource& source, QString* error) {
        const QString path = directory.filePath("solar_transit_ephemeris.dll");
        if (!directory.isValid() || !QFile::copy(source.dllPath, path)) {
            *error = "Unable to create an isolated ephemeris instance in the temporary folder.";
            return false;
        }
        if (!swe.load({path}, error)) return false;
        swe.setEphePath(source.ephePath);
        if (source.returnChart.zodiacSystem == ZodiacSystem::Sidereal)
            swe.setSidMode(siderealAyanamsaSwissMode(source.returnChart.siderealAyanamsa));
        return true;
    }
    ~IsolatedEphemeris() {
#ifdef _WIN32
        if (swe.isLoaded()) {
            const auto handle = GetModuleHandleW(swe.loadedPath().toStdWString().c_str());
            if (handle) {
                const auto close = reinterpret_cast<void (*)()>(GetProcAddress(handle, "swe_close"));
                if (close) close();
            }
        }
#endif
    }
};

double julianDay(SwissEph& swe, const QDateTime& utc) {
    return swe.julianDay(utc.date().year(), utc.date().month(), utc.date().day(),
        utc.time().msecsSinceStartOfDay() / 3600000.0, SE_GREG_CAL);
}

QString kindLabel(solar_transits::Kind kind) {
    using K = solar_transits::Kind;
    switch (kind) {
    case K::Exact: return "Exact contact";
    case K::Entry: return "Orb entry";
    case K::Exit: return "Orb exit";
    case K::NearMiss: return "Near miss at station";
    case K::StartProximity: return "Already within orb";
    case K::EndApproach: return "Approaching at year end";
    case K::Station: return "Station within orb";
    }
    return {};
}

} // namespace

QString solarTransitAspectLabel(int angle) {
    switch (angle) {
    case 0: return QString(QChar(0x260C)) + " Conjunction";
    case 60: return QString(QChar(0x2736)) + " Sextile";
    case 90: return QString(QChar(0x25A1)) + " Square";
    case 120: return QString(QChar(0x25B3)) + " Trine";
    case 180: return QString(QChar(0x260D)) + " Opposition";
    }
    return QString::number(angle);
}

SolarTransitResult calculateSolarTransits(const SolarTransitQuery& query,
    const std::function<bool()>& cancelled, const std::function<void(int, const QString&)>& progress) {
    SolarTransitResult output;
    output.query = query;
    struct Cancelled {};
    auto check = [&]() { if (cancelled()) throw Cancelled{}; };
    try {
        check();
        if (!query.source.returnChart.utcDateTime.isValid() || query.bodies.isEmpty()
            || query.orb <= 0 || query.orb > 15) throw QString("Invalid return chart, moving bodies, or orb.");
        IsolatedEphemeris isolated;
        if (!isolated.load(query.source, &output.error)) return output;
        auto& swe = isolated.swe;
        progress(0, "Finding the next solar return...");
        const auto& source = query.source;
        const auto returnTime = source.tajaka ? returncalc::tajakaSolarReturnTimeUtc : returncalc::solarReturnTimeUtc;
        if (!returnTime(swe, source.natalInput, source.returnYear + 1, source.returnInput.timezone,
                        source.natalSunLongitude, &output.endUtc, nullptr, &output.error, cancelled)) {
            check();
            if (output.error.isEmpty()) output.error = "Unable to calculate the next solar return.";
            return output;
        }
        // The shared return solver stops at whole-second precision. Refine
        // this half-open boundary so numerical rounding cannot count the
        // following year's solar contact as a second contact in this year.
        const double coarseEnd = julianDay(swe, output.endUtc);
        const int returnFlags = !source.tajaka && source.natalInput.zodiacSystem == ZodiacSystem::Sidereal ? SEFLG_SIDEREAL : 0;
        if (returnFlags) swe.setSidMode(siderealAyanamsaSwissMode(source.natalInput.siderealAyanamsa));
        auto solarDifference = [&](double day) {
            check();
            double longitude = 0; QString error;
            if (!swe.calcUt(day, SE_SUN, returnFlags, &longitude, &error)) throw error;
            return search_events::signedAngle(longitude - source.natalSunLongitude);
        };
        const double lower = coarseEnd - 120.0 / 86400.0, upper = coarseEnd + 120.0 / 86400.0;
        if (solarDifference(lower) * solarDifference(upper) > 0)
            throw QString("Unable to refine the next solar return boundary.");
        const double refinedEnd = search_events::root(lower, upper, solarDifference, check);
        output.endUtc = output.endUtc.addMSecs(qRound64((refinedEnd - coarseEnd) * 86400000.0));
        output.startUtc = source.returnChart.utcDateTime.toUTC();
        const double start = julianDay(swe, output.startUtc);
        const double end = julianDay(swe, output.endUtc);
        if (end - start < 300 || end - start > 400)
            throw QString("The next return is not one year after the loaded chart. Recalculate the Solar Return first.");
        // The return-time method can use a different zodiac (Tajaka).
        if (source.returnChart.zodiacSystem == ZodiacSystem::Sidereal)
            swe.setSidMode(siderealAyanamsaSwissMode(source.returnChart.siderealAyanamsa));
        const int flags = SEFLG_SPEED | (source.returnChart.zodiacSystem == ZodiacSystem::Sidereal ? SEFLG_SIDEREAL : 0);
        auto time = [&](double jd) { return output.startUtc.addMSecs(qRound64((jd - start) * 86400000.0)); };
        const int total = std::max(1, int(query.bodies.size() * (query.targets.size() * query.aspects.size() + (query.houses ? 12 : 0))));
        int completed = 0;
        for (const QString& name : query.bodies) {
            check();
            const int id = transitcalc::bodyIdForName(name, effectivePrimaryNodeType(source.returnChart.lunarNodePolicy));
            if (id < 0) { output.warnings << "Unsupported moving body: " + name; continue; }
            std::map<double, search_events::Position> cache;
            const search_events::Ephemeris body = [&](double jd) {
                check();
                const auto cached = cache.find(jd);
                if (cached != cache.end()) return cached->second;
                double xx[6] = {};
                QString error;
                if (!swe.calcUtFull(jd, id, flags, xx, &error)) throw QString("%1: %2").arg(name, error);
                double lon = xx[0];
                if (isLunarNodeName(name) && !isNorthLunarNodeName(name)) lon += 180;
                const search_events::Position position{search_events::wrap(lon), xx[3]};
                cache.emplace(jd, position);
                return position;
            };
            // Probe the full interval before committing any results for this
            // body, so missing asteroid data cannot silently truncate its year.
            try {
                for (double day = start; day < end; day += .25) body(day);
                body(end);
            } catch (const QString& error) {
                output.warnings << "Skipped " + error;
                completed += query.targets.size() * query.aspects.size() + (query.houses ? 12 : 0);
                continue;
            }
            for (const auto& target : query.targets) {
                for (int aspect : query.aspects) {
                    check();
                    ++output.searchedPairs;
                    const auto analysis = solar_transits::analyze(body, target.longitude, aspect, start, end, query.orb, check);
                    if (analysis.contacts.empty() && analysis.windows.empty()) ++output.inactivePairs;
                    int passCount = 0;
                    for (const auto& hit : analysis.contacts)
                        if (hit.kind == solar_transits::Kind::Exact) ++passCount;
                    int pass = 0;
                    for (const auto& hit : analysis.contacts) {
                        SolarTransitEvent event;
                        event.utc = time(hit.time);
                        event.body = name;
                        event.target = target.name;
                        event.targetLongitude = target.longitude;
                        event.aspect = aspect;
                        event.kind = kindLabel(hit.kind);
                        event.orb = hit.orb;
                        event.speed = hit.speed;
                        event.exact = hit.kind == solar_transits::Kind::Exact;
                        if (event.exact) {
                            event.pass = ++pass;
                            event.totalPasses = passCount;
                            if (std::abs(hit.time - start) < 1e-6) event.kind = "Exact at year start";
                        }
                        if (hit.kind == solar_transits::Kind::NearMiss)
                            event.note = "Local minimum at a planetary station; the aspect does not become exact on this approach.";
                        else if (hit.kind == solar_transits::Kind::EndApproach)
                            event.note = "Boundary measurement at the next return. The planet is still approaching; this is not a completed near miss.";
                        else if (hit.kind == solar_transits::Kind::Station)
                            event.note = body(hit.time + .01).speed < 0 ? "Turns retrograde." : "Turns direct.";
                        else if (hit.kind == solar_transits::Kind::StartProximity) {
                            for (const auto& window : analysis.windows) {
                                if (std::abs(window.start - start) < 1e-6) {
                                    event.untilUtc = time(window.end);
                                    if (window.clippedEnd) event.kind = "Within orb all year";
                                    break;
                                }
                            }
                            const double laterOrb = search_events::orb(body(start + .01).longitude - target.longitude, aspect);
                            event.note = QString("Proximity already present at the start of the return year; %1.")
                                .arg(laterOrb < hit.orb ? "applying" : laterOrb > hit.orb ? "separating" : "nearly unchanged");
                        }
                        output.events.push_back(event);
                    }
                    for (const auto& window : analysis.windows) {
                        output.windows.push_back({time(window.start), time(window.end), name, target.name,
                                                  aspect, window.clippedStart, window.clippedEnd});
                        const bool startsExact = search_events::orb(body(start).longitude - target.longitude, aspect) <= 1e-6;
                        if (window.clippedStart && window.clippedEnd && startsExact) {
                            SolarTransitEvent event;
                            event.utc = output.startUtc; event.untilUtc = output.endUtc;
                            event.body = name; event.target = target.name; event.targetLongitude = target.longitude;
                            event.aspect = aspect; event.kind = "Within orb all year";
                            event.orb = search_events::orb(body(start).longitude - target.longitude, aspect);
                            event.speed = body(start).speed;
                            event.note = "Begins exact and stays within the selected orb throughout the return year.";
                            output.events.push_back(event);
                        }
                    }
                    ++completed;
                    progress(completed * 100 / total, QString("%1 → SR %2 · %3").arg(name, target.name, solarTransitAspectLabel(aspect)));
                }
            }
            if (query.houses) {
                QVector<HouseCusp> cusps = source.returnChart.cusps;
                if (source.returnInput.houseSystem == HouseSystem::WholeSign) {
                    cusps.clear();
                    const double ascSign = std::floor(source.returnChart.angles.asc / 30) * 30;
                    for (int house = 1; house <= 12; ++house)
                        cusps.push_back({house, search_events::wrap(ascSign + (house - 1) * 30), {}});
                }
                if (cusps.size() != 12) throw QString("The return chart does not contain 12 valid house cusps.");
                auto houseAt = [&](double t) {
                    return transitcalc::calcHouseForLongitude(body(t).longitude, cusps,
                        source.returnChart.angles.asc, source.returnInput.houseSystem);
                };
                for (const auto& cusp : cusps) {
                    const search_events::Ephemeris fixed = [cusp](double) { return search_events::Position{cusp.longitude, 0}; };
                    search_events::aspects(body, fixed, start, end, 0, 0, true, check, [&](double t, double, int) {
                        if (t >= end - 1e-6) return;
                        const int before = houseAt(t - 1e-4), after = houseAt(t + 1e-4);
                        if (before <= 0 || after <= 0 || before == after) return;
                        SolarTransitEvent event;
                        event.utc = time(t);
                        event.body = name;
                        event.target = QString("House %1 → %2").arg(before).arg(after);
                        event.targetLongitude = cusp.longitude;
                        event.kind = "House crossing";
                        event.houseCrossing = true;
                        event.speed = body(t).speed;
                        event.note = QString("Leaves SR House %1 and enters SR House %2 (%3).")
                            .arg(before).arg(after).arg(source.returnInput.houseSystem == HouseSystem::Placidus ? "Placidus" : "Whole Sign");
                        output.events.push_back(event);
                    });
                    ++completed;
                    progress(completed * 100 / total, name + " · return house boundaries");
                }
            }
        }
        check();
        std::stable_sort(output.events.begin(), output.events.end(), [](const auto& a, const auto& b) { return a.utc < b.utc; });
    } catch (const Cancelled&) {
        output.cancelled = true;
        output.events.clear();
        output.windows.clear();
    } catch (const QString& error) {
        output.error = error.isEmpty() ? "Ephemeris calculation failed." : error;
        output.events.clear();
        output.windows.clear();
    }
    return output;
}

bool computeSolarTransitMoment(const SolarTransitSource& source, const QDateTime& utc,
                              NatalChart* chart, QString* error) {
    IsolatedEphemeris isolated;
    if (!isolated.load(source, error)) return false;
    QTimeZone zone;
    QString label;
    if (!parseTimezoneInput(source.returnInput.timezone, &zone, &label, error)) return false;
    const auto local = utc.toTimeZone(zone);
    NatalInput input = source.returnInput;
    input.date = local.date();
    input.time = local.time();
    input.name = "Transit to Solar Return";
    TropicalComputeOptions options;
    options.includeArabicLots = false;
    options.includeFixedStars = false;
    TropicalNatalEngine engine(&isolated.swe, source.ephePath);
    return engine.compute(input, options, chart, error);
}

} // namespace dracoved
