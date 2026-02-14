#include "fixed_stars.h"

namespace dracoved {

QStringList fixedStarCatalog() {
    return {
        "Aldebaran",
        "Algol",
        "Antares",
        "Regulus",
        "Spica",
        "Sirius",
        "Fomalhaut",
        "Capella",
        "Betelgeuse",
        "Rigel",
        "Bellatrix",
        "Deneb",
        "Altair",
        "Vega",
        "Arcturus",
        "Procyon",
        "Pollux",
        "Castor",
        "Alcyone",
        "Polaris",
        "Achernar",
        "Canopus",
        "Menkar",
        "Alphard",
        "Acubens",
    };
}

QStringList defaultFixedStars() {
    return {
        "Aldebaran",
        "Algol",
        "Antares",
        "Regulus",
        "Spica",
        "Sirius",
        "Fomalhaut",
        "Alcyone",
        "Polaris",
        "Canopus",
    };
}

bool isFixedStarInCatalog(const QString& name) {
    static const QStringList kCatalog = fixedStarCatalog();
    for (const auto& catalogName : kCatalog) {
        if (catalogName.compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace dracoved
