#pragma once

#include <QString>
#include <QStringList>

namespace dracoved {

struct NakshatraPlacement {
    bool valid = false;
    int index = -1;
    int pada = -1;
    QString name;
    QString lord;
};

QStringList vedicNakshatraNames();
QString vedicNakshatraLord(int index);
NakshatraPlacement classifyVedicNakshatra(double siderealLongitude);

struct TaraPlacement {
    int count = 0;  // Inclusive count, 1–27; zero means invalid input.
    int number = 0; // Repeating Tara category, 1–9.
    QString name;
};
TaraPlacement classifyVedicTara(int natalNakshatra, int transitNakshatra);

}  // namespace dracoved
