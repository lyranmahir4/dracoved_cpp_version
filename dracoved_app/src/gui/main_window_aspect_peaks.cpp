#include "main_window.h"
#include "chart_wheel_widget.h"
#include "aspect_peak_graph.h"
#include "transit_calc_service.h"
#include "transit_workers.h"
#include "return_calculation_service.h"

#include "../core/formatting.h"
#include "../core/timezone_utils.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QFile>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QRadioButton>
#include <QScrollBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSvgRenderer>
#include <QStandardItemModel>
#include <QThread>
#include <QTableWidget>
#include <QTimeEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <functional>

namespace dracoved {
namespace {

class PeakSortItem final : public QTableWidgetItem {
public:
    PeakSortItem(const QString& text, double sortValue,
                 Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter)
        : QTableWidgetItem(text), sortValue_(sortValue) {
        setFlags(flags() & ~Qt::ItemIsEditable);
        setTextAlignment(alignment);
    }

    bool operator<(const QTableWidgetItem& other) const override {
        if (const auto* peakOther = dynamic_cast<const PeakSortItem*>(&other)) {
            return sortValue_ < peakOther->sortValue_;
        }
        return QTableWidgetItem::operator<(other);
    }

private:
    double sortValue_ = 0.0;
};

QTableWidgetItem* peakCell(const QString& text, Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    item->setTextAlignment(alignment);
    return item;
}

QIcon peakTintedSvgIcon(const QString& resourcePath, const QColor& requestedColor,
                        int logicalSize = 15) {
    if (resourcePath.isEmpty() || logicalSize <= 0) {
        return {};
    }
    const QColor color = requestedColor.isValid() ? requestedColor : QColor(Qt::black);
    const QString cacheKey = QString("%1|%2|%3")
        .arg(resourcePath, color.name(QColor::HexArgb))
        .arg(logicalSize);
    static QHash<QString, QIcon> cache;
    const auto cached = cache.constFind(cacheKey);
    if (cached != cache.cend()) {
        return cached.value();
    }

    QFile svgFile(resourcePath);
    if (!svgFile.open(QIODevice::ReadOnly)) {
        return {};
    }
    QByteArray svgData = svgFile.readAll();
    svgData.replace("currentColor", color.name(QColor::HexRgb).toUtf8());
    QSvgRenderer renderer(svgData);
    if (!renderer.isValid()) {
        return {};
    }

    constexpr int renderScale = 2;
    QPixmap pixmap(logicalSize * renderScale, logicalSize * renderScale);
    pixmap.setDevicePixelRatio(renderScale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter, QRectF(1.0, 1.0, logicalSize - 2.0, logicalSize - 2.0));
    painter.end();

    QIcon icon(pixmap);
    cache.insert(cacheKey, icon);
    return icon;
}

QString peakSignSvgResourcePath(int signIndexValue) {
    static const QStringList files = {
        "aries", "taurus", "gemini", "cancer", "leo", "virgo",
        "libra", "scorpio", "sagittarius", "capricorn", "aquarius", "pisces",
    };
    if (signIndexValue < 0 || signIndexValue >= files.size()) {
        return {};
    }
    return QString(":/resources/icons/zodiac_releasing/%1.svg").arg(files[signIndexValue]);
}

QString peakPositionText(double longitude) {
    const double degrees = degInSign(longitude);
    int wholeDegrees = static_cast<int>(std::floor(degrees));
    int minutes = static_cast<int>(std::llround((degrees - wholeDegrees) * 60.0));
    if (minutes >= 60) {
        minutes = 0;
        wholeDegrees = (wholeDegrees + 1) % 30;
    }
    return QString("%1%2%3%4")
        .arg(QString::number(wholeDegrees).rightJustified(2, '0'))
        .arg(QChar(0x00B0))
        .arg(QString::number(minutes).rightJustified(2, '0'))
        .arg(QChar(0x2032));
}

QTableWidgetItem* peakBodyCell(const QString& bodyName, const QColor& color) {
    auto* item = peakCell(bodyName);
    const QIcon icon = peakTintedSvgIcon(bodySvgResourcePath(bodyName), color);
    if (!icon.isNull()) {
        item->setIcon(icon);
    }
    item->setToolTip(bodyName);
    return item;
}

QTableWidgetItem* peakPositionCell(double longitude, const QColor& color) {
    auto* item = peakCell(peakPositionText(longitude), Qt::AlignCenter);
    const QIcon icon = peakTintedSvgIcon(
        peakSignSvgResourcePath(signIndex(longitude)), color);
    if (!icon.isNull()) {
        item->setIcon(icon);
    }
    item->setToolTip(formatDegInSign(longitude));
    return item;
}
QString peakAspectSymbol(const QString& label) {
    if (label == "Conjunction") return QString(QChar(0x260C));
    if (label == "Sextile") return QString(QChar(0x2736));
    if (label == "Square") return QString(QChar(0x25A1));
    if (label == "Trine") return QString(QChar(0x25B3));
    if (label == "Opposition") return QString(QChar(0x260D));
    return {};
}

QColor peakAspectColor(const QString& label) {
    if (label == "Conjunction") return QColor("#C99A2E");
    if (label == "Sextile") return QColor("#3FA7D6");
    if (label == "Square") return QColor("#E0533D");
    if (label == "Trine") return QColor("#3FA66A");
    if (label == "Opposition") return QColor("#9B59B6");
    return QColor("#607D8B");
}

QTableWidgetItem* peakAspectCell(const QString& label) {
    const QString symbol = peakAspectSymbol(label);
    auto* item = peakCell(symbol.isEmpty() ? label : QString("%1  %2").arg(symbol, label),
                          Qt::AlignCenter);
    item->setForeground(peakAspectColor(label));
    QFont font = item->font();
    font.setBold(true);
    item->setFont(font);
    item->setToolTip(QString("%1 aspect").arg(label));
    return item;
}

QString formatPeakWeight(double value, bool includePositiveSign = true) {
    if (std::fabs(value) < 1e-9) value = 0.0;
    const double rounded = std::round(value);
    const int decimals = std::fabs(value - rounded) < 1e-9 ? 0 : 1;
    QString text = QString::number(value, 'f', decimals);
    if (includePositiveSign && value > 0.0) {
        text.prepend('+');
    }
    return text;
}

void stylePeakWeightItem(QTableWidgetItem* item, double signedValue) {
    if (!item) return;
    if (signedValue > 1e-9) {
        item->setForeground(QColor("#2E8B57"));
    } else if (signedValue < -1e-9) {
        item->setForeground(QColor("#C4473A"));
    }
    QFont font = item->font();
    font.setBold(true);
    item->setFont(font);
}

QTableWidgetItem* peakWeightCell(double value) {
    auto* item = peakCell(formatPeakWeight(value), Qt::AlignCenter);
    stylePeakWeightItem(item, value);
    return item;
}

PeakSortItem* peakWeightSortItem(double displayValue, double sortValue) {
    auto* item = new PeakSortItem(formatPeakWeight(displayValue), sortValue, Qt::AlignCenter);
    stylePeakWeightItem(item, displayValue);
    return item;
}

void setupPeakTable(QTableWidget* table, const QStringList& headers, int rows) {
    if (!table) return;
    table->setSortingEnabled(false);
    table->clear();
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setRowCount(std::max(0, rows));
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    if (auto* header = table->horizontalHeader()) {
        header->setStretchLastSection(false);
        for (int i = 0; i < headers.size(); ++i) {
            header->setSectionResizeMode(i, QHeaderView::ResizeToContents);
        }
    }
}

QStringList checkedItems(const QComboBox* combo) {
    QStringList result;
    if (!combo || !combo->model()) return result;
    for (int row = 0; row < combo->model()->rowCount(); ++row) {
        const QModelIndex index = combo->model()->index(row, 0);
        if (index.data(Qt::CheckStateRole).toInt() == Qt::Checked) {
            result.push_back(index.data(Qt::DisplayRole).toString());
        }
    }
    return result;
}

void updateCheckComboLabel(QComboBox* combo, const QString& emptyText) {
    if (!combo || !combo->lineEdit()) return;
    const QStringList selected = checkedItems(combo);
    if (selected.isEmpty()) {
        combo->lineEdit()->setText(emptyText);
    } else if (selected.size() == 1) {
        combo->lineEdit()->setText(selected.front());
    } else {
        combo->lineEdit()->setText(QString("%1 selected").arg(selected.size()));
    }
}

void configureCheckCombo(QComboBox* combo,
                         const QStringList& values,
                         const std::function<bool(const QString&)>& initiallyChecked,
                         const QString& emptyText) {
    combo->setEditable(true);
    combo->lineEdit()->setReadOnly(true);
    combo->lineEdit()->setCursor(Qt::ArrowCursor);
    auto* model = new QStandardItemModel(combo);
    for (const QString& value : values) {
        auto* item = new QStandardItem(value);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setData(initiallyChecked(value) ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole);
        model->appendRow(item);
    }
    combo->setModel(model);
    if (auto* view = combo->view()) {
        QObject::connect(view, &QAbstractItemView::pressed, combo, [model](const QModelIndex& index) {
            if (!index.isValid()) return;
            if (auto* item = model->itemFromIndex(index)) {
                item->setCheckState(item->checkState() == Qt::Checked ? Qt::Unchecked : Qt::Checked);
            }
        });
    }
    QObject::connect(model, &QStandardItemModel::dataChanged, combo,
            [combo, emptyText](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
        if (roles.isEmpty() || roles.contains(Qt::CheckStateRole)) {
            updateCheckComboLabel(combo, emptyText);
        }
    });
    updateCheckComboLabel(combo, emptyText);
}

QWidget* makeCheckComboRow(QComboBox* combo, QWidget* parent, const QString& emptyText) {
    auto* row = new QWidget(parent);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);
    auto* all = new QPushButton("All", row);
    auto* none = new QPushButton("None", row);
    all->setMaximumWidth(52);
    none->setMaximumWidth(52);
    layout->addWidget(combo, 1);
    layout->addWidget(all);
    layout->addWidget(none);
    QObject::connect(all, &QPushButton::clicked, combo, [combo, emptyText]() {
        QSignalBlocker blocker(combo->model());
        for (int i = 0; i < combo->model()->rowCount(); ++i) {
            combo->model()->setData(combo->model()->index(i, 0), Qt::Checked, Qt::CheckStateRole);
        }
        updateCheckComboLabel(combo, emptyText);
    });
    QObject::connect(none, &QPushButton::clicked, combo, [combo, emptyText]() {
        QSignalBlocker blocker(combo->model());
        for (int i = 0; i < combo->model()->rowCount(); ++i) {
            combo->model()->setData(combo->model()->index(i, 0), Qt::Unchecked, Qt::CheckStateRole);
        }
        updateCheckComboLabel(combo, emptyText);
    });
    return row;
}

QString formatPeakOrb(double orb) {
    int totalMinutes = qRound(std::max(0.0, orb) * 60.0);
    const int degrees = totalMinutes / 60;
    const int minutes = totalMinutes % 60;
    return QString("%1%2%3'")
        .arg(degrees)
        .arg(QChar(0x00B0))
        .arg(QString::number(minutes).rightJustified(2, '0'));
}

QString localMoment(const QDateTime& utc, const QTimeZone& timezone) {
    return utc.toTimeZone(timezone).toString("d MMM yyyy, h:mm AP");
}

QString peakScope(const MainWindow::TransitAspectPeakHit& hit) {
    return hit.transitSolarReturn ? "T-SR" : (hit.transitTransit ? "T-T" : "T-N");
}

QString peakSolarReference(const MainWindow::TransitAspectPeakResult& result, const QTimeZone& timezone) {
    return QString("SR %1: %2 to %3 (next return)")
        .arg(result.solarReturnYear)
        .arg(result.solarReturnUtc.toTimeZone(timezone).toString("d MMM yyyy HH:mm:ss"),
             result.nextSolarReturnUtc.toTimeZone(timezone).toString("d MMM yyyy HH:mm:ss"));
}

} // namespace

TransitAspectPeakWorker::TransitAspectPeakWorker(const Config& config)
    : config_(config) {}

const QVector<MainWindow::TransitAspectPeakResult>& TransitAspectPeakWorker::results() const {
    return results_;
}

bool TransitAspectPeakWorker::wasCancelled() const {
    return cancelled_.load();
}

void TransitAspectPeakWorker::run() {
    QString loadError;
    QStringList dllPaths = config_.dllSearchPaths;
    if (config_.includeSolarReturn) {
        // Return calculations must not share Swiss Ephemeris state with the GUI.
        const QString dll = solarEphemerisDirectory_.filePath("aspect_peaks_ephemeris.dll");
        if (!solarEphemerisDirectory_.isValid() || !QFile::copy(config_.ephemerisDllPath, dll)) {
            emit error("Unable to prepare the Solar Return calculation ephemeris.");
            emit finished();
            return;
        }
        dllPaths = {dll};
    }
    if (!swe_.load(dllPaths, &loadError)) {
        emit error(loadError);
        emit finished();
        return;
    }
    if (!config_.ephePath.isEmpty()) {
        swe_.setEphePath(config_.ephePath);
    }
    calcFlags_ = 0;
    if (config_.includeSolarReturn || config_.zodiacSystem == ZodiacSystem::Sidereal)
        swe_.setSidMode(siderealAyanamsaSwissMode(config_.siderealAyanamsa));
    if (config_.zodiacSystem == ZodiacSystem::Sidereal) {
        calcFlags_ = SEFLG_SIDEREAL;
    }
    if (!config_.startUtc.isValid() || !config_.endUtc.isValid()
        || config_.startUtc > config_.endUtc) {
        emit error("Invalid Aspect Peaks range.");
        emit finished();
        return;
    }
    if (config_.transitBodies.isEmpty() || config_.natalTargets.isEmpty()
        || config_.aspectLabels.isEmpty()) {
        emit error("Select transit bodies, natal targets, and at least one aspect.");
        emit finished();
        return;
    }

    const int stepMinutes = std::max(1, config_.sampleMinutes);
    const qint64 stepSeconds = static_cast<qint64>(stepMinutes) * 60;
    const qint64 rangeSeconds = std::max<qint64>(0, config_.startUtc.secsTo(config_.endUtc));
    const qint64 totalSamples64 = (rangeSeconds + stepSeconds - 1) / stepSeconds + 1;
    constexpr qint64 kMaximumSamples = 500000;
    if (totalSamples64 > kMaximumSamples) {
        emit error("The selected range and resolution exceed 500,000 samples. Use a coarser resolution or shorter range.");
        emit finished();
        return;
    }
    const int totalSamples = static_cast<int>(totalSamples64);
    const double allowedOrb = std::clamp(config_.orb, 0.0, 15.0);
    const int minimumHits = std::max(1, config_.minimumHits);

    struct SolarReference {
        QDateTime utc;
        QMap<QString, double> targets;
    };
    QMap<int, SolarReference> solarReferences;
    int solarYear = config_.startUtc.date().year();
    const auto loadSolarReference = [&](int year, bool needTargets, QString* error) {
        if (cancelled_.load()) return false;
        auto& reference = solarReferences[year];
        if (!reference.utc.isValid()) {
            const auto solver = config_.tajakaReturn
                ? returncalc::tajakaSolarReturnTimeUtc : returncalc::solarReturnTimeUtc;
            if (!solver(swe_, config_.natalInput, year, config_.solarReturnInput.timezone,
                        config_.natalSunLongitude, &reference.utc, nullptr, error,
                        [this] { return cancelled_.load(); })) return false;
        }
        if (!needTargets || !reference.targets.isEmpty()) return true;
        NatalInput input = config_.solarReturnInput;
        // The instant is already resolved; using UTC here also avoids DST folds.
        input.date = reference.utc.date();
        input.time = reference.utc.time();
        input.timezone = "UTC";
        TropicalComputeOptions options;
        options.includeArabicLots = false;
        options.includePartOfFortune = false;
        options.includeFixedStars = false;
        options.includeAspectGrid = false;
        TropicalNatalEngine engine(&swe_, config_.ephePath);
        NatalChart chart;
        if (!engine.compute(input, options, &chart, error)) return false;
        QMap<QString, double> positions{
            {"Ascendant", chart.angles.asc}, {"Descendant", chart.angles.desc},
            {"Midheaven", chart.angles.mc}, {"IC", chart.angles.ic}, {"Vertex", chart.angles.vertex}
        };
        for (const auto& body : chart.bodies) positions.insert(body.name, body.longitude);
        for (auto it = config_.natalTargets.cbegin(); it != config_.natalTargets.cend(); ++it) {
            if (!positions.contains(it.key())) {
                *error = QString("Solar Return %1 has no target named %2.").arg(year).arg(it.key());
                return false;
            }
            reference.targets.insert(it.key(), positions.value(it.key()));
        }
        return true;
    };
    const auto resolveSolarYear = [&](const QDateTime& utc, QString* error) {
        if (!loadSolarReference(solarYear, false, error)) return false;
        while (utc < solarReferences.value(solarYear).utc) {
            --solarYear;
            if (!loadSolarReference(solarYear, false, error)) return false;
        }
        for (;;) {
            if (!loadSolarReference(solarYear + 1, false, error)) return false;
            if (solarReferences.value(solarYear + 1).utc <= solarReferences.value(solarYear).utc) {
                *error = "Solar return boundaries are not in chronological order.";
                return false;
            }
            if (utc < solarReferences.value(solarYear + 1).utc) break;
            ++solarYear;
        }
        return loadSolarReference(solarYear, true, error);
    };

    QMap<QDate, MainWindow::TransitAspectPeakResult> bestByDay;
    MainWindow::TransitAspectPeakResult openPeriod;
    bool periodOpen = false;
    QDateTime previousQualifiedUtc;
    results_.clear();

    auto betterPeak = [](const MainWindow::TransitAspectPeakResult& candidate,
                         const MainWindow::TransitAspectPeakResult& current) {
        if (candidate.peakHitCount != current.peakHitCount) {
            return candidate.peakHitCount > current.peakHitCount;
        }
        if (std::fabs(candidate.tightness - current.tightness) > 1e-9) {
            return candidate.tightness > current.tightness;
        }
        return candidate.peakUtc < current.peakUtc;
    };
    auto copyPeak = [](MainWindow::TransitAspectPeakResult* destination,
                       const MainWindow::TransitAspectPeakResult& source) {
        if (!destination) {
            return;
        }
        destination->peakUtc = source.peakUtc;
        destination->peakHitCount = source.peakHitCount;
        destination->transitNatalHitCount = source.transitNatalHitCount;
        destination->transitTransitHitCount = source.transitTransitHitCount;
        destination->transitSolarReturnHitCount = source.transitSolarReturnHitCount;
        destination->solarReturnYear = source.solarReturnYear;
        destination->solarReturnUtc = source.solarReturnUtc;
        destination->nextSolarReturnUtc = source.nextSolarReturnUtc;
        destination->positiveWeight = source.positiveWeight;
        destination->negativeWeight = source.negativeWeight;
        destination->netWeight = source.netWeight;
        destination->tightness = source.tightness;
        destination->transitBodies = source.transitBodies;
        destination->natalTargets = source.natalTargets;
        destination->peakHits = source.peakHits;
    };
    auto finishPeriod = [&]() {
        if (!periodOpen) {
            return;
        }
        results_.push_back(openPeriod);
        openPeriod = MainWindow::TransitAspectPeakResult{};
        periodOpen = false;
        previousQualifiedUtc = QDateTime();
    };

    for (int sampleIndex = 0; sampleIndex < totalSamples; ++sampleIndex) {
        if (cancelled_.load()) {
            break;
        }
        QDateTime sampleUtc = config_.startUtc.addSecs(static_cast<qint64>(sampleIndex) * stepSeconds);
        if (sampleUtc > config_.endUtc) {
            sampleUtc = config_.endUtc;
        }

        QMap<QString, double> transitLongitudes;
        bool calculationFailed = false;
        QString calculationError;
        if (config_.includeSolarReturn && !resolveSolarYear(sampleUtc, &calculationError)) {
            if (cancelled_.load()) break;
            emit error(QString("Solar Return: %1").arg(calculationError));
            results_.clear();
            emit finished();
            return;
        }
        for (const auto& bodyName : config_.transitBodies) {
            double longitude = 0.0;
            if (!planetLongitude(sampleUtc, bodyName, &longitude, &calculationError)) {
                calculationFailed = true;
                break;
            }
            transitLongitudes.insert(bodyName, longitude);
        }
        if (calculationFailed) {
            emit error(calculationError);
            results_.clear();
            emit finished();
            return;
        }

        MainWindow::TransitAspectPeakResult sample;
        sample.startUtc = sampleUtc;
        sample.endUtc = sampleUtc;
        sample.peakUtc = sampleUtc;
        sample.sampleCount = 1;
        if (config_.includeSolarReturn) {
            sample.solarReturnYear = solarYear;
            sample.solarReturnUtc = solarReferences.value(solarYear).utc;
            sample.nextSolarReturnUtc = solarReferences.value(solarYear + 1).utc;
        }
        QSet<QString> activeTransitBodies;
        QSet<QString> activeNatalTargets;
        const auto applyAspectWeight = [&](MainWindow::TransitAspectPeakHit* hit) {
            if (!hit || !config_.weightingEnabled) {
                return;
            }
            hit->weight = config_.aspectWeights.value(hit->aspect, 0.0);
            if (hit->weight > 0.0) {
                sample.positiveWeight += hit->weight;
            } else if (hit->weight < 0.0) {
                sample.negativeWeight += -hit->weight;
            }
            sample.netWeight += hit->weight;
        };

        const auto addReferenceHits = [&](const QMap<QString, double>& moving,
                                          const QMap<QString, double>& targets, bool solar) {
            for (auto transitIt = moving.constBegin(); transitIt != moving.constEnd(); ++transitIt) {
                for (auto natalIt = targets.constBegin(); natalIt != targets.constEnd(); ++natalIt) {
                    const double separation = transitcalc::angularDiffAbs(transitIt.value(), natalIt.value());
                    QString bestAspect;
                    double bestOrb = std::numeric_limits<double>::max();
                    for (const auto& aspectLabel : config_.aspectLabels) {
                        const double aspectOrb = std::fabs(
                            separation - transitcalc::aspectAngleForLabel(aspectLabel));
                        if (aspectOrb <= allowedOrb + 1e-9 && aspectOrb < bestOrb) {
                            bestOrb = aspectOrb;
                            bestAspect = aspectLabel;
                        }
                    }
                    if (bestAspect.isEmpty()) {
                        continue;
                    }
                    MainWindow::TransitAspectPeakHit hit;
                    hit.transitBody = transitIt.key();
                    hit.aspect = bestAspect;
                    hit.natalTarget = natalIt.key();
                    hit.transitLongitude = transitIt.value();
                    hit.natalLongitude = natalIt.value();
                    hit.orb = bestOrb;
                    hit.transitSolarReturn = solar;
                    applyAspectWeight(&hit);
                    sample.peakHits.push_back(hit);
                    if (solar) sample.transitSolarReturnHitCount++;
                    else sample.transitNatalHitCount++;
                    activeTransitBodies.insert(hit.transitBody);
                    activeNatalTargets.insert(hit.natalTarget);
                    sample.tightness += allowedOrb > 1e-9
                        ? std::max(0.0, 1.0 - bestOrb / allowedOrb)
                        : (bestOrb <= 1e-9 ? 1.0 : 0.0);
                }
            }
        };
        addReferenceHits(transitLongitudes, config_.natalTargets, false);
        if (config_.includeSolarReturn) {
            QMap<QString, double> solarTransits = transitLongitudes;
            const int solarFlags = config_.solarReturnInput.zodiacSystem == ZodiacSystem::Sidereal
                ? SEFLG_SIDEREAL : 0;
            // Tajaka can use sidereal targets while the natal search is tropical.
            if (solarFlags != calcFlags_) {
                for (auto it = solarTransits.begin(); it != solarTransits.end(); ++it) {
                    if (!planetLongitude(sampleUtc, it.key(), &it.value(), &calculationError, solarFlags)) {
                        emit error(calculationError);
                        results_.clear();
                        emit finished();
                        return;
                    }
                }
            }
            addReferenceHits(solarTransits, solarReferences.value(solarYear).targets, true);
        }
        if (config_.includeTransitTransit) {
            const QStringList transitNames = transitLongitudes.keys();
            for (int firstIndex = 0; firstIndex < transitNames.size(); ++firstIndex) {
                const QString& firstName = transitNames[firstIndex];
                const double firstLongitude = transitLongitudes.value(firstName);
                for (int secondIndex = firstIndex + 1; secondIndex < transitNames.size(); ++secondIndex) {
                    const QString& secondName = transitNames[secondIndex];
                    const double secondLongitude = transitLongitudes.value(secondName);
                    const double separation = transitcalc::angularDiffAbs(firstLongitude, secondLongitude);
                    QString bestAspect;
                    double bestOrb = std::numeric_limits<double>::max();
                    for (const auto& aspectLabel : config_.aspectLabels) {
                        const double aspectOrb = std::fabs(
                            separation - transitcalc::aspectAngleForLabel(aspectLabel));
                        if (aspectOrb <= allowedOrb + 1e-9 && aspectOrb < bestOrb) {
                            bestOrb = aspectOrb;
                            bestAspect = aspectLabel;
                        }
                    }
                    if (bestAspect.isEmpty()) {
                        continue;
                    }
                    MainWindow::TransitAspectPeakHit hit;
                    hit.transitBody = firstName;
                    hit.aspect = bestAspect;
                    hit.natalTarget = secondName;
                    hit.transitLongitude = firstLongitude;
                    hit.natalLongitude = secondLongitude;
                    hit.orb = bestOrb;
                    hit.transitTransit = true;
                    applyAspectWeight(&hit);
                    sample.peakHits.push_back(hit);
                    sample.transitTransitHitCount++;
                    activeTransitBodies.insert(firstName);
                    activeTransitBodies.insert(secondName);
                    sample.tightness += allowedOrb > 1e-9
                        ? std::max(0.0, 1.0 - bestOrb / allowedOrb)
                        : (bestOrb <= 1e-9 ? 1.0 : 0.0);
                }
            }
        }

        std::sort(sample.peakHits.begin(), sample.peakHits.end(),
                  [](const MainWindow::TransitAspectPeakHit& a,
                     const MainWindow::TransitAspectPeakHit& b) {
            if (std::fabs(a.orb - b.orb) > 1e-9) {
                return a.orb < b.orb;
            }
            if (a.transitTransit != b.transitTransit) {
                return !a.transitTransit;
            }
            if (a.transitSolarReturn != b.transitSolarReturn) return !a.transitSolarReturn;
            if (a.transitBody != b.transitBody) {
                return a.transitBody < b.transitBody;
            }
            return a.natalTarget < b.natalTarget;
        });
        sample.peakHitCount = sample.peakHits.size();
        sample.transitBodies = activeTransitBodies.values();
        sample.natalTargets = activeNatalTargets.values();
        sample.transitBodies.sort(Qt::CaseInsensitive);
        sample.natalTargets.sort(Qt::CaseInsensitive);

        const bool qualifies = sample.peakHitCount >= minimumHits;
        if (config_.groupPeriods) {
            if (qualifies) {
                const bool continues = periodOpen && previousQualifiedUtc.isValid()
                    && previousQualifiedUtc.secsTo(sampleUtc) <= stepSeconds + 1
                    && openPeriod.solarReturnYear == sample.solarReturnYear;
                if (!continues) {
                    finishPeriod();
                    openPeriod = sample;
                    openPeriod.groupedPeriod = true;
                    periodOpen = true;
                } else {
                    openPeriod.endUtc = sampleUtc;
                    openPeriod.sampleCount++;
                    if (betterPeak(sample, openPeriod)) {
                        copyPeak(&openPeriod, sample);
                    }
                }
                previousQualifiedUtc = sampleUtc;
            } else {
                finishPeriod();
            }
        } else if (qualifies) {
            const QDate localDate = sampleUtc.toTimeZone(config_.timezone).date();
            if (!bestByDay.contains(localDate) || betterPeak(sample, bestByDay.value(localDate))) {
                bestByDay.insert(localDate, sample);
            }
        }

        if (sampleIndex % 25 == 0 || sampleIndex + 1 == totalSamples) {
            emit progress(sampleIndex + 1, totalSamples);
        }
    }
    finishPeriod();
    if (!config_.groupPeriods) {
        results_.clear();
        results_.reserve(bestByDay.size());
        for (auto it = bestByDay.constBegin(); it != bestByDay.constEnd(); ++it) {
            results_.push_back(it.value());
        }
    }
    emit finished();
}

void TransitAspectPeakWorker::cancel() {
    cancelled_.store(true);
}

bool TransitAspectPeakWorker::planetLongitude(const QDateTime& utc, const QString& name,
                                              double* outLongitude, QString* error, int flags) {
    const int bodyId = transitcalc::bodyIdForName(
        name, effectivePrimaryNodeType(config_.lunarNodePolicy));
    if (bodyId < 0) {
        if (error) {
            *error = QString("Unsupported transit body: %1").arg(name);
        }
        return false;
    }
    const QDate date = utc.date();
    const QTime time = utc.time();
    const double hour = time.hour() + time.minute() / 60.0
        + time.second() / 3600.0 + time.msec() / 3600000.0;
    const double jd = swe_.julianDay(
        date.year(), date.month(), date.day(), hour, SE_GREG_CAL);
    double longitude = 0.0;
    QString calcError;
    if (!swe_.calcUt(jd, bodyId, flags < 0 ? calcFlags_ : flags, &longitude, &calcError)) {
        if (error) {
            *error = calcError;
        }
        return false;
    }
    longitude = normalizeDegrees(longitude);
    if (isLunarNodeName(name) && !isNorthLunarNodeName(name)) {
        longitude = normalizeDegrees(longitude + 180.0);
    }
    if (outLongitude) {
        *outLongitude = longitude;
    }
    return true;
}

QWidget* MainWindow::createTransitAspectPeakPanel(QWidget* parent) {
    auto* panel = new QWidget(parent);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(7);

    auto* intro = new QLabel(
        "Find peak days or periods of transit-to-natal aspects, optionally including transit-to-transit and annual Solar Return hits.", panel);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    aspectPeakRangeGroup_ = new QGroupBox("Search Range", panel);
    auto* rangeGroup = aspectPeakRangeGroup_;
    auto* range = new QGridLayout(rangeGroup);
    aspectPeakStartDateEdit_ = new QDateEdit(QDate::currentDate(), rangeGroup);
    aspectPeakStartDateEdit_->setCalendarPopup(true);
    aspectPeakStartTimeEdit_ = new QTimeEdit(QTime::currentTime(), rangeGroup);
    aspectPeakEndDateEdit_ = new QDateEdit(QDate::currentDate().addYears(1), rangeGroup);
    aspectPeakEndDateEdit_->setCalendarPopup(true);
    aspectPeakEndTimeEdit_ = new QTimeEdit(QTime::currentTime(), rangeGroup);
    aspectPeakTimezoneEdit_ = new QLineEdit(rangeGroup);
    aspectPeakTimezoneEdit_->setPlaceholderText("e.g. Asia/Dhaka");
    range->addWidget(new QLabel("Start", rangeGroup), 0, 0);
    range->addWidget(aspectPeakStartDateEdit_, 0, 1);
    range->addWidget(aspectPeakStartTimeEdit_, 0, 2);
    range->addWidget(new QLabel("End", rangeGroup), 1, 0);
    range->addWidget(aspectPeakEndDateEdit_, 1, 1);
    range->addWidget(aspectPeakEndTimeEdit_, 1, 2);
    range->addWidget(new QLabel("Timezone", rangeGroup), 2, 0);
    range->addWidget(aspectPeakTimezoneEdit_, 2, 1, 1, 2);
    range->setColumnStretch(1, 1);
    layout->addWidget(rangeGroup);

    aspectPeakSelectionGroup_ = new QGroupBox("Bodies and Aspects", panel);
    auto* selectionGroup = aspectPeakSelectionGroup_;
    auto* selection = new QGridLayout(selectionGroup);
    aspectPeakTransitBodiesCombo_ = new QComboBox(selectionGroup);
    configureCheckCombo(aspectPeakTransitBodiesCombo_, transitcalc::transitCalculableBodyOrder(),
        [](const QString& name) {
            return QStringList{"Sun", "Moon", "Mercury", "Venus", "Mars",
                               "Jupiter", "Saturn", "Uranus", "Neptune", "Pluto"}.contains(name);
        }, "Select transit bodies");

    QStringList natalNames = transitcalc::transitCalculableBodyOrder();
    natalNames << "Ascendant" << "Descendant" << "Midheaven" << "IC" << "Vertex";
    aspectPeakNatalTargetsCombo_ = new QComboBox(selectionGroup);
    configureCheckCombo(aspectPeakNatalTargetsCombo_, natalNames,
        [](const QString& name) {
            return QStringList{"Sun", "Moon", "Mercury", "Venus", "Mars",
                               "Jupiter", "Saturn", "Uranus", "Neptune", "Pluto",
                               "Ascendant", "Descendant", "Midheaven", "IC", "Vertex"}.contains(name);
        }, "Select natal targets");

    aspectPeakAspectsCombo_ = new QComboBox(selectionGroup);
    configureCheckCombo(aspectPeakAspectsCombo_,
        {"Conjunction", "Sextile", "Square", "Trine", "Opposition"},
        [](const QString&) { return true; }, "Select aspects");

    selection->addWidget(new QLabel("Transit bodies", selectionGroup), 0, 0);
    selection->addWidget(makeCheckComboRow(aspectPeakTransitBodiesCombo_, selectionGroup, "Select transit bodies"), 0, 1);
    auto* targetsLabel = new QLabel("Chart targets", selectionGroup);
    targetsLabel->setToolTip("Target bodies and angles in the natal chart and, when enabled, each active Solar Return chart.");
    selection->addWidget(targetsLabel, 1, 0);
    selection->addWidget(makeCheckComboRow(aspectPeakNatalTargetsCombo_, selectionGroup, "Select natal targets"), 1, 1);
    selection->addWidget(new QLabel("Aspects", selectionGroup), 2, 0);
    selection->addWidget(makeCheckComboRow(aspectPeakAspectsCombo_, selectionGroup, "Select aspects"), 2, 1);
    selection->setColumnStretch(1, 1);
    layout->addWidget(selectionGroup);

    aspectPeakSettingsGroup_ = new QGroupBox("Peak Definition", panel);
    auto* settingsGroup = aspectPeakSettingsGroup_;
    auto* settings = new QGridLayout(settingsGroup);
    aspectPeakModeCombo_ = new QComboBox(settingsGroup);
    aspectPeakModeCombo_->addItem("Peak Days (daily maximum)", false);
    aspectPeakModeCombo_->addItem("Peak Periods (continuous)", true);
    aspectPeakOrbSpin_ = new QDoubleSpinBox(settingsGroup);
    aspectPeakOrbSpin_->setRange(0.1, 15.0);
    aspectPeakOrbSpin_->setDecimals(1);
    aspectPeakOrbSpin_->setSingleStep(0.5);
    aspectPeakOrbSpin_->setValue(1.0);
    aspectPeakOrbSpin_->setSuffix(" deg");
    aspectPeakResolutionCombo_ = new QComboBox(settingsGroup);
    aspectPeakResolutionCombo_->addItem("1 hour", 60);
    aspectPeakResolutionCombo_->addItem("3 hours", 180);
    aspectPeakResolutionCombo_->addItem("6 hours", 360);
    aspectPeakResolutionCombo_->addItem("12 hours", 720);
    aspectPeakResolutionCombo_->addItem("1 day", 1440);
    aspectPeakResolutionCombo_->setCurrentIndex(2);
    aspectPeakIncludeTransitTransitCheck_ = new QCheckBox("Include transit-transit hits", settingsGroup);
    aspectPeakIncludeTransitTransitCheck_->setChecked(false);
    aspectPeakIncludeTransitTransitCheck_->setToolTip(
        "Add aspects between unique pairs of the selected transit bodies to the same raw peak hit count.");
    aspectPeakIncludeSolarReturnCheck_ = new QCheckBox("Include transit-Solar Return hits", settingsGroup);
    aspectPeakIncludeSolarReturnCheck_->setToolTip(
        "Use the Solar Return tab's method and location. Switch annual targets at each return moment; "
        "use the same chart targets, aspects, orb, and weights. Counts include T-N and T-SR separately.");
    aspectPeakSolarContextLabel_ = new QLabel(settingsGroup);
    aspectPeakSolarContextLabel_->setWordWrap(true);
    aspectPeakSolarContextLabel_->hide();
    aspectPeakMinHitsSpin_ = new QSpinBox(settingsGroup);
    aspectPeakMinHitsSpin_->setRange(1, 500);
    aspectPeakMinHitsSpin_->setValue(2);
    aspectPeakTopCountSpin_ = new QSpinBox(settingsGroup);
    aspectPeakTopCountSpin_->setRange(1, 1000);
    aspectPeakTopCountSpin_->setValue(100);
    settings->addWidget(new QLabel("Result mode", settingsGroup), 0, 0);
    settings->addWidget(aspectPeakModeCombo_, 0, 1);
    settings->addWidget(new QLabel("Maximum orb", settingsGroup), 1, 0);
    settings->addWidget(aspectPeakOrbSpin_, 1, 1);
    settings->addWidget(new QLabel("Sampling resolution", settingsGroup), 2, 0);
    settings->addWidget(aspectPeakResolutionCombo_, 2, 1);
    settings->addWidget(aspectPeakIncludeTransitTransitCheck_, 3, 0, 1, 2);
    settings->addWidget(aspectPeakIncludeSolarReturnCheck_, 4, 0, 1, 2);
    settings->addWidget(aspectPeakSolarContextLabel_, 5, 0, 1, 2);
    settings->addWidget(new QLabel("Minimum simultaneous hits", settingsGroup), 6, 0);
    settings->addWidget(aspectPeakMinHitsSpin_, 6, 1);
    settings->addWidget(new QLabel("Show top", settingsGroup), 7, 0);
    settings->addWidget(aspectPeakTopCountSpin_, 7, 1);
    settings->setColumnStretch(1, 1);
    layout->addWidget(settingsGroup);

    aspectPeakWeightingGroup_ = new QGroupBox("Aspect Weighting (Optional)", panel);
    aspectPeakWeightingGroup_->setCheckable(true);
    aspectPeakWeightingGroup_->setChecked(false);
    auto* weightingOuter = new QVBoxLayout(aspectPeakWeightingGroup_);
    aspectPeakWeightingOptions_ = new QWidget(aspectPeakWeightingGroup_);
    auto* weighting = new QGridLayout(aspectPeakWeightingOptions_);
    weighting->setContentsMargins(0, 0, 0, 0);
    const auto makeWeightSpin = [this](QWidget* parent, double value) {
        auto* spin = new QDoubleSpinBox(parent);
        spin->setRange(-10.0, 10.0);
        spin->setDecimals(1);
        spin->setSingleStep(0.5);
        spin->setValue(value);
        spin->setToolTip("Signed contribution made by each matching aspect. Positive adds support; negative adds challenge.");
        return spin;
    };
    aspectPeakConjunctionWeightSpin_ = makeWeightSpin(aspectPeakWeightingOptions_, 0.0);
    aspectPeakSextileWeightSpin_ = makeWeightSpin(aspectPeakWeightingOptions_, 1.0);
    aspectPeakSquareWeightSpin_ = makeWeightSpin(aspectPeakWeightingOptions_, -2.0);
    aspectPeakTrineWeightSpin_ = makeWeightSpin(aspectPeakWeightingOptions_, 2.0);
    aspectPeakOppositionWeightSpin_ = makeWeightSpin(aspectPeakWeightingOptions_, -2.0);
    aspectPeakWeightRankingCombo_ = new QComboBox(aspectPeakWeightingOptions_);
    aspectPeakWeightRankingCombo_->addItem("Most simultaneous hits", "hits");
    aspectPeakWeightRankingCombo_->addItem("Highest net", "net_high");
    aspectPeakWeightRankingCombo_->addItem("Lowest net", "net_low");
    aspectPeakWeightRankingCombo_->addItem("Most positive", "positive");
    aspectPeakWeightRankingCombo_->addItem("Most negative", "negative");
    weighting->addWidget(new QLabel("Conjunction", aspectPeakWeightingOptions_), 0, 0);
    weighting->addWidget(aspectPeakConjunctionWeightSpin_, 0, 1);
    weighting->addWidget(new QLabel("Sextile", aspectPeakWeightingOptions_), 1, 0);
    weighting->addWidget(aspectPeakSextileWeightSpin_, 1, 1);
    weighting->addWidget(new QLabel("Square", aspectPeakWeightingOptions_), 2, 0);
    weighting->addWidget(aspectPeakSquareWeightSpin_, 2, 1);
    weighting->addWidget(new QLabel("Trine", aspectPeakWeightingOptions_), 3, 0);
    weighting->addWidget(aspectPeakTrineWeightSpin_, 3, 1);
    weighting->addWidget(new QLabel("Opposition", aspectPeakWeightingOptions_), 4, 0);
    weighting->addWidget(aspectPeakOppositionWeightSpin_, 4, 1);
    weighting->addWidget(new QLabel("Result ranking", aspectPeakWeightingOptions_), 5, 0);
    weighting->addWidget(aspectPeakWeightRankingCombo_, 5, 1);
    auto* weightingExplanation = new QLabel(
        "Net = positive total - negative total. Peak qualification remains based on raw simultaneous hits.",
        aspectPeakWeightingOptions_);
    weightingExplanation->setWordWrap(true);
    weighting->addWidget(weightingExplanation, 6, 0, 1, 2);
    weighting->setColumnStretch(1, 1);
    weightingOuter->addWidget(aspectPeakWeightingOptions_);
    aspectPeakWeightingOptions_->setVisible(false);
    layout->addWidget(aspectPeakWeightingGroup_);

    auto* runGroup = new QGroupBox("Run", panel);
    auto* run = new QGridLayout(runGroup);
    aspectPeakRunButton_ = new QPushButton("Find Aspect Peaks", runGroup);
    aspectPeakCancelButton_ = new QPushButton("Stop", runGroup);
    aspectPeakCancelButton_->setEnabled(false);
    aspectPeakProgressBar_ = new QProgressBar(runGroup);
    aspectPeakProgressBar_->setRange(0, 100);
    aspectPeakProgressBar_->setValue(0);
    aspectPeakStatusLabel_ = new QLabel("Idle", runGroup);
    run->addWidget(aspectPeakRunButton_, 0, 0);
    run->addWidget(aspectPeakCancelButton_, 0, 1);
    run->addWidget(aspectPeakProgressBar_, 1, 0, 1, 2);
    run->addWidget(aspectPeakStatusLabel_, 2, 0, 1, 2);
    layout->addWidget(runGroup);
    layout->addStretch();

    connect(aspectPeakRunButton_, &QPushButton::clicked, this, &MainWindow::handleTransitAspectPeakStart);
    connect(aspectPeakCancelButton_, &QPushButton::clicked, this, &MainWindow::handleTransitAspectPeakCancel);
    connect(aspectPeakTopCountSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int) {
        if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::AspectPeaks)
            updateTransitAspectPeakResultsTable();
    });
    const auto invalidateResults = [this]() {
        if (transitAspectPeakRunning_ || transitAspectPeakResults_.isEmpty()) return;
        transitAspectPeakResults_.clear();
        transitAspectPeakDisplayOrder_.clear();
        hasTransitAspectPeakSelection_ = false;
        if (aspectPeakStatusLabel_) aspectPeakStatusLabel_->setText("Settings changed - run the search again.");
        if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::AspectPeaks)
            refreshTransitAspectPeakTab();
    };
    connect(aspectPeakStartDateEdit_, &QDateEdit::dateChanged, this, invalidateResults);
    connect(aspectPeakStartTimeEdit_, &QTimeEdit::timeChanged, this, invalidateResults);
    connect(aspectPeakEndDateEdit_, &QDateEdit::dateChanged, this, invalidateResults);
    connect(aspectPeakEndTimeEdit_, &QTimeEdit::timeChanged, this, invalidateResults);
    connect(aspectPeakTimezoneEdit_, &QLineEdit::textChanged, this, invalidateResults);
    connect(aspectPeakOrbSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, invalidateResults);
    connect(aspectPeakMinHitsSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, invalidateResults);
    connect(aspectPeakResolutionCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, invalidateResults);
    connect(aspectPeakModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, invalidateResults);
    connect(aspectPeakIncludeTransitTransitCheck_, &QCheckBox::toggled, this, invalidateResults);
    connect(aspectPeakIncludeSolarReturnCheck_, &QCheckBox::toggled, this, [this, invalidateResults]() {
        invalidateResults();
        refreshTransitAspectPeakTab();
    });
    connect(aspectPeakWeightingGroup_, &QGroupBox::toggled, this,
            [this, invalidateResults](bool enabled) {
        if (aspectPeakWeightingOptions_) aspectPeakWeightingOptions_->setVisible(enabled);
        invalidateResults();
    });
    for (QDoubleSpinBox* spin : {aspectPeakConjunctionWeightSpin_, aspectPeakSextileWeightSpin_,
                                 aspectPeakSquareWeightSpin_, aspectPeakTrineWeightSpin_,
                                 aspectPeakOppositionWeightSpin_}) {
        connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, invalidateResults);
    }
    connect(aspectPeakWeightRankingCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        aspectPeakLastWeightRanking_ = aspectPeakWeightRankingCombo_
            ? aspectPeakWeightRankingCombo_->currentData().toString() : QString("hits");
        if (activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::AspectPeaks
            && !transitAspectPeakResults_.isEmpty()) {
            updateTransitAspectPeakResultsTable();
        }
    });
    for (QComboBox* combo : {aspectPeakTransitBodiesCombo_, aspectPeakNatalTargetsCombo_, aspectPeakAspectsCombo_}) {
        connect(combo->model(), &QAbstractItemModel::dataChanged, this,
                [invalidateResults](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
            if (roles.isEmpty() || roles.contains(Qt::CheckStateRole)) invalidateResults();
        });
    }
    return panel;
}

bool MainWindow::resolveAspectPeakSolarInput(NatalInput* input, QString* context, QString* error) const {
    QString timezone, location;
    double latitude = 0.0, longitude = 0.0;
    if (!resolveSolarReturnContext(&timezone, &location, &latitude, &longitude, error)) return false;
    *input = currentInput_;
    input->timezone = timezone;
    input->latitude = latitude;
    input->longitude = longitude;
    input->lunarNodePolicy = currentChart_.lunarNodePolicy;
    input->useDefaultLunarNodePolicy = false;
    const bool tajaka = solarChartMethod() == SolarChartMethod::Tajaka;
    if (tajaka) {
        input->zodiacSystem = ZodiacSystem::Sidereal;
        input->houseSystem = HouseSystem::WholeSign;
    }
    *context = QString("%1 · %2 · %3, %4 · %5 · %6")
        .arg(tajaka ? "Tajaka (tropical return timing)" : "Standard return",
             location.isEmpty() ? "Return location" : location)
        .arg(latitude, 0, 'f', 4).arg(longitude, 0, 'f', 4)
        .arg(timezone, input->zodiacSystem == ZodiacSystem::Sidereal
            ? "Sidereal / " + siderealAyanamsaToString(input->siderealAyanamsa) : QString("Tropical"));
    return true;
}

void MainWindow::handleTransitAspectPeakStart() {
    if (!hasCurrentChart_) {
        setStatusMessage("Load a natal chart before searching for aspect peaks.");
        return;
    }
    if (transitAspectPeakRunning_) return;

    QString timezoneText = aspectPeakTimezoneEdit_ ? aspectPeakTimezoneEdit_->text().trimmed() : QString();
    if (timezoneText.isEmpty()) timezoneText = currentInput_.timezone.trimmed();
    if (timezoneText.isEmpty()) timezoneText = "UTC";
    QTimeZone timezone;
    QString timezoneLabel;
    QString timezoneError;
    if (!parseTimezoneInput(timezoneText, &timezone, &timezoneLabel, &timezoneError)) {
        setStatusMessage(timezoneError);
        return;
    }

    QDateTime startLocal(aspectPeakStartDateEdit_->date(), aspectPeakStartTimeEdit_->time(), timezone);
    QDateTime endLocal(aspectPeakEndDateEdit_->date(), aspectPeakEndTimeEdit_->time(), timezone);
    if (!startLocal.isValid() || !endLocal.isValid() || startLocal > endLocal) {
        setStatusMessage("Aspect Peaks needs a valid start moment before the end moment.");
        return;
    }

    const QStringList transitBodies = checkedItems(aspectPeakTransitBodiesCombo_);
    const QStringList requestedTargets = checkedItems(aspectPeakNatalTargetsCombo_);
    const QStringList aspects = checkedItems(aspectPeakAspectsCombo_);
    if (transitBodies.isEmpty() || requestedTargets.isEmpty() || aspects.isEmpty()) {
        setStatusMessage("Select at least one transit body, natal target, and aspect.");
        return;
    }

    QMap<QString, double> natalTargets;
    for (const BodyPosition& body : currentChart_.bodies) {
        if (requestedTargets.contains(body.name)) natalTargets.insert(body.name, body.longitude);
    }
    const QMap<QString, double> angles = {
        {"Ascendant", currentChart_.angles.asc}, {"Descendant", currentChart_.angles.desc},
        {"Midheaven", currentChart_.angles.mc}, {"IC", currentChart_.angles.ic},
        {"Vertex", currentChart_.angles.vertex},
    };
    for (auto it = angles.constBegin(); it != angles.constEnd(); ++it) {
        if (requestedTargets.contains(it.key())) natalTargets.insert(it.key(), it.value());
    }
    if (natalTargets.isEmpty()) {
        setStatusMessage("None of the selected natal targets exists in the loaded chart.");
        return;
    }

    TransitAspectPeakWorker::Config config;
    config.startUtc = startLocal.toUTC();
    config.endUtc = endLocal.toUTC();
    config.timezone = timezone;
    config.timezoneLabel = timezoneLabel;
    config.zodiacSystem = currentInput_.zodiacSystem;
    config.siderealAyanamsa = currentInput_.siderealAyanamsa;
    config.lunarNodePolicy = currentChart_.lunarNodePolicy;
    config.transitBodies = transitBodies;
    config.natalTargets = natalTargets;
    config.aspectLabels = aspects;
    config.orb = aspectPeakOrbSpin_->value();
    config.sampleMinutes = aspectPeakResolutionCombo_->currentData().toInt();
    config.minimumHits = aspectPeakMinHitsSpin_->value();
    config.groupPeriods = aspectPeakModeCombo_->currentData().toBool();
    config.includeTransitTransit = aspectPeakIncludeTransitTransitCheck_
        && aspectPeakIncludeTransitTransitCheck_->isChecked();
    config.includeSolarReturn = aspectPeakIncludeSolarReturnCheck_
        && aspectPeakIncludeSolarReturnCheck_->isChecked();
    QString solarContext;
    if (config.includeSolarReturn) {
        QString error;
        if (!resolveAspectPeakSolarInput(&config.solarReturnInput, &solarContext, &error)
            || !solarReturnTargetSunLongitude(&config.natalSunLongitude, &error)) {
            setStatusMessage(error);
            return;
        }
        config.natalInput = currentInput_;
        config.tajakaReturn = solarChartMethod() == SolarChartMethod::Tajaka;
        config.ephemerisDllPath = swe_.loadedPath();
    }
    config.weightingEnabled = aspectPeakWeightingGroup_ && aspectPeakWeightingGroup_->isChecked();
    config.aspectWeights = {
        {"Conjunction", aspectPeakConjunctionWeightSpin_->value()},
        {"Sextile", aspectPeakSextileWeightSpin_->value()},
        {"Square", aspectPeakSquareWeightSpin_->value()},
        {"Trine", aspectPeakTrineWeightSpin_->value()},
        {"Opposition", aspectPeakOppositionWeightSpin_->value()},
    };
    config.ephePath = ephePath_;
    config.dllSearchPaths = sweSearchPaths();

    aspectPeakLastTz_ = timezone;
    aspectPeakLastTzLabel_ = timezoneLabel;
    aspectPeakLastRangeStartUtc_ = config.startUtc;
    aspectPeakLastRangeEndUtc_ = config.endUtc;
    aspectPeakLastTransitBodies_ = transitBodies;
    aspectPeakLastNatalTargets_ = natalTargets.keys();
    aspectPeakLastAspects_ = aspects;
    aspectPeakLastOrb_ = config.orb;
    aspectPeakLastResolutionMinutes_ = config.sampleMinutes;
    aspectPeakLastMinHits_ = config.minimumHits;
    aspectPeakLastGroupedPeriods_ = config.groupPeriods;
    aspectPeakLastIncludeTransitTransit_ = config.includeTransitTransit;
    aspectPeakLastIncludeSolarReturn_ = config.includeSolarReturn;
    aspectPeakLastSolarContext_ = solarContext;
    aspectPeakLastWeightingEnabled_ = config.weightingEnabled;
    aspectPeakLastAspectWeights_ = config.aspectWeights;
    aspectPeakLastWeightRanking_ = aspectPeakWeightRankingCombo_
        ? aspectPeakWeightRankingCombo_->currentData().toString() : QString("hits");

    transitAspectPeakResults_.clear();
    transitAspectPeakDisplayOrder_.clear();
    hasTransitAspectPeakSelection_ = false;
    transitAspectPeakRunning_ = true;
    aspectPeakLastScanPartial_ = false;
    aspectPeakRunButton_->setEnabled(false);
    aspectPeakCancelButton_->setEnabled(true);
    if (aspectPeakRangeGroup_) aspectPeakRangeGroup_->setEnabled(false);
    if (aspectPeakSelectionGroup_) aspectPeakSelectionGroup_->setEnabled(false);
    if (aspectPeakSettingsGroup_) aspectPeakSettingsGroup_->setEnabled(false);
    if (aspectPeakWeightingGroup_) aspectPeakWeightingGroup_->setEnabled(false);
    aspectPeakProgressBar_->setRange(0, 100);
    aspectPeakProgressBar_->setValue(0);
    aspectPeakStatusLabel_->setText("Scanning...");
    updateLunationCopyButtonState();

    auto* worker = new TransitAspectPeakWorker(config);
    aspectPeakWorker_ = worker;
    aspectPeakThread_ = new QThread(this);
    worker->moveToThread(aspectPeakThread_);
    connect(aspectPeakThread_, &QThread::started, worker, &TransitAspectPeakWorker::run);
    connect(worker, &TransitAspectPeakWorker::progress, this, [this](int done, int total) {
        if (!aspectPeakProgressBar_) return;
        aspectPeakProgressBar_->setRange(0, std::max(1, total));
        aspectPeakProgressBar_->setValue(done);
    });
    connect(worker, &TransitAspectPeakWorker::error, this, [this](const QString& message) {
        if (aspectPeakWorker_) aspectPeakWorker_->setProperty("runError", message);
        setStatusMessage(message);
        if (aspectPeakStatusLabel_) aspectPeakStatusLabel_->setText("Failed: " + message);
    });
    connect(worker, &TransitAspectPeakWorker::finished, this, &MainWindow::handleTransitAspectPeakFinished);
    connect(aspectPeakThread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(aspectPeakThread_, &QThread::finished, aspectPeakThread_, &QObject::deleteLater);
    aspectPeakThread_->start();
    refreshTransitAspectPeakTab();
}

void MainWindow::handleTransitAspectPeakCancel() {
    if (!transitAspectPeakRunning_ || !aspectPeakWorker_) return;
    if (auto* worker = qobject_cast<TransitAspectPeakWorker*>(aspectPeakWorker_)) worker->cancel();
    if (aspectPeakCancelButton_) aspectPeakCancelButton_->setEnabled(false);
    if (aspectPeakStatusLabel_) aspectPeakStatusLabel_->setText("Stopping...");
}

void MainWindow::handleTransitAspectPeakFinished() {
    bool cancelled = false;
    QString runError;
    if (aspectPeakWorker_) {
        runError = aspectPeakWorker_->property("runError").toString();
        const bool discardResults = aspectPeakWorker_->property("discardResults").toBool();
        if (auto* worker = qobject_cast<TransitAspectPeakWorker*>(aspectPeakWorker_)) {
            if (!discardResults) {
                transitAspectPeakResults_ = worker->results();
            } else {
                transitAspectPeakResults_.clear();
            }
            cancelled = worker->wasCancelled();
        }
        aspectPeakWorker_ = nullptr;
    }
    transitAspectPeakRunning_ = false;
    aspectPeakLastScanPartial_ = cancelled;
    if (aspectPeakRunButton_) aspectPeakRunButton_->setEnabled(true);
    if (aspectPeakCancelButton_) aspectPeakCancelButton_->setEnabled(false);
    if (aspectPeakRangeGroup_) aspectPeakRangeGroup_->setEnabled(true);
    if (aspectPeakSelectionGroup_) aspectPeakSelectionGroup_->setEnabled(true);
    if (aspectPeakSettingsGroup_) aspectPeakSettingsGroup_->setEnabled(true);
    if (aspectPeakWeightingGroup_) aspectPeakWeightingGroup_->setEnabled(true);
    if (aspectPeakProgressBar_ && !cancelled && runError.isEmpty()) {
        aspectPeakProgressBar_->setValue(aspectPeakProgressBar_->maximum());
    }
    if (aspectPeakStatusLabel_ && runError.isEmpty()) {
        aspectPeakStatusLabel_->setText(cancelled
            ? QString("Stopped — %1 partial results").arg(transitAspectPeakResults_.size())
            : QString("Done — %1 qualifying results").arg(transitAspectPeakResults_.size()));
    }
    if (aspectPeakThread_) {
        aspectPeakThread_->quit();
        aspectPeakThread_ = nullptr;
    }
    updateTransitAspectPeakResultsTable();
    refreshTransitAspectPeakTab();
}

void MainWindow::updateAspectPeakTableDensity() {
    const bool compact = activeTab_ == AppTab::Transits
        && transitSubTab_ == TransitSubTab::AspectPeaks;
    // These docks are shared by other tabs. Restore their original density on exit.
    for (auto* table : {rightTopTable_, rightBottomTable_}) {
        if (!table) continue;
        auto* horizontal = table->horizontalHeader();
        auto* vertical = table->verticalHeader();
        const QVariantMap saved = table->property("aspectPeakDensity").toMap();
        if (compact && saved.isEmpty()) {
            table->setProperty("aspectPeakDensity", QVariantMap{
                {"style", table->styleSheet()},
                {"columnMinimum", horizontal->minimumSectionSize()},
                {"rowMinimum", vertical->minimumSectionSize()},
                {"rowHeight", vertical->defaultSectionSize()},
                {"rowMode", int(vertical->sectionResizeMode(0))},
                {"scrollMode", int(table->horizontalScrollMode())}
            });
            table->setStyleSheet(table->styleSheet()
                + " QTableWidget::item { padding: 1px 3px; }"
                  " QHeaderView::section { padding: 2px 3px; }");
            horizontal->setMinimumSectionSize(24);
            vertical->setMinimumSectionSize(20);
            vertical->setSectionResizeMode(QHeaderView::Fixed);
            vertical->setDefaultSectionSize(std::max(22, table->fontMetrics().height() + 4));
            table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
            table->horizontalScrollBar()->setValue(0);
        } else if (!compact && !saved.isEmpty()) {
            table->setStyleSheet(saved.value("style").toString());
            horizontal->setMinimumSectionSize(saved.value("columnMinimum").toInt());
            vertical->setMinimumSectionSize(saved.value("rowMinimum").toInt());
            vertical->setSectionResizeMode(QHeaderView::ResizeMode(saved.value("rowMode").toInt()));
            vertical->setDefaultSectionSize(saved.value("rowHeight").toInt());
            table->setHorizontalScrollMode(QAbstractItemView::ScrollMode(saved.value("scrollMode").toInt()));
            table->setProperty("aspectPeakDensity", QVariant());
        }
    }
}

void MainWindow::updateAspectPeakGraphVisibility() {
    if (!aspectPeakGraph_ || !aspectPeakViewCombo_ || !centerStack_) return;
    const bool inPeaks = activeTab_ == AppTab::Transits && transitSubTab_ == TransitSubTab::AspectPeaks;
    const bool wasGraph = centerStack_->currentWidget() == aspectPeakGraph_;
    const bool graph = inPeaks && aspectPeakViewCombo_->currentIndex() == 1;
    aspectPeakViewCombo_->setVisible(inPeaks);
    if (!inPeaks && !wasGraph) return;
    centerStack_->setCurrentWidget(graph ? static_cast<QWidget*>(aspectPeakGraph_) : chartViewPanel_);
    if (chartTitleLabel_) chartTitleLabel_->setText(graph ? "Aspect Peaks timeline" : "Chart Wheel");
    for (auto* button : {zoomOutButton_, zoomResetButton_, zoomInButton_, chartSettingsButton_})
        if (button) button->setVisible(!graph);
    if (transitAspectGridToggleButton_)
        transitAspectGridToggleButton_->setVisible(!graph && activeTab_ == AppTab::Transits);
    updateChartLegend();
    if (graph && chartLegendLabel_) chartLegendLabel_->hide();
}

void MainWindow::updateTransitAspectPeakResultsTable() {
    if (!rightTopTable_) return;
    const QSignalBlocker blocker(rightTopTable_);
    if (!hasTransitAspectPeakSelection_ || selectedAspectPeakIndex_ >= transitAspectPeakResults_.size())
        selectedAspectPeakIndex_ = -1;
    if (aspectPeakGraph_) {
        QVector<AspectPeakGraphPoint> points;
        points.reserve(transitAspectPeakResults_.size());
        for (int i = 0; i < transitAspectPeakResults_.size(); ++i) {
            const auto& result = transitAspectPeakResults_[i];
            points.push_back({i, result.peakUtc, result.startUtc, result.endUtc,
                result.netWeight, result.positiveWeight, result.negativeWeight,
                result.peakHitCount, result.transitNatalHitCount, result.transitTransitHitCount,
                result.transitSolarReturnHitCount, result.solarReturnYear});
        }
        aspectPeakGraph_->setResults(std::move(points), aspectPeakLastRangeStartUtc_, aspectPeakLastRangeEndUtc_,
            aspectPeakLastTz_, aspectPeakLastWeightingEnabled_, aspectPeakLastGroupedPeriods_,
            aspectPeakLastResolutionMinutes_, aspectPeakLastScanPartial_);
        aspectPeakGraph_->setSelectedResult(selectedAspectPeakIndex_);
    }
    transitAspectPeakDisplayOrder_.clear();
    for (int i = 0; i < transitAspectPeakResults_.size(); ++i) transitAspectPeakDisplayOrder_.push_back(i);
    std::stable_sort(transitAspectPeakDisplayOrder_.begin(), transitAspectPeakDisplayOrder_.end(),
        [this](int a, int b) {
            const auto& left = transitAspectPeakResults_[a];
            const auto& right = transitAspectPeakResults_[b];
            const QString ranking = aspectPeakLastWeightingEnabled_
                ? aspectPeakLastWeightRanking_ : QString("hits");
            if (ranking == "net_high" && std::fabs(left.netWeight - right.netWeight) > 1e-9)
                return left.netWeight > right.netWeight;
            if (ranking == "net_low" && std::fabs(left.netWeight - right.netWeight) > 1e-9)
                return left.netWeight < right.netWeight;
            if (ranking == "positive" && std::fabs(left.positiveWeight - right.positiveWeight) > 1e-9)
                return left.positiveWeight > right.positiveWeight;
            if (ranking == "negative" && std::fabs(left.negativeWeight - right.negativeWeight) > 1e-9)
                return left.negativeWeight > right.negativeWeight;
            if (left.peakHitCount != right.peakHitCount) return left.peakHitCount > right.peakHitCount;
            if (std::fabs(left.tightness - right.tightness) > 1e-9) return left.tightness > right.tightness;
            return left.peakUtc < right.peakUtc;
        });
    const int limit = aspectPeakTopCountSpin_
        ? std::min(aspectPeakTopCountSpin_->value(), static_cast<int>(transitAspectPeakDisplayOrder_.size()))
        : transitAspectPeakDisplayOrder_.size();
    transitAspectPeakDisplayOrder_.resize(limit);
    const bool extraSelection = selectedAspectPeakIndex_ >= 0
        && !transitAspectPeakDisplayOrder_.contains(selectedAspectPeakIndex_);
    if (extraSelection) transitAspectPeakDisplayOrder_.push_back(selectedAspectPeakIndex_);

    QStringList headers = aspectPeakLastGroupedPeriods_
        ? QStringList{"Period", "Peak", "Hits"}
        : QStringList{"Date", "Time", "Hits"};
    if (aspectPeakLastWeightingEnabled_) {
        headers << "Pos" << "Neg" << "Net";
    }
    if (aspectPeakLastIncludeTransitTransit_ || aspectPeakLastIncludeSolarReturn_) headers << "T-N";
    if (aspectPeakLastIncludeTransitTransit_) headers << "T-T";
    if (aspectPeakLastIncludeSolarReturn_) headers << "T-SR" << "SR";
    headers << "Orb" << "Bodies";
    setupPeakTable(rightTopTable_, headers, transitAspectPeakDisplayOrder_.size());
    rightTopTable_->horizontalHeaderItem(1)->setToolTip("Peak time in the search timezone");
    if (aspectPeakLastWeightingEnabled_) {
        rightTopTable_->horizontalHeaderItem(3)->setToolTip("Sum of positive aspect weights");
        rightTopTable_->horizontalHeaderItem(4)->setToolTip("Sum of negative aspect weights");
    }
    rightTopTable_->horizontalHeaderItem(headers.size() - 2)->setToolTip("Tightest aspect orb at the peak");
    for (int row = 0; row < transitAspectPeakDisplayOrder_.size(); ++row) {
        const int index = transitAspectPeakDisplayOrder_[row];
        const auto& result = transitAspectPeakResults_[index];
        const QDateTime start = result.startUtc.toTimeZone(aspectPeakLastTz_);
        const QDateTime end = result.endUtc.toTimeZone(aspectPeakLastTz_);
        const QDateTime peak = result.peakUtc.toTimeZone(aspectPeakLastTz_);
        QString first;
        if (aspectPeakLastGroupedPeriods_) {
            first = start.date() == end.date()
                ? QString("%1, %2-%3").arg(start.toString("d MMM yyyy"), start.toString("h:mm AP"), end.toString("h:mm AP"))
                : QString("%1 - %2").arg(start.toString("d MMM yyyy, h:mm AP"), end.toString("d MMM yyyy, h:mm AP"));
        } else {
            first = peak.toString("d MMM yyyy");
        }
        auto* firstItem = new PeakSortItem(first, static_cast<double>(result.startUtc.toMSecsSinceEpoch()));
        firstItem->setData(Qt::UserRole, index);
        firstItem->setToolTip(first);
        if (extraSelection && index == selectedAspectPeakIndex_)
            firstItem->setToolTip(first + "\nSelected graph peak (outside the current Show top limit).");
        rightTopTable_->setItem(row, 0, firstItem);
        rightTopTable_->setItem(row, 1, new PeakSortItem(
            peak.toString(aspectPeakLastGroupedPeriods_ ? "d MMM yyyy, h:mm AP" : "h:mm AP"),
            static_cast<double>(result.peakUtc.toMSecsSinceEpoch())));
        auto* totalItem = new PeakSortItem(
            QString::number(result.peakHitCount), result.peakHitCount, Qt::AlignCenter);
        QString counts = QString("%1 transit-natal").arg(result.transitNatalHitCount);
        if (aspectPeakLastIncludeTransitTransit_)
            counts += QString(" + %1 transit-transit").arg(result.transitTransitHitCount);
        if (aspectPeakLastIncludeSolarReturn_)
            counts += QString(" + %1 transit-Solar Return").arg(result.transitSolarReturnHitCount);
        totalItem->setToolTip(counts);
        rightTopTable_->setItem(row, 2, totalItem);

        int column = 3;
        if (aspectPeakLastWeightingEnabled_) {
            auto* positiveItem = peakWeightSortItem(result.positiveWeight, result.positiveWeight);
            positiveItem->setToolTip("Sum of positive aspect weights");
            rightTopTable_->setItem(row, column++, positiveItem);
            auto* negativeItem = peakWeightSortItem(-result.negativeWeight, result.negativeWeight);
            negativeItem->setToolTip("Magnitude of negative aspect weights");
            rightTopTable_->setItem(row, column++, negativeItem);
            auto* netItem = peakWeightSortItem(result.netWeight, result.netWeight);
            netItem->setToolTip("Positive total minus negative total");
            rightTopTable_->setItem(row, column++, netItem);
        }
        if (aspectPeakLastIncludeTransitTransit_ || aspectPeakLastIncludeSolarReturn_) {
            auto* transitNatalItem = new PeakSortItem(
                QString::number(result.transitNatalHitCount), result.transitNatalHitCount, Qt::AlignCenter);
            transitNatalItem->setToolTip("Transit-to-natal hits");
            rightTopTable_->setItem(row, column++, transitNatalItem);
        }
        if (aspectPeakLastIncludeTransitTransit_) {
            auto* transitTransitItem = new PeakSortItem(
                QString::number(result.transitTransitHitCount), result.transitTransitHitCount, Qt::AlignCenter);
            transitTransitItem->setToolTip("Transit-to-transit hits");
            rightTopTable_->setItem(row, column++, transitTransitItem);
        }
        if (aspectPeakLastIncludeSolarReturn_) {
            auto* solarHits = new PeakSortItem(QString::number(result.transitSolarReturnHitCount),
                                             result.transitSolarReturnHitCount, Qt::AlignCenter);
            solarHits->setToolTip("Transit-to-Solar Return hits");
            rightTopTable_->setItem(row, column++, solarHits);
            auto* solarYear = new PeakSortItem(QString::number(result.solarReturnYear),
                                             result.solarReturnYear, Qt::AlignCenter);
            solarYear->setToolTip(peakSolarReference(result, aspectPeakLastTz_) + "\n" + aspectPeakLastSolarContext_);
            rightTopTable_->setItem(row, column++, solarYear);
        }
        const QString tightest = result.peakHits.isEmpty() ? "-" : formatPeakOrb(result.peakHits.front().orb);
        rightTopTable_->setItem(row, column++, new PeakSortItem(
            tightest, result.peakHits.isEmpty() ? 999.0 : result.peakHits.front().orb,
            Qt::AlignRight | Qt::AlignVCenter));
        auto* bodiesItem = peakCell(result.transitBodies.join(", "));
        bodiesItem->setToolTip(bodiesItem->text());
        rightTopTable_->setItem(row, column, bodiesItem);
    }
    // A long list of bodies must not determine the width of the whole table.
    const int bodiesColumn = headers.size() - 1;
    rightTopTable_->horizontalHeader()->setSectionResizeMode(bodiesColumn, QHeaderView::Interactive);
    rightTopTable_->setColumnWidth(bodiesColumn, rightTopTable_->fontMetrics().horizontalAdvance("Jupiter, Saturn, Mars") + 8);
    if (aspectPeakLastGroupedPeriods_) {
        rightTopTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
        rightTopTable_->setColumnWidth(0, rightTopTable_->fontMetrics().horizontalAdvance("17 Sep 2026, 03:18-09:18") + 8);
    }
    rightTopTable_->setSortingEnabled(true);
    for (int row = 0; row < rightTopTable_->rowCount(); ++row) {
        auto* item = rightTopTable_->item(row, 0);
        if (item && item->data(Qt::UserRole).toInt() == selectedAspectPeakIndex_) {
            rightTopTable_->setCurrentCell(row, 0);
            rightTopTable_->selectRow(row);
            rightTopTable_->scrollToItem(item);
            break;
        }
    }
}

void MainWindow::refreshTransitAspectPeakTab() {
    if (activeTab_ != AppTab::Transits || transitSubTab_ != TransitSubTab::AspectPeaks) return;
    if (aspectPeakSolarContextLabel_) {
        const bool enabled = aspectPeakIncludeSolarReturnCheck_ && aspectPeakIncludeSolarReturnCheck_->isChecked();
        aspectPeakSolarContextLabel_->setVisible(enabled);
        if (enabled) {
            NatalInput input;
            QString context, error;
            if (transitAspectPeakRunning_) context = aspectPeakLastSolarContext_;
            else if (!resolveAspectPeakSolarInput(&input, &context, &error)) context = error;
            aspectPeakSolarContextLabel_->setText(context);
            aspectPeakSolarContextLabel_->setToolTip("Solar Return settings for the next scan; one location for the whole range. "
                "The SR column identifies each result's actual reference chart.");
        }
    }
    if (aspectPeakTimezoneEdit_ && aspectPeakTimezoneEdit_->text().trimmed().isEmpty() && hasCurrentChart_) {
        aspectPeakTimezoneEdit_->setText(currentInput_.timezone);
    }
    updateTransitAspectPeakResultsTable();
    if (!hasTransitAspectPeakSelection_) {
        setupPeakTable(rightBottomTable_, {"Info"}, 1);
        rightBottomTable_->setItem(0, 0, peakCell(hasCurrentChart_
            ? "Run Aspect Peaks and select a result to see every contributing aspect."
            : "Load a natal chart to use Aspect Peaks."));
    }
    updateLunationCopyButtonState();
}

void MainWindow::handleTransitAspectPeakResultActivated(int row, int column) {
    Q_UNUSED(column);
    if (!rightTopTable_ || row < 0 || row >= rightTopTable_->rowCount()) return;
    auto* item = rightTopTable_->item(row, 0);
    if (!item) return;
    const int index = item->data(Qt::UserRole).toInt();
    selectTransitAspectPeakResult(index);
}

void MainWindow::selectTransitAspectPeakResult(int index) {
    if (index < 0 || index >= transitAspectPeakResults_.size()) return;
    if (hasTransitAspectPeakSelection_ && selectedAspectPeakIndex_ == index) return;
    const auto& result = transitAspectPeakResults_[index];
    const QDateTime local = result.peakUtc.toTimeZone(aspectPeakLastTz_);
    NatalChart chart;
    QString error;
    if (!computeTransitChartAt(local, aspectPeakLastTzLabel_, &chart, &error)) {
        hasTransitAspectPeakSelection_ = false;
        selectedAspectPeakIndex_ = -1;
        if (aspectPeakGraph_) aspectPeakGraph_->setSelectedResult(-1);
        updateLunationCopyButtonState();
        setStatusMessage(error);
        return;
    }
    currentTransitChart_ = chart;
    hasTransitChart_ = true;
    hasTransitAspectPeakSelection_ = true;
    selectedAspectPeakIndex_ = index;
    lastTransitAspectPeakSelection_ = result;
    transitPending_ = false;
    lastTransitCalculated_ = QDateTime::currentDateTime();
    transitMode_ = TransitMode::NatalOverlay;
    if (transitOverlayRadio_) transitOverlayRadio_->setChecked(true);
    updateTransitTargetLabels();
    if (chartWheel_) {
        if (transitMode_ == TransitMode::NatalOverlay && hasCurrentChart_) {
            chartWheel_->setShowAspects(true);
            chartWheel_->setOverlayLabel("Transit");
            chartWheel_->setOverlayCharts(natalChartForTransitDisplay(), chart, transitHouseSystem_, aspectOrbs_);
            chartWheel_->setOverlayAspectScopes(overlayAspectsTransitNatal_, overlayAspectsTransitTransit_, overlayAspectsNatalNatal_);
        } else {
            chartWheel_->setTransitChart(chart, transitHouseSystem_);
        }
        chartWheel_->setAspectDisplayMaxOrb(aspectDisplayMaxOrb_);
        chartWheel_->clearHighlight();
    }
    showTransitAspectPeakDetails(index);
    // Keep the selected point inspectable even when it is outside the ranked top N.
    if (!transitAspectPeakDisplayOrder_.contains(index)
        || transitAspectPeakDisplayOrder_.size() > aspectPeakTopCountSpin_->value()) {
        updateTransitAspectPeakResultsTable();
    } else {
        const QSignalBlocker blocker(rightTopTable_);
        for (int row = 0; row < rightTopTable_->rowCount(); ++row) {
            auto* item = rightTopTable_->item(row, 0);
            if (item && item->data(Qt::UserRole).toInt() == index) {
                rightTopTable_->setCurrentCell(row, 0);
                rightTopTable_->selectRow(row);
                rightTopTable_->scrollToItem(item);
                break;
            }
        }
    }
    if (aspectPeakGraph_) aspectPeakGraph_->setSelectedResult(index);
    updateAspectPeakGraphVisibility();
    updateLunationCopyButtonState();
    QString status = QString("Loaded aspect peak: %1 (%2 hits).")
        .arg(localMoment(result.peakUtc, aspectPeakLastTz_)).arg(result.peakHitCount);
    if (result.solarReturnYear) status += " " + peakSolarReference(result, aspectPeakLastTz_);
    setStatusMessage(status);
}

void MainWindow::showTransitAspectPeakDetails(int index) {
    if (!rightBottomTable_ || index < 0 || index >= transitAspectPeakResults_.size()) return;
    const auto& result = transitAspectPeakResults_[index];
    QStringList headers{"Scope", "Transit", "Pos", "Aspect"};
    if (aspectPeakLastWeightingEnabled_) headers << "Wt";
    headers << "Target" << "Pos" << "Orb";
    setupPeakTable(rightBottomTable_, headers, result.peakHits.size());
    rightBottomTable_->setIconSize(QSize(15, 15));
    rightBottomTable_->horizontalHeaderItem(2)->setToolTip("Transiting body's position");
    rightBottomTable_->horizontalHeaderItem(headers.size() - 2)->setToolTip("Target's position");
    if (aspectPeakLastWeightingEnabled_)
        rightBottomTable_->horizontalHeaderItem(4)->setToolTip("Aspect weight");

    const QColor iconColor = rightBottomTable_->palette().text().color();
    for (int row = 0; row < result.peakHits.size(); ++row) {
        const auto& hit = result.peakHits[row];
        int column = 0;
        auto* scopeItem = peakCell(peakScope(hit), Qt::AlignCenter);
        scopeItem->setToolTip(hit.transitSolarReturn
            ? peakSolarReference(result, aspectPeakLastTz_) + "\n" + aspectPeakLastSolarContext_
            : (hit.transitTransit ? "Transit to transit" : "Transit to natal"));
        rightBottomTable_->setItem(row, column++, scopeItem);
        rightBottomTable_->setItem(row, column++, peakBodyCell(hit.transitBody, iconColor));
        rightBottomTable_->setItem(row, column++, peakPositionCell(hit.transitLongitude, iconColor));
        rightBottomTable_->setItem(row, column++, peakAspectCell(hit.aspect));
        if (aspectPeakLastWeightingEnabled_) {
            auto* weightItem = peakWeightCell(hit.weight);
            weightItem->setToolTip(QString("%1 weight: %2")
                .arg(hit.aspect, formatPeakWeight(hit.weight)));
            rightBottomTable_->setItem(row, column++, weightItem);
        }
        rightBottomTable_->setItem(row, column++, peakBodyCell(hit.natalTarget, iconColor));
        rightBottomTable_->setItem(row, column++, peakPositionCell(hit.natalLongitude, iconColor));
        rightBottomTable_->setItem(row, column,
            peakCell(formatPeakOrb(hit.orb), Qt::AlignRight | Qt::AlignVCenter));
    }
}
QString MainWindow::buildTransitAspectPeakClipboardText() const {
    if (!hasTransitAspectPeakSelection_) return QString();
    const auto& result = lastTransitAspectPeakSelection_;
    const QMap<QString, QString> rankingLabels = {
        {"hits", "Most simultaneous hits"},
        {"net_high", "Highest net"},
        {"net_low", "Lowest net"},
        {"positive", "Most positive"},
        {"negative", "Most negative"},
    };
    QStringList lines;
    lines << "# Transit Aspect Peak Report" << "";
    lines << QString("- Natal chart: %1").arg(currentInput_.name.trimmed().isEmpty() ? "Untitled" : currentInput_.name);
    lines << QString("- Natal birth: %1, %2 (%3)")
        .arg(currentInput_.date.toString("d MMM yyyy"), currentInput_.time.toString("h:mm:ss AP"), currentInput_.timezone);
    lines << QString("- Search range: %1 - %2 (%3)")
        .arg(localMoment(aspectPeakLastRangeStartUtc_, aspectPeakLastTz_),
             localMoment(aspectPeakLastRangeEndUtc_, aspectPeakLastTz_), aspectPeakLastTzLabel_);
    lines << QString("- Mode: %1").arg(aspectPeakLastGroupedPeriods_ ? "Peak Periods" : "Peak Days");
    lines << QString("- Sampling resolution: %1 minutes").arg(aspectPeakLastResolutionMinutes_);
    lines << QString("- Maximum orb: %1 degrees").arg(aspectPeakLastOrb_, 0, 'f', 1);
    lines << QString("- Minimum hits: %1").arg(aspectPeakLastMinHits_);
    lines << QString("- Transit-transit hits: %1").arg(
        aspectPeakLastIncludeTransitTransit_ ? "Included" : "Not included");
    lines << QString("- Transit-Solar Return hits: %1").arg(
        aspectPeakLastIncludeSolarReturn_ ? "Included (automatic annual switching)" : "Not included");
    if (aspectPeakLastIncludeSolarReturn_) {
        lines << "- Solar Return settings: " + aspectPeakLastSolarContext_;
        lines << "- Solar Return targets: " + aspectPeakLastNatalTargets_.join(", ");
    }
    if (aspectPeakLastWeightingEnabled_) {
        lines << "- Aspect weighting: Enabled";
        lines << QString("- Aspect weights: Conjunction %1; Sextile %2; Square %3; Trine %4; Opposition %5")
            .arg(formatPeakWeight(aspectPeakLastAspectWeights_.value("Conjunction")),
                 formatPeakWeight(aspectPeakLastAspectWeights_.value("Sextile")),
                 formatPeakWeight(aspectPeakLastAspectWeights_.value("Square")),
                 formatPeakWeight(aspectPeakLastAspectWeights_.value("Trine")),
                 formatPeakWeight(aspectPeakLastAspectWeights_.value("Opposition")));
        lines << QString("- Result ranking: %1").arg(
            rankingLabels.value(aspectPeakLastWeightRanking_, "Most simultaneous hits"));
    }
    lines << QString("- Transit bodies: %1").arg(aspectPeakLastTransitBodies_.join(", "));
    lines << QString("- Natal targets: %1").arg(aspectPeakLastNatalTargets_.join(", "));
    lines << QString("- Aspects: %1").arg(aspectPeakLastAspects_.join(", ")) << "";
    if (result.groupedPeriod) {
        lines << QString("## Selected Period: %1 - %2")
            .arg(localMoment(result.startUtc, aspectPeakLastTz_), localMoment(result.endUtc, aspectPeakLastTz_));
    } else {
        lines << QString("## Selected Peak Day: %1").arg(result.peakUtc.toTimeZone(aspectPeakLastTz_).toString("d MMM yyyy"));
    }
    lines << QString("- Peak moment: %1 (%2)").arg(localMoment(result.peakUtc, aspectPeakLastTz_), aspectPeakLastTzLabel_);
    lines << QString("- Simultaneous hits: %1").arg(result.peakHitCount);
    lines << QString("- Transit-to-natal hits: %1").arg(result.transitNatalHitCount);
    if (aspectPeakLastIncludeTransitTransit_) {
        lines << QString("- Transit-to-transit hits: %1").arg(result.transitTransitHitCount);
    }
    if (aspectPeakLastIncludeSolarReturn_) {
        lines << QString("- Transit-to-Solar Return hits: %1").arg(result.transitSolarReturnHitCount);
        lines << "- Reference: " + peakSolarReference(result, aspectPeakLastTz_) + " (" + aspectPeakLastTzLabel_ + ")";
    }
    if (aspectPeakLastWeightingEnabled_) {
        lines << QString("- Positive total: %1").arg(formatPeakWeight(result.positiveWeight));
        lines << QString("- Negative total: %1").arg(formatPeakWeight(-result.negativeWeight));
        lines << QString("- Net value: %1").arg(formatPeakWeight(result.netWeight));
    }
    lines << "";
    if (aspectPeakLastWeightingEnabled_) {
        lines << "| Scope | Subject | Aspect | Weight | Target | Orb |";
        lines << "| --- | --- | --- | ---: | --- | ---: |";
        for (const auto& hit : result.peakHits) {
            lines << QString("| %1 | %2 | %3 | %4 | %5 | %6 |")
                .arg(peakScope(hit), hit.transitBody, hit.aspect,
                     formatPeakWeight(hit.weight), hit.natalTarget, formatPeakOrb(hit.orb));
        }
    } else {
        lines << "| Scope | Subject | Aspect | Target | Orb |";
        lines << "| --- | --- | --- | --- | ---: |";
        for (const auto& hit : result.peakHits) {
            lines << QString("| %1 | %2 | %3 | %4 | %5 |")
                .arg(peakScope(hit), hit.transitBody,
                     hit.aspect, hit.natalTarget, formatPeakOrb(hit.orb));
        }
    }
    return lines.join('\n');
}
void MainWindow::handleCopyTransitAspectPeakDetails() {
    const QString text = buildTransitAspectPeakClipboardText();
    if (text.isEmpty()) {
        setStatusMessage("Select an Aspect Peak result first.");
        return;
    }
    QApplication::clipboard()->setText(text);
    setStatusMessage("Copied Aspect Peak report as Markdown.");
}

} // namespace dracoved
