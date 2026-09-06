#pragma once

#include "../core/chart_types.h"

#include <QString>
#include <QStringList>

namespace dracoved::profilestore {

// Saved charts are stored as one JSON file per profile in <appDir>/profiles.
QString profilesDir();
QString sanitizeProfileName(const QString& name);
QString profileFilePath(const QString& name);
QStringList listProfiles();

// Parses a saved chart profile into a NatalInput WITHOUT touching any
// application state, so a second chart (synastry Person B) can be read while
// the active chart stays exactly as it is. Returns false and fills outError on
// failure. outLocation receives the stored location name, which the caller
// needs because it is not part of NatalInput.
//
// appDefaultNodePolicy is the application-wide lunar node default. It supplies
// the policy for profiles that opted to follow the app default, and for legacy
// (version 3 and older) profiles saved before the node model was selectable.
bool readProfileInput(const QString& profileName,
                      const LunarNodePolicy& appDefaultNodePolicy,
                      NatalInput* outInput,
                      QString* outLocation,
                      QString* outError);

// Writes a chart profile as JSON in the same format version 4 the app's own save
// path produces, so a profile written here is indistinguishable from one saved
// from the Natal tab. Overwrites without asking: any confirmation prompt belongs
// to the caller. Returns false and fills outError on failure.
bool writeProfileInput(const QString& profileName,
                       const NatalInput& input,
                       const QString& locationName,
                       QString* outError);

}  // namespace dracoved::profilestore
