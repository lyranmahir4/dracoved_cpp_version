#include "zodiacal_releasing.h"

#include "arabic_lots.h"

#include <QtGlobal>
#include <QTimeZone>

#include <algorithm>
#include <array>
#include <cmath>

namespace dracoved {

namespace {

constexpr qint64 kDayMilliseconds = 24LL * 60LL * 60LL * 1000LL;

double normalizedLongitude(double longitude) {
    double value = std::fmod(longitude, 360.0);
    if (value < 0.0) value += 360.0;
    return value;
}

int signForLongitude(double longitude) {
    return qBound(0, static_cast<int>(std::floor(normalizedLongitude(longitude) / 30.0)), 11);
}

QDateTime utcDateTime(qint64 milliseconds) {
    return QDateTime::fromMSecsSinceEpoch(milliseconds, QTimeZone::UTC);
}

QString fortuneRelationship(int signIndex, int fortuneSignIndex, int* house) {
    if (signIndex < 0 || signIndex > 11 || fortuneSignIndex < 0 || fortuneSignIndex > 11) {
        if (house) *house = 0;
        return {};
    }
    const int resolvedHouse = ((signIndex - fortuneSignIndex + 12) % 12) + 1;
    if (house) *house = resolvedHouse;
    switch (resolvedHouse) {
        case 1: return "Major peak - 1st from Fortune";
        case 4: return "Angular - 4th from Fortune";
        case 7: return "Angular - 7th from Fortune";
        case 10: return "Major peak - 10th from Fortune";
        default: return QString("%1%2 from Fortune")
            .arg(resolvedHouse)
            .arg(resolvedHouse == 1 ? "st"
                 : resolvedHouse == 2 ? "nd"
                 : resolvedHouse == 3 ? "rd" : "th");
    }
}

ZodiacalReleasingPeriod makePeriod(
    int level,
    int signIndex,
    int sequenceIndex,
    int parentSignIndex,
    const QString& stableId,
    const QDateTime& startUtc,
    const QDateTime& endUtc,
    bool truncated,
    bool loosingOfBond,
    bool foreshadowing,
    int fortuneSignIndex) {
    ZodiacalReleasingPeriod period;
    period.stableId = stableId;
    period.level = level;
    period.signIndex = signIndex;
    period.signName = zodiacalReleasingSignName(signIndex);
    period.ruler = zodiacalReleasingRuler(signIndex);
    period.startUtc = startUtc;
    period.endUtc = endUtc;
    period.sequenceIndex = sequenceIndex;
    period.parentSignIndex = parentSignIndex;
    period.truncated = truncated;
    period.loosingOfBond = loosingOfBond;
    period.foreshadowing = foreshadowing;
    period.fortuneRelationship = fortuneRelationship(
        signIndex, fortuneSignIndex, &period.fortuneHouse);
    return period;
}

bool containsMoment(const ZodiacalReleasingPeriod& period, const QDateTime& momentUtc) {
    return momentUtc >= period.startUtc && momentUtc < period.endUtc;
}

}  // namespace

QString zodiacalReleasingPointName(ZodiacalReleasingPoint point) {
    switch (point) {
        case ZodiacalReleasingPoint::Spirit: return "Lot of Spirit";
        case ZodiacalReleasingPoint::Fortune: return "Lot of Fortune";
        case ZodiacalReleasingPoint::Eros: return "Lot of Eros";
    }
    return "Unknown Lot";
}

QString zodiacalReleasingTimeKeyName(ZodiacalReleasingTimeKey timeKey) {
    return timeKey == ZodiacalReleasingTimeKey::Calendar3652425
        ? "365.2425-day alternative"
        : "Traditional 360-day year";
}

QString zodiacalReleasingSignName(int signIndex) {
    static const QStringList names = {
        "Aries", "Taurus", "Gemini", "Cancer", "Leo", "Virgo",
        "Libra", "Scorpio", "Sagittarius", "Capricorn", "Aquarius", "Pisces",
    };
    return signIndex >= 0 && signIndex < names.size() ? names[signIndex] : "Unknown";
}

QString zodiacalReleasingSignGlyph(int signIndex) {
    static const QStringList glyphs = {
        QString(QChar(0x2648)), QString(QChar(0x2649)), QString(QChar(0x264A)),
        QString(QChar(0x264B)), QString(QChar(0x264C)), QString(QChar(0x264D)),
        QString(QChar(0x264E)), QString(QChar(0x264F)), QString(QChar(0x2650)),
        QString(QChar(0x2651)), QString(QChar(0x2652)), QString(QChar(0x2653)),
    };
    return signIndex >= 0 && signIndex < glyphs.size() ? glyphs[signIndex] : QString();
}

QString zodiacalReleasingRuler(int signIndex) {
    static const QStringList rulers = {
        "Mars", "Venus", "Mercury", "Moon", "Sun", "Mercury",
        "Venus", "Mars", "Jupiter", "Saturn", "Saturn", "Jupiter",
    };
    return signIndex >= 0 && signIndex < rulers.size() ? rulers[signIndex] : "Unknown";
}

int zodiacalReleasingSignPeriod(int signIndex, int capricornYears) {
    static constexpr std::array<int, 12> periods = {
        15, 8, 20, 25, 19, 20, 8, 15, 12, 27, 30, 12,
    };
    if (signIndex < 0 || signIndex >= static_cast<int>(periods.size())) return 0;
    if (signIndex == 9) return capricornYears == 30 ? 30 : 27;
    return periods[static_cast<std::size_t>(signIndex)];
}

qint64 zodiacalReleasingUnitMilliseconds(
    int level,
    ZodiacalReleasingTimeKey timeKey) {
    if (level < 1 || level > 4) return 0;
    const long double yearDays =
        timeKey == ZodiacalReleasingTimeKey::Calendar3652425 ? 365.2425L : 360.0L;
    long double milliseconds = yearDays * static_cast<long double>(kDayMilliseconds);
    for (int current = 1; current < level; ++current) {
        milliseconds /= 12.0L;
    }
    return static_cast<qint64>(std::llround(milliseconds));
}

ZodiacalReleasingTimeline calculateZodiacalReleasing(
    const ZodiacalReleasingContext& context,
    const ZodiacalReleasingSettings& requestedSettings,
    const QDateTime& rangeEndUtc) {
    ZodiacalReleasingTimeline timeline;
    timeline.context = context;
    timeline.settings = requestedSettings;
    timeline.settings.capricornYears =
        requestedSettings.capricornYears == 30 ? 30 : 27;
    timeline.settings.maximumLevel = qBound(1, requestedSettings.maximumLevel, 4);
    timeline.settings.maximumAge = qBound(1, requestedSettings.maximumAge, 300);

    if (!context.birthUtc.isValid()) {
        timeline.error = "The natal chart does not contain a valid UTC birth moment.";
        return timeline;
    }
    if (!rangeEndUtc.isValid() || rangeEndUtc <= context.birthUtc) {
        timeline.error = "The releasing range must end after the birth moment.";
        return timeline;
    }
    if (!context.hasFortune) {
        timeline.error = "The natal chart does not contain a valid Lot of Fortune.";
        return timeline;
    }
    if (timeline.settings.releasePoint == ZodiacalReleasingPoint::Spirit
        && !context.hasSpirit) {
        timeline.error = "The natal chart does not contain a valid Lot of Spirit.";
        return timeline;
    }
    if (timeline.settings.releasePoint == ZodiacalReleasingPoint::Eros
        && !context.hasEros) {
        timeline.error = "The natal chart does not contain a valid Lot of Eros.";
        return timeline;
    }

    timeline.fortuneSignIndex = signForLongitude(context.fortuneLongitude);
    switch (timeline.settings.releasePoint) {
        case ZodiacalReleasingPoint::Fortune:
            timeline.releaseLongitude = normalizedLongitude(context.fortuneLongitude);
            break;
        case ZodiacalReleasingPoint::Spirit:
            timeline.releaseLongitude = normalizedLongitude(context.spiritLongitude);
            break;
        case ZodiacalReleasingPoint::Eros:
            timeline.releaseLongitude = normalizedLongitude(context.erosLongitude);
            break;
        default:
            timeline.error = "The selected Zodiacal Releasing point is unsupported.";
            return timeline;
    }
    timeline.lotSignIndex = signForLongitude(timeline.releaseLongitude);
    timeline.startSignIndex = timeline.lotSignIndex;
    if (timeline.settings.releasePoint == ZodiacalReleasingPoint::Spirit
        && timeline.settings.applySameSignSpiritRule
        && signForLongitude(context.spiritLongitude) == timeline.fortuneSignIndex) {
        timeline.startSignIndex = (timeline.startSignIndex + 1) % 12;
        timeline.sameSignSpiritAdjustmentApplied = true;
    }
    timeline.rangeEndUtc = rangeEndUtc.toUTC();

    const qint64 levelOneUnit = zodiacalReleasingUnitMilliseconds(
        1, timeline.settings.timeKey);
    QDateTime cursor = context.birthUtc.toUTC();
    int sequenceIndex = 0;
    while (cursor < timeline.rangeEndUtc) {
        const int signIndex = (timeline.startSignIndex + sequenceIndex) % 12;
        const qint64 duration = levelOneUnit
            * zodiacalReleasingSignPeriod(signIndex, timeline.settings.capricornYears);
        const QDateTime naturalEnd = utcDateTime(cursor.toMSecsSinceEpoch() + duration);
        const bool truncated = naturalEnd > timeline.rangeEndUtc;
        const QDateTime end = truncated ? timeline.rangeEndUtc : naturalEnd;
        timeline.levelOnePeriods.push_back(makePeriod(
            1, signIndex, sequenceIndex, -1,
            QString("L1:%1:%2").arg(sequenceIndex).arg(cursor.toMSecsSinceEpoch()),
            cursor, end, truncated, false, false, timeline.fortuneSignIndex));
        cursor = end;
        ++sequenceIndex;
        if (sequenceIndex > 400 || end <= timeline.levelOnePeriods.last().startUtc) {
            timeline.error = "The releasing sequence could not advance safely.";
            timeline.levelOnePeriods.clear();
            return timeline;
        }
    }

    timeline.valid = !timeline.levelOnePeriods.isEmpty();
    if (!timeline.valid && timeline.error.isEmpty()) {
        timeline.error = "No Zodiacal Releasing periods were produced.";
    }
    return timeline;
}

QVector<ZodiacalReleasingPeriod> zodiacalReleasingChildren(
    const ZodiacalReleasingPeriod& parent,
    const ZodiacalReleasingTimeline& timeline) {
    QVector<ZodiacalReleasingPeriod> children;
    if (!timeline.valid || parent.level < 1
        || parent.level >= timeline.settings.maximumLevel
        || !parent.startUtc.isValid() || !parent.endUtc.isValid()
        || parent.endUtc <= parent.startUtc) {
        return children;
    }

    const int level = parent.level + 1;
    const qint64 unit = zodiacalReleasingUnitMilliseconds(
        level, timeline.settings.timeKey);
    if (unit <= 0) return children;

    qint64 fullCircuitDuration = 0;
    for (int offset = 0; offset < 12; ++offset) {
        const int signIndex = (parent.signIndex + offset) % 12;
        fullCircuitDuration += unit
            * zodiacalReleasingSignPeriod(signIndex, timeline.settings.capricornYears);
    }
    const bool hasLoosingOfBond =
        parent.startUtc.toMSecsSinceEpoch() + fullCircuitDuration < parent.endUtc.toMSecsSinceEpoch();

    QDateTime cursor = parent.startUtc;
    int sequenceIndex = 0;
    while (cursor < parent.endUtc) {
        const int signIndex = sequenceIndex < 12
            ? (parent.signIndex + sequenceIndex) % 12
            : (parent.signIndex + 6 + (sequenceIndex - 12)) % 12;
        const qint64 duration = unit
            * zodiacalReleasingSignPeriod(signIndex, timeline.settings.capricornYears);
        const QDateTime naturalEnd = utcDateTime(cursor.toMSecsSinceEpoch() + duration);
        const bool truncated = naturalEnd > parent.endUtc;
        const QDateTime end = truncated ? parent.endUtc : naturalEnd;
        const bool loosing = sequenceIndex == 12 && hasLoosingOfBond;
        const bool foreshadowing = sequenceIndex == 6 && hasLoosingOfBond;
        children.push_back(makePeriod(
            level, signIndex, sequenceIndex, parent.signIndex,
            QString("%1/L%2:%3:%4")
                .arg(parent.stableId)
                .arg(level)
                .arg(sequenceIndex)
                .arg(cursor.toMSecsSinceEpoch()),
            cursor, end, truncated, loosing, foreshadowing,
            timeline.fortuneSignIndex));
        cursor = end;
        ++sequenceIndex;
        if (sequenceIndex > 64 || end <= children.last().startUtc) {
            children.clear();
            return children;
        }
    }
    return children;
}

QVector<ZodiacalReleasingPeriod> zodiacalReleasingActiveChain(
    const ZodiacalReleasingTimeline& timeline,
    const QDateTime& momentUtc) {
    QVector<ZodiacalReleasingPeriod> chain;
    if (!timeline.valid || !momentUtc.isValid()) return chain;
    const QDateTime utc = momentUtc.toUTC();

    ZodiacalReleasingPeriod current;
    bool found = false;
    for (const auto& period : timeline.levelOnePeriods) {
        if (containsMoment(period, utc)) {
            current = period;
            chain.push_back(current);
            found = true;
            break;
        }
    }
    if (!found) return chain;

    while (current.level < timeline.settings.maximumLevel) {
        const auto children = zodiacalReleasingChildren(current, timeline);
        bool childFound = false;
        for (const auto& child : children) {
            if (containsMoment(child, utc)) {
                chain.push_back(child);
                current = child;
                childFound = true;
                break;
            }
        }
        if (!childFound) break;
    }
    return chain;
}

bool zodiacalReleasingSelfCheck(QString* error) {
    auto fail = [error](const QString& message) {
        if (error) *error = message;
        return false;
    };
    const std::array<int, 12> expected = {
        15, 8, 20, 25, 19, 20, 8, 15, 12, 27, 30, 12,
    };
    for (int sign = 0; sign < 12; ++sign) {
        if (zodiacalReleasingSignPeriod(sign, 27)
            != expected[static_cast<std::size_t>(sign)]) {
            return fail(QString("Unexpected period length for sign %1.").arg(sign));
        }
    }
    if (zodiacalReleasingUnitMilliseconds(
            1, ZodiacalReleasingTimeKey::Traditional360)
        != 360LL * kDayMilliseconds
        || zodiacalReleasingUnitMilliseconds(
            2, ZodiacalReleasingTimeKey::Traditional360)
        != 30LL * kDayMilliseconds
        || zodiacalReleasingUnitMilliseconds(
            3, ZodiacalReleasingTimeKey::Traditional360)
        != 60LL * 60LL * 1000LL * 60LL
        || zodiacalReleasingUnitMilliseconds(
            4, ZodiacalReleasingTimeKey::Traditional360)
        != 5LL * 60LL * 60LL * 1000LL) {
        return fail("Traditional level scaling is inconsistent.");
    }
    if (std::fabs(calculateLotOfErosLongitude(10.0, 100.0, 130.0, true) - 40.0) > 1e-9
        || std::fabs(calculateLotOfErosLongitude(10.0, 100.0, 130.0, false) - 340.0) > 1e-9) {
        return fail("The sect-dependent Lot of Eros formula is inconsistent.");
    }

    ZodiacalReleasingContext context;
    context.birthUtc = QDateTime(QDate(2000, 1, 1), QTime(0, 0), QTimeZone::UTC);
    context.fortuneLongitude = 90.0;
    context.spiritLongitude = 90.0;
    context.erosLongitude = 210.0;
    context.hasFortune = true;
    context.hasSpirit = true;
    context.hasEros = true;
    ZodiacalReleasingSettings settings;
    settings.releasePoint = ZodiacalReleasingPoint::Spirit;
    settings.maximumAge = 40;
    const QDateTime end = context.birthUtc.addMSecs(40LL * 365LL * kDayMilliseconds);
    const auto timeline = calculateZodiacalReleasing(context, settings, end);
    if (!timeline.valid || !timeline.sameSignSpiritAdjustmentApplied
        || timeline.startSignIndex != 4) {
        return fail("The Fortune/Spirit same-sign adjustment failed.");
    }

    ZodiacalReleasingSettings fortuneSettings = settings;
    fortuneSettings.releasePoint = ZodiacalReleasingPoint::Fortune;
    const auto fortuneTimeline = calculateZodiacalReleasing(
        context, fortuneSettings, end);
    if (!fortuneTimeline.valid || fortuneTimeline.levelOnePeriods.isEmpty()
        || fortuneTimeline.levelOnePeriods.first().signIndex != 3) {
        return fail("The Fortune releasing start sign failed.");
    }

    ZodiacalReleasingSettings erosSettings = settings;
    erosSettings.releasePoint = ZodiacalReleasingPoint::Eros;
    const auto erosTimeline = calculateZodiacalReleasing(
        context, erosSettings, end);
    if (!erosTimeline.valid || erosTimeline.levelOnePeriods.isEmpty()
        || erosTimeline.levelOnePeriods.first().signIndex != 7
        || erosTimeline.sameSignSpiritAdjustmentApplied) {
        return fail("The Eros releasing start sign failed.");
    }

    const auto children = zodiacalReleasingChildren(
        fortuneTimeline.levelOnePeriods.first(), fortuneTimeline);
    if (children.size() < 13 || children[12].signIndex != 9
        || !children[12].loosingOfBond) {
        return fail("The Loosing of the Bond sequence failed.");
    }
    if (error) error->clear();
    return true;
}

}  // namespace dracoved
