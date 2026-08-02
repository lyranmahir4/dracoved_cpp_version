#include "preferences_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace dracoved {

namespace {

QDoubleSpinBox* makeOrbSpin(QWidget* parent) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(0.1, 15.0);
    spin->setDecimals(2);
    spin->setSingleStep(0.25);
    spin->setSuffix(" deg");
    return spin;
}

}  // namespace

PreferencesDialog::PreferencesDialog(const PreferencesData& current, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Preferences");
    setModal(true);
    setMinimumSize(620, 500);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto* tabs = new QTabWidget(this);

    auto* calculationPage = new QWidget(tabs);
    auto* calculationPageLayout = new QVBoxLayout(calculationPage);
    calculationPageLayout->setContentsMargins(0, 0, 0, 0);
    auto* calculationContent = new QWidget(calculationPage);
    auto* calculationLayout = new QVBoxLayout(calculationContent);
    calculationLayout->setContentsMargins(12, 12, 12, 12);
    calculationLayout->setSpacing(12);

    auto* nodeGroup = new QGroupBox("Lunar Nodes", calculationContent);
    auto* nodeForm = new QFormLayout(nodeGroup);
    nodeModeCombo_ = new QComboBox(nodeGroup);
    nodeModeCombo_->addItem("Mean Nodes", static_cast<int>(LunarNodeMode::MeanOnly));
    nodeModeCombo_->addItem("True Nodes", static_cast<int>(LunarNodeMode::TrueOnly));
    nodeModeCombo_->addItem("Show Both", static_cast<int>(LunarNodeMode::Both));
    nodePrimaryCombo_ = new QComboBox(nodeGroup);
    nodePrimaryCombo_->addItem("Mean", static_cast<int>(LunarNodeType::Mean));
    nodePrimaryCombo_->addItem("True", static_cast<int>(LunarNodeType::True));
    nodeExplanationLabel_ = new QLabel(nodeGroup);
    nodeExplanationLabel_->setWordWrap(true);
    nodeExplanationLabel_->setObjectName("hintLabel");
    nodeForm->addRow("Default calculation:", nodeModeCombo_);
    nodeForm->addRow("Primary when showing both:", nodePrimaryCombo_);
    nodeForm->addRow(nodeExplanationLabel_);
    if (current.hasCurrentChart) {
        auto* currentChartLabel = new QLabel(
            QString("%1 (%2)")
                .arg(lunarNodePolicySummary(current.currentChartNodePolicy),
                     current.currentChartUsesDefaultNodePolicy
                         ? QString("uses application default")
                         : QString("chart-specific override")),
            nodeGroup);
        currentChartLabel->setWordWrap(true);
        nodeForm->addRow("Current chart:", currentChartLabel);
        applyNodesToCurrentChartCheck_ = new QCheckBox(
            current.currentChartUsesDefaultNodePolicy
                ? "Recalculate the current chart with this setting"
                : "Apply this setting to the current chart and replace its override",
            nodeGroup);
        applyNodesToCurrentChartCheck_->setChecked(current.applyNodePolicyToCurrentChart);
        applyNodesToCurrentChartCheck_->setToolTip(
            "When enabled, Apply immediately recalculates the loaded natal chart and dependent views.");
        nodeForm->addRow(applyNodesToCurrentChartCheck_);
    }
    calculationLayout->addWidget(nodeGroup);

    auto* chartDefaultsGroup = new QGroupBox("New Chart Defaults", calculationContent);
    auto* chartDefaultsForm = new QFormLayout(chartDefaultsGroup);
    houseSystemCombo_ = new QComboBox(chartDefaultsGroup);
    houseSystemCombo_->addItem("Whole Sign", static_cast<int>(HouseSystem::WholeSign));
    houseSystemCombo_->addItem("Placidus", static_cast<int>(HouseSystem::Placidus));
    chartDefaultsForm->addRow("House system:", houseSystemCombo_);
    auto* defaultHint = new QLabel(
        "These defaults apply to newly created charts. A saved chart may keep its own calculation override.",
        chartDefaultsGroup);
    defaultHint->setWordWrap(true);
    defaultHint->setObjectName("hintLabel");
    chartDefaultsForm->addRow(defaultHint);
    calculationLayout->addWidget(chartDefaultsGroup);

    auto* releasingGroup = new QGroupBox("Zodiacal Releasing Defaults", calculationContent);
    auto* releasingForm = new QFormLayout(releasingGroup);
    zrReleasePointCombo_ = new QComboBox(releasingGroup);
    zrReleasePointCombo_->addItem(
        "Lot of Spirit", static_cast<int>(ZodiacalReleasingPoint::Spirit));
    zrReleasePointCombo_->addItem(
        "Lot of Fortune", static_cast<int>(ZodiacalReleasingPoint::Fortune));
    zrReleasePointCombo_->addItem(
        "Lot of Eros", static_cast<int>(ZodiacalReleasingPoint::Eros));
    zrTimeKeyCombo_ = new QComboBox(releasingGroup);
    zrTimeKeyCombo_->addItem(
        "Traditional 360-day year",
        static_cast<int>(ZodiacalReleasingTimeKey::Traditional360));
    zrTimeKeyCombo_->addItem(
        "Alternative 365.2425-day year",
        static_cast<int>(ZodiacalReleasingTimeKey::Calendar3652425));
    zrCapricornCombo_ = new QComboBox(releasingGroup);
    zrCapricornCombo_->addItem("27 years - Valens / standard", 27);
    zrCapricornCombo_->addItem("30 years - alternative", 30);
    zrSameSignRuleCheck_ = new QCheckBox(
        "Apply Valens' next-sign rule when Fortune and Spirit share a sign",
        releasingGroup);
    zrMaximumAgeSpin_ = new QSpinBox(releasingGroup);
    zrMaximumAgeSpin_->setRange(1, 300);
    zrMaximumAgeSpin_->setSuffix(" years");
    zrMaximumLevelCombo_ = new QComboBox(releasingGroup);
    zrMaximumLevelCombo_->addItem("L1 only", 1);
    zrMaximumLevelCombo_->addItem("L1-L2", 2);
    zrMaximumLevelCombo_->addItem("L1-L3", 3);
    zrMaximumLevelCombo_->addItem("L1-L4", 4);
    releasingForm->addRow("Default release point:", zrReleasePointCombo_);
    releasingForm->addRow("Time key:", zrTimeKeyCombo_);
    releasingForm->addRow("Capricorn period:", zrCapricornCombo_);
    releasingForm->addRow(zrSameSignRuleCheck_);
    releasingForm->addRow("Default maximum age:", zrMaximumAgeSpin_);
    releasingForm->addRow("Default detail:", zrMaximumLevelCombo_);
    auto* releasingHint = new QLabel(
        "The workspace always shows the active method. Zodiacal Releasing follows the loaded chart's "
        "Tropical or Sidereal zodiac but uses traditional sign rulers.",
        releasingGroup);
    releasingHint->setWordWrap(true);
    releasingHint->setObjectName("hintLabel");
    releasingForm->addRow(releasingHint);
    calculationLayout->addWidget(releasingGroup);
    calculationLayout->addStretch();
    auto* calculationScroll = new QScrollArea(calculationPage);
    calculationScroll->setWidgetResizable(true);
    calculationScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    calculationScroll->setWidget(calculationContent);
    calculationPageLayout->addWidget(calculationScroll);
    tabs->addTab(calculationPage, "Calculations");

    auto* appearancePage = new QWidget(tabs);
    auto* appearanceLayout = new QVBoxLayout(appearancePage);
    appearanceLayout->setContentsMargins(12, 12, 12, 12);
    appearanceLayout->setSpacing(12);

    auto* appearanceGroup = new QGroupBox("Application Appearance", appearancePage);
    auto* appearanceForm = new QFormLayout(appearanceGroup);
    themeCombo_ = new QComboBox(appearanceGroup);
    themeCombo_->addItem("Light", 0);
    themeCombo_->addItem("Dark", 1);
    themeCombo_->addItem("Creme", 2);
    fontScaleSpin_ = new QDoubleSpinBox(appearanceGroup);
    fontScaleSpin_->setRange(0.8, 1.4);
    fontScaleSpin_->setDecimals(2);
    fontScaleSpin_->setSingleStep(0.05);
    fontScaleSpin_->setSuffix("x");
    appearanceForm->addRow("Theme:", themeCombo_);
    appearanceForm->addRow("Chart font scale:", fontScaleSpin_);
    appearanceLayout->addWidget(appearanceGroup);

    auto* wheelGroup = new QGroupBox("Main Chart Wheel", appearancePage);
    auto* wheelForm = new QFormLayout(wheelGroup);
    showAspectsCheck_ = new QCheckBox("Draw aspect lines", wheelGroup);
    showTicksCheck_ = new QCheckBox("Draw degree ticks", wheelGroup);
    showDegreesCheck_ = new QCheckBox("Show body and angle degrees", wheelGroup);
    showAspectSymbolsCheck_ = new QCheckBox("Show aspect symbols", wheelGroup);
    tickDensityCombo_ = new QComboBox(wheelGroup);
    tickDensityCombo_->addItem("Full", 0);
    tickDensityCombo_->addItem("Medium", 1);
    tickDensityCombo_->addItem("Minimal", 2);
    wheelForm->addRow(showAspectsCheck_);
    wheelForm->addRow(showTicksCheck_);
    wheelForm->addRow(showDegreesCheck_);
    wheelForm->addRow(showAspectSymbolsCheck_);
    wheelForm->addRow("Tick density:", tickDensityCombo_);
    appearanceLayout->addWidget(wheelGroup);
    appearanceLayout->addStretch();
    tabs->addTab(appearancePage, "Appearance");

    auto* aspectsPage = new QWidget(tabs);
    auto* aspectsLayout = new QVBoxLayout(aspectsPage);
    aspectsLayout->setContentsMargins(12, 12, 12, 12);
    auto* aspectsGroup = new QGroupBox("Major Aspect Orbs", aspectsPage);
    auto* aspectsForm = new QFormLayout(aspectsGroup);
    conjunctionOrbSpin_ = makeOrbSpin(aspectsGroup);
    sextileOrbSpin_ = makeOrbSpin(aspectsGroup);
    squareOrbSpin_ = makeOrbSpin(aspectsGroup);
    trineOrbSpin_ = makeOrbSpin(aspectsGroup);
    oppositionOrbSpin_ = makeOrbSpin(aspectsGroup);
    aspectsForm->addRow("Conjunction:", conjunctionOrbSpin_);
    aspectsForm->addRow("Sextile:", sextileOrbSpin_);
    aspectsForm->addRow("Square:", squareOrbSpin_);
    aspectsForm->addRow("Trine:", trineOrbSpin_);
    aspectsForm->addRow("Opposition:", oppositionOrbSpin_);
    auto* aspectsHint = new QLabel(
        "These orbs are used for chart aspect grids and newly calculated charts.", aspectsGroup);
    aspectsHint->setWordWrap(true);
    aspectsHint->setObjectName("hintLabel");
    aspectsForm->addRow(aspectsHint);
    aspectsLayout->addWidget(aspectsGroup);
    aspectsLayout->addStretch();
    tabs->addTab(aspectsPage, "Aspects");

    root->addWidget(tabs, 1);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::RestoreDefaults | QDialogButtonBox::Cancel | QDialogButtonBox::Ok,
        Qt::Horizontal,
        this);
    buttons->button(QDialogButtonBox::Ok)->setText("Apply");
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
            this, &PreferencesDialog::restoreDefaults);
    connect(nodeModeCombo_, &QComboBox::currentIndexChanged, this, &PreferencesDialog::updateNodeUi);
    connect(zrReleasePointCombo_, &QComboBox::currentIndexChanged,
            this, &PreferencesDialog::updateZodiacalReleasingUi);

    const int modeIndex = nodeModeCombo_->findData(static_cast<int>(current.lunarNodePolicy.mode));
    nodeModeCombo_->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
    const int primaryIndex = nodePrimaryCombo_->findData(static_cast<int>(current.lunarNodePolicy.primary));
    nodePrimaryCombo_->setCurrentIndex(primaryIndex >= 0 ? primaryIndex : 0);
    houseSystemCombo_->setCurrentIndex(current.defaultHouseSystem == HouseSystem::Placidus ? 1 : 0);
    themeCombo_->setCurrentIndex(qBound(0, current.themeMode, 2));
    showAspectsCheck_->setChecked(current.showAspects);
    showTicksCheck_->setChecked(current.showTicks);
    showDegreesCheck_->setChecked(current.showDegrees);
    showAspectSymbolsCheck_->setChecked(current.showAspectSymbols);
    tickDensityCombo_->setCurrentIndex(qBound(0, current.tickDensity, 2));
    fontScaleSpin_->setValue(current.fontScale);
    conjunctionOrbSpin_->setValue(current.aspectOrbs.conjunction);
    sextileOrbSpin_->setValue(current.aspectOrbs.sextile);
    squareOrbSpin_->setValue(current.aspectOrbs.square);
    trineOrbSpin_->setValue(current.aspectOrbs.trine);
    oppositionOrbSpin_->setValue(current.aspectOrbs.opposition);
    zrReleasePointCombo_->setCurrentIndex(std::max(
        0, zrReleasePointCombo_->findData(static_cast<int>(
               current.zodiacalReleasing.releasePoint))));
    zrTimeKeyCombo_->setCurrentIndex(std::max(
        0, zrTimeKeyCombo_->findData(static_cast<int>(
               current.zodiacalReleasing.timeKey))));
    zrCapricornCombo_->setCurrentIndex(std::max(
        0, zrCapricornCombo_->findData(
               current.zodiacalReleasing.capricornYears == 30 ? 30 : 27)));
    zrSameSignRuleCheck_->setChecked(
        current.zodiacalReleasing.applySameSignSpiritRule);
    zrMaximumAgeSpin_->setValue(
        qBound(1, current.zodiacalReleasing.maximumAge, 300));
    zrMaximumLevelCombo_->setCurrentIndex(std::max(
        0, zrMaximumLevelCombo_->findData(
               qBound(1, current.zodiacalReleasing.maximumLevel, 4))));
    updateNodeUi();
    updateZodiacalReleasingUi();
}

PreferencesData PreferencesDialog::preferences() const {
    PreferencesData out;
    out.lunarNodePolicy.mode = static_cast<LunarNodeMode>(nodeModeCombo_->currentData().toInt());
    out.lunarNodePolicy.primary = static_cast<LunarNodeType>(nodePrimaryCombo_->currentData().toInt());
    if (out.lunarNodePolicy.mode == LunarNodeMode::MeanOnly) {
        out.lunarNodePolicy.primary = LunarNodeType::Mean;
    } else if (out.lunarNodePolicy.mode == LunarNodeMode::TrueOnly) {
        out.lunarNodePolicy.primary = LunarNodeType::True;
    }
    out.applyNodePolicyToCurrentChart = applyNodesToCurrentChartCheck_
        && applyNodesToCurrentChartCheck_->isChecked();
    out.defaultHouseSystem = static_cast<HouseSystem>(houseSystemCombo_->currentData().toInt());
    out.themeMode = themeCombo_->currentData().toInt();
    out.showAspects = showAspectsCheck_->isChecked();
    out.showTicks = showTicksCheck_->isChecked();
    out.showDegrees = showDegreesCheck_->isChecked();
    out.showAspectSymbols = showAspectSymbolsCheck_->isChecked();
    out.tickDensity = tickDensityCombo_->currentData().toInt();
    out.fontScale = fontScaleSpin_->value();
    out.aspectOrbs.conjunction = conjunctionOrbSpin_->value();
    out.aspectOrbs.sextile = sextileOrbSpin_->value();
    out.aspectOrbs.square = squareOrbSpin_->value();
    out.aspectOrbs.trine = trineOrbSpin_->value();
    out.aspectOrbs.opposition = oppositionOrbSpin_->value();
    out.zodiacalReleasing.releasePoint = static_cast<ZodiacalReleasingPoint>(
        zrReleasePointCombo_->currentData().toInt());
    out.zodiacalReleasing.timeKey = static_cast<ZodiacalReleasingTimeKey>(
        zrTimeKeyCombo_->currentData().toInt());
    out.zodiacalReleasing.capricornYears =
        zrCapricornCombo_->currentData().toInt() == 30 ? 30 : 27;
    out.zodiacalReleasing.applySameSignSpiritRule =
        zrSameSignRuleCheck_->isChecked();
    out.zodiacalReleasing.maximumAge = zrMaximumAgeSpin_->value();
    out.zodiacalReleasing.maximumLevel =
        zrMaximumLevelCombo_->currentData().toInt();
    return out;
}

void PreferencesDialog::restoreDefaults() {
    const PreferencesData defaults;
    nodeModeCombo_->setCurrentIndex(nodeModeCombo_->findData(static_cast<int>(defaults.lunarNodePolicy.mode)));
    nodePrimaryCombo_->setCurrentIndex(nodePrimaryCombo_->findData(static_cast<int>(defaults.lunarNodePolicy.primary)));
    houseSystemCombo_->setCurrentIndex(0);
    themeCombo_->setCurrentIndex(0);
    showAspectsCheck_->setChecked(true);
    showTicksCheck_->setChecked(true);
    showDegreesCheck_->setChecked(true);
    showAspectSymbolsCheck_->setChecked(true);
    tickDensityCombo_->setCurrentIndex(0);
    fontScaleSpin_->setValue(1.0);
    const auto orbs = defaultAspectOrbs();
    conjunctionOrbSpin_->setValue(orbs.conjunction);
    sextileOrbSpin_->setValue(orbs.sextile);
    squareOrbSpin_->setValue(orbs.square);
    trineOrbSpin_->setValue(orbs.trine);
    oppositionOrbSpin_->setValue(orbs.opposition);
    zrReleasePointCombo_->setCurrentIndex(
        zrReleasePointCombo_->findData(
            static_cast<int>(defaults.zodiacalReleasing.releasePoint)));
    zrTimeKeyCombo_->setCurrentIndex(
        zrTimeKeyCombo_->findData(
            static_cast<int>(defaults.zodiacalReleasing.timeKey)));
    zrCapricornCombo_->setCurrentIndex(
        zrCapricornCombo_->findData(defaults.zodiacalReleasing.capricornYears));
    zrSameSignRuleCheck_->setChecked(
        defaults.zodiacalReleasing.applySameSignSpiritRule);
    zrMaximumAgeSpin_->setValue(defaults.zodiacalReleasing.maximumAge);
    zrMaximumLevelCombo_->setCurrentIndex(
        zrMaximumLevelCombo_->findData(defaults.zodiacalReleasing.maximumLevel));
    updateNodeUi();
    updateZodiacalReleasingUi();
}

void PreferencesDialog::updateZodiacalReleasingUi() {
    const bool spiritSelected = zrReleasePointCombo_->currentData().toInt()
        == static_cast<int>(ZodiacalReleasingPoint::Spirit);
    zrSameSignRuleCheck_->setEnabled(spiritSelected);
    zrSameSignRuleCheck_->setToolTip(spiritSelected
        ? "Applied only when releasing from Spirit and Fortune and Spirit share a sign."
        : "This special rule applies only when releasing from Lot of Spirit.");
}

void PreferencesDialog::updateNodeUi() {
    const auto mode = static_cast<LunarNodeMode>(nodeModeCombo_->currentData().toInt());
    nodePrimaryCombo_->setEnabled(mode == LunarNodeMode::Both);
    if (mode == LunarNodeMode::MeanOnly) {
        nodePrimaryCombo_->setCurrentIndex(nodePrimaryCombo_->findData(static_cast<int>(LunarNodeType::Mean)));
        nodeExplanationLabel_->setText(
            "Uses the averaged lunar-node motion. This preserves DracoVed's historical calculation behavior.");
    } else if (mode == LunarNodeMode::TrueOnly) {
        nodePrimaryCombo_->setCurrentIndex(nodePrimaryCombo_->findData(static_cast<int>(LunarNodeType::True)));
        nodeExplanationLabel_->setText(
            "Uses the oscillating true lunar node from Swiss Ephemeris. True nodes can briefly move direct.");
    } else {
        nodeExplanationLabel_->setText(
            "Calculates and displays both models. The primary model is used when a legacy feature asks for the generic North or South Node.");
    }
}

}  // namespace dracoved
