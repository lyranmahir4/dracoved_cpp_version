#include "synastry_controller.h"

#include "../core/swiss_eph.h"
#include "chart_profile_store.h"
#include "chart_setup_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace dracoved {

namespace {

QString describeInput(const NatalInput& input, const QString& location) {
    const QString name = input.name.trimmed().isEmpty()
        ? QStringLiteral("(unnamed)")
        : input.name.trimmed();
    QString text = name;
    if (input.date.isValid()) {
        text += QStringLiteral("\n") + input.date.toString("d MMM yyyy");
        if (input.time.isValid()) {
            text += QStringLiteral(", ") + input.time.toString("HH:mm");
        }
    }
    const QString place = location.trimmed();
    if (!place.isEmpty()) {
        text += QStringLiteral("\n") + place;
    }
    return text;
}

}  // namespace

SynastryController::SynastryController(SwissEph* swe,
                                      QNetworkAccessManager* network,
                                      QObject* parent)
    : QObject(parent),
      swe_(swe),
      network_(network),
      engine_(swe, QString()) {
    buildFiltersUi();
    updateSummary();
}

QWidget* SynastryController::filtersWidget() const {
    return filtersRoot_;
}

void SynastryController::setEphePath(const QString& ephePath) {
    if (!ephePath.isEmpty()) {
        engine_.setEphePath(ephePath);
    }
}

void SynastryController::setPersonA(const NatalInput& input, const QString& locationName) {
    personAInput_ = input;
    personALocation_ = locationName;
    hasPersonA_ = true;
    // Person B must follow A's zodiac. If B was computed under a different
    // zodiac, recompute it now so the pair stays comparable.
    if (hasPersonB_
        && (personBInput_.zodiacSystem != input.zodiacSystem
            || personBInput_.siderealAyanamsa != input.siderealAyanamsa)) {
        computePersonB(personBInput_, personBLocation_, personBLabel_);
    }
    updateSummary();
}

void SynastryController::clearPersonA() {
    hasPersonA_ = false;
    personAInput_ = NatalInput();
    personALocation_.clear();
    updateSummary();
}

void SynastryController::setActive(bool active) {
    active_ = active;
    if (active_) {
        refreshProfileList();
        updateSummary();
    }
}

bool SynastryController::hasPersonB() const {
    return hasPersonB_;
}

const NatalChart& SynastryController::personBChart() const {
    return personBChart_;
}

const NatalInput& SynastryController::personBInput() const {
    return personBInput_;
}

QString SynastryController::personBDisplayName() const {
    if (!personBLabel_.trimmed().isEmpty()) {
        return personBLabel_.trimmed();
    }
    const QString name = personBInput_.name.trimmed();
    return name.isEmpty() ? QStringLiteral("Person B") : name;
}

QString SynastryController::personBLocation() const {
    return personBLocation_;
}

QString SynastryController::personALabelText() const {
    const QString name = personAInput_.name.trimmed();
    return name.isEmpty() ? QStringLiteral("Person A") : name;
}

bool SynastryController::isSwapped() const {
    return swapped_;
}

void SynastryController::setDefaultLunarNodePolicy(const LunarNodePolicy& policy) {
    defaultLunarNodePolicy_ = policy;
}

void SynastryController::setDefaultHouseSystem(HouseSystem system) {
    defaultHouseSystem_ = system;
}

void SynastryController::refreshProfileList() {
    if (!profileCombo_) {
        return;
    }
    const QString previous = profileCombo_->currentData().toString();
    const QSignalBlocker blocker(profileCombo_);
    profileCombo_->clear();
    profileCombo_->addItem(QStringLiteral("Select saved chart..."), QString());
    for (const QString& name : profilestore::listProfiles()) {
        profileCombo_->addItem(name, name);
    }
    const int index = previous.isEmpty() ? 0 : profileCombo_->findData(previous);
    profileCombo_->setCurrentIndex(index < 0 ? 0 : index);
}

void SynastryController::buildFiltersUi() {
    filtersRoot_ = new QWidget();
    auto* layout = new QVBoxLayout(filtersRoot_);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    auto* personABox = new QGroupBox(QStringLiteral("Person A (active chart)"), filtersRoot_);
    auto* personALayout = new QVBoxLayout(personABox);
    personALayout->setContentsMargins(6, 6, 6, 6);
    personALayout->setSpacing(2);
    personASummary_ = new QLabel(personABox);
    personASummary_->setWordWrap(true);
    personALayout->addWidget(personASummary_);
    layout->addWidget(personABox);

    auto* personBBox = new QGroupBox(QStringLiteral("Person B"), filtersRoot_);
    auto* personBLayout = new QVBoxLayout(personBBox);
    personBLayout->setContentsMargins(6, 6, 6, 6);
    personBLayout->setSpacing(4);

    profileCombo_ = new QComboBox(personBBox);
    profileCombo_->setToolTip(QStringLiteral("Pick a saved chart to compare against."));
    personBLayout->addWidget(profileCombo_);

    auto* pickRow = new QWidget(personBBox);
    auto* pickRowLayout = new QHBoxLayout(pickRow);
    pickRowLayout->setContentsMargins(0, 0, 0, 0);
    pickRowLayout->setSpacing(4);
    loadProfileButton_ = new QPushButton(QStringLiteral("Use Selected"), pickRow);
    manualEntryButton_ = new QPushButton(QStringLiteral("Enter Birth Data..."), pickRow);
    pickRowLayout->addWidget(loadProfileButton_);
    pickRowLayout->addWidget(manualEntryButton_);
    personBLayout->addWidget(pickRow);

    personBSummary_ = new QLabel(personBBox);
    personBSummary_->setWordWrap(true);
    personBLayout->addWidget(personBSummary_);

    auto* actionRow = new QWidget(personBBox);
    auto* actionRowLayout = new QHBoxLayout(actionRow);
    actionRowLayout->setContentsMargins(0, 0, 0, 0);
    actionRowLayout->setSpacing(4);
    saveProfileButton_ = new QPushButton(QStringLiteral("Save as Chart"), actionRow);
    saveProfileButton_->setToolTip(
        QStringLiteral("Save Person B as a saved chart so it can be picked again later."));
    clearButton_ = new QPushButton(QStringLiteral("Clear"), actionRow);
    actionRowLayout->addWidget(saveProfileButton_);
    actionRowLayout->addWidget(clearButton_);
    personBLayout->addWidget(actionRow);

    layout->addWidget(personBBox);

    swapCheck_ = new QCheckBox(QStringLiteral("Draw Person B on the inner ring"), filtersRoot_);
    swapCheck_->setToolTip(
        QStringLiteral("The wheel is oriented on the inner chart's Ascendant, so swapping "
                       "re-frames the comparison in the other person's chart."));
    layout->addWidget(swapCheck_);

    statusLabel_ = new QLabel(filtersRoot_);
    statusLabel_->setObjectName(QStringLiteral("hintLabel"));
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    layout->addStretch(1);

    connect(loadProfileButton_, &QPushButton::clicked,
            this, &SynastryController::loadSelectedProfile);
    connect(manualEntryButton_, &QPushButton::clicked,
            this, &SynastryController::enterManualPersonB);
    connect(saveProfileButton_, &QPushButton::clicked,
            this, &SynastryController::requestSavePersonB);
    connect(clearButton_, &QPushButton::clicked,
            this, &SynastryController::clearPersonB);
    connect(swapCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        swapped_ = checked;
        emit personBChanged();
    });

    refreshProfileList();
}

void SynastryController::updateSummary() {
    if (personASummary_) {
        personASummary_->setText(hasPersonA_
            ? describeInput(personAInput_, personALocation_)
            : QStringLiteral("No chart loaded. Load or create a chart first."));
    }
    if (personBSummary_) {
        personBSummary_->setText(hasPersonB_
            ? describeInput(personBInput_, personBLocation_)
            : QStringLiteral("No Person B selected."));
    }
    if (saveProfileButton_) {
        saveProfileButton_->setEnabled(hasPersonB_);
    }
    if (clearButton_) {
        clearButton_->setEnabled(hasPersonB_);
    }
    if (swapCheck_) {
        swapCheck_->setEnabled(hasPersonB_);
    }
}

void SynastryController::report(const QString& message) {
    if (statusLabel_) {
        statusLabel_->setText(message);
    }
    emit statusMessage(message);
}

void SynastryController::loadSelectedProfile() {
    if (!profileCombo_) {
        return;
    }
    const QString name = profileCombo_->currentData().toString().trimmed();
    if (name.isEmpty()) {
        report(QStringLiteral("Select a saved chart to compare against."));
        return;
    }
    NatalInput input;
    QString location;
    QString error;
    if (!profilestore::readProfileInput(name, defaultLunarNodePolicy_,
                                        &input, &location, &error)) {
        report(error);
        return;
    }
    computePersonB(input, location, name);
}

void SynastryController::enterManualPersonB() {
    ChartSetupDialog dialog(network_, filtersRoot_);
    dialog.setDefaultHouseSystem(defaultHouseSystem_);
    dialog.setDefaultLunarNodePolicy(defaultLunarNodePolicy_);
    if (hasPersonB_) {
        dialog.setInput(personBInput_, personBLocation_);
    }
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    const NatalInput input = dialog.input();
    computePersonB(input, dialog.locationName(), input.name);
}

void SynastryController::requestSavePersonB() {
    if (!hasPersonB_) {
        return;
    }
    emit saveProfileRequested(personBInput_, personBLocation_, personBDisplayName());
}

void SynastryController::clearPersonB() {
    if (!hasPersonB_) {
        return;
    }
    hasPersonB_ = false;
    personBInput_ = NatalInput();
    personBLocation_.clear();
    personBChart_ = NatalChart();
    personBLabel_.clear();
    updateSummary();
    emit personBChanged();
    report(QStringLiteral("Person B cleared."));
}

bool SynastryController::computePersonB(NatalInput input, const QString& locationName,
                                        const QString& label) {
    if (!hasPersonA_) {
        report(QStringLiteral("Load a chart for Person A before comparing."));
        return false;
    }
    // Both charts must share one zodiac, matching how the rest of the app treats
    // the top-bar zodiac controls as authoritative.
    input.zodiacSystem = personAInput_.zodiacSystem;
    input.siderealAyanamsa = personAInput_.siderealAyanamsa;

    NatalChart chart;
    QString error;
    if (!engine_.compute(input, &chart, &error)) {
        report(error.isEmpty()
            ? QStringLiteral("Unable to calculate Person B.")
            : error);
        return false;
    }

    personBInput_ = input;
    personBLocation_ = locationName;
    personBChart_ = chart;
    personBLabel_ = label.trimmed();
    hasPersonB_ = true;
    updateSummary();
    emit personBChanged();
    report(QStringLiteral("Person B set to %1.").arg(personBDisplayName()));
    return true;
}

}  // namespace dracoved
