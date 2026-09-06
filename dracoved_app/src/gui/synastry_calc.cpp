#include "synastry_calc.h"

#include "transit_calc_service.h"

#include <QPair>

#include <algorithm>
#include <cmath>

namespace dracoved::synastry {

namespace {

// The shared applying test is file-static inside main_window.cpp, so this is a
// local equivalent: applying when the orb shrinks after a small step forward.
bool applyingFor(double lonA, double speedA, double lonB, double speedB, double exact) {
    const double dt = 0.05;
    const double current =
        std::fabs(transitcalc::angularDiffAbs(lonA, lonB) - exact);
    const double next = std::fabs(
        transitcalc::angularDiffAbs(lonA + speedA * dt, lonB + speedB * dt) - exact);
    return next < current;
}

bool nameAllowed(const QStringList& allowed, const QString& name) {
    return allowed.isEmpty() || allowed.contains(name);
}

}  // namespace

QVector<Point> collectPoints(const NatalChart& chart, const Options& options) {
    QVector<Point> points;
    points.reserve(chart.bodies.size() + 4);
    for (const auto& body : chart.bodies) {
        if (!nameAllowed(options.allowedNames, body.name)) {
            continue;
        }
        Point point;
        point.name = body.name;
        point.longitude = body.longitude;
        point.speed = body.speed;
        point.hasSpeed = body.hasSpeed;
        points.push_back(point);
    }
    if (options.includeAngles) {
        const QVector<QPair<QString, double>> angles = {
            {QStringLiteral("Ascendant"), chart.angles.asc},
            {QStringLiteral("Midheaven"), chart.angles.mc},
            {QStringLiteral("Descendant"), chart.angles.desc},
            {QStringLiteral("IC"), chart.angles.ic},
        };
        for (const auto& entry : angles) {
            if (!nameAllowed(options.allowedNames, entry.first)) {
                continue;
            }
            Point point;
            point.name = entry.first;
            point.longitude = entry.second;
            points.push_back(point);
        }
    }
    return points;
}

QVector<Contact> westernContacts(const NatalChart& chartA, const NatalChart& chartB,
                                 const Options& options) {
    const QVector<Point> aPoints = collectPoints(chartA, options);
    const QVector<Point> bPoints = collectPoints(chartB, options);

    QVector<Contact> contacts;
    contacts.reserve(aPoints.size() * 2);
    for (const auto& a : aPoints) {
        for (const auto& b : bPoints) {
            const double diff = transitcalc::angularDiffAbs(a.longitude, b.longitude);
            QString label;
            double orb = 0.0;
            double maxOrb = 0.0;
            if (!transitcalc::aspectForDiff(diff, options.orbs, &label, &orb, &maxOrb)) {
                continue;
            }
            Contact contact;
            contact.system = System::WesternAspects;
            contact.aName = a.name;
            contact.bName = b.name;
            contact.label = label;
            contact.aLongitude = a.longitude;
            contact.bLongitude = b.longitude;
            contact.orb = orb;
            contact.maxOrb = maxOrb;
            if (a.hasSpeed || b.hasSpeed) {
                const bool isApplying = applyingFor(
                    a.longitude, a.hasSpeed ? a.speed : 0.0,
                    b.longitude, b.hasSpeed ? b.speed : 0.0,
                    transitcalc::aspectAngleForLabel(label));
                contact.applying = isApplying ? 1 : 0;
            }
            contacts.push_back(contact);
        }
    }
    markMutualPairs(contacts);
    std::sort(contacts.begin(), contacts.end(),
              [](const Contact& x, const Contact& y) { return x.orb < y.orb; });
    return contacts;
}

QVector<Contact> contactsFor(System system, const NatalChart& chartA,
                            const NatalChart& chartB, const Options& options) {
    switch (system) {
        case System::WesternAspects:
            return westernContacts(chartA, chartB, options);
        case System::VedicAshtakoota:
            // Not implemented yet. Returning empty keeps every call site written
            // once, so adding guna matching later touches only this module.
            return {};
    }
    return {};
}

void markMutualPairs(QVector<Contact>& contacts) {
    for (int i = 0; i < contacts.size(); ++i) {
        for (int j = 0; j < contacts.size(); ++j) {
            if (i == j) {
                continue;
            }
            if (contacts[i].aName == contacts[j].bName
                && contacts[i].bName == contacts[j].aName) {
                contacts[i].mutualPair = true;
                break;
            }
        }
    }
}

}  // namespace dracoved::synastry
