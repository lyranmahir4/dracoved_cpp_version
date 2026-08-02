#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

namespace dracoved {

enum class ZodiacalReleasingPoint {
    Spirit,
    Fortune,
    Eros,
};

enum class ZodiacalReleasingTimeKey {
    Traditional360,
    Calendar3652425,
};

struct ZodiacalReleasingSettings {
    ZodiacalReleasingPoint releasePoint = ZodiacalReleasingPoint::Spirit;
    ZodiacalReleasingTimeKey timeKey = ZodiacalReleasingTimeKey::Traditional360;
    int capricornYears = 27;
    bool applySameSignSpiritRule = true;
    int maximumLevel = 4;
    int maximumAge = 120;
};

struct ZodiacalReleasingContext {
    QDateTime birthUtc;
    QString timezone;
    double fortuneLongitude = 0.0;
    double spiritLongitude = 0.0;
    double erosLongitude = 0.0;
    bool hasFortune = false;
    bool hasSpirit = false;
    bool hasEros = false;
    bool isDayChart = false;
};

struct ZodiacalReleasingPeriod {
    QString stableId;
    int level = 1;
    int signIndex = -1;
    QString signName;
    QString ruler;
    QDateTime startUtc;
    QDateTime endUtc;
    int sequenceIndex = 0;
    int parentSignIndex = -1;
    bool truncated = false;
    bool loosingOfBond = false;
    bool foreshadowing = false;
    int fortuneHouse = 0;
    QString fortuneRelationship;
};

struct ZodiacalReleasingTimeline {
    bool valid = false;
    QString error;
    ZodiacalReleasingContext context;
    ZodiacalReleasingSettings settings;
    int fortuneSignIndex = -1;
    int lotSignIndex = -1;
    int startSignIndex = -1;
    double releaseLongitude = 0.0;
    bool sameSignSpiritAdjustmentApplied = false;
    QDateTime rangeEndUtc;
    QVector<ZodiacalReleasingPeriod> levelOnePeriods;
};

QString zodiacalReleasingPointName(ZodiacalReleasingPoint point);
QString zodiacalReleasingTimeKeyName(ZodiacalReleasingTimeKey timeKey);
QString zodiacalReleasingSignName(int signIndex);
QString zodiacalReleasingSignGlyph(int signIndex);
QString zodiacalReleasingRuler(int signIndex);
int zodiacalReleasingSignPeriod(int signIndex, int capricornYears = 27);
qint64 zodiacalReleasingUnitMilliseconds(int level, ZodiacalReleasingTimeKey timeKey);

ZodiacalReleasingTimeline calculateZodiacalReleasing(
    const ZodiacalReleasingContext& context,
    const ZodiacalReleasingSettings& settings,
    const QDateTime& rangeEndUtc);

QVector<ZodiacalReleasingPeriod> zodiacalReleasingChildren(
    const ZodiacalReleasingPeriod& parent,
    const ZodiacalReleasingTimeline& timeline);

QVector<ZodiacalReleasingPeriod> zodiacalReleasingActiveChain(
    const ZodiacalReleasingTimeline& timeline,
    const QDateTime& momentUtc);

bool zodiacalReleasingSelfCheck(QString* error = nullptr);

}  // namespace dracoved
