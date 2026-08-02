#include "return_finder_controller.cpp"

namespace dracoved {

class ReturnFinderConditionRow : public QFrame {
public:
    ReturnFinderConditionRow(const ReturnFinderCondition& initial,
                             const std::function<void()>& changed,
                             const std::function<void(ReturnFinderConditionRow*)>& duplicate,
                             const std::function<void(ReturnFinderConditionRow*)>& remove,
                             QWidget* parent)
        : QFrame(parent), changed_(changed), duplicate_(duplicate), remove_(remove) {
        setFrameShape(QFrame::StyledPanel);
        setObjectName("returnFinderConditionCard");
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(7, 7, 7, 7);
        root->setSpacing(5);

        auto* header = new QWidget(this);
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(0, 0, 0, 0);
        enabledCheck_ = new QCheckBox("Enabled", header);
        includeCombo_ = new QComboBox(header);
        includeCombo_->addItem("Include", false);
        includeCombo_->addItem("Exclude", true);
        typeCombo_ = new QComboBox(header);
        typeCombo_->addItem("Planet Placement", static_cast<int>(ReturnFinderConditionType::PlanetPlacement));
        typeCombo_->addItem("Aspect", static_cast<int>(ReturnFinderConditionType::Aspect));
        typeCombo_->addItem("House Lord Placement", static_cast<int>(ReturnFinderConditionType::HouseLordPlacement));
        typeCombo_->addItem("Profection Lord Placement", static_cast<int>(ReturnFinderConditionType::ProfectionLordPlacement));
        typeCombo_->addItem("Stellium", static_cast<int>(ReturnFinderConditionType::Stellium));
        auto* duplicateButton = new QToolButton(header);
        duplicateButton->setText("Duplicate");
        auto* removeButton = new QToolButton(header);
        removeButton->setText("Remove");
        headerLayout->addWidget(enabledCheck_);
        headerLayout->addWidget(includeCombo_);
        headerLayout->addWidget(typeCombo_, 1);
        headerLayout->addWidget(duplicateButton);
        headerLayout->addWidget(removeButton);
        root->addWidget(header);

        pages_ = new QStackedWidget(this);
        buildPlanetPage();
        buildAspectPage();
        buildHouseLordPage();
        buildProfectionPage();
        buildStelliumPage();
        root->addWidget(pages_);

        auto notify = [this]() { if (!loading_ && changed_) changed_(); };
        connect(enabledCheck_, &QCheckBox::toggled, this, notify);
        connect(includeCombo_, &QComboBox::currentIndexChanged, this, notify);
        connect(typeCombo_, &QComboBox::currentIndexChanged, this, [this, notify]() {
            pages_->setCurrentIndex(typeCombo_->currentIndex());
            notify();
        });
        connect(duplicateButton, &QToolButton::clicked, this, [this]() { if (duplicate_) duplicate_(this); });
        connect(removeButton, &QToolButton::clicked, this, [this]() { if (remove_) remove_(this); });
        setCondition(initial);
    }

    ReturnFinderCondition condition() const {
        ReturnFinderCondition value;
        value.id = id_;
        value.enabled = enabledCheck_->isChecked();
        value.exclude = includeCombo_->currentData().toBool();
        value.type = static_cast<ReturnFinderConditionType>(typeCombo_->currentData().toInt());
        value.subject.scope = ReturnFinderScope::Return;
        value.subject.kind = ReturnFinderTargetKind::Planet;
        value.subject.name = planetCombo_->currentText();
        value.placementKind = static_cast<ReturnFinderPlacementKind>(planetPlacementCombo_->currentData().toInt());
        assignPlacementValue(value, planetValueCombo_);
        value.planetPlacementEnabled = planetPlacementCheck_->isChecked();
        value.planetAngleContactEnabled = planetAngleCheck_->isChecked();
        value.target.scope = static_cast<ReturnFinderScope>(planetAngleScopeCombo_->currentData().toInt());
        value.target.kind = ReturnFinderTargetKind::Angle;
        value.target.name = planetAngleCombo_->currentData().toString();
        value.orb = planetAngleOrbSpin_->value();
        if (value.type == ReturnFinderConditionType::Aspect) {
            value.subject = subjectEditor_->target();
            value.target = targetEditor_->target();
            value.aspect = static_cast<ReturnFinderAspect>(aspectCombo_->currentData().toInt());
            value.orb = aspectOrbSpin_->value();
        } else if (value.type == ReturnFinderConditionType::HouseLordPlacement) {
            value.houseLordScope = static_cast<ReturnFinderScope>(lordScopeCombo_->currentData().toInt());
            value.houseLordHouses = parseHouses(lordHousesEdit_->text());
            value.houseLordMatch = static_cast<ReturnFinderHouseLordMatch>(lordMatchCombo_->currentData().toInt());
            value.placementKind = static_cast<ReturnFinderPlacementKind>(lordPlacementCombo_->currentData().toInt());
            assignPlacementValue(value, lordValueCombo_);
        } else if (value.type == ReturnFinderConditionType::ProfectionLordPlacement) {
            value.placementKind = static_cast<ReturnFinderPlacementKind>(profectionPlacementCombo_->currentData().toInt());
            assignPlacementValue(value, profectionValueCombo_);
        } else if (value.type == ReturnFinderConditionType::Stellium) {
            value.stelliumMinimum = stelliumMinimumSpin_->value();
            value.stelliumBySign = stelliumKindCombo_->currentData().toInt() == 1;
            value.stelliumAny = stelliumAnyCheck_->isChecked();
            value.stelliumTarget = stelliumValueCombo_->currentData().toInt();
        }
        return value;
    }

    void setCondition(const ReturnFinderCondition& value) {
        loading_ = true;
        id_ = value.id;
        enabledCheck_->setChecked(value.enabled);
        includeCombo_->setCurrentIndex(value.exclude ? 1 : 0);
        typeCombo_->setCurrentIndex(std::max(0, typeCombo_->findData(static_cast<int>(value.type))));
        pages_->setCurrentIndex(typeCombo_->currentIndex());
        planetCombo_->setCurrentText(value.subject.name.isEmpty() ? "Sun" : value.subject.name);
        setPlacementWidgets(planetPlacementCombo_, planetValueCombo_, value);
        planetPlacementCheck_->setChecked(value.planetPlacementEnabled);
        planetAngleCheck_->setChecked(value.planetAngleContactEnabled);
        ReturnFinderTarget angleTarget = value.target;
        if (angleTarget.kind != ReturnFinderTargetKind::Angle
            || !finderAngles().contains(angleTarget.name)) {
            angleTarget.scope = ReturnFinderScope::Return;
            angleTarget.kind = ReturnFinderTargetKind::Angle;
            angleTarget.name = "Descendant";
        }
        planetAngleScopeCombo_->setCurrentIndex(
            std::max(0, planetAngleScopeCombo_->findData(static_cast<int>(angleTarget.scope))));
        planetAngleCombo_->setCurrentIndex(
            std::max(0, planetAngleCombo_->findData(angleTarget.name)));
        planetAngleOrbSpin_->setValue(std::clamp(value.orb, 0.1, 15.0));
        updatePlanetConstraintUi();
        subjectEditor_->setTarget(value.subject);
        targetEditor_->setTarget(value.target);
        aspectCombo_->setCurrentIndex(std::max(0, aspectCombo_->findData(static_cast<int>(value.aspect))));
        aspectOrbSpin_->setValue(std::clamp(value.orb, 0.1, 15.0));
        lordScopeCombo_->setCurrentIndex(std::max(0, lordScopeCombo_->findData(static_cast<int>(value.houseLordScope))));
        lordHousesEdit_->setText(housesText(value.houseLordHouses));
        lordMatchCombo_->setCurrentIndex(std::max(0, lordMatchCombo_->findData(static_cast<int>(value.houseLordMatch))));
        setPlacementWidgets(lordPlacementCombo_, lordValueCombo_, value);
        setPlacementWidgets(profectionPlacementCombo_, profectionValueCombo_, value);
        stelliumMinimumSpin_->setValue(std::clamp(value.stelliumMinimum, 2, 10));
        stelliumKindCombo_->setCurrentIndex(value.stelliumBySign ? 1 : 0);
        stelliumAnyCheck_->setChecked(value.stelliumAny);
        refillStelliumValues(value.stelliumTarget);
        stelliumValueCombo_->setEnabled(!value.stelliumAny);
        loading_ = false;
    }

    void setId(const QString& id) { id_ = id; }

private:
    void assignPlacementValue(ReturnFinderCondition& value, QComboBox* combo) const {
        if (value.placementKind == ReturnFinderPlacementKind::Sign) value.targetSign = combo->currentData().toInt();
        else value.targetHouse = combo->currentData().toInt();
    }

    void setPlacementWidgets(QComboBox* kindCombo, QComboBox* valueCombo, const ReturnFinderCondition& value) {
        kindCombo->setCurrentIndex(std::max(0, kindCombo->findData(static_cast<int>(value.placementKind))));
        refillPlacementValues(valueCombo, value.placementKind,
                              value.placementKind == ReturnFinderPlacementKind::Sign
                                  ? value.targetSign : value.targetHouse);
    }

    void connectPlacementWidgets(QComboBox* kindCombo, QComboBox* valueCombo) {
        auto notify = [this]() { if (!loading_ && changed_) changed_(); };
        connect(kindCombo, &QComboBox::currentIndexChanged, this, [this, kindCombo, valueCombo, notify]() {
            const auto kind = static_cast<ReturnFinderPlacementKind>(kindCombo->currentData().toInt());
            refillPlacementValues(valueCombo, kind, kind == ReturnFinderPlacementKind::Sign ? 0 : 1);
            notify();
        });
        connect(valueCombo, &QComboBox::currentIndexChanged, this, notify);
    }

    void updatePlanetConstraintUi() {
        const bool requirePlacement = planetPlacementCheck_ && planetPlacementCheck_->isChecked();
        const bool requireAngle = planetAngleCheck_ && planetAngleCheck_->isChecked();
        if (planetPlacementCombo_) planetPlacementCombo_->setEnabled(requirePlacement);
        if (planetValueCombo_) planetValueCombo_->setEnabled(requirePlacement);
        if (planetAngleScopeCombo_) planetAngleScopeCombo_->setEnabled(requireAngle);
        if (planetAngleCombo_) planetAngleCombo_->setEnabled(requireAngle);
        if (planetAngleOrbSpin_) planetAngleOrbSpin_->setEnabled(requireAngle);
        if (!planetConstraintHint_) return;

        if (requirePlacement && requireAngle) {
            planetConstraintHint_->setText(
                "Both must match: the selected house/sign placement and the angle conjunction.");
            planetConstraintHint_->setStyleSheet(QString());
        } else if (requirePlacement) {
            planetConstraintHint_->setText("Matches the selected house or sign placement.");
            planetConstraintHint_->setStyleSheet(QString());
        } else if (requireAngle) {
            planetConstraintHint_->setText(
                "House/sign is ignored; the planet may conjunct the angle from either side of the cusp.");
            planetConstraintHint_->setStyleSheet(QString());
        } else {
            planetConstraintHint_->setText(
                "Select at least one requirement: house/sign placement or angle conjunction.");
            planetConstraintHint_->setStyleSheet("color: #c62828; font-weight: 600;");
        }
    }

    void buildPlanetPage() {
        auto* page = new QWidget(pages_);
        auto* layout = new QGridLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        planetCombo_ = new QComboBox(page);
        planetCombo_->addItems(finderPlanets());
        planetPlacementCheck_ = new QCheckBox("Require house/sign placement", page);
        planetPlacementCheck_->setChecked(true);
        planetPlacementCheck_->setToolTip(
            "Turn this off to search only for the angle conjunction, regardless of house or sign.");
        planetPlacementCombo_ = new QComboBox(page);
        planetPlacementCombo_->addItem("In House", static_cast<int>(ReturnFinderPlacementKind::House));
        planetPlacementCombo_->addItem("In Sign", static_cast<int>(ReturnFinderPlacementKind::Sign));
        planetValueCombo_ = new QComboBox(page);
        refillPlacementValues(planetValueCombo_, ReturnFinderPlacementKind::House, 1);

        planetAngleCheck_ = new QCheckBox("Require conjunction to an angle", page);
        planetAngleCheck_->setToolTip(
            "Use alone or together with the house/sign placement. This is always a conjunction.");
        planetAngleScopeCombo_ = new QComboBox(page);
        planetAngleScopeCombo_->addItem("Return", static_cast<int>(ReturnFinderScope::Return));
        planetAngleScopeCombo_->addItem("Natal", static_cast<int>(ReturnFinderScope::Natal));
        planetAngleCombo_ = new QComboBox(page);
        fillFinderAngles(planetAngleCombo_);
        planetAngleCombo_->setCurrentIndex(
            std::max(0, planetAngleCombo_->findData(QString("Descendant"))));
        planetAngleOrbSpin_ = new QDoubleSpinBox(page);
        planetAngleOrbSpin_->setRange(0.1, 15.0);
        planetAngleOrbSpin_->setDecimals(2);
        planetAngleOrbSpin_->setSingleStep(0.1);
        planetAngleOrbSpin_->setValue(1.0);
        planetAngleOrbSpin_->setSuffix(" deg");
        planetConstraintHint_ = new QLabel(page);
        planetConstraintHint_->setWordWrap(true);
        planetConstraintHint_->setObjectName("hintLabel");

        layout->addWidget(new QLabel("Return Planet", page), 0, 0);
        layout->addWidget(planetCombo_, 0, 1);
        layout->addWidget(planetPlacementCheck_, 1, 0, 1, 2);
        layout->addWidget(planetPlacementCombo_, 2, 0);
        layout->addWidget(planetValueCombo_, 2, 1);
        layout->addWidget(planetAngleCheck_, 3, 0, 1, 2);

        auto* angleTargetRow = new QWidget(page);
        auto* angleTargetLayout = new QHBoxLayout(angleTargetRow);
        angleTargetLayout->setContentsMargins(0, 0, 0, 0);
        angleTargetLayout->setSpacing(6);
        angleTargetLayout->addWidget(planetAngleScopeCombo_);
        angleTargetLayout->addWidget(planetAngleCombo_, 1);
        layout->addWidget(new QLabel("Angle Target", page), 4, 0);
        layout->addWidget(angleTargetRow, 4, 1);
        layout->addWidget(new QLabel("Maximum Orb", page), 5, 0);
        layout->addWidget(planetAngleOrbSpin_, 5, 1);
        layout->addWidget(planetConstraintHint_, 6, 0, 1, 2);
        pages_->addWidget(page);
        auto notify = [this]() { if (!loading_ && changed_) changed_(); };
        connect(planetCombo_, &QComboBox::currentIndexChanged, this, notify);
        connect(planetPlacementCheck_, &QCheckBox::toggled, this, [this, notify]() {
            updatePlanetConstraintUi();
            notify();
        });
        connect(planetAngleCheck_, &QCheckBox::toggled, this, [this, notify]() {
            updatePlanetConstraintUi();
            notify();
        });
        connect(planetAngleScopeCombo_, &QComboBox::currentIndexChanged, this, notify);
        connect(planetAngleCombo_, &QComboBox::currentIndexChanged, this, notify);
        connect(planetAngleOrbSpin_, &QDoubleSpinBox::valueChanged, this, notify);
        connectPlacementWidgets(planetPlacementCombo_, planetValueCombo_);
        updatePlanetConstraintUi();
    }

    void buildAspectPage() {
        auto* page = new QWidget(pages_);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        auto notify = [this]() { if (!loading_ && changed_) changed_(); };
        subjectEditor_ = new ReturnFinderTargetEditor("Subject", page, notify);
        targetEditor_ = new ReturnFinderTargetEditor("Target", page, notify);
        auto* aspectRow = new QWidget(page);
        auto* aspectLayout = new QHBoxLayout(aspectRow);
        aspectLayout->setContentsMargins(0, 0, 0, 0);
        aspectCombo_ = new QComboBox(aspectRow);
        aspectCombo_->addItem("Conjunction", static_cast<int>(ReturnFinderAspect::Conjunction));
        aspectCombo_->addItem("Sextile", static_cast<int>(ReturnFinderAspect::Sextile));
        aspectCombo_->addItem("Square", static_cast<int>(ReturnFinderAspect::Square));
        aspectCombo_->addItem("Trine", static_cast<int>(ReturnFinderAspect::Trine));
        aspectCombo_->addItem("Opposition", static_cast<int>(ReturnFinderAspect::Opposition));
        aspectCombo_->addItem("Any Major Aspect", static_cast<int>(ReturnFinderAspect::AnyMajor));
        aspectOrbSpin_ = new QDoubleSpinBox(aspectRow);
        aspectOrbSpin_->setRange(0.1, 15.0);
        aspectOrbSpin_->setDecimals(2);
        aspectOrbSpin_->setSingleStep(0.1);
        aspectOrbSpin_->setValue(1.0);
        aspectOrbSpin_->setSuffix(" deg");
        aspectLayout->addWidget(new QLabel("Aspect", aspectRow));
        aspectLayout->addWidget(aspectCombo_, 1);
        aspectLayout->addWidget(new QLabel("Orb", aspectRow));
        aspectLayout->addWidget(aspectOrbSpin_);
        layout->addWidget(subjectEditor_);
        layout->addWidget(aspectRow);
        layout->addWidget(targetEditor_);
        auto* angleHint = new QLabel(
            "For ASC, DSC, MC or IC contacts, choose 'Angle' as the Subject or Target type.",
            page);
        angleHint->setWordWrap(true);
        angleHint->setObjectName("hintLabel");
        layout->addWidget(angleHint);
        pages_->addWidget(page);
        connect(aspectCombo_, &QComboBox::currentIndexChanged, this, notify);
        connect(aspectOrbSpin_, &QDoubleSpinBox::valueChanged, this, notify);
    }

    void buildHouseLordPage() {
        auto* page = new QWidget(pages_);
        auto* layout = new QGridLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        lordScopeCombo_ = new QComboBox(page);
        lordScopeCombo_->addItem("Return house lord", static_cast<int>(ReturnFinderScope::Return));
        lordScopeCombo_->addItem("Natal house lord", static_cast<int>(ReturnFinderScope::Natal));
        lordHousesEdit_ = new QLineEdit(page);
        lordHousesEdit_->setPlaceholderText("Houses, e.g. 8, 12");
        lordMatchCombo_ = new QComboBox(page);
        lordMatchCombo_->addItem("Any selected lord", static_cast<int>(ReturnFinderHouseLordMatch::Any));
        lordMatchCombo_->addItem("All selected lords", static_cast<int>(ReturnFinderHouseLordMatch::All));
        lordPlacementCombo_ = new QComboBox(page);
        lordPlacementCombo_->addItem("In House", static_cast<int>(ReturnFinderPlacementKind::House));
        lordPlacementCombo_->addItem("In Sign", static_cast<int>(ReturnFinderPlacementKind::Sign));
        lordValueCombo_ = new QComboBox(page);
        refillPlacementValues(lordValueCombo_, ReturnFinderPlacementKind::House, 1);
        layout->addWidget(new QLabel("Lord Source", page), 0, 0);
        layout->addWidget(lordScopeCombo_, 0, 1);
        layout->addWidget(new QLabel("House(s)", page), 1, 0);
        layout->addWidget(lordHousesEdit_, 1, 1);
        layout->addWidget(new QLabel("Multiple Lords", page), 2, 0);
        layout->addWidget(lordMatchCombo_, 2, 1);
        layout->addWidget(lordPlacementCombo_, 3, 0);
        layout->addWidget(lordValueCombo_, 3, 1);
        pages_->addWidget(page);
        auto notify = [this]() { if (!loading_ && changed_) changed_(); };
        connect(lordScopeCombo_, &QComboBox::currentIndexChanged, this, notify);
        connect(lordHousesEdit_, &QLineEdit::textChanged, this, notify);
        connect(lordMatchCombo_, &QComboBox::currentIndexChanged, this, notify);
        connectPlacementWidgets(lordPlacementCombo_, lordValueCombo_);
    }

    void buildProfectionPage() {
        auto* page = new QWidget(pages_);
        auto* layout = new QGridLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        auto* hint = new QLabel("Uses the existing annual profection-lord calculation.", page);
        hint->setWordWrap(true);
        hint->setObjectName("hintLabel");
        profectionPlacementCombo_ = new QComboBox(page);
        profectionPlacementCombo_->addItem("In House", static_cast<int>(ReturnFinderPlacementKind::House));
        profectionPlacementCombo_->addItem("In Sign", static_cast<int>(ReturnFinderPlacementKind::Sign));
        profectionValueCombo_ = new QComboBox(page);
        refillPlacementValues(profectionValueCombo_, ReturnFinderPlacementKind::House, 1);
        layout->addWidget(hint, 0, 0, 1, 2);
        layout->addWidget(profectionPlacementCombo_, 1, 0);
        layout->addWidget(profectionValueCombo_, 1, 1);
        pages_->addWidget(page);
        connectPlacementWidgets(profectionPlacementCombo_, profectionValueCombo_);
    }

    void buildStelliumPage() {
        auto* page = new QWidget(pages_);
        auto* layout = new QGridLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        stelliumMinimumSpin_ = new QSpinBox(page);
        stelliumMinimumSpin_->setRange(2, 10);
        stelliumMinimumSpin_->setValue(3);
        stelliumMinimumSpin_->setSuffix(" planets");
        stelliumKindCombo_ = new QComboBox(page);
        stelliumKindCombo_->addItem("By House", 0);
        stelliumKindCombo_->addItem("By Sign", 1);
        stelliumAnyCheck_ = new QCheckBox("Any house/sign", page);
        stelliumValueCombo_ = new QComboBox(page);
        refillStelliumValues(1);
        layout->addWidget(new QLabel("Minimum", page), 0, 0);
        layout->addWidget(stelliumMinimumSpin_, 0, 1);
        layout->addWidget(stelliumKindCombo_, 1, 0);
        layout->addWidget(stelliumValueCombo_, 1, 1);
        layout->addWidget(stelliumAnyCheck_, 2, 0, 1, 2);
        pages_->addWidget(page);
        auto notify = [this]() { if (!loading_ && changed_) changed_(); };
        connect(stelliumMinimumSpin_, &QSpinBox::valueChanged, this, notify);
        connect(stelliumKindCombo_, &QComboBox::currentIndexChanged, this, [this, notify]() {
            refillStelliumValues(stelliumKindCombo_->currentData().toInt() == 1 ? 0 : 1);
            notify();
        });
        connect(stelliumAnyCheck_, &QCheckBox::toggled, this, [this, notify](bool checked) {
            stelliumValueCombo_->setEnabled(!checked);
            notify();
        });
        connect(stelliumValueCombo_, &QComboBox::currentIndexChanged, this, notify);
    }

    void refillStelliumValues(int selected) {
        if (stelliumKindCombo_ && stelliumKindCombo_->currentData().toInt() == 1) {
            refillPlacementValues(stelliumValueCombo_, ReturnFinderPlacementKind::Sign, std::clamp(selected, 0, 11));
        } else {
            refillPlacementValues(stelliumValueCombo_, ReturnFinderPlacementKind::House, std::clamp(selected, 1, 12));
        }
    }

    QString id_;
    bool loading_ = false;
    std::function<void()> changed_;
    std::function<void(ReturnFinderConditionRow*)> duplicate_;
    std::function<void(ReturnFinderConditionRow*)> remove_;
    QCheckBox* enabledCheck_ = nullptr;
    QComboBox* includeCombo_ = nullptr;
    QComboBox* typeCombo_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QComboBox* planetCombo_ = nullptr;
    QCheckBox* planetPlacementCheck_ = nullptr;
    QComboBox* planetPlacementCombo_ = nullptr;
    QComboBox* planetValueCombo_ = nullptr;
    QCheckBox* planetAngleCheck_ = nullptr;
    QComboBox* planetAngleScopeCombo_ = nullptr;
    QComboBox* planetAngleCombo_ = nullptr;
    QDoubleSpinBox* planetAngleOrbSpin_ = nullptr;
    QLabel* planetConstraintHint_ = nullptr;
    ReturnFinderTargetEditor* subjectEditor_ = nullptr;
    ReturnFinderTargetEditor* targetEditor_ = nullptr;
    QComboBox* aspectCombo_ = nullptr;
    QDoubleSpinBox* aspectOrbSpin_ = nullptr;
    QComboBox* lordScopeCombo_ = nullptr;
    QLineEdit* lordHousesEdit_ = nullptr;
    QComboBox* lordMatchCombo_ = nullptr;
    QComboBox* lordPlacementCombo_ = nullptr;
    QComboBox* lordValueCombo_ = nullptr;
    QComboBox* profectionPlacementCombo_ = nullptr;
    QComboBox* profectionValueCombo_ = nullptr;
    QSpinBox* stelliumMinimumSpin_ = nullptr;
    QComboBox* stelliumKindCombo_ = nullptr;
    QCheckBox* stelliumAnyCheck_ = nullptr;
    QComboBox* stelliumValueCombo_ = nullptr;
};
