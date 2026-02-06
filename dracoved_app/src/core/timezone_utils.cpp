#include "timezone_utils.h"

#include <QRegularExpression>

namespace dracoved {

static bool parseOffsetString(const QString& raw, int* outSeconds) {
    if (!outSeconds) {
        return false;
    }
    QString text = raw.trimmed();
    if (text.isEmpty()) {
        return false;
    }
    QString sign = "+";
    if (text.startsWith("+") || text.startsWith("-")) {
        sign = text.left(1);
        text = text.mid(1);
    }
    if (text.isEmpty()) {
        return false;
    }

    int hours = 0;
    int minutes = 0;
    if (text.contains(":")) {
        const auto parts = text.split(":");
        if (parts.size() != 2) {
            return false;
        }
        bool okh = false;
        bool okm = false;
        hours = parts[0].toInt(&okh);
        minutes = parts[1].toInt(&okm);
        if (!okh || !okm) {
            return false;
        }
        if (hours < 0 || minutes < 0 || minutes >= 60) {
            return false;
        }
    } else if (text.contains(".")) {
        bool ok = false;
        double h = text.toDouble(&ok);
        if (!ok) {
            return false;
        }
        if (h < 0.0) {
            return false;
        }
        hours = static_cast<int>(std::floor(h));
        const double frac = h - hours;
        minutes = static_cast<int>(std::llround(frac * 60.0));
        if (minutes >= 60) {
            minutes = 0;
            hours += 1;
        }
    } else {
        bool ok = false;
        hours = text.toInt(&ok);
        if (!ok) {
            return false;
        }
        if (hours < 0) {
            return false;
        }
        minutes = 0;
    }

    int total = hours * 3600 + minutes * 60;
    constexpr int kMaxOffsetSeconds = 14 * 3600;
    if (total < 0 || total > kMaxOffsetSeconds) {
        return false;
    }
    if (sign == "-") {
        total = -total;
    }
    *outSeconds = total;
    return true;
}

bool parseTimezoneInput(const QString& input, QTimeZone* outTz, QString* outLabel, QString* error) {
    if (!outTz) {
        return false;
    }
    QString trimmed = input.trimmed();
    if (trimmed.isEmpty() || trimmed.compare("UTC", Qt::CaseInsensitive) == 0 ||
        trimmed.compare("GMT", Qt::CaseInsensitive) == 0 || trimmed == "Z") {
        *outTz = QTimeZone::utc();
        if (outLabel) {
            *outLabel = "UTC";
        }
        return true;
    }

    auto setOffset = [&](int offsetSeconds, const QString& label) {
        *outTz = QTimeZone::fromSecondsAheadOfUtc(offsetSeconds);
        if (outLabel) {
            *outLabel = label;
        }
    };

    const int openParen = trimmed.indexOf('(');
    const int closeParen = (openParen >= 0) ? trimmed.indexOf(')', openParen + 1) : -1;
    if (openParen >= 0 && closeParen > openParen) {
        const QString inside = trimmed.mid(openParen + 1, closeParen - openParen - 1).trimmed();
        if (!inside.isEmpty()) {
            int offsetSeconds = 0;
            if (parseOffsetString(inside, &offsetSeconds)) {
                const QString label = QString("UTC%1").arg(
                    inside.startsWith("+") || inside.startsWith("-") ? inside : "+" + inside);
                setOffset(offsetSeconds, label);
                return true;
            }
            QTimeZone tz(inside.toUtf8());
            if (tz.isValid()) {
                *outTz = tz;
                if (outLabel) {
                    *outLabel = QString::fromUtf8(tz.id());
                }
                return true;
            }
        }
    }

    QString work = trimmed;
    if (openParen >= 0 && closeParen > openParen) {
        work = (trimmed.left(openParen) + " " + trimmed.mid(closeParen + 1)).trimmed();
    }
    if (work.startsWith("UTC", Qt::CaseInsensitive)) {
        work = work.mid(3).trimmed();
    } else if (work.startsWith("GMT", Qt::CaseInsensitive)) {
        work = work.mid(3).trimmed();
    }

    if (!work.isEmpty()) {
        int offsetSeconds = 0;
        if (parseOffsetString(work, &offsetSeconds)) {
            const QString label = QString("UTC%1").arg(
                work.startsWith("+") || work.startsWith("-") ? work : "+" + work);
            setOffset(offsetSeconds, label);
            return true;
        }
    }

    QTimeZone tz(work.toUtf8());
    if (!tz.isValid()) {
        if (error) {
            *error = "Invalid timezone. Use IANA name (e.g., Asia/Dhaka) or numeric offset (e.g., +5.5).";
        }
        return false;
    }
    *outTz = tz;
    if (outLabel) {
        *outLabel = QString::fromUtf8(tz.id());
    }
    return true;
}

}  // namespace dracoved
