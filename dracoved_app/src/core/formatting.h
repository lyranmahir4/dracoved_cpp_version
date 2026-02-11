#pragma once

#include <QString>
#include <QStringList>

namespace dracoved {

QStringList zodiacSigns();
QStringList zodiacSignGlyphs();
QString signName(int index);
int signIndex(double longitude);
double normalizeDegrees(double deg);
double degInSign(double longitude);
QString formatDegInSign(double longitude);
QString formatDegOnly(double longitude);
QString elementForSign(const QString& signName);
int elementIndexForSign(int signIndex);
QString modeForSign(const QString& signName);
QString dignityLabel(const QString& planet, const QString& signName);

QStringList tropicalBodyOrder();
QStringList tropicalBodyAbbrev();
QStringList tropicalBodyGlyphs();
QString bodyGlyph(const QString& name);
QStringList arabicLotOrder();
bool isArabicLotName(const QString& name);
QStringList asteroidBodyOrder();
bool isAsteroidBody(const QString& name);

}  // namespace dracoved
