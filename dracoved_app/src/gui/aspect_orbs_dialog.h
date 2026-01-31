#pragma once

#include <QDialog>

#include "../core/chart_types.h"

class QDoubleSpinBox;
class QPushButton;
class QRadioButton;
class QGroupBox;

namespace dracoved {

class AspectOrbsDialog : public QDialog {
    Q_OBJECT

public:
    explicit AspectOrbsDialog(const AspectOrbs& current, QWidget* parent = nullptr);

    AspectOrbs orbs() const;

private:
    void applyDefaults();
    void applyBasicValue();
    void applyAdvancedEnabled(bool enabled);

    QRadioButton* basicRadio_ = nullptr;
    QRadioButton* advancedRadio_ = nullptr;
    QGroupBox* basicGroup_ = nullptr;
    QGroupBox* advancedGroup_ = nullptr;
    QDoubleSpinBox* basicSpin_ = nullptr;
    QDoubleSpinBox* conjSpin_ = nullptr;
    QDoubleSpinBox* sextSpin_ = nullptr;
    QDoubleSpinBox* squareSpin_ = nullptr;
    QDoubleSpinBox* trineSpin_ = nullptr;
    QDoubleSpinBox* oppSpin_ = nullptr;
    QPushButton* restoreButton_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    AspectOrbs cachedAdvanced_;
    bool cachedAdvancedValid_ = false;
};

}  // namespace dracoved
