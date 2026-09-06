#include <QFrame>
#include <QSet>

#include "return_finder_controller_impl.cpp"

ReturnFinderController::ReturnFinderController(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<ReturnFinderResult>("dracoved::ReturnFinderResult");
    qRegisterMetaType<QVector<ReturnFinderResult>>("QVector<dracoved::ReturnFinderResult>");
    qRegisterMetaType<ReturnFinderRunSummary>("dracoved::ReturnFinderRunSummary");
    buildFiltersUi();
    buildWorkspaceUi();
    ReturnFinderCondition initial;
    initial.id = QString("condition_%1").arg(nextConditionId_++);
    initial.subject.name = "Sun";
    initial.placementKind = ReturnFinderPlacementKind::House;
    initial.targetHouse = 1;
    initial.target.kind = ReturnFinderTargetKind::Angle;
    initial.target.name = "Descendant";
    addCondition(initial);
    refreshPresetList();
    updateRangeUi();
    updateLocationUi();
    updateRunUi();
}

ReturnFinderController::~ReturnFinderController() {
    shutdown();
}

QWidget* ReturnFinderController::filtersWidget() const {
    return filtersRoot_;
}

QWidget* ReturnFinderController::workspaceWidget() const {
    return workspaceRoot_;
}

void ReturnFinderController::setRuntimePaths(const QString& ephePath, const QStringList& dllSearchPaths) {
    ephePath_ = ephePath;
    dllSearchPaths_ = dllSearchPaths;
}

void ReturnFinderController::setNatalContext(const NatalInput& input,
                                             const NatalChart& chart,
                                             const QString& locationName) {
    const bool preserveCompletedResults = hasRun_;
    if (running_) {
        ++runGeneration_;
        cancelSearch();
    }
    natalInput_ = input;
    natalChart_ = chart;
    natalLocationName_ = locationName;
    hasNatalContext_ = true;
    if (contextLabel_) {
        contextLabel_->setText(QString("Natal chart: %1").arg(input.name.isEmpty() ? "Current chart" : input.name));
    }
    if (locationModeCombo_ && locationModeCombo_->currentIndex() == 0) {
        const QSignalBlocker b1(locationEdit_);
        const QSignalBlocker b2(timezoneEdit_);
        const QSignalBlocker b3(latitudeSpin_);
        const QSignalBlocker b4(longitudeSpin_);
        locationEdit_->setText(locationName);
        timezoneEdit_->setText(input.timezone);
        latitudeSpin_->setValue(input.latitude);
        longitudeSpin_->setValue(input.longitude);
    }
    if (preserveCompletedResults) {
        stale_ = true;
        if (workspaceStateLabel_) {
            workspaceStateLabel_->setText("Stale results - natal chart changed; run the search again");
        }
    } else {
        results_.clear();
        summary_ = {};
        hasRun_ = false;
        stale_ = false;
    }
    populateResults();
    updateLocationUi();
    updateRunUi();
    emit selectionChanged();
    emit summaryChanged();
}

void ReturnFinderController::clearNatalContext() {
    const bool preserveCompletedResults = hasRun_;
    if (running_) {
        ++runGeneration_;
        cancelSearch();
    }
    hasNatalContext_ = false;
    natalInput_ = {};
    natalChart_ = {};
    natalLocationName_.clear();
    if (preserveCompletedResults) {
        stale_ = true;
        if (workspaceStateLabel_) {
            workspaceStateLabel_->setText("Stale results - natal chart unloaded");
        }
    } else {
        results_.clear();
        summary_ = {};
        hasRun_ = false;
        stale_ = false;
    }
    if (contextLabel_) contextLabel_->setText("Load a natal chart to run return research.");
    populateResults();
    updateRunUi();
    emit selectionChanged();
    emit summaryChanged();
}

void ReturnFinderController::setReturnType(ReturnFinderType type) {
    if (!returnTypeCombo_) return;
    const int index = returnTypeCombo_->findData(static_cast<int>(type));
    if (index >= 0) returnTypeCombo_->setCurrentIndex(index);
}

ReturnFinderType ReturnFinderController::returnType() const {
    return returnTypeCombo_
        ? static_cast<ReturnFinderType>(returnTypeCombo_->currentData().toInt())
        : ReturnFinderType::Solar;
}

bool ReturnFinderController::tajakaMethodActive() const {
    return returnMethodCombo_
        && returnMethodCombo_->currentData().toInt() == 1
        && returnType() == ReturnFinderType::Solar;
}

bool ReturnFinderController::isRunning() const { return running_; }
bool ReturnFinderController::hasCompletedRun() const { return hasRun_; }

void ReturnFinderController::cancelSearch() {
    if (!running_ || !worker_) return;
    worker_->cancel();
    if (statusLabel_) statusLabel_->setText("Stopping...");
    if (stopButton_) stopButton_->setEnabled(false);
}

bool ReturnFinderController::shutdown(int timeoutMs) {
    if (!workerThread_ || !workerThread_->isRunning()) return true;
    if (worker_) worker_->cancel();
    workerThread_->quit();
    return workerThread_->wait(timeoutMs);
}

void ReturnFinderController::markResultsStale() {
    if (!hasRun_) return;
    stale_ = true;
    if (workspaceStateLabel_) workspaceStateLabel_->setText("Stale results - run the search again");
    updateRunUi();
    emit summaryChanged();
}

bool ReturnFinderController::hasSelectedResult() const {
    return resultForStableId(selectedStableId()) != nullptr;
}

ReturnFinderResult ReturnFinderController::selectedResult() const {
    const auto* result = resultForStableId(selectedStableId());
    return result ? *result : ReturnFinderResult{};
}

ReturnFinderQuery ReturnFinderController::lastQuery() const { return lastQuery_; }
ReturnFinderRunSummary ReturnFinderController::runSummary() const { return summary_; }
bool ReturnFinderController::resultsAreStale() const { return stale_; }

void ReturnFinderController::buildFiltersUi() {
    auto* page = new QWidget();
    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->setContentsMargins(6, 6, 6, 6);
    pageLayout->setSpacing(6);
    auto* intro = new QLabel("Solar & Lunar Return Placement Finder", page);
    intro->setStyleSheet("font-weight: 600;");
    pageLayout->addWidget(intro);
    contextLabel_ = new QLabel("Load a natal chart to run return research.", page);
    contextLabel_->setObjectName("hintLabel");
    contextLabel_->setWordWrap(true);
    pageLayout->addWidget(contextLabel_);

    auto* scopeGroup = new QGroupBox("Search Scope", page);
    auto* scopeLayout = new QGridLayout(scopeGroup);
    returnTypeCombo_ = new QComboBox(scopeGroup);
    returnTypeCombo_->addItem("Solar Returns", static_cast<int>(ReturnFinderType::Solar));
    returnTypeCombo_->addItem("Lunar Returns", static_cast<int>(ReturnFinderType::Lunar));
    rangeStack_ = new QStackedWidget(scopeGroup);
    auto* solarRange = new QWidget(rangeStack_);
    auto* solarLayout = new QGridLayout(solarRange);
    solarLayout->setContentsMargins(0, 0, 0, 0);
    startYearSpin_ = new QSpinBox(solarRange);
    endYearSpin_ = new QSpinBox(solarRange);
    startYearSpin_->setRange(1800, 2399);
    endYearSpin_->setRange(1800, 2399);
    const int year = QDate::currentDate().year();
    startYearSpin_->setValue(std::clamp(year - 5, 1800, 2399));
    endYearSpin_->setValue(std::clamp(year + 5, 1800, 2399));
    solarLayout->addWidget(new QLabel("Start Year", solarRange), 0, 0);
    solarLayout->addWidget(startYearSpin_, 0, 1);
    solarLayout->addWidget(new QLabel("End Year", solarRange), 1, 0);
    solarLayout->addWidget(endYearSpin_, 1, 1);
    auto* lunarRange = new QWidget(rangeStack_);
    auto* lunarLayout = new QGridLayout(lunarRange);
    lunarLayout->setContentsMargins(0, 0, 0, 0);
    startDateEdit_ = new QDateEdit(lunarRange);
    endDateEdit_ = new QDateEdit(lunarRange);
    for (QDateEdit* edit : {startDateEdit_, endDateEdit_}) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat("yyyy-MM-dd");
        edit->setDateRange(QDate(1800, 1, 1), QDate(2399, 12, 31));
    }
    startDateEdit_->setDate(QDate::currentDate().addMonths(-6));
    endDateEdit_->setDate(QDate::currentDate().addMonths(6));
    lunarLayout->addWidget(new QLabel("Start Date", lunarRange), 0, 0);
    lunarLayout->addWidget(startDateEdit_, 0, 1);
    lunarLayout->addWidget(new QLabel("End Date", lunarRange), 1, 0);
    lunarLayout->addWidget(endDateEdit_, 1, 1);
    rangeStack_->addWidget(solarRange);
    rangeStack_->addWidget(lunarRange);
    scopeLayout->addWidget(new QLabel("Return Type", scopeGroup), 0, 0);
    scopeLayout->addWidget(returnTypeCombo_, 0, 1);
    scopeLayout->addWidget(rangeStack_, 1, 0, 1, 2);
    pageLayout->addWidget(scopeGroup);

    auto* locationGroup = new QGroupBox("Return Location", page);
    auto* locationLayout = new QGridLayout(locationGroup);
    locationModeCombo_ = new QComboBox(locationGroup);
    locationModeCombo_->addItem("Use natal location", true);
    locationModeCombo_->addItem("Use custom location", false);
    locationEdit_ = new QLineEdit(locationGroup);
    timezoneEdit_ = new QLineEdit(locationGroup);
    timezoneEdit_->setPlaceholderText("e.g. Asia/Dhaka");
    latitudeSpin_ = new QDoubleSpinBox(locationGroup);
    longitudeSpin_ = new QDoubleSpinBox(locationGroup);
    latitudeSpin_->setRange(-90.0, 90.0);
    longitudeSpin_->setRange(-180.0, 180.0);
    latitudeSpin_->setDecimals(6);
    longitudeSpin_->setDecimals(6);
    locationLayout->addWidget(locationModeCombo_, 0, 0, 1, 2);
    locationLayout->addWidget(new QLabel("Location", locationGroup), 1, 0);
    locationLayout->addWidget(locationEdit_, 1, 1);
    locationLayout->addWidget(new QLabel("Timezone", locationGroup), 2, 0);
    locationLayout->addWidget(timezoneEdit_, 2, 1);
    locationLayout->addWidget(new QLabel("Latitude", locationGroup), 3, 0);
    locationLayout->addWidget(latitudeSpin_, 3, 1);
    locationLayout->addWidget(new QLabel("Longitude", locationGroup), 4, 0);
    locationLayout->addWidget(longitudeSpin_, 4, 1);
    pageLayout->addWidget(locationGroup);

    auto* settingsGroup = new QGroupBox("Research Settings", page);
    auto* settingsLayout = new QGridLayout(settingsGroup);
    houseModeCombo_ = new QComboBox(settingsGroup);
    houseModeCombo_->addItem("Whole Sign", static_cast<int>(ReturnFinderHouseMode::WholeSign));
    houseModeCombo_->addItem("Placidus", static_cast<int>(ReturnFinderHouseMode::Placidus));
    houseModeCombo_->addItem("Both (OR)", static_cast<int>(ReturnFinderHouseMode::BothOr));
    houseModeCombo_->addItem("Both (AND)", static_cast<int>(ReturnFinderHouseMode::BothAnd));
    rulershipCombo_ = new QComboBox(settingsGroup);
    rulershipCombo_->addItem("Traditional", 0);
    rulershipCombo_->addItem("Modern", 1);
    matchModeCombo_ = new QComboBox(settingsGroup);
    matchModeCombo_->addItem("Match All include conditions", static_cast<int>(ReturnFinderMatchMode::All));
    matchModeCombo_->addItem("Match Any include condition", static_cast<int>(ReturnFinderMatchMode::Any));
    returnMethodCombo_ = new QComboBox(settingsGroup);
    returnMethodCombo_->addItem("Standard", 0);
    returnMethodCombo_->addItem("Tajaka (P.V.R. Rao)", 1);
    returnMethodCombo_->setToolTip(
        "Standard: each return is the Sun (or Moon) reaching its natal position in the "
        "active zodiac.\n"
        "Tajaka: solar returns use the Sun's natal tropical longitude for the return "
        "moment, every scanned chart is judged sidereally, and the scan is pinned to "
        "the natal location. Solar returns only.");
    settingsLayout->addWidget(new QLabel("House System", settingsGroup), 0, 0);
    settingsLayout->addWidget(houseModeCombo_, 0, 1);
    settingsLayout->addWidget(new QLabel("Rulership", settingsGroup), 1, 0);
    settingsLayout->addWidget(rulershipCombo_, 1, 1);
    settingsLayout->addWidget(new QLabel("Logic", settingsGroup), 2, 0);
    settingsLayout->addWidget(matchModeCombo_, 2, 1);
    settingsLayout->addWidget(new QLabel("Return Method", settingsGroup), 3, 0);
    settingsLayout->addWidget(returnMethodCombo_, 3, 1);
    pageLayout->addWidget(settingsGroup);

    auto* presetGroup = new QGroupBox("Search Presets", page);
    auto* presetLayout = new QGridLayout(presetGroup);
    presetCombo_ = new QComboBox(presetGroup);
    auto* loadPresetButton = new QPushButton("Load", presetGroup);
    auto* savePresetButton = new QPushButton("Save", presetGroup);
    auto* renamePresetButton = new QPushButton("Rename", presetGroup);
    auto* deletePresetButton = new QPushButton("Delete", presetGroup);
    presetLayout->addWidget(presetCombo_, 0, 0, 1, 2);
    presetLayout->addWidget(loadPresetButton, 1, 0);
    presetLayout->addWidget(savePresetButton, 1, 1);
    presetLayout->addWidget(renamePresetButton, 2, 0);
    presetLayout->addWidget(deletePresetButton, 2, 1);
    pageLayout->addWidget(presetGroup);

    auto* conditionsGroup = new QGroupBox("Conditions", page);
    auto* conditionsRootLayout = new QVBoxLayout(conditionsGroup);
    auto* conditionActions = new QWidget(conditionsGroup);
    auto* conditionActionsLayout = new QHBoxLayout(conditionActions);
    conditionActionsLayout->setContentsMargins(0, 0, 0, 0);
    auto* addButton = new QPushButton("+ Add Condition", conditionActions);
    auto* addMenu = new QMenu(addButton);
    auto* addPlanetAction = addMenu->addAction("Planet Placement");
    auto* addAspectAction = addMenu->addAction("Aspect");
    auto* addHouseLordAction = addMenu->addAction("House Lord Placement");
    auto* addProfectionAction = addMenu->addAction("Profection Lord Placement");
    auto* addStelliumAction = addMenu->addAction("Stellium");
    auto* addMunthaAction = addMenu->addAction("Muntha Placement");
    auto* addTajakaAspectAction = addMenu->addAction("Tajaka Aspect");
    auto* addLordOfYearAction = addMenu->addAction("Lord of the Year Placement");
    addButton->setMenu(addMenu);
    auto* clearButton = new QPushButton("Clear", conditionActions);
    conditionActionsLayout->addWidget(addButton);
    conditionActionsLayout->addStretch();
    conditionActionsLayout->addWidget(clearButton);
    conditionsRootLayout->addWidget(conditionActions);
    conditionsContainer_ = new QWidget(conditionsGroup);
    conditionsLayout_ = new QVBoxLayout(conditionsContainer_);
    conditionsLayout_->setContentsMargins(0, 0, 0, 0);
    conditionsLayout_->setSpacing(6);
    conditionsLayout_->addStretch();
    conditionsRootLayout->addWidget(conditionsContainer_);
    pageLayout->addWidget(conditionsGroup);

    auto* runGroup = new QGroupBox("Run Search", page);
    auto* runLayout = new QGridLayout(runGroup);
    runButton_ = new QPushButton("Find Matching Returns", runGroup);
    stopButton_ = new QPushButton("Stop", runGroup);
    progressBar_ = new QProgressBar(runGroup);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    statusLabel_ = new QLabel("Idle", runGroup);
    statusLabel_->setObjectName("hintLabel");
    runLayout->addWidget(runButton_, 0, 0);
    runLayout->addWidget(stopButton_, 0, 1);
    runLayout->addWidget(progressBar_, 1, 0, 1, 2);
    runLayout->addWidget(statusLabel_, 2, 0, 1, 2);
    pageLayout->addWidget(runGroup);
    pageLayout->addStretch();

    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(page);
    filtersRoot_ = scroll;

    auto changed = [this]() { conditionChanged(); };
    connect(returnTypeCombo_, &QComboBox::currentIndexChanged, this, [this, changed]() {
        if (tajakaMethodActive() && houseModeCombo_) {
            // Returning to Solar with the Tajaka method: re-pin the visible
            // house mode (it may have been changed while Lunar was active).
            const QSignalBlocker blocker(houseModeCombo_);
            houseModeCombo_->setCurrentIndex(
                std::max(0, houseModeCombo_->findData(
                                static_cast<int>(ReturnFinderHouseMode::WholeSign))));
        }
        updateRangeUi();
        // The effective Tajaka state depends on the return type too.
        emit tajakaMethodChanged(tajakaMethodActive());
        changed();
    });
    connect(startYearSpin_, &QSpinBox::valueChanged, this, changed);
    connect(endYearSpin_, &QSpinBox::valueChanged, this, changed);
    connect(startDateEdit_, &QDateEdit::dateChanged, this, changed);
    connect(endDateEdit_, &QDateEdit::dateChanged, this, changed);
    connect(locationModeCombo_, &QComboBox::currentIndexChanged, this, [this, changed]() { updateLocationUi(); changed(); });
    connect(locationEdit_, &QLineEdit::textChanged, this, changed);
    connect(timezoneEdit_, &QLineEdit::textChanged, this, changed);
    connect(latitudeSpin_, &QDoubleSpinBox::valueChanged, this, changed);
    connect(longitudeSpin_, &QDoubleSpinBox::valueChanged, this, changed);
    connect(houseModeCombo_, &QComboBox::currentIndexChanged, this, changed);
    connect(rulershipCombo_, &QComboBox::currentIndexChanged, this, changed);
    connect(matchModeCombo_, &QComboBox::currentIndexChanged, this, changed);
    connect(returnMethodCombo_, &QComboBox::currentIndexChanged, this, [this, changed]() {
        if (tajakaMethodActive() && houseModeCombo_) {
            // Tajaka judgments are whole-sign based; sync the visible mode.
            const QSignalBlocker blocker(houseModeCombo_);
            houseModeCombo_->setCurrentIndex(
                std::max(0, houseModeCombo_->findData(
                                static_cast<int>(ReturnFinderHouseMode::WholeSign))));
        }
        updateLocationUi();
        updateRunUi();
        emit tajakaMethodChanged(tajakaMethodActive());
        changed();
    });
    auto addTypedCondition = [this](ReturnFinderConditionType type) {
        ReturnFinderCondition condition;
        condition.id = QString("condition_%1").arg(nextConditionId_++);
        condition.type = type;
        condition.subject.name = returnType() == ReturnFinderType::Lunar ? "Moon" : "Sun";
        if (type == ReturnFinderConditionType::Aspect) {
            condition.target.kind = ReturnFinderTargetKind::Angle;
            condition.target.name = "Midheaven";
        } else if (type == ReturnFinderConditionType::PlanetPlacement) {
            condition.target.kind = ReturnFinderTargetKind::Angle;
            condition.target.name = "Descendant";
        } else if (type == ReturnFinderConditionType::TajakaAspect) {
            condition.subject.name = "Jupiter";
            condition.target.kind = ReturnFinderTargetKind::Planet;
            condition.target.name = "Venus";
        }
        addCondition(condition);
    };
    connect(addPlanetAction, &QAction::triggered, this, [addTypedCondition]() {
        addTypedCondition(ReturnFinderConditionType::PlanetPlacement);
    });
    connect(addAspectAction, &QAction::triggered, this, [addTypedCondition]() {
        addTypedCondition(ReturnFinderConditionType::Aspect);
    });
    connect(addHouseLordAction, &QAction::triggered, this, [addTypedCondition]() {
        addTypedCondition(ReturnFinderConditionType::HouseLordPlacement);
    });
    connect(addProfectionAction, &QAction::triggered, this, [addTypedCondition]() {
        addTypedCondition(ReturnFinderConditionType::ProfectionLordPlacement);
    });
    connect(addStelliumAction, &QAction::triggered, this, [addTypedCondition]() {
        addTypedCondition(ReturnFinderConditionType::Stellium);
    });
    connect(addMunthaAction, &QAction::triggered, this, [addTypedCondition]() {
        addTypedCondition(ReturnFinderConditionType::MunthaPlacement);
    });
    connect(addTajakaAspectAction, &QAction::triggered, this, [addTypedCondition]() {
        addTypedCondition(ReturnFinderConditionType::TajakaAspect);
    });
    connect(addLordOfYearAction, &QAction::triggered, this, [addTypedCondition]() {
        addTypedCondition(ReturnFinderConditionType::LordOfYearPlacement);
    });
    connect(clearButton, &QPushButton::clicked, this, &ReturnFinderController::clearConditions);
    connect(loadPresetButton, &QPushButton::clicked, this, &ReturnFinderController::loadSelectedPreset);
    connect(savePresetButton, &QPushButton::clicked, this, &ReturnFinderController::savePreset);
    connect(renamePresetButton, &QPushButton::clicked, this, &ReturnFinderController::renameSelectedPreset);
    connect(deletePresetButton, &QPushButton::clicked, this, &ReturnFinderController::deleteSelectedPreset);
    connect(runButton_, &QPushButton::clicked, this, &ReturnFinderController::startSearch);
    connect(stopButton_, &QPushButton::clicked, this, &ReturnFinderController::cancelSearch);
}

void ReturnFinderController::buildWorkspaceUi() {
    workspaceRoot_ = new QWidget();
    auto* layout = new QVBoxLayout(workspaceRoot_);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    auto* header = new QWidget(workspaceRoot_);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    auto* title = new QLabel("Solar & Lunar Return Finder", header);
    title->setStyleSheet("font-weight: 600;");
    workspaceStateLabel_ = new QLabel("Ready", header);
    workspaceStateLabel_->setObjectName("hintLabel");
    openButton_ = new QPushButton("Open Return Chart", header);
    copyButton_ = new QPushButton("Copy Results", header);
    copyButton_->setToolTip("Copy a complete Markdown research report with natal context, query settings and result details");
    headerLayout->addWidget(title);
    headerLayout->addWidget(workspaceStateLabel_);
    headerLayout->addStretch();
    headerLayout->addWidget(copyButton_);
    headerLayout->addWidget(openButton_);
    layout->addWidget(header);
    resultsTable_ = new QTableWidget(workspaceRoot_);
    resultsTable_->setColumnCount(6);
    resultsTable_->setHorizontalHeaderLabels({
        "Date", "Local Time", "Type", "Matched Conditions", "Aspect Orb", "Calculation Notes",
    });
    resultsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    resultsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resultsTable_->setAlternatingRowColors(true);
    resultsTable_->setWordWrap(false);
    resultsTable_->verticalHeader()->setVisible(false);
    resultsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    resultsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    resultsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    resultsTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    resultsTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    resultsTable_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    resultsTable_->setColumnHidden(5, true);
    if (auto* item = resultsTable_->horizontalHeaderItem(0)) {
        item->setToolTip("Return date in the selected return timezone");
    }
    if (auto* item = resultsTable_->horizontalHeaderItem(1)) {
        item->setToolTip("Local return time; hover a cell to see the exact timestamp and timezone");
    }
    if (auto* item = resultsTable_->horizontalHeaderItem(4)) {
        item->setToolTip("Closest matched aspect orb; N/A when the search has no aspect condition");
    }
    if (auto* item = resultsTable_->horizontalHeaderItem(5)) {
        item->setToolTip("Non-fatal calculation notes, shown only when at least one result has one");
    }
    layout->addWidget(resultsTable_, 1);
    connect(resultsTable_, &QTableWidget::itemSelectionChanged, this, &ReturnFinderController::handleSelectionChanged);
    connect(resultsTable_, &QTableWidget::cellDoubleClicked, this, [this](int, int) { openSelectedResult(); });
    connect(openButton_, &QPushButton::clicked, this, &ReturnFinderController::openSelectedResult);
    connect(copyButton_, &QPushButton::clicked, this, &ReturnFinderController::copyResults);
}

void ReturnFinderController::addCondition(const ReturnFinderCondition& requested) {
    ReturnFinderCondition condition = requested;
    if (condition.id.isEmpty()) condition.id = QString("condition_%1").arg(nextConditionId_++);
    auto* row = new ReturnFinderConditionRow(
        condition,
        [this]() { conditionChanged(); },
        [this](ReturnFinderConditionRow* item) { duplicateCondition(item); },
        [this](ReturnFinderConditionRow* item) { removeCondition(item); },
        conditionsContainer_);
    conditionRows_.push_back(row);
    conditionsLayout_->insertWidget(std::max(0, conditionsLayout_->count() - 1), row);
    conditionChanged();
}

void ReturnFinderController::duplicateCondition(ReturnFinderConditionRow* row) {
    if (!row) return;
    ReturnFinderCondition condition = row->condition();
    condition.id = QString("condition_%1").arg(nextConditionId_++);
    addCondition(condition);
}

void ReturnFinderController::removeCondition(ReturnFinderConditionRow* row) {
    if (!row) return;
    conditionRows_.removeOne(row);
    row->deleteLater();
    conditionChanged();
}

void ReturnFinderController::clearConditions() {
    for (auto* row : conditionRows_) row->deleteLater();
    conditionRows_.clear();
    conditionChanged();
}

void ReturnFinderController::conditionChanged() {
    if (running_) {
        ++runGeneration_;
        cancelSearch();
        if (statusLabel_) statusLabel_->setText("Stopping because the search query changed...");
        return;
    }
    if (hasRun_) markResultsStale();
}

void ReturnFinderController::updateRangeUi() {
    if (rangeStack_) rangeStack_->setCurrentIndex(returnType() == ReturnFinderType::Lunar ? 1 : 0);
    if (returnMethodCombo_) {
        returnMethodCombo_->setEnabled(
            !running_ && returnType() == ReturnFinderType::Solar);
    }
}

void ReturnFinderController::updateLocationUi() {
    const bool tajaka = tajakaMethodActive();
    if (tajaka && locationModeCombo_ && locationModeCombo_->currentIndex() != 0) {
        const QSignalBlocker blocker(locationModeCombo_);
        locationModeCombo_->setCurrentIndex(0);
    }
    const bool useNatal = tajaka || !locationModeCombo_ || locationModeCombo_->currentData().toBool();
    // Under Tajaka the custom fields are left untouched (just disabled) so a
    // location typed before switching is not destroyed; the query always
    // uses the natal location anyway.
    if (useNatal && !tajaka && hasNatalContext_) {
        const QSignalBlocker b1(locationEdit_);
        const QSignalBlocker b2(timezoneEdit_);
        const QSignalBlocker b3(latitudeSpin_);
        const QSignalBlocker b4(longitudeSpin_);
        locationEdit_->setText(natalLocationName_);
        timezoneEdit_->setText(natalInput_.timezone);
        latitudeSpin_->setValue(natalInput_.latitude);
        longitudeSpin_->setValue(natalInput_.longitude);
    }
    if (locationModeCombo_) {
        locationModeCombo_->setEnabled(!tajaka && !running_);
    }
    const bool editable = !useNatal && !running_;
    locationEdit_->setEnabled(editable);
    timezoneEdit_->setEnabled(editable);
    latitudeSpin_->setEnabled(editable);
    longitudeSpin_->setEnabled(editable);
}

void ReturnFinderController::updateRunUi() {
    if (runButton_) runButton_->setEnabled(hasNatalContext_ && !running_);
    if (stopButton_) stopButton_->setEnabled(running_);
    if (openButton_) openButton_->setEnabled(hasSelectedResult() && !stale_ && !running_);
    if (copyButton_) copyButton_->setEnabled(!results_.isEmpty());
    for (auto* row : conditionRows_) row->setEnabled(!running_);
    for (QWidget* widget : {static_cast<QWidget*>(returnTypeCombo_), static_cast<QWidget*>(startYearSpin_),
                            static_cast<QWidget*>(endYearSpin_), static_cast<QWidget*>(startDateEdit_),
                            static_cast<QWidget*>(endDateEdit_), static_cast<QWidget*>(locationModeCombo_),
                            static_cast<QWidget*>(houseModeCombo_), static_cast<QWidget*>(rulershipCombo_),
                            static_cast<QWidget*>(matchModeCombo_), static_cast<QWidget*>(presetCombo_)}) {
        if (widget) widget->setEnabled(!running_);
    }
    if (returnMethodCombo_) {
        returnMethodCombo_->setEnabled(
            !running_ && returnType() == ReturnFinderType::Solar);
    }
    if (houseModeCombo_) {
        // Whole Sign is the Tajaka basis; the house system choice is locked
        // while the Tajaka method is active.
        houseModeCombo_->setEnabled(!running_ && !tajakaMethodActive());
    }
    updateLocationUi();
}

bool ReturnFinderController::buildQuery(ReturnFinderQuery* query, QString* error) const {
    if (!query) return false;
    if (!hasNatalContext_) {
        if (error) *error = "Load a natal chart before running Return Finder.";
        return false;
    }
    ReturnFinderQuery value;
    value.returnType = returnType();
    value.startYear = startYearSpin_->value();
    value.endYear = endYearSpin_->value();
    value.startDate = startDateEdit_->date();
    value.endDate = endDateEdit_->date();
    value.useNatalLocation = locationModeCombo_->currentData().toBool();
    value.locationName = value.useNatalLocation ? natalLocationName_ : locationEdit_->text().trimmed();
    value.timezone = value.useNatalLocation ? natalInput_.timezone : timezoneEdit_->text().trimmed();
    value.latitude = value.useNatalLocation ? natalInput_.latitude : latitudeSpin_->value();
    value.longitude = value.useNatalLocation ? natalInput_.longitude : longitudeSpin_->value();
    value.tajakaMode = tajakaMethodActive();
    if (value.tajakaMode) {
        // Tajaka scans are pinned to the natal (birthplace) location and the
        // rasi (whole-sign) chart, per the source methodology.
        value.useNatalLocation = true;
        value.locationName = natalLocationName_;
        value.timezone = natalInput_.timezone;
        value.latitude = natalInput_.latitude;
        value.longitude = natalInput_.longitude;
        value.houseMode = ReturnFinderHouseMode::WholeSign;
    }
    if (value.timezone.isEmpty()) {
        if (error) *error = "Set a valid return timezone.";
        return false;
    }
    QTimeZone validatedTimezone;
    QString normalizedTimezone;
    QString timezoneError;
    if (!parseTimezoneInput(value.timezone, &validatedTimezone, &normalizedTimezone, &timezoneError)) {
        if (error) *error = timezoneError;
        return false;
    }
    value.timezone = normalizedTimezone;
    if (!value.useNatalLocation && value.locationName.isEmpty()
        && std::fabs(value.latitude) < 0.0001 && std::fabs(value.longitude) < 0.0001) {
        if (error) *error = "Set a custom return location or coordinates.";
        return false;
    }
    value.houseMode = static_cast<ReturnFinderHouseMode>(houseModeCombo_->currentData().toInt());
    if (value.tajakaMode) {
        // Applied after the combo read so a stale/disabled combo value can
        // never leak into a Tajaka query (e.g. after a preset load).
        value.houseMode = ReturnFinderHouseMode::WholeSign;
    }
    value.modernRulership = rulershipCombo_->currentData().toInt() == 1;
    value.matchMode = static_cast<ReturnFinderMatchMode>(matchModeCombo_->currentData().toInt());
    bool hasInclude = false;
    for (auto* row : conditionRows_) {
        ReturnFinderCondition condition = row->condition();
        if (!condition.enabled) {
            value.conditions.push_back(condition);
            continue;
        }
        if (!condition.exclude) hasInclude = true;
        if (condition.type == ReturnFinderConditionType::PlanetPlacement
            && !condition.planetPlacementEnabled
            && !condition.planetAngleContactEnabled) {
            if (error) {
                *error = "Each enabled Planet Placement condition must require a house/sign placement, "
                         "an angle conjunction, or both.";
            }
            return false;
        }
        if (condition.type == ReturnFinderConditionType::PlanetPlacement
            && condition.planetAngleContactEnabled
            && (condition.target.kind != ReturnFinderTargetKind::Angle
                || !finderAngles().contains(condition.target.name))) {
            if (error) *error = "Choose a valid angle for the Planet Placement conjunction.";
            return false;
        }
        if (condition.type == ReturnFinderConditionType::Aspect
            && condition.subject.scope == condition.target.scope
            && condition.subject.kind == condition.target.kind
            && (condition.subject.kind == ReturnFinderTargetKind::Planet
                || condition.subject.kind == ReturnFinderTargetKind::Angle)
            && condition.subject.name == condition.target.name) {
            if (error) {
                *error = "An Aspect condition cannot compare a planet or angle with itself "
                         "in the same Return/Natal scope.";
            }
            return false;
        }
        if (condition.type == ReturnFinderConditionType::Aspect
            && condition.subject.scope == condition.target.scope
            && condition.subject.kind == ReturnFinderTargetKind::ProfectionLord
            && condition.target.kind == ReturnFinderTargetKind::ProfectionLord) {
            if (error) {
                *error = "An Aspect condition cannot compare the profection lord with "
                         "itself in the same Return/Natal scope.";
            }
            return false;
        }
        if ((condition.type == ReturnFinderConditionType::HouseLordPlacement
             && condition.houseLordHouses.isEmpty())
            || (condition.type == ReturnFinderConditionType::Aspect
                && ((condition.subject.kind == ReturnFinderTargetKind::HouseLord && condition.subject.houses.isEmpty())
                    || (condition.target.kind == ReturnFinderTargetKind::HouseLord && condition.target.houses.isEmpty())))) {
            if (error) *error = "Every house-lord condition needs at least one valid house number (1-12).";
            return false;
        }
        if (condition.type == ReturnFinderConditionType::TajakaAspect) {
            const bool planetsOk =
                condition.subject.kind == ReturnFinderTargetKind::Planet
                && condition.target.kind == ReturnFinderTargetKind::Planet
                && finderClassicalPlanets().contains(condition.subject.name)
                && finderClassicalPlanets().contains(condition.target.name)
                && condition.subject.name != condition.target.name;
            if (!planetsOk) {
                if (error) {
                    *error = "Tajaka Aspect requires two different classical planets "
                             "(Sun through Saturn) in the return chart.";
                }
                return false;
            }
        }
        const bool tajakaFamilyCondition =
            condition.type == ReturnFinderConditionType::MunthaPlacement
            || condition.type == ReturnFinderConditionType::LordOfYearPlacement
            || condition.type == ReturnFinderConditionType::TajakaAspect
            || condition.subject.kind == ReturnFinderTargetKind::Muntha
            || condition.subject.kind == ReturnFinderTargetKind::MunthaLord
            || condition.subject.kind == ReturnFinderTargetKind::LordOfYear
            || condition.target.kind == ReturnFinderTargetKind::Muntha
            || condition.target.kind == ReturnFinderTargetKind::MunthaLord
            || condition.target.kind == ReturnFinderTargetKind::LordOfYear;
        if (tajakaFamilyCondition && !value.tajakaMode
            && natalInput_.zodiacSystem != ZodiacSystem::Sidereal) {
            // Muntha and the Tajaka lord of the year are sidereal references;
            // with a tropical natal chart they need the Tajaka method (which
            // computes a sidereal natal reference).
            if (error) {
                *error = "Muntha, Muntha Lord, Lord of the Year and Tajaka Aspect "
                         "conditions need the Tajaka method (or a sidereal natal chart).";
            }
            return false;
        }
        if (condition.type == ReturnFinderConditionType::Aspect
            && condition.subject.scope == condition.target.scope) {
            const auto isResolvedPointKind = [](ReturnFinderTargetKind kind) {
                return kind == ReturnFinderTargetKind::Muntha
                    || kind == ReturnFinderTargetKind::MunthaLord
                    || kind == ReturnFinderTargetKind::LordOfYear;
            };
            const bool sameResolvedPoint =
                isResolvedPointKind(condition.subject.kind)
                && condition.subject.kind == condition.target.kind;
            if (sameResolvedPoint) {
                if (error) {
                    *error = "An Aspect condition cannot compare a Tajaka point "
                             "with itself in the same Return/Natal scope.";
                }
                return false;
            }
        }
        value.conditions.push_back(condition);
    }
    if (!hasInclude) {
        if (error) *error = "Add and enable at least one Include condition.";
        return false;
    }
    value.natalInput = natalInput_;
    value.natalChart = natalChart_;
    value.natalLocationName = natalLocationName_;
    value.aspectOrbs = natalInput_.aspectOrbs;
    value.ephePath = ephePath_;
    value.dllSearchPaths = dllSearchPaths_;
    *query = value;
    return true;
}

void ReturnFinderController::startSearch() {
    if (running_) return;
    ReturnFinderQuery query;
    QString error;
    if (!buildQuery(&query, &error)) {
        statusLabel_->setText(error);
        emit statusMessage(error);
        return;
    }
    if (ephePath_.isEmpty() || dllSearchPaths_.isEmpty()) {
        const QString message = "Swiss Ephemeris runtime is not available.";
        statusLabel_->setText(message);
        emit statusMessage(message);
        return;
    }

    results_.clear();
    summary_ = {};
    stale_ = false;
    hasRun_ = false;
    lastQuery_ = query;
    populateResults();
    running_ = true;
    statusLabel_->setText("Starting...");
    workspaceStateLabel_->setText("Search running");
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    updateRunUi();
    emit summaryChanged();

    const quint64 generation = ++runGeneration_;
    workerThread_ = new QThread(this);
    worker_ = new ReturnFinderWorker(query);
    worker_->moveToThread(workerThread_);
    connect(workerThread_, &QThread::started, worker_, &ReturnFinderWorker::run);
    connect(worker_, &ReturnFinderWorker::progress, this, [this, generation](int completed, int total, const QString& status) {
        if (generation != runGeneration_) return;
        const int safeTotal = std::max(total, 1);
        progressBar_->setValue(std::clamp(completed * 100 / safeTotal, 0, 100));
        statusLabel_->setText(status);
    });
    connect(worker_, &ReturnFinderWorker::finished, this,
            [this, generation](const QVector<ReturnFinderResult>& results,
                   const ReturnFinderRunSummary& summary,
                   const QString& workerError) {
        running_ = false;
        if (generation != runGeneration_) {
            hasRun_ = false;
            stale_ = false;
            progressBar_->setValue(0);
            statusLabel_->setText("Search discarded because its natal or query context changed.");
            workspaceStateLabel_->setText("Run a fresh search");
            populateResults();
            updateRunUi();
            emit summaryChanged();
            emit statusMessage(statusLabel_->text());
            return;
        }
        results_ = results;
        summary_ = summary;
        hasRun_ = workerError.isEmpty();
        stale_ = false;
        progressBar_->setValue(100);
        if (!workerError.isEmpty()) {
            statusLabel_->setText(workerError);
            workspaceStateLabel_->setText("Search failed");
            emit statusMessage(workerError);
        } else if (summary.cancelled) {
            statusLabel_->setText(QString("Cancelled - %1 partial matches").arg(results.size()));
            workspaceStateLabel_->setText("Partial results");
        } else if (summary.capped) {
            statusLabel_->setText(QString("Completed at safety cap - %1 matches").arg(results.size()));
            workspaceStateLabel_->setText("Capped results");
        } else {
            statusLabel_->setText(QString("Scanned %1; matched %2; failed %3")
                                      .arg(summary.scanned).arg(results.size()).arg(summary.failed));
            workspaceStateLabel_->setText("Current results");
        }
        populateResults();
        updateRunUi();
        emit summaryChanged();
        emit statusMessage(statusLabel_->text());
    });
    connect(worker_, &ReturnFinderWorker::finished, worker_, &QObject::deleteLater);
    connect(worker_, &ReturnFinderWorker::finished, workerThread_, &QThread::quit);
    connect(worker_, &QObject::destroyed, this, [this]() { worker_ = nullptr; });
    connect(workerThread_, &QThread::finished, workerThread_, &QObject::deleteLater);
    connect(workerThread_, &QObject::destroyed, this, [this]() { workerThread_ = nullptr; });
    workerThread_->start();
}

void ReturnFinderController::populateResults() {
    if (!resultsTable_) return;
    const QSignalBlocker blocker(resultsTable_);
    resultsTable_->setSortingEnabled(false);
    resultsTable_->clearContents();
    resultsTable_->setRowCount(results_.size());
    const bool hasCalculationNotes = std::any_of(
        results_.cbegin(), results_.cend(),
        [](const ReturnFinderResult& result) { return !result.warning.trimmed().isEmpty(); });
    resultsTable_->setColumnHidden(5, !hasCalculationNotes);
    for (int row = 0; row < results_.size(); ++row) {
        const auto& result = results_[row];
        const QString exactTimestamp = QString("%1 (%2)")
            .arg(result.localDateTime.toString("dddd, d MMMM yyyy 'at' h:mm:ss AP"),
                 lastQuery_.timezone);
        auto* dateItem = new ReturnFinderNumberItem(
            result.localDateTime.toString("d MMM yyyy"),
            static_cast<double>(result.localDateTime.toMSecsSinceEpoch()));
        dateItem->setData(Qt::UserRole, result.stableId);
        dateItem->setToolTip(exactTimestamp);
        resultsTable_->setItem(row, 0, dateItem);

        auto* timeItem = new ReturnFinderNumberItem(
            result.localDateTime.toString("h:mm AP"),
            static_cast<double>(result.localDateTime.time().msecsSinceStartOfDay()));
        timeItem->setToolTip(exactTimestamp);
        resultsTable_->setItem(row, 1, timeItem);
        resultsTable_->setItem(row, 2,
            new QTableWidgetItem(returnFinderTypeLabel(result.returnType)));

        auto* matchItem = new QTableWidgetItem(result.matchSummary);
        matchItem->setToolTip(result.matchSummary);
        resultsTable_->setItem(row, 3, matchItem);

        const bool hasAspectOrb = std::isfinite(result.closestOrb) && result.closestOrb >= 0.0;
        auto* orbItem = new ReturnFinderNumberItem(
            hasAspectOrb ? QString("%1 deg").arg(result.closestOrb, 0, 'f', 2)
                         : QString("N/A"),
            hasAspectOrb ? result.closestOrb : std::numeric_limits<double>::infinity());
        orbItem->setToolTip(hasAspectOrb
            ? QString("Closest matched aspect orb: %1 degrees").arg(result.closestOrb, 0, 'f', 2)
            : QString("No aspect condition contributed to this match."));
        resultsTable_->setItem(row, 4, orbItem);

        if (hasCalculationNotes) {
            auto* noteItem = new QTableWidgetItem(
                result.warning.trimmed().isEmpty() ? QString("None") : result.warning);
            noteItem->setToolTip(result.warning.trimmed().isEmpty()
                ? QString("No calculation notes for this result.") : result.warning);
            resultsTable_->setItem(row, 5, noteItem);
        }
    }
    resultsTable_->setSortingEnabled(true);
    if (!results_.isEmpty()) resultsTable_->selectRow(0);
    updateRunUi();
    emit selectionChanged();
}

void ReturnFinderController::handleSelectionChanged() {
    updateRunUi();
    emit selectionChanged();
}

int ReturnFinderController::selectedStableId() const {
    if (!resultsTable_ || resultsTable_->currentRow() < 0) return -1;
    auto* item = resultsTable_->item(resultsTable_->currentRow(), 0);
    return item ? item->data(Qt::UserRole).toInt() : -1;
}

const ReturnFinderResult* ReturnFinderController::resultForStableId(int stableId) const {
    for (const auto& result : results_) {
        if (result.stableId == stableId) return &result;
    }
    return nullptr;
}

void ReturnFinderController::openSelectedResult() {
    if (stale_ || running_) return;
    const auto* result = resultForStableId(selectedStableId());
    if (!result) return;
    emit openResultRequested(*result, lastQuery_);
}

void ReturnFinderController::copyResults() {
    if (results_.isEmpty()) return;

    const ReturnFinderQuery& query = lastQuery_;
    QStringList lines;
    const auto addTableRow = [&lines](const QString& key, const QString& value) {
        lines.push_back(QString("| %1 | %2 |")
            .arg(markdownTableCell(key), markdownTableCell(value)));
    };
    const auto boolText = [](bool value) { return value ? QString("Yes") : QString("No"); };
    const auto coordinates = [](double latitude, double longitude) {
        return QString("%1, %2")
            .arg(latitude, 0, 'f', 6)
            .arg(longitude, 0, 'f', 6);
    };

    QString reportState = "Completed";
    if (stale_) {
        reportState = "Stale - natal chart, zodiac, location or query changed after this run";
    } else if (summary_.cancelled) {
        reportState = "Partial results - search cancelled";
    } else if (summary_.capped) {
        reportState = "Partial results - 5,000-return safety cap reached";
    } else if (!hasRun_) {
        reportState = "Incomplete run";
    }

    QString zodiac = zodiacSystemToString(query.natalInput.zodiacSystem);
    if (query.natalInput.zodiacSystem == ZodiacSystem::Sidereal) {
        zodiac += QString(" - %1 ayanamsa")
            .arg(siderealAyanamsaToString(query.natalInput.siderealAyanamsa));
    }
    const QString natalName = query.natalInput.name.trimmed().isEmpty()
        ? QString("Unnamed natal chart") : query.natalInput.name.trimmed();
    const QString natalLocation = query.natalLocationName.trimmed().isEmpty()
        ? QString("Unnamed natal location") : query.natalLocationName.trimmed();
    const QString returnLocation = query.locationName.trimmed().isEmpty()
        ? QString("Unnamed return location") : query.locationName.trimmed();
    const QString birthLocal = QString("%1 %2")
        .arg(query.natalInput.date.toString("yyyy-MM-dd"),
             query.natalInput.time.toString("HH:mm:ss"));
    const QString birthUtc = query.natalChart.utcDateTime.isValid()
        ? query.natalChart.utcDateTime.toString("yyyy-MM-dd HH:mm:ss 'UTC'")
        : QString("Unavailable");

    lines.push_back("# DracoVed Return Finder Research Report");
    lines.push_back(QString());
    lines.push_back(QString("_Generated %1 (system local time)_")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss t")));
    lines.push_back(QString());
    if (stale_) {
        lines.push_back("> **Warning:** These results are stale. The natal chart or search context changed after the scan; rerun the search before relying on or opening them.");
        lines.push_back(QString());
    } else if (summary_.cancelled || summary_.capped) {
        lines.push_back("> **Partial results:** The scan did not cover the entire requested range. See Run Summary for details.");
        lines.push_back(QString());
    }

    lines.push_back("## Base Natal Chart");
    lines.push_back(QString());
    lines.push_back("| Field | Value |");
    lines.push_back("|---|---|");
    addTableRow("Chart Name", natalName);
    addTableRow("Gender", genderToString(query.natalInput.gender));
    addTableRow("Birth Date / Time (Local)", birthLocal);
    addTableRow("Birth Timezone", query.natalInput.timezone);
    addTableRow("Birth Date / Time (UTC)", birthUtc);
    addTableRow("Natal Location", natalLocation);
    addTableRow("Natal Coordinates", coordinates(query.natalInput.latitude,
                                                   query.natalInput.longitude));
    addTableRow("Zodiac", zodiac);
    addTableRow("Lunar Nodes", lunarNodePolicySummary(query.natalChart.lunarNodePolicy));
    addTableRow("Natal House System", reportHouseSystemLabel(query.natalInput.houseSystem));
    addTableRow("Natal Calculation Warnings",
                query.natalChart.warnings.isEmpty()
                    ? QString("None") : query.natalChart.warnings.join("<br>"));
    lines.push_back(QString());

    QString searchRange;
    if (query.returnType == ReturnFinderType::Solar) {
        searchRange = QString("%1 through %2")
            .arg(std::min(query.startYear, query.endYear))
            .arg(std::max(query.startYear, query.endYear));
    } else {
        const QDate first = std::min(query.startDate, query.endDate);
        const QDate last = std::max(query.startDate, query.endDate);
        searchRange = QString("%1 through %2")
            .arg(first.toString("yyyy-MM-dd"), last.toString("yyyy-MM-dd"));
    }
    int enabledCount = 0;
    int includeCount = 0;
    int excludeCount = 0;
    for (const auto& condition : query.conditions) {
        if (!condition.enabled) continue;
        ++enabledCount;
        condition.exclude ? ++excludeCount : ++includeCount;
    }
    const QString logic = query.matchMode == ReturnFinderMatchMode::All
        ? "Match all enabled Include conditions; every Exclude condition vetoes a result"
        : "Match any enabled Include condition; every Exclude condition vetoes a result";

    lines.push_back("## Search Configuration");
    lines.push_back(QString());
    lines.push_back("| Field | Value |");
    lines.push_back("|---|---|");
    addTableRow("Return Type", returnFinderTypeLabel(query.returnType));
    addTableRow("Return Method", query.tajakaMode
        ? QString("Tajaka (tropical Sun return, sidereal chart, natal location)")
        : QString("Standard"));
    addTableRow("Requested Range", searchRange);
    addTableRow("Location Basis", query.useNatalLocation
        ? QString("Natal chart location") : QString("Custom return location"));
    addTableRow("Return Location", returnLocation);
    addTableRow("Return Coordinates", coordinates(query.latitude, query.longitude));
    addTableRow("Return Timezone", query.timezone);
    addTableRow("Zodiac", zodiac);
    addTableRow("Lunar Nodes", lunarNodePolicySummary(query.natalChart.lunarNodePolicy));
    addTableRow("House Evaluation", reportHouseModeLabel(query.houseMode));
    addTableRow("Rulership", query.modernRulership ? "Modern" : "Traditional");
    addTableRow("Boolean Logic", logic);
    addTableRow("Condition Counts", QString("%1 enabled (%2 Include, %3 Exclude); %4 total")
        .arg(enabledCount).arg(includeCount).arg(excludeCount).arg(query.conditions.size()));
    lines.push_back(QString());

    lines.push_back("### Conditions");
    lines.push_back(QString());
    lines.push_back("| # | State | Role | Type | Definition |");
    lines.push_back("|---:|---|---|---|---|");
    for (int i = 0; i < query.conditions.size(); ++i) {
        const auto& condition = query.conditions[i];
        lines.push_back(QString("| %1 | %2 | %3 | %4 | %5 |")
            .arg(i + 1)
            .arg(condition.enabled ? "Enabled" : "Disabled")
            .arg(condition.exclude ? "Exclude" : "Include")
            .arg(markdownTableCell(reportConditionTypeLabel(condition.type)))
            .arg(markdownTableCell(reportConditionDescription(condition))));
    }
    lines.push_back(QString());

    lines.push_back("## Run Summary");
    lines.push_back(QString());
    lines.push_back("| Item | Value |");
    lines.push_back("|---|---:|");
    addTableRow("Result State", reportState);
    addTableRow("Candidates Scanned", QString::number(summary_.scanned));
    addTableRow("Matches Reported by Worker", QString::number(summary_.matched));
    addTableRow("Results Included in Report", QString::number(results_.size()));
    addTableRow("Candidate Failures", QString::number(summary_.failed));
    addTableRow("Cancelled", boolText(summary_.cancelled));
    addTableRow("Safety Cap Reached", boolText(summary_.capped));
    addTableRow("Run Warning Count", QString::number(summary_.warnings.size()));
    lines.push_back(QString());

    if (!summary_.warnings.isEmpty()) {
        lines.push_back("### Run Warnings");
        lines.push_back(QString());
        for (const QString& warning : summary_.warnings) {
            lines.push_back(QString("- %1").arg(markdownTableCell(warning)));
        }
        lines.push_back(QString());
    }

    lines.push_back("## Matched Returns");
    lines.push_back(QString());
    lines.push_back(QString("Return date/times below use **%1**.")
        .arg(markdownTableCell(query.timezone)));
    lines.push_back(QString());
    const bool hasResultNotes = std::any_of(
        results_.cbegin(), results_.cend(),
        [](const ReturnFinderResult& result) { return !result.warning.trimmed().isEmpty(); });
    if (hasResultNotes) {
        lines.push_back("| # | Date | Local Time | Type | Matched Conditions | Closest Aspect Orb | Calculation Notes |");
        lines.push_back("|---:|---|---|---|---|---:|---|");
    } else {
        lines.push_back("| # | Date | Local Time | Type | Matched Conditions | Closest Aspect Orb |");
        lines.push_back("|---:|---|---|---|---|---:|");
    }
    for (int i = 0; i < results_.size(); ++i) {
        const auto& result = results_[i];
        QString conditions = result.matchSummary;
        conditions.replace("; ", "<br>");
        const QString orb = std::isfinite(result.closestOrb) && result.closestOrb >= 0.0
            ? QString("%1 deg").arg(result.closestOrb, 0, 'f', 2) : QString("N/A");
        const QString commonCells = QString("| %1 | %2 | %3 | %4 | %5 | %6")
            .arg(i + 1)
            .arg(markdownTableCell(result.localDateTime.toString("d MMM yyyy")))
            .arg(markdownTableCell(result.localDateTime.toString("h:mm:ss AP")))
            .arg(markdownTableCell(returnFinderTypeLabel(result.returnType)))
            .arg(markdownTableCell(conditions))
            .arg(markdownTableCell(orb));
        if (hasResultNotes) {
            lines.push_back(QString("%1 | %2 |")
                .arg(commonCells,
                     markdownTableCell(result.warning.trimmed().isEmpty()
                         ? QString("None") : result.warning)));
        } else {
            lines.push_back(commonCells + " |");
        }
    }
    lines.push_back(QString());

    constexpr int detailedResultLimit = 100;
    const int detailedCount = std::min<int>(results_.size(), detailedResultLimit);
    bool hasEvaluations = false;
    for (int i = 0; i < detailedCount && !hasEvaluations; ++i) {
        hasEvaluations = !results_[i].evaluations.isEmpty();
    }
    if (hasEvaluations) {
        lines.push_back("## Detailed Condition Evaluations");
        lines.push_back(QString());
        if (results_.size() > detailedResultLimit) {
            lines.push_back(QString("> Detailed evaluations are included for the first %1 of %2 results. The Matched Returns table above still contains every result.")
                .arg(detailedResultLimit).arg(results_.size()));
            lines.push_back(QString());
        }

        const auto conditionForId = [&query](const QString& id, int* index) -> const ReturnFinderCondition* {
            for (int i = 0; i < query.conditions.size(); ++i) {
                if (query.conditions[i].id == id) {
                    if (index) *index = i;
                    return &query.conditions[i];
                }
            }
            if (index) *index = -1;
            return nullptr;
        };

        for (int resultIndex = 0; resultIndex < detailedCount; ++resultIndex) {
            const auto& result = results_[resultIndex];
            if (result.evaluations.isEmpty()) continue;
            lines.push_back(QString("### %1. %2 - %3")
                .arg(resultIndex + 1)
                .arg(returnFinderTypeLabel(result.returnType),
                     result.localDateTime.toString("yyyy-MM-dd HH:mm:ss")));
            lines.push_back(QString());
            lines.push_back(QString("- **Matched summary:** %1")
                .arg(markdownTableCell(result.matchSummary)));
            lines.push_back(QString("- **Closest aspect orb:** %1")
                .arg(std::isfinite(result.closestOrb) && result.closestOrb >= 0.0
                         ? QString("%1 deg").arg(result.closestOrb, 0, 'f', 2)
                         : QString("N/A")));
            if (!result.warning.isEmpty()) {
                lines.push_back(QString("- **Calculation note:** %1")
                    .arg(markdownTableCell(result.warning)));
            }
            lines.push_back(QString());
            lines.push_back("| Condition | Role | Status | Resolved Points | Orb | Evaluation |");
            lines.push_back("|---|---|---|---|---:|---|");
            for (const auto& evaluation : result.evaluations) {
                int conditionIndex = -1;
                const ReturnFinderCondition* condition = conditionForId(
                    evaluation.conditionId, &conditionIndex);
                const QString conditionLabel = condition
                    ? QString("%1. %2").arg(conditionIndex + 1)
                          .arg(reportConditionTypeLabel(condition->type))
                    : evaluation.conditionId;
                const QString role = evaluation.excluded ? "Exclude" : "Include";
                QString status;
                if (evaluation.excluded) {
                    status = evaluation.matched ? "Exclude triggered" : "Exclude not triggered";
                } else {
                    status = evaluation.matched ? "Matched" : "Not matched";
                }
                QString resolved;
                if (!evaluation.resolvedSubject.isEmpty() && !evaluation.resolvedTarget.isEmpty()) {
                    resolved = QString("%1 -> %2")
                        .arg(evaluation.resolvedSubject, evaluation.resolvedTarget);
                } else if (!evaluation.resolvedSubject.isEmpty()) {
                    resolved = evaluation.resolvedSubject;
                } else if (!evaluation.resolvedTarget.isEmpty()) {
                    resolved = evaluation.resolvedTarget;
                }
                if (!evaluation.systemLabel.isEmpty()) {
                    resolved += resolved.isEmpty()
                        ? evaluation.systemLabel : QString(" (%1)").arg(evaluation.systemLabel);
                }
                const QString orb = std::isfinite(evaluation.orb) && evaluation.orb >= 0.0
                    ? QString("%1 deg").arg(evaluation.orb, 0, 'f', 2) : QString("N/A");
                lines.push_back(QString("| %1 | %2 | %3 | %4 | %5 | %6 |")
                    .arg(markdownTableCell(conditionLabel),
                         markdownTableCell(role),
                         markdownTableCell(status),
                         markdownTableCell(resolved),
                         markdownTableCell(orb),
                         markdownTableCell(evaluation.description)));
            }
            lines.push_back(QString());
        }
    }

    lines.push_back("## Interpretation Notes");
    lines.push_back(QString());
    lines.push_back("- `N/A` under **Closest Aspect Orb** means the matching conditions did not require an aspect, such as a stellium or placement-only search.");
    lines.push_back("- **Calculation Notes** appear only when a return chart completed with a non-fatal issue or fallback worth reviewing; the column is omitted when every result is clean.");
    lines.push_back("- Exclude conditions always veto a candidate when they are triggered, regardless of Match All or Match Any mode.");
    lines.push_back("- House-dependent evaluations follow the selected Whole Sign/Placidus mode and rulership system shown above.");
    lines.push_back("- Open a listed return in DracoVed for the complete chart and visual inspection.");
    lines.push_back(QString());
    lines.push_back("---");
    lines.push_back("_Generated by DracoVed Return Finder._");

    const QString markdown = lines.join('\n');
    auto* mimeData = new QMimeData();
    mimeData->setText(markdown);
    mimeData->setData("text/markdown", markdown.toUtf8());
    QApplication::clipboard()->setMimeData(mimeData);
    emit statusMessage(QString("Copied a Markdown Return Finder report with %1 matched result(s).")
        .arg(results_.size()));
}

QString ReturnFinderController::presetsDirectory() const {
    return QDir(QCoreApplication::applicationDirPath()).filePath("return_finder_presets");
}

void ReturnFinderController::refreshPresetList(const QString& preferred) {
    if (!presetCombo_) return;
    QDir dir(presetsDirectory());
    if (!dir.exists()) QDir().mkpath(dir.absolutePath());
    const QSignalBlocker blocker(presetCombo_);
    presetCombo_->clear();
    presetCombo_->addItem("Select preset...", QString());
    const QStringList files = dir.entryList({"*.json"}, QDir::Files, QDir::Name);
    for (const QString& file : files) {
        const QString base = QFileInfo(file).completeBaseName();
        presetCombo_->addItem(base, dir.filePath(file));
    }
    if (!preferred.isEmpty()) {
        const int index = presetCombo_->findData(preferred);
        if (index >= 0) presetCombo_->setCurrentIndex(index);
    }
}

bool ReturnFinderController::writePresetFile(const QString& path,
                                             const QString& displayName,
                                             QString* error) const {
    QJsonObject root;
    root["schema_version"] = 1;
    root["display_name"] = displayName;
    root["house_mode"] = houseModeCombo_->currentData().toInt();
    root["modern_rulership"] = rulershipCombo_->currentData().toInt() == 1;
    root["match_mode"] = matchModeCombo_->currentData().toInt();
    QJsonArray conditions;
    for (auto* row : conditionRows_) conditions.push_back(conditionToJson(row->condition()));
    root["conditions"] = conditions;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

void ReturnFinderController::savePreset() {
    bool ok = false;
    const QString requested = QInputDialog::getText(filtersRoot_, "Save Return Finder Preset",
                                                     "Preset name:", QLineEdit::Normal,
                                                     presetCombo_->currentIndex() > 0 ? presetCombo_->currentText() : QString(), &ok);
    if (!ok) return;
    const QString name = sanitizedPresetName(requested);
    if (name.isEmpty()) {
        emit statusMessage("Preset name cannot be empty.");
        return;
    }
    QDir dir(presetsDirectory());
    if (!dir.exists()) QDir().mkpath(dir.absolutePath());
    const QString path = dir.filePath(name + ".json");
    if (QFileInfo::exists(path)
        && QMessageBox::question(filtersRoot_, "Overwrite Preset",
                                 QString("Overwrite preset '%1'?").arg(name)) != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!writePresetFile(path, name, &error)) {
        emit statusMessage(QString("Unable to save preset: %1").arg(error));
        return;
    }
    refreshPresetList(path);
    emit statusMessage(QString("Saved Return Finder preset '%1'.").arg(name));
}

bool ReturnFinderController::applyPresetFile(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QString("Invalid preset JSON: %1").arg(parseError.errorString());
        return false;
    }
    const QJsonObject root = document.object();
    if (root.value("schema_version").toInt() != 1) {
        if (error) *error = "Unsupported Return Finder preset version.";
        return false;
    }
    const QJsonArray array = root.value("conditions").toArray();
    QVector<ReturnFinderCondition> conditions;
    for (const auto& value : array) {
        if (value.isObject()) conditions.push_back(conditionFromJson(value.toObject()));
    }
    if (conditions.isEmpty()) {
        if (error) *error = "Preset contains no conditions.";
        return false;
    }
    houseModeCombo_->setCurrentIndex(std::max(0, houseModeCombo_->findData(root.value("house_mode").toInt(0))));
    rulershipCombo_->setCurrentIndex(root.value("modern_rulership").toBool(false) ? 1 : 0);
    matchModeCombo_->setCurrentIndex(std::max(0, matchModeCombo_->findData(root.value("match_mode").toInt(0))));
    for (auto* row : conditionRows_) row->deleteLater();
    conditionRows_.clear();
    for (auto condition : conditions) {
        condition.id = QString("condition_%1").arg(nextConditionId_++);
        addCondition(condition);
    }
    updateRangeUi();
    conditionChanged();
    return true;
}

void ReturnFinderController::loadSelectedPreset() {
    const QString path = presetCombo_->currentData().toString();
    if (path.isEmpty()) return;
    QString error;
    if (!applyPresetFile(path, &error)) {
        emit statusMessage(QString("Unable to load preset: %1").arg(error));
        return;
    }
    emit statusMessage(QString("Loaded Return Finder preset '%1'.").arg(presetCombo_->currentText()));
}

void ReturnFinderController::renameSelectedPreset() {
    const QString oldPath = presetCombo_->currentData().toString();
    if (oldPath.isEmpty()) return;
    bool ok = false;
    const QString requested = QInputDialog::getText(filtersRoot_, "Rename Return Finder Preset",
                                                     "New name:", QLineEdit::Normal,
                                                     presetCombo_->currentText(), &ok);
    if (!ok) return;
    const QString name = sanitizedPresetName(requested);
    if (name.isEmpty()) return;
    const QString newPath = QDir(presetsDirectory()).filePath(name + ".json");
    if (QFileInfo::exists(newPath)) {
        emit statusMessage("A preset with that name already exists.");
        return;
    }
    if (!QFile::rename(oldPath, newPath)) {
        emit statusMessage("Unable to rename the preset file.");
        return;
    }
    refreshPresetList(newPath);
    emit statusMessage(QString("Renamed preset to '%1'.").arg(name));
}

void ReturnFinderController::deleteSelectedPreset() {
    const QString path = presetCombo_->currentData().toString();
    if (path.isEmpty()) return;
    const QString name = presetCombo_->currentText();
    if (QMessageBox::question(filtersRoot_, "Delete Return Finder Preset",
                              QString("Delete preset '%1'?").arg(name)) != QMessageBox::Yes) {
        return;
    }
    if (!QFile::remove(path)) {
        emit statusMessage("Unable to delete the preset file.");
        return;
    }
    refreshPresetList();
    emit statusMessage(QString("Deleted preset '%1'.").arg(name));
}

}  // namespace dracoved
