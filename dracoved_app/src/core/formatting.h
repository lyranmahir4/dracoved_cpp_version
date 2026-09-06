#pragma once

#include <QString>
#include <QStringList>

#include "chart_types.h"

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
QStringList bodyOrderForLunarNodePolicy(const LunarNodePolicy& policy);
QStringList tropicalBodyAbbrev();
QStringList tropicalBodyGlyphs();
QString bodyGlyph(const QString& name);
QString bodySvgResourcePath(const QString& name);
QStringList arabicLotOrder();
bool isArabicLotName(const QString& name);
QStringList asteroidBodyOrder();
bool isAsteroidBody(const QString& name);

// Bodies the user can individually show/hide on the chart wheel, in display
// order. Angles (AC/DC/MC/IC) are intentionally excluded.
QStringList chartToggleableBodyOrder();
// Maps a stored body name onto a stable key for visibility lookups. Lunar
// nodes collapse onto "North Node"/"South Node" so a single entry keeps
// working when the Mean/True node policy changes their stored names.
QString chartBodyVisibilityKey(const QString& name);

// Named visibility presets for the chart display dialog. Values are persisted
// as integers, so keep existing entries stable when adding new ones.
enum class ChartBodyPreset {
    AllBodies = 0,
    Classical = 1,      // Sun..Saturn + lunar nodes
    Modern = 2,         // Sun..Pluto + lunar nodes
    MainPlanetsOnly = 3,  // Sun..Pluto, no nodes
    Custom = 4,
};

// Bodies shown by the given preset. Returns an empty list for Custom, which
// has no fixed membership.
QStringList chartBodyPresetVisibleBodies(ChartBodyPreset preset);
// Reverse lookup: the preset matching this visible-body set, or Custom.
ChartBodyPreset chartBodyPresetForVisibleBodies(const QStringList& visibleBodies);
QString chartBodyPresetLabel(ChartBodyPreset preset);

}  // namespace dracoved
