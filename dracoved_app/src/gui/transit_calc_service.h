#pragma once

#include "../core/chart_types.h"

#include <QColor>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace dracoved::transitcalc {

double angularDiffSigned(double a, double b);
double angularDiffAbs(double a, double b);

bool isNodeName(const QString& name);
bool isAngleName(const QString& name);
bool isDerivedPointName(const QString& name);
bool isBenefic(const QString& name);
bool isMalefic(const QString& name);

double bodyWeightFor(const QString& name);
int bodyIdForName(const QString& name, LunarNodeType genericNodeType = LunarNodeType::Mean);
bool isComputableBody(const QString& name);

QStringList transitCalculableBodyOrder();
QStringList geodeticBodyOrder();
QColor geodeticColorForIndex(int index);

// Identity colour for a body, using the same hues the astrocartography map
// draws, so the map and the chart wheel agree on what "Mars" looks like.
// Returns an INVALID QColor for everything without an identity hue (Arabic
// Lots, asteroids, Vertex and the four chart angles) so callers keep their own
// lane colour for those instead of turning the wheel into confetti.
QColor bodyAccentColor(const QString& name);

double aspectAngleForLabel(const QString& label);
double clampStepDays(double speedAbs);
QDateTime midTimeUtc(const QDateTime& a, const QDateTime& b);

QString formatDegreeDms(double deg);
QString aspectTargetFromLabel(const QString& text);
QString abbrevForName(const QString& name);

bool findAngleLongitude(const NatalChart& chart, const QString& name, double* outLon);
bool findBodyLongitude(const NatalChart& chart, const QString& name, double* outLon);

bool aspectForDiff(double diff, const AspectOrbs& orbs, QString* outLabel, double* outOrb, double* outMaxOrb);
int calcHouseForLongitude(double lon, const QVector<HouseCusp>& cusps, double asc, HouseSystem system);

}  // namespace dracoved::transitcalc
