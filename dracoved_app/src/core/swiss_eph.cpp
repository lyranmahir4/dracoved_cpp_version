#include "swiss_eph.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QByteArray>

#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace dracoved {

SwissEph::SwissEph() = default;

SwissEph::~SwissEph() {
    unload();
}

void SwissEph::unload() {
#ifdef _WIN32
    if (dll_) {
        FreeLibrary(reinterpret_cast<HMODULE>(dll_));
        dll_ = nullptr;
    }
#endif
    dllPath_.clear();
    sweSetEphePath_ = nullptr;
    sweSetSidMode_ = nullptr;
    sweJulDay_ = nullptr;
    sweRevJul_ = nullptr;
    sweCalcUt_ = nullptr;
    sweFixstarUt_ = nullptr;
    sweFixstar2Ut_ = nullptr;
    sweHouses_ = nullptr;
    sweHousesEx_ = nullptr;
    sweHousesArmc_ = nullptr;
    sweGetAyanamsaUt_ = nullptr;
    sweSolEclipseWhenGlob_ = nullptr;
    sweLunEclipseWhen_ = nullptr;
    sweRiseTrans_ = nullptr;
}

bool SwissEph::bind(QString* error) {
#ifdef _WIN32
    auto loadSym = [this](const char* name) -> FARPROC {
        return GetProcAddress(reinterpret_cast<HMODULE>(dll_), name);
    };
    sweSetEphePath_ = reinterpret_cast<SweSetEphePath>(loadSym("swe_set_ephe_path"));
    sweSetSidMode_ = reinterpret_cast<SweSetSidMode>(loadSym("swe_set_sid_mode"));
    sweJulDay_ = reinterpret_cast<SweJulDay>(loadSym("swe_julday"));
    sweRevJul_ = reinterpret_cast<SweRevJul>(loadSym("swe_revjul"));
    sweCalcUt_ = reinterpret_cast<SweCalcUt>(loadSym("swe_calc_ut"));
    sweFixstarUt_ = reinterpret_cast<SweFixstarUt>(loadSym("swe_fixstar_ut"));
    sweFixstar2Ut_ = reinterpret_cast<SweFixstarUt>(loadSym("swe_fixstar2_ut"));
    sweHouses_ = reinterpret_cast<SweHouses>(loadSym("swe_houses"));
    sweHousesEx_ = reinterpret_cast<SweHousesEx>(loadSym("swe_houses_ex"));
    sweHousesArmc_ = reinterpret_cast<SweHousesArmc>(loadSym("swe_houses_armc"));
    sweGetAyanamsaUt_ = reinterpret_cast<SweGetAyanamsaUt>(loadSym("swe_get_ayanamsa_ut"));
    sweSolEclipseWhenGlob_ = reinterpret_cast<SweSolEclipseWhenGlob>(loadSym("swe_sol_eclipse_when_glob"));
    sweLunEclipseWhen_ = reinterpret_cast<SweLunEclipseWhen>(loadSym("swe_lun_eclipse_when"));
    sweRiseTrans_ = reinterpret_cast<SweRiseTrans>(loadSym("swe_rise_trans"));

    if (!sweSetEphePath_ || !sweSetSidMode_ || !sweJulDay_ || !sweRevJul_
        || !sweCalcUt_ || (!sweFixstarUt_ && !sweFixstar2Ut_)
        || !sweHouses_ || !sweHousesEx_ || !sweHousesArmc_ || !sweGetAyanamsaUt_
        || !sweSolEclipseWhenGlob_ || !sweLunEclipseWhen_) {
        if (error) {
            *error = "Failed to bind one or more Swiss Ephemeris symbols.";
        }
        return false;
    }
    return true;
#else
    if (error) {
        *error = "Swiss Ephemeris dynamic loading is only implemented for Windows.";
    }
    return false;
#endif
}

bool SwissEph::load(const QStringList& searchPaths, QString* error) {
    unload();
#ifdef _WIN32
    QStringList candidates;
    const QStringList defaultNames = {
        "swedll64.dll",
        "swisseph.dll",
        "swedll32.dll",
    };
    for (const auto& entry : searchPaths) {
        if (entry.isEmpty()) {
            continue;
        }
        if (entry.endsWith(".dll", Qt::CaseInsensitive)) {
            candidates.push_back(entry);
        } else {
            QDir dir(entry);
            for (const auto& name : defaultNames) {
                candidates.push_back(dir.filePath(name));
            }
        }
    }

    for (const auto& path : candidates) {
        if (!QFileInfo::exists(path)) {
            continue;
        }
        const auto wide = path.toStdWString();
        HMODULE handle = LoadLibraryW(wide.c_str());
        if (!handle) {
            continue;
        }
        dll_ = reinterpret_cast<void*>(handle);
        dllPath_ = path;
        if (!bind(error)) {
            unload();
            return false;
        }
        return true;
    }

    if (error) {
        *error = "Unable to locate Swiss Ephemeris DLL (swedll64.dll/swisseph.dll). Place it next to the app or set DRACOVED_SWE_DLL.";
    }
    return false;
#else
    if (error) {
        *error = "Swiss Ephemeris dynamic loading is only implemented for Windows.";
    }
    return false;
#endif
}

bool SwissEph::isLoaded() const {
    return dll_ != nullptr;
}

QString SwissEph::loadedPath() const {
    return dllPath_;
}

void SwissEph::setEphePath(const QString& path) {
    if (sweSetEphePath_) {
        sweSetEphePath_(path.toUtf8().constData());
    }
}

void SwissEph::setSidMode(int mode, double t0, double ayanT0) {
    if (sweSetSidMode_) {
        sweSetSidMode_(mode, t0, ayanT0);
    }
}

double SwissEph::julianDay(int year, int month, int day, double hour, int gregFlag) const {
    if (!sweJulDay_) {
        return 0.0;
    }
    return sweJulDay_(year, month, day, hour, gregFlag);
}

bool SwissEph::revJul(double jd, int gregFlag, int* year, int* month, int* day, double* hour, QString* error) const {
    if (!sweRevJul_ || !year || !month || !day || !hour) {
        if (error) {
            *error = "swe_revjul unavailable.";
        }
        return false;
    }
    sweRevJul_(jd, gregFlag, year, month, day, hour);
    return true;
}

bool SwissEph::calcUt(double jdUt, int body, int flags, double* outLon, QString* error) const {
    if (!sweCalcUt_ || !outLon) {
        return false;
    }
    double xx[6] = {0};
    char serr[256] = {0};
    int ret = sweCalcUt_(jdUt, body, flags, xx, serr);
    if (ret < 0) {
        if (error) {
            *error = QString("swe_calc_ut failed: %1").arg(serr);
        }
        return false;
    }
    *outLon = xx[0];
    return true;
}


bool SwissEph::calcUtFull(double jdUt, int body, int flags, double* outValues, QString* error) const {
    if (!sweCalcUt_ || !outValues) {
        if (error) {
            *error = "swe_calc_ut unavailable.";
        }
        return false;
    }
    double xx[6] = {0};
    char serr[256] = {0};
    const int ret = sweCalcUt_(jdUt, body, flags, xx, serr);
    if (ret < 0) {
        if (error) {
            *error = QString("swe_calc_ut failed: %1").arg(serr);
        }
        return false;
    }
    for (int i = 0; i < 6; ++i) {
        outValues[i] = xx[i];
    }
    return true;
}
bool SwissEph::fixstarUt(const QString& starName, double jdUt, int flags,
                         double* outLon, QString* outResolvedName, QString* error) const {
    SweFixstarUt fixFn = sweFixstar2Ut_ ? sweFixstar2Ut_ : sweFixstarUt_;
    if (!fixFn || !outLon) {
        if (error) {
            *error = "swe_fixstar_ut unavailable.";
        }
        return false;
    }

    QByteArray starUtf8 = starName.trimmed().toUtf8();
    if (starUtf8.isEmpty()) {
        if (error) {
            *error = "Fixed star name is empty.";
        }
        return false;
    }

    char starBuf[256] = {0};
    const int maxCopy = static_cast<int>(sizeof(starBuf)) - 1;
    const int copyLen = std::min(maxCopy, static_cast<int>(starUtf8.size()));
    std::memcpy(starBuf, starUtf8.constData(), static_cast<size_t>(copyLen));
    starBuf[copyLen] = '\0';

    double xx[6] = {0};
    char serr[256] = {0};
    const int ret = fixFn(starBuf, jdUt, flags, xx, serr);
    if (ret < 0) {
        if (error) {
            *error = QString("swe_fixstar_ut failed: %1").arg(serr);
        }
        return false;
    }

    *outLon = xx[0];
    if (outResolvedName) {
        *outResolvedName = QString::fromUtf8(starBuf).trimmed();
    }
    return true;
}

bool SwissEph::houses(double jdUt, double geoLat, double geoLon, char hsys, double* cusps, double* ascmc, QString* error) const {
    if (!sweHouses_ || !cusps || !ascmc) {
        return false;
    }
    int ret = sweHouses_(jdUt, geoLat, geoLon, static_cast<int>(hsys), cusps, ascmc);
    if (ret < 0) {
        if (error) {
            *error = "swe_houses failed.";
        }
        return false;
    }
    return true;
}

bool SwissEph::housesEx(double jdUt, int flags, double geoLat, double geoLon, char hsys,
                        double* cusps, double* ascmc, QString* error) const {
    if (!sweHousesEx_ || !cusps || !ascmc) {
        return false;
    }
    int ret = sweHousesEx_(jdUt, flags, geoLat, geoLon, static_cast<int>(hsys), cusps, ascmc);
    if (ret < 0) {
        if (error) {
            *error = "swe_houses_ex failed.";
        }
        return false;
    }
    return true;
}

bool SwissEph::housesArmc(double armc, double geoLat, double eps, char hsys, double* cusps, double* ascmc, QString* error) const {
    if (!sweHousesArmc_ || !cusps || !ascmc) {
        return false;
    }
    int ret = sweHousesArmc_(armc, geoLat, eps, static_cast<int>(hsys), cusps, ascmc);
    if (ret < 0) {
        if (error) {
            *error = "swe_houses_armc failed.";
        }
        return false;
    }
    return true;
}

bool SwissEph::getAyanamsaUt(double jdUt, double* outAyanamsa, QString* error) const {
    if (!sweGetAyanamsaUt_ || !outAyanamsa) {
        if (error) {
            *error = "swe_get_ayanamsa_ut unavailable.";
        }
        return false;
    }
    const double ayanamsa = sweGetAyanamsaUt_(jdUt);
    if (!std::isfinite(ayanamsa)) {
        if (error) {
            *error = "swe_get_ayanamsa_ut returned invalid value.";
        }
        return false;
    }
    *outAyanamsa = ayanamsa;
    return true;
}

int SwissEph::solEclipseWhenGlob(double jdStart, int flags, int typeFlags, double* tret, int backward, QString* error) const {
    if (!sweSolEclipseWhenGlob_ || !tret) {
        if (error) {
            *error = "swe_sol_eclipse_when_glob unavailable.";
        }
        return -1;
    }
    char serr[256] = {0};
    int ret = sweSolEclipseWhenGlob_(jdStart, flags, typeFlags, tret, backward, serr);
    if (ret < 0 && error) {
        *error = QString("swe_sol_eclipse_when_glob failed: %1").arg(serr);
    }
    return ret;
}

int SwissEph::lunEclipseWhen(double jdStart, int flags, int typeFlags, double* tret, int backward, QString* error) const {
    if (!sweLunEclipseWhen_ || !tret) {
        if (error) {
            *error = "swe_lun_eclipse_when unavailable.";
        }
        return -1;
    }
    char serr[256] = {0};
    int ret = sweLunEclipseWhen_(jdStart, flags, typeFlags, tret, backward, serr);
    if (ret < 0 && error) {
        *error = QString("swe_lun_eclipse_when failed: %1").arg(serr);
    }
    return ret;
}

int SwissEph::riseTrans(double jdStartUt, int body, int flags, int riseSetFlags,
                        double geoLon, double geoLat, double altitudeMeters,
                        double pressureHPa, double temperatureC,
                        double* outJd, QString* error) const {
    if (!sweRiseTrans_ || !outJd) {
        if (error) *error = "swe_rise_trans unavailable.";
        return -1;
    }
    double geopos[3] = {geoLon, geoLat, altitudeMeters};
    char serr[256] = {0};
    const int result = sweRiseTrans_(jdStartUt, body, nullptr, flags, riseSetFlags,
                                     geopos, pressureHPa, temperatureC, outJd, serr);
    if (result < 0 && error) {
        if (result == -2) {
            *error = "The requested rise or set event does not occur at this location.";
        } else {
            *error = QString("swe_rise_trans failed: %1").arg(serr);
        }
    }
    return result;
}

}  // namespace dracoved

