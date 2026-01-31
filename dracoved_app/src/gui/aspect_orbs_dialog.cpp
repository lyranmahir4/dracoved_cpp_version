#include "aspect_orbs_dialog.h"

#include <cmath>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace dracoved {

namespace {

QDoubleSpinBox* makeOrbSpin(QWidget* parent) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(0.0, 20.0);
    spin->setDecimals(1);
    spin->setSingleStep(0.1);
    return spin;
}

}  // namespace

AspectOrbsDialog::AspectOrbsDialog(const AspectOrbs& current, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Aspect Orbs");
    setModal(true);
    setMinimumWidth(420);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    auto* modeGroup = new QGroupBox("Mode", this);
    auto* modeLayout = new QHBoxLayout(modeGroup);
    modeLayout->setContentsMargins(10, 8, 10, 8);
    basicRadio_ = new QRadioButton("Basic (single orb)", modeGroup);
    advancedRadio_ = new QRadioButton("Advanced (per aspect)", modeGroup);
    modeLayout->addWidget(basicRadio_);
    modeLayout->addWidget(advancedRadio_);
    modeLayout->addStretch();
    mainLayout->addWidget(modeGroup);

    basicGroup_ = new QGroupBox("Basic", this);
    auto* basicLayout = new QHBoxLayout(basicGroup_);
    basicLayout->setContentsMargins(10, 8, 10, 8);
    auto* basicLabel = new QLabel("Orb (deg)", basicGroup_);
    basicSpin_ = makeOrbSpin(basicGroup_);
    basicSpin_->setMinimumWidth(120);
    basicLayout->addWidget(basicLabel);
    basicLayout->addWidget(basicSpin_, 1);
    mainLayout->addWidget(basicGroup_);

    advancedGroup_ = new QGroupBox("Advanced", this);
    auto* form = new QFormLayout(advancedGroup_);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFormAlignment(Qt::AlignTop);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);

    conjSpin_ = makeOrbSpin(advancedGroup_);
    sextSpin_ = makeOrbSpin(advancedGroup_);
    squareSpin_ = makeOrbSpin(advancedGroup_);
    trineSpin_ = makeOrbSpin(advancedGroup_);
    oppSpin_ = makeOrbSpin(advancedGroup_);
    conjSpin_->setMinimumWidth(120);
    sextSpin_->setMinimumWidth(120);
    squareSpin_->setMinimumWidth(120);
    trineSpin_->setMinimumWidth(120);
    oppSpin_->setMinimumWidth(120);

    conjSpin_->setValue(current.conjunction);
    sextSpin_->setValue(current.sextile);
    squareSpin_->setValue(current.square);
    trineSpin_->setValue(current.trine);
    oppSpin_->setValue(current.opposition);

    form->addRow("Conjunction", conjSpin_);
    form->addRow("Sextile", sextSpin_);
    form->addRow("Square", squareSpin_);
    form->addRow("Trine", trineSpin_);
    form->addRow("Opposition", oppSpin_);

    mainLayout->addWidget(advancedGroup_);

    cachedAdvanced_ = current;
    cachedAdvancedValid_ = true;

    const bool allEqual = std::fabs(current.conjunction - current.sextile) < 0.05
        && std::fabs(current.conjunction - current.square) < 0.05
        && std::fabs(current.conjunction - current.trine) < 0.05
        && std::fabs(current.conjunction - current.opposition) < 0.05;
    basicSpin_->setValue(current.conjunction);
    basicRadio_->setChecked(allEqual);
    advancedRadio_->setChecked(!allEqual);
    applyAdvancedEnabled(!allEqual);
    if (allEqual) {
        applyBasicValue();
    }

    auto* buttons = new QHBoxLayout();
    restoreButton_ = new QPushButton("Restore Defaults", this);
    cancelButton_ = new QPushButton("Cancel", this);
    applyButton_ = new QPushButton("Apply", this);
    applyButton_->setDefault(true);

    buttons->addWidget(restoreButton_);
    buttons->addStretch();
    buttons->addWidget(cancelButton_);
    buttons->addWidget(applyButton_);
    mainLayout->addLayout(buttons);

    connect(restoreButton_, &QPushButton::clicked, this, &AspectOrbsDialog::applyDefaults);
    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
    connect(applyButton_, &QPushButton::clicked, this, &QDialog::accept);
    connect(basicRadio_, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            cachedAdvanced_ = orbs();
            cachedAdvancedValid_ = true;
            applyBasicValue();
        } else if (cachedAdvancedValid_) {
            conjSpin_->setValue(cachedAdvanced_.conjunction);
            sextSpin_->setValue(cachedAdvanced_.sextile);
            squareSpin_->setValue(cachedAdvanced_.square);
            trineSpin_->setValue(cachedAdvanced_.trine);
            oppSpin_->setValue(cachedAdvanced_.opposition);
        }
        applyAdvancedEnabled(!checked);
    });
    connect(basicSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) {
        if (basicRadio_->isChecked()) {
            applyBasicValue();
        }
    });
}

void AspectOrbsDialog::applyBasicValue() {
    const double value = basicSpin_->value();
    conjSpin_->setValue(value);
    sextSpin_->setValue(value);
    squareSpin_->setValue(value);
    trineSpin_->setValue(value);
    oppSpin_->setValue(value);
}

void AspectOrbsDialog::applyAdvancedEnabled(bool enabled) {
    if (advancedGroup_) {
        advancedGroup_->setEnabled(enabled);
    }
    if (basicGroup_) {
        basicGroup_->setEnabled(!enabled);
    }
}

void AspectOrbsDialog::applyDefaults() {
    const AspectOrbs defaults = defaultAspectOrbs();
    basicSpin_->setValue(defaults.conjunction);
    conjSpin_->setValue(defaults.conjunction);
    sextSpin_->setValue(defaults.sextile);
    squareSpin_->setValue(defaults.square);
    trineSpin_->setValue(defaults.trine);
    oppSpin_->setValue(defaults.opposition);
    cachedAdvanced_ = defaults;
    cachedAdvancedValid_ = true;
    if (basicRadio_->isChecked()) {
        applyBasicValue();
    }
}

AspectOrbs AspectOrbsDialog::orbs() const {
    AspectOrbs out;
    if (basicRadio_ && basicRadio_->isChecked()) {
        const double value = basicSpin_->value();
        out.conjunction = value;
        out.sextile = value;
        out.square = value;
        out.trine = value;
        out.opposition = value;
    } else {
        out.conjunction = conjSpin_->value();
        out.sextile = sextSpin_->value();
        out.square = squareSpin_->value();
        out.trine = trineSpin_->value();
        out.opposition = oppSpin_->value();
    }
    return out;
}

}  // namespace dracoved
