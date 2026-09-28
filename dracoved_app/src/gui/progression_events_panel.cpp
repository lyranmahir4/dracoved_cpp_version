#include "progression_events_panel.h"
#include "../core/formatting.h"
#include "../core/timezone_utils.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QDateEdit>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>
#include <utility>

namespace dracoved {
class ProgressionEventsWorker : public QThread {
    Q_OBJECT
public:
    ProgressionEventQuery query;
    QString ephePath, dllPath, error;
    QVector<ProgressionEvent> results;
signals:
    void progress(int percent);
protected:
    void run() override {
        SwissEph swe;
        if (!swe.load({dllPath}, &error)) return;
        results = findProgressionEvents(swe, ephePath, query,
            [this] { return isInterruptionRequested(); },
            [this](int percent) { emit progress(percent); }, &error);
    }
};

ProgressionEventsPanel::ProgressionEventsPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    contextLabel_ = new QLabel("Load a natal chart to find progression events.", this);
    contextLabel_->setWordWrap(true);
    layout->addWidget(contextLabel_);
    auto* range = new QGroupBox("Search range", this);
    auto* form = new QFormLayout(range);
    from_ = new QDateEdit(QDate::currentDate(), range);
    through_ = new QDateEdit(QDate::currentDate().addYears(10), range);
    for (auto* edit : {from_, through_}) {
        edit->setDateRange(QDate(1, 1, 1), QDate(9999, 12, 31));
        edit->setCalendarPopup(true);
        edit->setDisplayFormat("dd MMM yyyy");
    }
    timezone_ = new QLineEdit("UTC", range);
    form->addRow("From", from_);
    form->addRow("Through", through_);
    form->addRow("Timezone", timezone_);
    body_ = new QComboBox(range);
    body_->addItem("All planets", QString());
    for (const auto& name : QStringList{"Sun", "Moon", "Mercury", "Venus", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune", "Pluto"}) {
        body_->addItem(name, name);
    }
    form->addRow("Planet", body_);
    layout->addWidget(range);
    auto* types = new QGroupBox("Events", this);
    auto* eventLayout = new QVBoxLayout(types);
    signs_ = new QCheckBox("Planet enters a sign", types);
    houses_ = new QCheckBox("Planet crosses a house cusp", types);
    angles_ = new QCheckBox("Ascendant / Midheaven changes sign", types);
    stations_ = new QCheckBox("Planet turns direct / retrograde", types);
    signs_->setChecked(true);
    houses_->setChecked(true);
    angles_->setChecked(true);
    for (auto* check : {signs_, houses_, angles_, stations_}) eventLayout->addWidget(check);
    houseReference_ = new QComboBox(types);
    houseReference_->addItems({"Natal house cusps", "Progressed house cusps"});
    eventLayout->addWidget(houseReference_);
    connect(houses_, &QCheckBox::toggled, houseReference_, &QWidget::setEnabled);
    layout->addWidget(types);
    auto* buttons = new QHBoxLayout();
    find_ = new QPushButton("Find events", this);
    stop_ = new QPushButton("Stop", this);
    copy_ = new QPushButton("Copy events", this);
    find_->setEnabled(false);
    stop_->setEnabled(false);
    copy_->setEnabled(false);
    buttons->addWidget(find_);
    buttons->addWidget(stop_);
    buttons->addWidget(copy_);
    layout->addLayout(buttons);
    progress_ = new QProgressBar(this);
    progress_->setRange(0, 100);
    progress_->setValue(0);
    layout->addWidget(progress_);
    status_ = new QLabel(resultStatus_, this);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto* hint = new QLabel("Secondary progression: day for a year; Naibod angles. Select a result to load that moment on the chart. Times refer to your life calendar, not the ephemeris date.", this);
    hint->setWordWrap(true);
    hint->setObjectName("hintLabel");
    layout->addWidget(hint);
    layout->addStretch();
    connect(find_, &QPushButton::clicked, this, &ProgressionEventsPanel::start);
    connect(stop_, &QPushButton::clicked, this, [this] {
        if (worker_) worker_->requestInterruption();
        stop_->setEnabled(false);
        status_->setText("Stopping…");
    });
    connect(copy_, &QPushButton::clicked, this, &ProgressionEventsPanel::copy);
}

ProgressionEventsPanel::~ProgressionEventsPanel() {
    if (worker_) {
        worker_->requestInterruption();
        worker_->wait();
        delete worker_;
    }
}

void ProgressionEventsPanel::setNatalContext(const NatalInput& input, const NatalChart& chart,
                                            const QString& ephePath, const QString& dllPath) {
    ++generation_;
    if (worker_) worker_->requestInterruption();
    context_.input = input;
    context_.natal = chart;
    ephePath_ = ephePath;
    dllPath_ = dllPath;
    timezone_->setText(input.timezone);
    hasContext_ = true;
    results_.clear();
    lastQuery_ = {};
    selected_ = -1;
    copy_->setEnabled(false);
    find_->setEnabled(!worker_);
    progress_->setValue(0);
    resultStatus_ = "Chart updated. Choose a range, then Find events.";
    status_->setText(resultStatus_);
    contextLabel_->setText(QString("%1 · %2 houses · %3")
        .arg(zodiacDescription(input.zodiacSystem, input.siderealAyanamsa),
             input.houseSystem == HouseSystem::WholeSign ? "Whole Sign" : "Placidus", lunarNodePolicySummary(input.lunarNodePolicy)));
    // Keep explicit node names consistent with the loaded chart's node policy.
    while (body_->count() > 11) body_->removeItem(11);
    for (const auto& body : chart.bodies) {
        if (body.isLunarNode) body_->addItem(lunarNodeDisplayName(body.name, input.lunarNodePolicy), body.name);
    }
    emit resultsChanged();
}

void ProgressionEventsPanel::start() {
    if (worker_ || !hasContext_) return;
    ProgressionEventQuery query = context_;
    QTimeZone zone;
    QString message;
    if (!parseTimezoneInput(timezone_->text(), &zone, &query.timezone, &message)) {
        status_->setText(message);
        return;
    }
    query.start = from_->date().startOfDay(zone);
    query.end = through_->date().addDays(1).startOfDay(zone);
    if (!query.start.isValid() || !query.end.isValid() || query.start >= query.end) {
        status_->setText("Choose a valid range; Through must be on or after From.");
        return;
    }
    query.signs = signs_->isChecked();
    query.houses = houses_->isChecked();
    query.angles = angles_->isChecked();
    query.stations = stations_->isChecked();
    query.progressedHouses = houseReference_->currentIndex() == 1;
    if (!query.signs && !query.houses && !query.angles && !query.stations) {
        status_->setText("Select at least one event type.");
        return;
    }
    const QString name = body_->currentData().toString();
    query.bodies = name.isEmpty()
        ? QStringList{"Sun", "Moon", "Mercury", "Venus", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune", "Pluto"}
        : QStringList{name};
    lastQuery_ = query;
    results_.clear();
    selected_ = -1;
    find_->setEnabled(false);
    stop_->setEnabled(true);
    copy_->setEnabled(false);
    progress_->setValue(0);
    resultStatus_ = "Searching progression events…";
    status_->setText(resultStatus_);
    emit resultsChanged();
    const int generation = generation_;
    auto* worker = new ProgressionEventsWorker;
    worker_ = worker;
    worker->query = query;
    worker->ephePath = ephePath_;
    worker->dllPath = dllPath_;
    connect(worker, &ProgressionEventsWorker::progress, this, [this, generation](int value) {
        if (generation == generation_) progress_->setValue(value);
    });
    connect(worker, &QThread::finished, this, [this, worker, generation] {
        const bool stopped = worker->isInterruptionRequested();
        if (generation == generation_) {
            results_ = std::move(worker->results);
            resultStatus_ = worker->error.isEmpty()
                ? QString("%1 · %2 events · %3").arg(stopped ? "Stopped (partial results)" : "Complete").arg(results_.size()).arg(lastQuery_.timezone)
                : QString("Incomplete · %1 events · %2").arg(results_.size()).arg(worker->error);
            status_->setText(resultStatus_);
            if (!stopped && worker->error.isEmpty()) progress_->setValue(100);
            copy_->setEnabled(!results_.isEmpty());
        }
        worker_ = nullptr;
        worker->deleteLater();
        stop_->setEnabled(false);
        find_->setEnabled(hasContext_);
        emit resultsChanged();
    });
    worker->start();
}

void ProgressionEventsPanel::showResults(QTableWidget* table, QTableWidget* details) {
    if (!table || !details) return;
    const QSignalBlocker blocker(table);
    table->setSortingEnabled(false);
    table->clear();
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels({"Date", "Time", "Body", "Event", "Change", "Motion"});
    table->setRowCount(results_.size());
    table->setWordWrap(false);
    table->verticalHeader()->setDefaultSectionSize(22);
    auto* header = table->horizontalHeader();
    header->setMinimumSectionSize(28);
    header->setSortIndicatorShown(false);
    for (int col = 0; col < 6; ++col) header->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    header->setStretchLastSection(true);
    for (int row = 0; row < results_.size(); ++row) {
        const auto& event = results_[row];
        const QStringList cells = {event.time.toString("d MMM yyyy"), event.time.toString("HH:mm:ss"),
            lunarNodeDisplayName(event.body, lastQuery_.input.lunarNodePolicy), event.kind, event.transition, event.motion};
        const QString tip = QString("%1 · %2\n%3\n%4")
            .arg(event.time.toString("ddd, d MMM yyyy HH:mm:ss t"), lastQuery_.timezone, formatDegInSign(event.longitude), event.detail);
        for (int col = 0; col < cells.size(); ++col) {
            auto* item = new QTableWidgetItem(cells[col]);
            item->setToolTip(tip);
            if (col == 3) item->setForeground(QColor(event.kind == "Station" ? "#956126" : event.kind == "Sign entry" ? "#216a8a" : "#7452a0"));
            table->setItem(row, col, item);
        }
    }
    details->clear();
    details->setColumnCount(2);
    details->setHorizontalHeaderLabels({"Field", "Value"});
    QVector<QPair<QString, QString>> values;
    values.push_back({"Search", resultStatus_});
    if (lastQuery_.start.isValid()) {
        values.push_back({"Range", lastQuery_.start.toString("d MMM yyyy") + " → " + lastQuery_.end.addDays(-1).toString("d MMM yyyy")});
        values.push_back({"Context", zodiacDescription(lastQuery_.input.zodiacSystem, lastQuery_.input.siderealAyanamsa)
            + " · " + (lastQuery_.input.houseSystem == HouseSystem::WholeSign ? "Whole Sign" : "Placidus")});
    }
    if (selected_ >= 0 && selected_ < results_.size()) {
        const auto& event = results_[selected_];
        table->setCurrentCell(selected_, 0);
        table->scrollToItem(table->item(selected_, 0));
        values.push_back({"Moment", event.time.toString("ddd, d MMM yyyy HH:mm:ss t")});
        values.push_back({"Timezone", lastQuery_.timezone});
        values.push_back({"Body", lunarNodeDisplayName(event.body, lastQuery_.input.lunarNodePolicy)});
        values.push_back({"Event", event.kind + " · " + event.transition});
        values.push_back({"Position", formatDegInSign(event.longitude)});
        values.push_back({"Motion", event.motion});
        values.push_back({"Detail", event.detail});
    } else {
        values.push_back({"Selection", results_.isEmpty() ? "No events to select." : "Select an event above to load its progressed chart."});
    }
    details->setRowCount(values.size());
    details->setWordWrap(true);
    details->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    details->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    details->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int row = 0; row < values.size(); ++row) {
        details->setItem(row, 0, new QTableWidgetItem(values[row].first));
        details->setItem(row, 1, new QTableWidgetItem(values[row].second));
    }
}

void ProgressionEventsPanel::activateRow(int row) {
    if (row < 0 || row >= results_.size()) return;
    selected_ = row;
    emit eventActivated(results_[row].time, lastQuery_.timezone);
    emit resultsChanged();
}

void ProgressionEventsPanel::copy() {
    if (results_.isEmpty()) return;
    QStringList lines{QString("Progression events · %1 · %2").arg(lastQuery_.timezone, resultStatus_),
        QString("%1 → %2 · %3 · %4 houses").arg(lastQuery_.start.toString("d MMM yyyy"),
            lastQuery_.end.addDays(-1).toString("d MMM yyyy"), zodiacDescription(lastQuery_.input.zodiacSystem, lastQuery_.input.siderealAyanamsa),
            lastQuery_.input.houseSystem == HouseSystem::WholeSign ? "Whole Sign" : "Placidus"),
        "Date\tTime\tBody\tEvent\tChange\tMotion\tPosition\tDetail"};
    for (const auto& event : results_) {
        lines << QStringList{event.time.toString("d MMM yyyy"), event.time.toString("HH:mm:ss"),
            lunarNodeDisplayName(event.body, lastQuery_.input.lunarNodePolicy), event.kind, event.transition,
            event.motion, formatDegInSign(event.longitude), event.detail}.join('\t');
    }
    QApplication::clipboard()->setText(lines.join('\n'));
    status_->setText("Copied progression events.");
}
} // namespace dracoved

#include "progression_events_panel.moc"
