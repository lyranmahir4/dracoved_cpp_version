#pragma once

#include <QString>
#include <QStringList>

namespace dracoved {

struct SweConfig {
    QString ephePath;
    QString dllPath;
};

class SwissEph {
public:
    SwissEph();
    ~SwissEph();

    bool load(const QStringList& searchPaths, QString* error);
    bool isLoaded() const;
    QString loadedPath() const;

    void setEphePath(const QString& path);
    void setSidMode(int mode, double t0 = 0.0, double ayanT0 = 0.0);

    double julianDay(int year, int month, int day, double hour, int gregFlag) const;
    bool revJul(double jd, int gregFlag, int* year, int* month, int* day, double* hour, QString* error) const;
    bool calcUt(double jdUt, int body, int flags, double* outLon, QString* error) const;
    bool fixstarUt(const QString& starName, double jdUt, int flags,
                   double* outLon, QString* outResolvedName, QString* error) const;
    bool houses(double jdUt, double geoLat, double geoLon, char hsys, double* cusps, double* ascmc, QString* error) const;
    bool housesEx(double jdUt, int flags, double geoLat, double geoLon, char hsys, double* cusps, double* ascmc, QString* error) const;
    bool housesArmc(double armc, double geoLat, double eps, char hsys, double* cusps, double* ascmc, QString* error) const;
    bool getAyanamsaUt(double jdUt, double* outAyanamsa, QString* error) const;
    int solEclipseWhenGlob(double jdStart, int flags, int typeFlags, double* tret, int backward, QString* error) const;
    int lunEclipseWhen(double jdStart, int flags, int typeFlags, double* tret, int backward, QString* error) const;

private:
    void unload();
    bool bind(QString* error);

    void* dll_ = nullptr;
    QString dllPath_;

    using SweSetEphePath = void (*)(const char*);
    using SweSetSidMode = void (*)(int, double, double);
    using SweJulDay = double (*)(int, int, int, double, int);
    using SweRevJul = void (*)(double, int, int*, int*, int*, double*);
    using SweCalcUt = int (*)(double, int, int, double*, char*);
    using SweFixstarUt = int (*)(char*, double, int, double*, char*);
    using SweHouses = int (*)(double, double, double, int, double*, double*);
    using SweHousesEx = int (*)(double, int, double, double, int, double*, double*);
    using SweHousesArmc = int (*)(double, double, double, int, double*, double*);
    using SweGetAyanamsaUt = double (*)(double);
    using SweSolEclipseWhenGlob = int (*)(double, int, int, double*, int, char*);
    using SweLunEclipseWhen = int (*)(double, int, int, double*, int, char*);

    SweSetEphePath sweSetEphePath_ = nullptr;
    SweSetSidMode sweSetSidMode_ = nullptr;
    SweJulDay sweJulDay_ = nullptr;
    SweRevJul sweRevJul_ = nullptr;
    SweCalcUt sweCalcUt_ = nullptr;
    SweFixstarUt sweFixstarUt_ = nullptr;
    SweFixstarUt sweFixstar2Ut_ = nullptr;
    SweHouses sweHouses_ = nullptr;
    SweHousesEx sweHousesEx_ = nullptr;
    SweHousesArmc sweHousesArmc_ = nullptr;
    SweGetAyanamsaUt sweGetAyanamsaUt_ = nullptr;
    SweSolEclipseWhenGlob sweSolEclipseWhenGlob_ = nullptr;
    SweLunEclipseWhen sweLunEclipseWhen_ = nullptr;
};

// Minimal constants needed for tropical natal calculations.
constexpr int SE_GREG_CAL = 1;
constexpr int SEFLG_SIDEREAL = (64 * 1024);
constexpr int SE_ECL_NUT = -1;
constexpr int SE_SUN = 0;
constexpr int SE_MOON = 1;
constexpr int SE_MERCURY = 2;
constexpr int SE_VENUS = 3;
constexpr int SE_MARS = 4;
constexpr int SE_JUPITER = 5;
constexpr int SE_SATURN = 6;
constexpr int SE_URANUS = 7;
constexpr int SE_NEPTUNE = 8;
constexpr int SE_PLUTO = 9;
constexpr int SE_MEAN_NODE = 10;
constexpr int SE_TRUE_NODE = 11;
constexpr int SE_MEAN_APOG = 21; // Lilith (Black Moon)
constexpr int SE_CHIRON = 15;
constexpr int SE_PHOLUS = 16;
constexpr int SE_CERES = 17;
constexpr int SE_PALLAS = 18;
constexpr int SE_JUNO = 19;
constexpr int SE_VESTA = 20;

// Eclipse type flags (from Swiss Ephemeris: swephexp.h)
constexpr int SE_ECL_TOTAL = 4;
constexpr int SE_ECL_ANNULAR = 8;
constexpr int SE_ECL_PARTIAL = 16;
constexpr int SE_ECL_ANNULAR_TOTAL = 32;
constexpr int SE_ECL_PENUMBRAL = 64;

}  // namespace dracoved
