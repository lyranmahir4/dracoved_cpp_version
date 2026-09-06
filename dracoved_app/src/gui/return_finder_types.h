#pragma once

#include "../core/chart_types.h"

#include <QDate>
#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace dracoved {

enum class ReturnFinderType {
    Solar,
    Lunar,
};

enum class ReturnFinderHouseMode {
    WholeSign,
    Placidus,
    BothOr,
    BothAnd,
};

enum class ReturnFinderMatchMode {
    All,
    Any,
};

enum class ReturnFinderConditionType {
    PlanetPlacement,
    Aspect,
    HouseLordPlacement,
    ProfectionLordPlacement,
    Stellium,
    MunthaPlacement,
    TajakaAspect,
    LordOfYearPlacement,
};

enum class ReturnFinderScope {
    Return,
    Natal,
};

enum class ReturnFinderTargetKind {
    Planet,
    Angle,
    HouseLord,
    ProfectionLord,
    Muntha,
    MunthaLord,
    LordOfYear,
};

enum class ReturnFinderTajakaMotion {
    Any,
    Ithasala,
    Eesarpha,
};

enum class ReturnFinderPlacementKind {
    Sign,
    House,
};

enum class ReturnFinderAspect {
    Conjunction,
    Sextile,
    Square,
    Trine,
    Opposition,
    AnyMajor,
};

enum class ReturnFinderHouseLordMatch {
    Any,
    All,
};

struct ReturnFinderTarget {
    ReturnFinderScope scope = ReturnFinderScope::Return;
    ReturnFinderTargetKind kind = ReturnFinderTargetKind::Planet;
    QString name = "Sun";
    QVector<int> houses = {1};
    ReturnFinderHouseLordMatch houseLordMatch = ReturnFinderHouseLordMatch::Any;
};

struct ReturnFinderCondition {
    QString id;
    bool enabled = true;
    bool exclude = false;
    ReturnFinderConditionType type = ReturnFinderConditionType::PlanetPlacement;

    ReturnFinderTarget subject;
    ReturnFinderTarget target;
    ReturnFinderPlacementKind placementKind = ReturnFinderPlacementKind::House;
    int targetSign = 0;
    int targetHouse = 1;
    bool planetPlacementEnabled = true;
    bool planetAngleContactEnabled = false;
    ReturnFinderAspect aspect = ReturnFinderAspect::Conjunction;
    double orb = 1.0;

    QVector<int> houseLordHouses = {1};
    ReturnFinderScope houseLordScope = ReturnFinderScope::Return;
    ReturnFinderHouseLordMatch houseLordMatch = ReturnFinderHouseLordMatch::Any;

    int stelliumMinimum = 3;
    bool stelliumBySign = false;
    bool stelliumAny = false;
    int stelliumTarget = 1;

    ReturnFinderTajakaMotion tajakaMotion = ReturnFinderTajakaMotion::Ithasala;
};

struct ReturnFinderQuery {
    ReturnFinderType returnType = ReturnFinderType::Solar;
    int startYear = 0;
    int endYear = 0;
    QDate startDate;
    QDate endDate;

    bool useNatalLocation = true;
    QString locationName;
    QString timezone;
    double latitude = 0.0;
    double longitude = 0.0;

    ReturnFinderHouseMode houseMode = ReturnFinderHouseMode::WholeSign;
    bool modernRulership = false;
    ReturnFinderMatchMode matchMode = ReturnFinderMatchMode::All;
    // Tajaka (P.V.R. Rao) solar scans: the return moment uses the Sun's natal
    // tropical longitude and every scanned chart is judged sidereally, cast
    // for the natal location. Ignored for lunar scans.
    bool tajakaMode = false;
    QVector<ReturnFinderCondition> conditions;

    NatalInput natalInput;
    NatalChart natalChart;
    AspectOrbs aspectOrbs;
    QString natalLocationName;
    QString ephePath;
    QStringList dllSearchPaths;
};

struct ReturnFinderConditionEvaluation {
    QString conditionId;
    bool matched = false;
    bool excluded = false;
    QString description;
    QString systemLabel;
    QString resolvedSubject;
    QString resolvedTarget;
    QString aspectLabel;
    double orb = -1.0;
};

struct ReturnFinderResult {
    int stableId = 0;
    ReturnFinderType returnType = ReturnFinderType::Solar;
    int year = 0;
    QDateTime utcDateTime;
    QDateTime localDateTime;
    QString matchSummary;
    double closestOrb = -1.0;
    QString warning;
    QVector<ReturnFinderConditionEvaluation> evaluations;
};

struct ReturnFinderRunSummary {
    int scanned = 0;
    int matched = 0;
    int failed = 0;
    bool cancelled = false;
    bool capped = false;
    QStringList warnings;
};

QString returnFinderTypeLabel(ReturnFinderType type);
QString returnFinderAspectLabel(ReturnFinderAspect aspect);
double returnFinderAspectAngle(ReturnFinderAspect aspect);

}  // namespace dracoved

Q_DECLARE_METATYPE(dracoved::ReturnFinderResult)
Q_DECLARE_METATYPE(QVector<dracoved::ReturnFinderResult>)
Q_DECLARE_METATYPE(dracoved::ReturnFinderRunSummary)
