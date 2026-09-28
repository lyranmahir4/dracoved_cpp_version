#pragma once
#include <QString>
#include <QVector>

namespace dracoved {
struct DashaPeriod {
    int lord = -1; // Ketu, Venus, Sun, Moon, Mars, Rahu, Jupiter, Saturn, Mercury.
    int level = 0; // MD=0 .. Prana=4.
    qint64 startMs = 0;
    qint64 endMs = 0; // UTC, exclusive; a boundary belongs to the next period.
    bool contains(qint64 utcMs) const { return startMs <= utcMs && utcMs < endMs; }
};
QString dashaLordName(int lord);
QString dashaLevelName(int level);
QString dashaLevelAbbreviation(int level);
int dashaLordYears(int lord);

class Vimshottari {
public:
    bool initialize(qint64 birthUtcMs, double moonLongitude, double yearDays = 365.25);
    bool isValid() const { return valid_; }
    qint64 birthMs() const { return birthMs_; }
    double yearDays() const { return yearDays_; }
    DashaPeriod birthMajor() const { return birthMajor_; }
    QVector<DashaPeriod> majorCycle(qint64 inspectionUtcMs) const;
    QVector<DashaPeriod> activeAt(qint64 inspectionUtcMs) const;
    static QVector<DashaPeriod> children(const DashaPeriod& parent);
private:
    bool valid_ = false;
    double yearDays_ = 365.25;
    qint64 birthMs_ = 0;
    qint64 yearMs_ = 0;
    DashaPeriod birthMajor_;
};
} // namespace dracoved
