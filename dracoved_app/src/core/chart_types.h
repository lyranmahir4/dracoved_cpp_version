#pragma once

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QVector>

namespace dracoved {

enum class HouseSystem {
    WholeSign,
    Placidus,
};

struct AspectOrbs {
    double conjunction = 10.0;
    double sextile = 10.0;
    double square = 10.0;
    double trine = 10.0;
    double opposition = 10.0;
};

inline AspectOrbs defaultAspectOrbs() {
    return AspectOrbs{};
}

struct NatalInput {
    QString name;
    QDate date;
    QTime time;
    QString timezone;
    double latitude = 0.0;
    double longitude = 0.0;
    HouseSystem houseSystem = HouseSystem::WholeSign;
    AspectOrbs aspectOrbs = defaultAspectOrbs();
};

struct BodyPosition {
    QString name;
    double longitude = 0.0;   // 0..360
    int signIndex = -1;       // 0..11
    QString signName;
    double degInSign = 0.0;
    int house = 0;            // 1..12
    bool retrograde = false;
    QString element;
    QString mode;
    QString dignity;
};

struct AnglePositions {
    double asc = 0.0;
    double mc = 0.0;
    double desc = 0.0;
    double ic = 0.0;
    double vertex = 0.0;
};

struct HouseCusp {
    int number = 0;
    double longitude = 0.0;
    QString signName;
};

struct AspectGrid {
    QVector<QString> bodyOrder;
    QVector<QString> bodyAbbrev;
    QVector<QString> bodyGlyphs;
    struct Cell {
        QString symbol;
        QString label;
        double orb = 0.0;
        double maxOrb = 0.0;
        bool hasAspect = false;
    };
    QVector<QVector<Cell>> cells;
};

struct NatalChart {
    QDateTime localDateTime;
    QDateTime utcDateTime;
    QString timezoneLabel;
    AnglePositions angles;
    QVector<BodyPosition> bodies;
    QVector<HouseCusp> cusps;
    QStringList warnings;
    bool isDayChart = false;
    double partOfFortune = 0.0;
    bool hasPartOfFortune = false;
    AspectGrid aspects;
};

}  // namespace dracoved
