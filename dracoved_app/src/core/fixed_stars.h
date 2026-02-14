#pragma once

#include <QString>
#include <QStringList>

namespace dracoved {

QStringList fixedStarCatalog();
QStringList defaultFixedStars();
bool isFixedStarInCatalog(const QString& name);

}  // namespace dracoved

