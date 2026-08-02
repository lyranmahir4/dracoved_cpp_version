#pragma once

#include <QDialog>

#include "../core/chart_types.h"
#include "../core/zodiacal_releasing.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;

namespace dracoved {

struct PreferencesData {
    LunarNodePolicy lunarNodePolicy;
    bool hasCurrentChart = false;
    bool currentChartUsesDefaultNodePolicy = true;
    LunarNodePolicy currentChartNodePolicy;
    bool applyNodePolicyToCurrentChart = true;
    HouseSystem defaultHouseSystem = HouseSystem::WholeSign;
    AspectOrbs aspectOrbs = defaultAspectOrbs();
    int themeMode = 0;
    bool showAspects = true;
    bool showTicks = true;
    bool showDegrees = true;
    bool showAspectSymbols = true;
    int tickDensity = 0;
    double fontScale = 1.0;
    ZodiacalReleasingSettings zodiacalReleasing;
};

class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(const PreferencesData& current, QWidget* parent = nullptr);

    PreferencesData preferences() const;

private:
    void restoreDefaults();
    void updateNodeUi();
    void updateZodiacalReleasingUi();

    QComboBox* nodeModeCombo_ = nullptr;
    QComboBox* nodePrimaryCombo_ = nullptr;
    QLabel* nodeExplanationLabel_ = nullptr;
    QCheckBox* applyNodesToCurrentChartCheck_ = nullptr;
    QComboBox* houseSystemCombo_ = nullptr;
    QComboBox* zrReleasePointCombo_ = nullptr;
    QComboBox* zrTimeKeyCombo_ = nullptr;
    QComboBox* zrCapricornCombo_ = nullptr;
    QCheckBox* zrSameSignRuleCheck_ = nullptr;
    QSpinBox* zrMaximumAgeSpin_ = nullptr;
    QComboBox* zrMaximumLevelCombo_ = nullptr;
    QComboBox* themeCombo_ = nullptr;
    QCheckBox* showAspectsCheck_ = nullptr;
    QCheckBox* showTicksCheck_ = nullptr;
    QCheckBox* showDegreesCheck_ = nullptr;
    QCheckBox* showAspectSymbolsCheck_ = nullptr;
    QComboBox* tickDensityCombo_ = nullptr;
    QDoubleSpinBox* fontScaleSpin_ = nullptr;
    QDoubleSpinBox* conjunctionOrbSpin_ = nullptr;
    QDoubleSpinBox* sextileOrbSpin_ = nullptr;
    QDoubleSpinBox* squareOrbSpin_ = nullptr;
    QDoubleSpinBox* trineOrbSpin_ = nullptr;
    QDoubleSpinBox* oppositionOrbSpin_ = nullptr;
};

}  // namespace dracoved
