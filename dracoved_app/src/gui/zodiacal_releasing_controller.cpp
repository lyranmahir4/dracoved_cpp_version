#include "zodiacal_releasing_controller.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDateEdit>
#include <QFile>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QSvgRenderer>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTimeEdit>
#include <QTimeZone>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>

namespace dracoved {

namespace {

constexpr int kStableIdRole = Qt::UserRole + 11;
constexpr int kDummyRole = Qt::UserRole + 12;
constexpr int kHighlightRole = Qt::UserRole + 13;
constexpr int kActiveChainRole = Qt::UserRole + 14;
constexpr int kSignIndexRole = Qt::UserRole + 15;

enum TimelineHighlightFlag {
    HighlightNone = 0,
    HighlightFortuneAngle = 1 << 0,
    HighlightForeshadowing = 1 << 1,
    HighlightMajorPeak = 1 << 2,
    HighlightLoosingOfBond = 1 << 3,
};


int highlightFlagsForPeriod(const ZodiacalReleasingPeriod& period) {
    int flags = HighlightNone;
    if (period.fortuneHouse == 1 || period.fortuneHouse == 10) {
        flags |= HighlightMajorPeak;
    } else if (period.fortuneHouse == 4 || period.fortuneHouse == 7) {
        flags |= HighlightFortuneAngle;
    }
    if (period.foreshadowing) flags |= HighlightForeshadowing;
    if (period.loosingOfBond) flags |= HighlightLoosingOfBond;
    return flags;
}

}  // namespace

QIcon zodiacalReleasingSignIcon(int signIndex, const QColor& requestedColor) {
    static constexpr std::array<const char*, 12> paths = {
        ":/resources/icons/zodiac_releasing/aries.svg",
        ":/resources/icons/zodiac_releasing/taurus.svg",
        ":/resources/icons/zodiac_releasing/gemini.svg",
        ":/resources/icons/zodiac_releasing/cancer.svg",
        ":/resources/icons/zodiac_releasing/leo.svg",
        ":/resources/icons/zodiac_releasing/virgo.svg",
        ":/resources/icons/zodiac_releasing/libra.svg",
        ":/resources/icons/zodiac_releasing/scorpio.svg",
        ":/resources/icons/zodiac_releasing/sagittarius.svg",
        ":/resources/icons/zodiac_releasing/capricorn.svg",
        ":/resources/icons/zodiac_releasing/aquarius.svg",
        ":/resources/icons/zodiac_releasing/pisces.svg",
    };
    if (signIndex < 0 || signIndex >= static_cast<int>(paths.size())) return {};

    const QColor color = requestedColor.isValid() ? requestedColor : QColor(Qt::black);
    const QString cacheKey =
        QString("%1:%2").arg(signIndex).arg(static_cast<quint32>(color.rgba()), 8, 16, QChar('0'));
    static QHash<QString, QIcon> cache;
    const auto cached = cache.constFind(cacheKey);
    if (cached != cache.cend()) return cached.value();

    QFile svgFile(QString::fromLatin1(paths[static_cast<std::size_t>(signIndex)]));
    if (!svgFile.open(QIODevice::ReadOnly)) return {};
    QByteArray svgData = svgFile.readAll();
    svgData.replace("currentColor", color.name(QColor::HexRgb).toUtf8());

    QSvgRenderer renderer(svgData);
    if (!renderer.isValid()) return {};

    constexpr int logicalSize = 18;
    constexpr int renderScale = 2;
    QPixmap pixmap(logicalSize * renderScale, logicalSize * renderScale);
    pixmap.setDevicePixelRatio(renderScale);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter, QRectF(1.0, 1.0, logicalSize - 2.0, logicalSize - 2.0));
    painter.end();

    const QIcon icon(pixmap);
    cache.insert(cacheKey, icon);
    return icon;
}

namespace {

constexpr char kZodiacSignIndexProperty[] = "zodiacReleasingSignIndex";

class ZodiacSignIconLabel final : public QLabel {
public:
    explicit ZodiacSignIconLabel(QWidget* parent = nullptr)
        : QLabel(parent) {}

protected:
    void paintEvent(QPaintEvent* event) override {
        QLabel::paintEvent(event);
        const QVariant signData = property(kZodiacSignIndexProperty);
        if (!signData.isValid()) return;
        const int signIndex = signData.toInt();
        if (signIndex < 0 || signIndex > 11) return;
        QPainter painter(this);
        const QIcon icon = zodiacalReleasingSignIcon(
            signIndex, palette().color(QPalette::WindowText));
        icon.paint(&painter, contentsRect(), Qt::AlignCenter,
                   isEnabled() ? QIcon::Normal : QIcon::Disabled);
    }
};

void setZodiacSignIcon(QLabel* label, int signIndex) {
    if (!label) return;
    if (signIndex < 0 || signIndex > 11) {
        label->setProperty(kZodiacSignIndexProperty, QVariant());
        label->hide();
    } else {
        label->setProperty(kZodiacSignIndexProperty, signIndex);
        label->show();
    }
    label->update();
}

class TimelineHighlightDelegate final : public QStyledItemDelegate {
public:
    explicit TimelineHighlightDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

protected:
    void initStyleOption(QStyleOptionViewItem* option,
                         const QModelIndex& index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        const QModelIndex rowIndex = index.siblingAtColumn(0);
        const int flags = rowIndex.data(kHighlightRole).toInt();
        const bool activeChain = rowIndex.data(kActiveChainRole).toBool();
        const QVariant signIndexData = rowIndex.data(kSignIndexRole);
        if (index.column() == 1 && signIndexData.isValid()) {
            const bool selected = option->state.testFlag(QStyle::State_Selected);
            const QColor iconColor = selected
                ? option->palette.highlightedText().color()
                : option->palette.text().color();
            option->icon = zodiacalReleasingSignIcon(signIndexData.toInt(), iconColor);
            if (!option->icon.isNull()) {
                option->features |= QStyleOptionViewItem::HasDecoration;
                option->decorationSize = QSize(18, 18);
                option->decorationPosition = QStyleOptionViewItem::Left;
                option->decorationAlignment = Qt::AlignLeft | Qt::AlignVCenter;
            }
        }
        if (activeChain || (flags & HighlightLoosingOfBond)) {
            option->font.setBold(true);
        }
    }

    void paint(QPainter* painter,
               const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyledItemDelegate::paint(painter, option, index);
        if (option.state.testFlag(QStyle::State_Selected)) return;

        const QModelIndex rowIndex = index.siblingAtColumn(0);
        const int flags = rowIndex.data(kHighlightRole).toInt();
        const bool activeChain = rowIndex.data(kActiveChainRole).toBool();
        QColor tint;
        int lightAlpha = 0;
        int darkAlpha = 0;
        if (flags & HighlightLoosingOfBond) {
            tint = QColor("#d76255");
            lightAlpha = 54;
            darkAlpha = 78;
        } else if (flags & HighlightMajorPeak) {
            tint = QColor("#d5a628");
            lightAlpha = 46;
            darkAlpha = 68;
        } else if (flags & HighlightForeshadowing) {
            tint = QColor("#8d6ad8");
            lightAlpha = 42;
            darkAlpha = 64;
        } else if (flags & HighlightFortuneAngle) {
            tint = QColor("#2e9c91");
            lightAlpha = 38;
            darkAlpha = 60;
        } else if (activeChain) {
            tint = QColor("#4a86c5");
            lightAlpha = 30;
            darkAlpha = 52;
        } else {
            return;
        }

        const bool darkTheme = option.palette.base().color().lightness() < 100;
        int alpha = darkTheme ? darkAlpha : lightAlpha;
        if (option.state.testFlag(QStyle::State_MouseOver)) alpha += 8;
        tint.setAlpha(qBound(0, alpha, 255));
        painter->save();
        painter->fillRect(option.rect, tint);
        painter->restore();
    }
};

void updateActiveHighlightRoles(QTreeWidgetItem* item,
                                const QSet<QString>& activeIds) {
    if (!item) return;
    if (!item->data(0, kDummyRole).toBool()) {
        item->setData(0, kActiveChainRole,
                      activeIds.contains(item->data(0, kStableIdRole).toString()));
    }
    for (int index = 0; index < item->childCount(); ++index) {
        updateActiveHighlightRoles(item->child(index), activeIds);
    }
}

QString markdownCell(QString text) {
    text.replace('|', "\\|");
    text.replace('\n', "<br>");
    return text;
}

QString longitudeText(double longitude) {
    double normalized = std::fmod(longitude, 360.0);
    if (normalized < 0.0) normalized += 360.0;
    const int sign = qBound(0, static_cast<int>(std::floor(normalized / 30.0)), 11);
    const double inSign = normalized - sign * 30.0;
    const int degrees = static_cast<int>(std::floor(inSign));
    const int minutes = static_cast<int>(std::floor((inSign - degrees) * 60.0));
    const int seconds = qRound((((inSign - degrees) * 60.0) - minutes) * 60.0);
    return QString("%1 %2%3 %4'%5\"")
        .arg(zodiacalReleasingSignName(sign))
        .arg(degrees, 2, 10, QChar('0'))
        .arg(QChar(0x00B0))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

void appendTreeRows(QTreeWidgetItem* parent, QStringList* rows) {
    if (!parent || !rows) return;
    if (!parent->data(0, kDummyRole).toBool()) {
        rows->push_back(QString("| %1 | %2 | %3 | %4 | %5 | %6 |")
            .arg(markdownCell(parent->text(0)),
                 markdownCell(parent->text(1)),
                 markdownCell(parent->text(2)),
                 markdownCell(parent->text(3)),
                 markdownCell(parent->text(4)),
                 markdownCell(parent->text(6))));
    }
    for (int index = 0; index < parent->childCount(); ++index) {
        appendTreeRows(parent->child(index), rows);
    }
}

}  // namespace

ZodiacalReleasingController::ZodiacalReleasingController(QObject* parent)
    : QObject(parent) {
    engineHealthy_ = zodiacalReleasingSelfCheck(&engineError_);
    buildFiltersUi();
    buildWorkspaceUi();
    syncControlsFromDefaults();
    showEmptyState(engineHealthy_
        ? "Load a natal chart to calculate Zodiacal Releasing."
        : QString("Zodiacal Releasing self-check failed: %1").arg(engineError_));
}

QWidget* ZodiacalReleasingController::filtersWidget() const {
    return filtersRoot_;
}

QWidget* ZodiacalReleasingController::workspaceWidget() const {
    return workspaceRoot_;
}

void ZodiacalReleasingController::setNatalContext(
    const NatalInput& input,
    const NatalChart& chart,
    const QString& locationNameValue) {
    natalInput_ = input;
    natalChart_ = chart;
    natalLocationName_ = locationNameValue;
    hasNatalContext_ = chart.utcDateTime.isValid();
    stale_ = true;
    timeline_ = {};
    referenceChain_.clear();
    periodById_.clear();
    if (periodsTree_) periodsTree_->clear();
    contextLabel_->setText(hasNatalContext_
        ? QString("%1 - born %2\n%3 | %4")
            .arg(input.name.trimmed().isEmpty() ? QString("Untitled") : input.name.trimmed(),
                 chart.localDateTime.toString("d MMM yyyy, h:mm AP"),
                 input.zodiacSystem == ZodiacSystem::Sidereal
                     ? QString("Sidereal - %1").arg(siderealAyanamsaToString(input.siderealAyanamsa))
                     : QString("Tropical"),
                 chart.isDayChart ? QString("Day chart") : QString("Night chart"))
        : QString("Load a natal chart to use Zodiacal Releasing."));
    if (hasNatalContext_) {
        const QSignalBlocker dateBlock(referenceDateEdit_);
        const QSignalBlocker timeBlock(referenceTimeEdit_);
        referenceDateEdit_->setDate(QDate::currentDate());
        referenceTimeEdit_->setTime(QTime::currentTime());
        if (active_) calculate();
        else showEmptyState("Zodiacal Releasing is ready. Open this tab to calculate the timeline.");
    } else {
        showEmptyState("Load a natal chart to calculate Zodiacal Releasing.");
    }
    emit timelineChanged();
}

void ZodiacalReleasingController::clearNatalContext() {
    hasNatalContext_ = false;
    natalInput_ = {};
    natalChart_ = {};
    natalLocationName_.clear();
    timeline_ = {};
    referenceChain_.clear();
    periodById_.clear();
    stale_ = true;
    if (periodsTree_) periodsTree_->clear();
    if (contextLabel_) contextLabel_->setText(
        "Load a natal chart to use Zodiacal Releasing.");
    showEmptyState("Load a natal chart to calculate Zodiacal Releasing.");
    emit timelineChanged();
}

void ZodiacalReleasingController::setDefaults(
    const ZodiacalReleasingSettings& defaults) {
    defaults_ = defaults;
    defaults_.capricornYears = defaults.capricornYears == 30 ? 30 : 27;
    defaults_.maximumLevel = qBound(1, defaults.maximumLevel, 4);
    defaults_.maximumAge = qBound(1, defaults.maximumAge, 300);
    syncControlsFromDefaults();
    markStale();
}

void ZodiacalReleasingController::setActive(bool active) {
    active_ = active;
    if (active_ && hasNatalContext_ && (!timeline_.valid || stale_)) {
        calculate();
    }
}

void ZodiacalReleasingController::markStale() {
    stale_ = true;
    if (calculationStatusLabel_) {
        calculationStatusLabel_->setText(
            hasNatalContext_ ? "Pending changes - recalculate the timeline." : "No natal chart loaded.");
    }
    if (methodLabel_ && timeline_.valid) {
        methodLabel_->setText("Results are stale - recalculate before research or copying.");
        setZodiacSignIcon(methodSignIconLabel_, -1);
        if (methodSuffixLabel_) methodSuffixLabel_->hide();
    }
    emit timelineChanged();
}

bool ZodiacalReleasingController::hasTimeline() const {
    return timeline_.valid;
}

bool ZodiacalReleasingController::isStale() const {
    return stale_;
}

bool ZodiacalReleasingController::hasSelectedPeriod() const {
    return periodsTree_ && periodsTree_->currentItem()
        && periodById_.contains(periodsTree_->currentItem()->data(0, kStableIdRole).toString());
}

ZodiacalReleasingPeriod ZodiacalReleasingController::selectedPeriod() const {
    if (!hasSelectedPeriod()) return {};
    return periodById_.value(
        periodsTree_->currentItem()->data(0, kStableIdRole).toString());
}

QVector<ZodiacalReleasingPeriod> ZodiacalReleasingController::referenceChain() const {
    return referenceChain_;
}

ZodiacalReleasingTimeline ZodiacalReleasingController::timeline() const {
    return timeline_;
}

ZodiacalReleasingSettings ZodiacalReleasingController::settings() const {
    return timeline_.valid ? timeline_.settings : settingsFromControls();
}

QDateTime ZodiacalReleasingController::referenceMomentUtc() const {
    return referenceMomentUtc_;
}

QString ZodiacalReleasingController::natalName() const {
    return natalInput_.name.trimmed().isEmpty() ? QString("Untitled") : natalInput_.name.trimmed();
}

QString ZodiacalReleasingController::locationName() const {
    return natalLocationName_;
}

void ZodiacalReleasingController::buildFiltersUi() {
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(7, 7, 7, 7);
    layout->setSpacing(7);

    auto* title = new QLabel("Zodiacal Releasing", page);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    contextLabel_ = new QLabel(
        "Load a natal chart to use Zodiacal Releasing.", page);
    contextLabel_->setObjectName("hintLabel");
    contextLabel_->setWordWrap(true);
    layout->addWidget(contextLabel_);

    auto* methodGroup = new QGroupBox("Releasing Method", page);
    auto* methodLayout = new QGridLayout(methodGroup);
    releasePointCombo_ = new QComboBox(methodGroup);
    releasePointCombo_->addItem("Lot of Spirit - actions and vocation",
                                static_cast<int>(ZodiacalReleasingPoint::Spirit));
    releasePointCombo_->addItem("Lot of Fortune - body and circumstances",
                                static_cast<int>(ZodiacalReleasingPoint::Fortune));
    releasePointCombo_->addItem("Lot of Eros - love, desire and relationships",
                                static_cast<int>(ZodiacalReleasingPoint::Eros));
    timeKeyCombo_ = new QComboBox(methodGroup);
    timeKeyCombo_->addItem("Traditional 360-day year",
                           static_cast<int>(ZodiacalReleasingTimeKey::Traditional360));
    timeKeyCombo_->addItem("Alternative 365.2425-day year",
                           static_cast<int>(ZodiacalReleasingTimeKey::Calendar3652425));
    capricornCombo_ = new QComboBox(methodGroup);
    capricornCombo_->addItem("27 years - Valens / standard", 27);
    capricornCombo_->addItem("30 years - alternative", 30);
    sameSignRuleCheck_ = new QCheckBox(
        "Move Spirit forward one sign when Fortune and Spirit share a sign",
        methodGroup);
    sameSignRuleCheck_->setChecked(true);
    sameSignRuleCheck_->setToolTip(
        "Valens' special rule for releasing from Spirit when both Lots occupy the same sign.");
    methodLayout->addWidget(new QLabel("Release from", methodGroup), 0, 0);
    methodLayout->addWidget(releasePointCombo_, 0, 1);
    methodLayout->addWidget(new QLabel("Time key", methodGroup), 1, 0);
    methodLayout->addWidget(timeKeyCombo_, 1, 1);
    methodLayout->addWidget(new QLabel("Capricorn period", methodGroup), 2, 0);
    methodLayout->addWidget(capricornCombo_, 2, 1);
    methodLayout->addWidget(sameSignRuleCheck_, 3, 0, 1, 2);
    methodLayout->setColumnStretch(1, 1);
    layout->addWidget(methodGroup);

    auto* rangeGroup = new QGroupBox("Timeline Range", page);
    auto* rangeLayout = new QGridLayout(rangeGroup);
    startAgeSpin_ = new QSpinBox(rangeGroup);
    startAgeSpin_->setRange(0, 299);
    startAgeSpin_->setValue(0);
    startAgeSpin_->setSuffix(" years");
    endAgeSpin_ = new QSpinBox(rangeGroup);
    endAgeSpin_->setRange(1, 300);
    endAgeSpin_->setValue(120);
    endAgeSpin_->setSuffix(" years");
    maximumLevelCombo_ = new QComboBox(rangeGroup);
    maximumLevelCombo_->addItem("L1 only", 1);
    maximumLevelCombo_->addItem("L1-L2", 2);
    maximumLevelCombo_->addItem("L1-L3", 3);
    maximumLevelCombo_->addItem("L1-L4", 4);
    maximumLevelCombo_->setCurrentIndex(3);
    rangeLayout->addWidget(new QLabel("Start age", rangeGroup), 0, 0);
    rangeLayout->addWidget(startAgeSpin_, 0, 1);
    rangeLayout->addWidget(new QLabel("End age", rangeGroup), 1, 0);
    rangeLayout->addWidget(endAgeSpin_, 1, 1);
    rangeLayout->addWidget(new QLabel("Maximum detail", rangeGroup), 2, 0);
    rangeLayout->addWidget(maximumLevelCombo_, 2, 1);
    rangeLayout->setColumnStretch(1, 1);
    layout->addWidget(rangeGroup);

    auto* referenceGroup = new QGroupBox("Research Date", page);
    auto* referenceLayout = new QGridLayout(referenceGroup);
    referenceDateEdit_ = new QDateEdit(referenceGroup);
    referenceDateEdit_->setCalendarPopup(true);
    referenceDateEdit_->setDisplayFormat("d MMM yyyy");
    referenceDateEdit_->setDateRange(QDate(1800, 1, 1), QDate(2399, 12, 31));
    referenceDateEdit_->setDate(QDate::currentDate());
    referenceTimeEdit_ = new QTimeEdit(referenceGroup);
    referenceTimeEdit_->setDisplayFormat("h:mm:ss AP");
    referenceTimeEdit_->setTime(QTime::currentTime());
    locateButton_ = new QPushButton("Locate Date", referenceGroup);
    nowButton_ = new QPushButton("Now", referenceGroup);
    birthButton_ = new QPushButton("Birth", referenceGroup);
    referenceLayout->addWidget(new QLabel("Date", referenceGroup), 0, 0);
    referenceLayout->addWidget(referenceDateEdit_, 0, 1, 1, 2);
    referenceLayout->addWidget(new QLabel("Time", referenceGroup), 1, 0);
    referenceLayout->addWidget(referenceTimeEdit_, 1, 1, 1, 2);
    referenceLayout->addWidget(birthButton_, 2, 0);
    referenceLayout->addWidget(nowButton_, 2, 1);
    referenceLayout->addWidget(locateButton_, 2, 2);
    referenceLayout->setColumnStretch(1, 1);
    layout->addWidget(referenceGroup);

    auto* calculateGroup = new QGroupBox("Calculation", page);
    auto* calculateLayout = new QVBoxLayout(calculateGroup);
    calculateButton_ = new QPushButton("Calculate Zodiacal Releasing", calculateGroup);
    calculationStatusLabel_ = new QLabel("No natal chart loaded.", calculateGroup);
    calculationStatusLabel_->setObjectName("hintLabel");
    calculationStatusLabel_->setWordWrap(true);
    auto* methodNote = new QLabel(
        "Signs release zodiacally using traditional rulers. Child periods restart from their parent sign, "
        "truncate at the parent boundary, and jump opposite at a Loosing of the Bond.",
        calculateGroup);
    methodNote->setObjectName("hintLabel");
    methodNote->setWordWrap(true);
    calculateLayout->addWidget(calculateButton_);
    calculateLayout->addWidget(calculationStatusLabel_);
    calculateLayout->addWidget(methodNote);
    layout->addWidget(calculateGroup);
    layout->addStretch();

    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    filtersRoot_ = scroll;

    connect(releasePointCombo_, &QComboBox::currentIndexChanged, this, [this] {
        const bool spiritSelected = releasePointCombo_->currentData().toInt()
            == static_cast<int>(ZodiacalReleasingPoint::Spirit);
        sameSignRuleCheck_->setEnabled(spiritSelected);
        handleSettingsChanged();
    });
    connect(timeKeyCombo_, &QComboBox::currentIndexChanged,
            this, &ZodiacalReleasingController::handleSettingsChanged);
    connect(capricornCombo_, &QComboBox::currentIndexChanged,
            this, &ZodiacalReleasingController::handleSettingsChanged);
    connect(sameSignRuleCheck_, &QCheckBox::toggled,
            this, &ZodiacalReleasingController::handleSettingsChanged);
    connect(startAgeSpin_, &QSpinBox::valueChanged, this, [this](int value) {
        if (endAgeSpin_ && endAgeSpin_->value() <= value) {
            const QSignalBlocker blocker(endAgeSpin_);
            endAgeSpin_->setValue(value + 1);
        }
        handleSettingsChanged();
    });
    connect(endAgeSpin_, &QSpinBox::valueChanged, this, [this](int value) {
        if (startAgeSpin_ && startAgeSpin_->value() >= value) {
            const QSignalBlocker blocker(startAgeSpin_);
            startAgeSpin_->setValue(std::max(0, value - 1));
        }
        handleSettingsChanged();
    });
    connect(maximumLevelCombo_, &QComboBox::currentIndexChanged,
            this, &ZodiacalReleasingController::handleSettingsChanged);
    connect(calculateButton_, &QPushButton::clicked,
            this, &ZodiacalReleasingController::calculate);
    connect(locateButton_, &QPushButton::clicked,
            this, &ZodiacalReleasingController::locateEditedMoment);
    connect(nowButton_, &QPushButton::clicked,
            this, &ZodiacalReleasingController::locateNow);
    connect(birthButton_, &QPushButton::clicked,
            this, &ZodiacalReleasingController::locateBirth);
}

void ZodiacalReleasingController::buildWorkspaceUi() {
    workspaceRoot_ = new QWidget();
    auto* layout = new QVBoxLayout(workspaceRoot_);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(7);

    auto* header = new QWidget(workspaceRoot_);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel("Zodiacal Releasing Timeline", header);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    auto* methodSummary = new QWidget(header);
    auto* methodSummaryLayout = new QHBoxLayout(methodSummary);
    methodSummaryLayout->setContentsMargins(0, 0, 0, 0);
    methodSummaryLayout->setSpacing(3);
    methodLabel_ = new QLabel("Load a natal chart to begin.", methodSummary);
    methodLabel_->setObjectName("hintLabel");
    methodLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    methodSignIconLabel_ = new ZodiacSignIconLabel(methodSummary);
    methodSignIconLabel_->setObjectName("hintLabel");
    methodSignIconLabel_->setFixedSize(18, 18);
    setZodiacSignIcon(methodSignIconLabel_, -1);
    methodSuffixLabel_ = new QLabel(methodSummary);
    methodSuffixLabel_->setObjectName("hintLabel");
    methodSuffixLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    methodSuffixLabel_->hide();
    methodSummaryLayout->addWidget(methodLabel_);
    methodSummaryLayout->addWidget(methodSignIconLabel_);
    methodSummaryLayout->addWidget(methodSuffixLabel_);
    methodSummaryLayout->addStretch(1);
    expandCurrentButton_ = new QPushButton("Expand Reference Chain", header);
    collapseButton_ = new QPushButton("Collapse All", header);
    copyChainButton_ = new QPushButton("Copy Chain", header);
    copyScheduleButton_ = new QPushButton("Copy Visible Schedule", header);
    headerLayout->addWidget(title);
    headerLayout->addWidget(methodSummary, 1);
    headerLayout->addWidget(expandCurrentButton_);
    headerLayout->addWidget(collapseButton_);
    headerLayout->addWidget(copyChainButton_);
    headerLayout->addWidget(copyScheduleButton_);
    layout->addWidget(header);

    auto* chainGroup = new QGroupBox("Active Chain at Research Date", workspaceRoot_);
    auto* chainLayout = new QGridLayout(chainGroup);
    for (int index = 0; index < 4; ++index) {
        auto* levelLabel = new QLabel(QString("L%1").arg(index + 1), chainGroup);
        QFont levelFont = levelLabel->font();
        levelFont.setBold(true);
        levelLabel->setFont(levelFont);
        auto* card = new QFrame(chainGroup);
        card->setObjectName("zrChainCard");
        card->setFrameShape(QFrame::StyledPanel);
        card->setMinimumWidth(145);
        auto* cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(7, 7, 7, 7);
        cardLayout->setSpacing(6);
        chainIconLabels_[index] = new ZodiacSignIconLabel(card);
        chainIconLabels_[index]->setFixedSize(18, 18);
        setZodiacSignIcon(chainIconLabels_[index], -1);
        chainLabels_[index] = new QLabel("Not calculated", card);
        chainLabels_[index]->setTextInteractionFlags(Qt::TextSelectableByMouse);
        chainLabels_[index]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        cardLayout->addWidget(chainIconLabels_[index], 0, Qt::AlignTop);
        cardLayout->addWidget(chainLabels_[index], 1);
        chainLayout->addWidget(levelLabel, 0, index);
        chainLayout->addWidget(card, 1, index);
        chainLayout->setColumnStretch(index, 1);
    }
    layout->addWidget(chainGroup);

    auto* highlightLegend = new QLabel(
        "<b>Row highlights:</b> "
        "<span style='color:#d5a628'>&#9632;</span> Major peak &nbsp; "
        "<span style='color:#2e9c91'>&#9632;</span> Fortune angle &nbsp; "
        "<span style='color:#8d6ad8'>&#9632;</span> Foreshadowing &nbsp; "
        "<span style='color:#d76255'>&#9632;</span> Loosing of the Bond &nbsp; "
        "<span style='color:#4a86c5'>&#9632;</span> <b>Active chain</b>",
        workspaceRoot_);
    highlightLegend->setObjectName("hintLabel");
    highlightLegend->setTextFormat(Qt::RichText);
    highlightLegend->setWordWrap(true);
    layout->addWidget(highlightLegend);

    periodsTree_ = new QTreeWidget(workspaceRoot_);
    periodsTree_->setItemDelegate(new TimelineHighlightDelegate(periodsTree_));
    periodsTree_->setColumnCount(7);
    periodsTree_->setHeaderLabels({
        "Period", "Sign", "Ruler", "Starts", "Ends", "Age", "Markers",
    });
    periodsTree_->setAlternatingRowColors(true);
    periodsTree_->setRootIsDecorated(true);
    periodsTree_->setUniformRowHeights(true);
    periodsTree_->setSelectionMode(QAbstractItemView::SingleSelection);
    periodsTree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    periodsTree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    periodsTree_->setSortingEnabled(false);
    if (auto* treeHeader = periodsTree_->header()) {
        treeHeader->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        treeHeader->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        treeHeader->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        treeHeader->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        treeHeader->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        treeHeader->setSectionResizeMode(5, QHeaderView::ResizeToContents);
        treeHeader->setSectionResizeMode(6, QHeaderView::Stretch);
    }
    layout->addWidget(periodsTree_, 1);

    connect(periodsTree_, &QTreeWidget::itemExpanded,
            this, &ZodiacalReleasingController::ensureChildren);
    connect(periodsTree_, &QTreeWidget::itemSelectionChanged,
            this, &ZodiacalReleasingController::handleSelectionChanged);
    connect(periodsTree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item) {
                ensureChildren(item);
                item->setExpanded(true);
            });
    connect(expandCurrentButton_, &QPushButton::clicked,
            this, &ZodiacalReleasingController::expandReferenceChain);
    connect(collapseButton_, &QPushButton::clicked,
            this, &ZodiacalReleasingController::collapseAll);
    connect(copyChainButton_, &QPushButton::clicked,
            this, &ZodiacalReleasingController::copyReferenceChain);
    connect(copyScheduleButton_, &QPushButton::clicked,
            this, &ZodiacalReleasingController::copyVisibleSchedule);
}

void ZodiacalReleasingController::syncControlsFromDefaults() {
    if (!releasePointCombo_) return;
    updatingControls_ = true;
    releasePointCombo_->setCurrentIndex(std::max(
        0, releasePointCombo_->findData(static_cast<int>(defaults_.releasePoint))));
    timeKeyCombo_->setCurrentIndex(std::max(
        0, timeKeyCombo_->findData(static_cast<int>(defaults_.timeKey))));
    capricornCombo_->setCurrentIndex(std::max(
        0, capricornCombo_->findData(defaults_.capricornYears)));
    sameSignRuleCheck_->setChecked(defaults_.applySameSignSpiritRule);
    endAgeSpin_->setValue(defaults_.maximumAge);
    maximumLevelCombo_->setCurrentIndex(std::max(
        0, maximumLevelCombo_->findData(defaults_.maximumLevel)));
    updatingControls_ = false;
}

void ZodiacalReleasingController::handleSettingsChanged() {
    if (updatingControls_) return;
    markStale();
}

ZodiacalReleasingSettings ZodiacalReleasingController::settingsFromControls() const {
    ZodiacalReleasingSettings out;
    out.releasePoint = static_cast<ZodiacalReleasingPoint>(
        releasePointCombo_->currentData().toInt());
    out.timeKey = static_cast<ZodiacalReleasingTimeKey>(
        timeKeyCombo_->currentData().toInt());
    out.capricornYears = capricornCombo_->currentData().toInt() == 30 ? 30 : 27;
    out.applySameSignSpiritRule = sameSignRuleCheck_->isChecked();
    out.maximumLevel = qBound(1, maximumLevelCombo_->currentData().toInt(), 4);
    out.maximumAge = qBound(1, endAgeSpin_->value(), 300);
    return out;
}

void ZodiacalReleasingController::calculate() {
    if (!engineHealthy_) {
        showEmptyState(QString("Zodiacal Releasing self-check failed: %1").arg(engineError_));
        emit statusMessage("Zodiacal Releasing is unavailable because its internal self-check failed.");
        return;
    }
    if (!hasNatalContext_) {
        showEmptyState("Load a natal chart to calculate Zodiacal Releasing.");
        emit statusMessage("Load a natal chart before calculating Zodiacal Releasing.");
        return;
    }

    double fortune = 0.0;
    double spirit = 0.0;
    double eros = 0.0;
    bool hasFortune = false;
    bool hasSpirit = false;
    bool hasEros = false;
    resolveLotLongitudes(
        &fortune, &spirit, &eros, &hasFortune, &hasSpirit, &hasEros);

    ZodiacalReleasingContext context;
    context.birthUtc = natalChart_.utcDateTime.toUTC();
    context.timezone = natalInput_.timezone;
    context.fortuneLongitude = fortune;
    context.spiritLongitude = spirit;
    context.erosLongitude = eros;
    context.hasFortune = hasFortune;
    context.hasSpirit = hasSpirit;
    context.hasEros = hasEros;
    context.isDayChart = natalChart_.isDayChart;

    const ZodiacalReleasingSettings requested = settingsFromControls();
    displayStartUtc_ = ageBoundaryUtc(startAgeSpin_->value());
    const QDateTime endUtc = ageBoundaryUtc(endAgeSpin_->value());
    timeline_ = calculateZodiacalReleasing(context, requested, endUtc);
    if (!timeline_.valid) {
        stale_ = true;
        showEmptyState(timeline_.error);
        calculationStatusLabel_->setText(timeline_.error);
        emit statusMessage(timeline_.error);
        emit timelineChanged();
        return;
    }

    stale_ = false;
    populateTimeline();
    calculationStatusLabel_->setText(QString(
        "Calculated ages %1-%2 using %3. Expand a period for deeper levels.")
        .arg(startAgeSpin_->value())
        .arg(endAgeSpin_->value())
        .arg(zodiacalReleasingTimeKeyName(timeline_.settings.timeKey)));
    methodLabel_->setText(QString("%1 | starts in")
        .arg(zodiacalReleasingPointName(timeline_.settings.releasePoint)));
    setZodiacSignIcon(methodSignIconLabel_, timeline_.startSignIndex);
    methodSuffixLabel_->setText(QString("%1 | %2 | Capricorn %3")
        .arg(zodiacalReleasingSignName(timeline_.startSignIndex),
             zodiacalReleasingTimeKeyName(timeline_.settings.timeKey))
        .arg(timeline_.settings.capricornYears));
    methodSuffixLabel_->show();

    QDateTime initial = QDateTime::currentDateTimeUtc();
    if (initial < displayStartUtc_ || initial >= timeline_.rangeEndUtc) {
        initial = std::max(timeline_.context.birthUtc, displayStartUtc_);
    }
    locateReferenceMoment(initial, true);
    emit statusMessage(QString("Calculated Zodiacal Releasing from %1.")
                           .arg(zodiacalReleasingPointName(timeline_.settings.releasePoint)));
    emit timelineChanged();
}

void ZodiacalReleasingController::populateTimeline() {
    periodsTree_->clear();
    periodById_.clear();
    for (const auto& period : timeline_.levelOnePeriods) {
        if (period.endUtc <= displayStartUtc_) continue;
        auto* item = makePeriodItem(period);
        periodsTree_->addTopLevelItem(item);
    }
    if (periodsTree_->topLevelItemCount() > 0) {
        periodsTree_->setCurrentItem(periodsTree_->topLevelItem(0));
    }
}

QTreeWidgetItem* ZodiacalReleasingController::makePeriodItem(
    const ZodiacalReleasingPeriod& period) {
    auto* item = new QTreeWidgetItem({
        QString("L%1 period %2").arg(period.level).arg(period.sequenceIndex + 1),
        period.signName,
        period.ruler,
        localDateTimeText(period.startUtc),
        localDateTimeText(period.endUtc),
        ageText(period.startUtc),
        markerText(period),
    });
    item->setData(0, kStableIdRole, period.stableId);
    item->setData(0, kSignIndexRole, period.signIndex);
    item->setData(0, kHighlightRole, highlightFlagsForPeriod(period));
    item->setData(0, kActiveChainRole, std::any_of(
        referenceChain_.cbegin(), referenceChain_.cend(),
        [&period](const ZodiacalReleasingPeriod& active) {
            return active.stableId == period.stableId;
        }));
    item->setToolTip(6, period.fortuneRelationship);
    periodById_.insert(period.stableId, period);
    if (period.level < timeline_.settings.maximumLevel) {
        auto* dummy = new QTreeWidgetItem({"Loading..."});
        dummy->setData(0, kDummyRole, true);
        item->addChild(dummy);
    }
    return item;
}

void ZodiacalReleasingController::ensureChildren(QTreeWidgetItem* item) {
    if (!item || item->data(0, kDummyRole).toBool()) return;
    const QString stableId = item->data(0, kStableIdRole).toString();
    if (!periodById_.contains(stableId)) return;
    if (item->childCount() != 1
        || !item->child(0)->data(0, kDummyRole).toBool()) {
        return;
    }
    delete item->takeChild(0);
    const auto children = zodiacalReleasingChildren(periodById_.value(stableId), timeline_);
    for (const auto& child : children) {
        if (child.endUtc <= displayStartUtc_) continue;
        item->addChild(makePeriodItem(child));
    }
}

void ZodiacalReleasingController::handleSelectionChanged() {
    emit selectionChanged();
}

void ZodiacalReleasingController::locateReferenceMoment(
    const QDateTime& utc,
    bool updateEditors) {
    if (!timeline_.valid || stale_ || !utc.isValid()) return;
    referenceMomentUtc_ = utc.toUTC();
    if (updateEditors) {
        QTimeZone timezone(natalInput_.timezone.toUtf8());
        const QDateTime local = timezone.isValid()
            ? referenceMomentUtc_.toTimeZone(timezone)
            : referenceMomentUtc_.toUTC();
        const QSignalBlocker dateBlock(referenceDateEdit_);
        const QSignalBlocker timeBlock(referenceTimeEdit_);
        referenceDateEdit_->setDate(local.date());
        referenceTimeEdit_->setTime(local.time());
    }
    updateReferenceChain(referenceMomentUtc_);
    expandReferenceChain();
    emit timelineChanged();
}

void ZodiacalReleasingController::locateEditedMoment() {
    if (!timeline_.valid || stale_) {
        emit statusMessage("Recalculate the Zodiacal Releasing timeline first.");
        return;
    }
    QTimeZone timezone(natalInput_.timezone.toUtf8());
    if (!timezone.isValid()) {
        emit statusMessage("The natal chart timezone is invalid.");
        return;
    }
    QDateTime local(referenceDateEdit_->date(), referenceTimeEdit_->time(), timezone);
    if (!local.isValid()) {
        emit statusMessage("The selected research date/time is invalid in the natal timezone.");
        return;
    }
    const QDateTime utc = local.toUTC();
    if (utc < displayStartUtc_ || utc >= timeline_.rangeEndUtc) {
        emit statusMessage("The selected date is outside the calculated age range.");
        return;
    }
    locateReferenceMoment(utc, false);
}

void ZodiacalReleasingController::locateNow() {
    if (!timeline_.valid || stale_) {
        emit statusMessage("Recalculate the Zodiacal Releasing timeline first.");
        return;
    }
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (now < displayStartUtc_ || now >= timeline_.rangeEndUtc) {
        emit statusMessage("The current moment is outside the calculated age range.");
        return;
    }
    locateReferenceMoment(now, true);
}

void ZodiacalReleasingController::locateBirth() {
    if (!timeline_.valid || stale_) {
        emit statusMessage("Recalculate the Zodiacal Releasing timeline first.");
        return;
    }
    if (timeline_.context.birthUtc < displayStartUtc_) {
        emit statusMessage("Birth is outside the displayed start-age range.");
        return;
    }
    locateReferenceMoment(timeline_.context.birthUtc, true);
}

void ZodiacalReleasingController::updateReferenceChain(const QDateTime& utc) {
    referenceChain_ = zodiacalReleasingActiveChain(timeline_, utc);
    QSet<QString> activeIds;
    for (const auto& period : referenceChain_) activeIds.insert(period.stableId);
    for (int index = 0; periodsTree_ && index < periodsTree_->topLevelItemCount(); ++index) {
        updateActiveHighlightRoles(periodsTree_->topLevelItem(index), activeIds);
    }
    if (periodsTree_ && periodsTree_->viewport()) periodsTree_->viewport()->update();
    updateChainCards();
}

void ZodiacalReleasingController::updateChainCards() {
    for (int index = 0; index < 4; ++index) {
        if (index >= referenceChain_.size()) {
            setZodiacSignIcon(chainIconLabels_[index], -1);
            chainLabels_[index]->setText(
                index < timeline_.settings.maximumLevel ? "Outside range" : "Disabled");
            continue;
        }
        const auto& period = referenceChain_[index];
        QString markers = markerText(period);
        if (markers == "-") markers.clear();
        setZodiacSignIcon(chainIconLabels_[index], period.signIndex);
        chainLabels_[index]->setText(QString("%1\n%2\n%3 - %4%5")
            .arg(period.signName,
                 period.ruler,
                 localDateTimeText(period.startUtc),
                 localDateTimeText(period.endUtc),
                 markers.isEmpty() ? QString() : QString("\n%1").arg(markers)));
        chainLabels_[index]->setToolTip(period.fortuneRelationship);
        chainIconLabels_[index]->setToolTip(period.fortuneRelationship);
    }
}

QTreeWidgetItem* ZodiacalReleasingController::findTopLevelItem(
    const QString& stableId) const {
    for (int index = 0; periodsTree_ && index < periodsTree_->topLevelItemCount(); ++index) {
        auto* item = periodsTree_->topLevelItem(index);
        if (item->data(0, kStableIdRole).toString() == stableId) return item;
    }
    return nullptr;
}

QTreeWidgetItem* ZodiacalReleasingController::findDirectChild(
    QTreeWidgetItem* parent,
    const QString& stableId) const {
    if (!parent) return nullptr;
    for (int index = 0; index < parent->childCount(); ++index) {
        auto* child = parent->child(index);
        if (child->data(0, kStableIdRole).toString() == stableId) return child;
    }
    return nullptr;
}

void ZodiacalReleasingController::expandReferenceChain() {
    if (referenceChain_.isEmpty() || !periodsTree_) return;
    QTreeWidgetItem* item = findTopLevelItem(referenceChain_.first().stableId);
    if (!item) return;
    for (int level = 1; level < referenceChain_.size(); ++level) {
        ensureChildren(item);
        item->setExpanded(true);
        item = findDirectChild(item, referenceChain_[level].stableId);
        if (!item) break;
    }
    if (item) {
        periodsTree_->setCurrentItem(item);
        periodsTree_->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    }
}

void ZodiacalReleasingController::collapseAll() {
    if (periodsTree_) periodsTree_->collapseAll();
}

void ZodiacalReleasingController::copyReferenceChain() {
    if (!timeline_.valid || stale_ || referenceChain_.isEmpty()) {
        emit statusMessage("There is no current Zodiacal Releasing chain to copy.");
        return;
    }
    QString output;
    output += "# Zodiacal Releasing - Active Chain\n\n";
    output += QString("- **Natal chart:** %1\n").arg(natalName());
    output += QString("- **Birth:** %1\n").arg(
        natalChart_.localDateTime.toString("d MMMM yyyy, h:mm:ss AP"));
    output += QString("- **Location:** %1\n").arg(
        natalLocationName_.isEmpty() ? QString("-") : natalLocationName_);
    output += QString("- **Zodiac:** %1\n").arg(
        natalInput_.zodiacSystem == ZodiacSystem::Sidereal
            ? QString("Sidereal - %1").arg(siderealAyanamsaToString(natalInput_.siderealAyanamsa))
            : QString("Tropical"));
    output += QString("- **Release point:** %1 (%2)\n")
        .arg(zodiacalReleasingPointName(timeline_.settings.releasePoint),
             longitudeText(timeline_.releaseLongitude));
    output += QString("- **Method:** %1; Capricorn %2 years\n")
        .arg(zodiacalReleasingTimeKeyName(timeline_.settings.timeKey))
        .arg(timeline_.settings.capricornYears);
    output += QString("- **Research moment:** %1\n\n")
        .arg(localDateTimeText(referenceMomentUtc_));
    output += "| Level | Sign | Ruler | Starts | Ends | Fortune Relationship | Markers |\n";
    output += "|---|---|---|---|---|---|---|\n";
    for (const auto& period : referenceChain_) {
        output += QString("| L%1 | %2 | %3 | %4 | %5 | %6 | %7 |\n")
            .arg(period.level)
            .arg(markdownCell(period.signName),
                 markdownCell(period.ruler),
                 markdownCell(localDateTimeText(period.startUtc)),
                 markdownCell(localDateTimeText(period.endUtc)),
                 markdownCell(period.fortuneRelationship),
                 markdownCell(markerText(period)));
    }
    QApplication::clipboard()->setText(output);
    emit statusMessage("Copied the active Zodiacal Releasing chain as Markdown.");
}

void ZodiacalReleasingController::copyVisibleSchedule() {
    if (!timeline_.valid || stale_ || !periodsTree_) {
        emit statusMessage("There is no current Zodiacal Releasing schedule to copy.");
        return;
    }
    QStringList rows;
    for (int index = 0; index < periodsTree_->topLevelItemCount(); ++index) {
        appendTreeRows(periodsTree_->topLevelItem(index), &rows);
    }
    QString output;
    output += "# Zodiacal Releasing - Visible Schedule\n\n";
    output += QString("- **Natal chart:** %1\n").arg(natalName());
    output += QString("- **Release point:** %1\n")
        .arg(zodiacalReleasingPointName(timeline_.settings.releasePoint));
    output += QString("- **Method:** %1; Capricorn %2 years\n")
        .arg(zodiacalReleasingTimeKeyName(timeline_.settings.timeKey))
        .arg(timeline_.settings.capricornYears);
    output += QString("- **Displayed ages:** %1-%2\n")
        .arg(startAgeSpin_->value())
        .arg(endAgeSpin_->value());
    output += "- **Scope:** Includes the periods currently materialized in the expanded tree.\n\n";
    output += "| Period | Sign | Ruler | Starts | Ends | Markers |\n";
    output += "|---|---|---|---|---|---|\n";
    output += rows.join('\n');
    QApplication::clipboard()->setText(output);
    emit statusMessage("Copied the visible Zodiacal Releasing schedule as Markdown.");
}

QString ZodiacalReleasingController::localDateTimeText(const QDateTime& utc) const {
    if (!utc.isValid()) return "-";
    QTimeZone timezone(natalInput_.timezone.toUtf8());
    const QDateTime local = timezone.isValid() ? utc.toTimeZone(timezone) : utc.toUTC();
    return local.toString("d MMM yyyy, h:mm AP");
}

QString ZodiacalReleasingController::durationText(
    const ZodiacalReleasingPeriod& period) const {
    const qint64 seconds = std::max<qint64>(0, period.startUtc.secsTo(period.endUtc));
    const long double days = static_cast<long double>(seconds) / 86400.0L;
    if (days >= 365.0L) {
        return QString("%1 years (%2 days)")
            .arg(static_cast<double>(days / 365.2425L), 0, 'f', 2)
            .arg(static_cast<qint64>(std::llround(days)));
    }
    if (days >= 2.0L) {
        const qint64 wholeDays = seconds / 86400;
        const qint64 hours = (seconds % 86400) / 3600;
        return QString("%1d %2h").arg(wholeDays).arg(hours);
    }
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    return QString("%1h %2m").arg(hours).arg(minutes);
}

QString ZodiacalReleasingController::markerText(
    const ZodiacalReleasingPeriod& period) const {
    QStringList markers;
    if (period.loosingOfBond) markers << "Loosing of the Bond";
    if (period.foreshadowing) markers << "Foreshadowing";
    if (period.fortuneHouse == 1 || period.fortuneHouse == 10) {
        markers << "Major peak";
    } else if (period.fortuneHouse == 4 || period.fortuneHouse == 7) {
        markers << "Fortune angle";
    }
    if (period.truncated) markers << "Truncated at parent boundary";
    return markers.isEmpty() ? "-" : markers.join(" | ");
}

QString ZodiacalReleasingController::ageText(const QDateTime& utc) const {
    if (!utc.isValid() || !natalChart_.utcDateTime.isValid()) return "-";
    const long double years = static_cast<long double>(
        natalChart_.utcDateTime.toUTC().msecsTo(utc.toUTC()))
        / (365.2425L * 86400000.0L);
    return QString("%1").arg(static_cast<double>(years), 0, 'f', 2);
}

QDateTime ZodiacalReleasingController::ageBoundaryUtc(int age) const {
    if (!natalChart_.utcDateTime.isValid()) return {};
    QTimeZone timezone(natalInput_.timezone.toUtf8());
    if (timezone.isValid() && natalChart_.localDateTime.isValid()) {
        const QDate date = natalChart_.localDateTime.date().addYears(age);
        QDateTime local(date, natalChart_.localDateTime.time(), timezone);
        if (local.isValid()) return local.toUTC();
    }
    const long double milliseconds = static_cast<long double>(age)
        * 365.2425L * 86400000.0L;
    return natalChart_.utcDateTime.toUTC().addMSecs(
        static_cast<qint64>(std::llround(milliseconds)));
}

bool ZodiacalReleasingController::resolveLotLongitudes(
    double* fortune,
    double* spirit,
    double* eros,
    bool* hasFortune,
    bool* hasSpirit,
    bool* hasEros) const {
    if (fortune) *fortune = natalChart_.partOfFortune;
    if (spirit) *spirit = 0.0;
    if (eros) *eros = 0.0;
    if (hasFortune) *hasFortune = natalChart_.hasPartOfFortune;
    if (hasSpirit) *hasSpirit = false;
    if (hasEros) *hasEros = false;
    for (const auto& body : natalChart_.bodies) {
        if (body.name == "Part of Fortune") {
            if (fortune) *fortune = body.longitude;
            if (hasFortune) *hasFortune = true;
        } else if (body.name == "Lot of Spirit") {
            if (spirit) *spirit = body.longitude;
            if (hasSpirit) *hasSpirit = true;
        } else if (body.name == "Lot of Eros") {
            if (eros) *eros = body.longitude;
            if (hasEros) *hasEros = true;
        }
    }
    return hasFortune && *hasFortune
        && hasSpirit && *hasSpirit
        && hasEros && *hasEros;
}

void ZodiacalReleasingController::showEmptyState(const QString& message) {
    if (periodsTree_) {
        periodsTree_->clear();
        auto* item = new QTreeWidgetItem({message});
        item->setFirstColumnSpanned(true);
        item->setDisabled(true);
        periodsTree_->addTopLevelItem(item);
    }
    if (methodLabel_) methodLabel_->setText(message);
    setZodiacSignIcon(methodSignIconLabel_, -1);
    if (methodSuffixLabel_) methodSuffixLabel_->hide();
    for (int index = 0; index < 4; ++index) {
        setZodiacSignIcon(chainIconLabels_[index], -1);
        if (chainLabels_[index]) chainLabels_[index]->setText("Not calculated");
    }
}

}  // namespace dracoved
