#include "dasha_panel.h"
#include "compact_controls.h"
#include "../core/formatting.h"
#include "../core/timezone_utils.h"
#include "../core/vedic_nakshatra.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateEdit>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTimeEdit>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace dracoved {
namespace {
constexpr double epochJd = 2440587.5;
constexpr qint64 dayMs = 86400000;
enum { LordRole = Qt::UserRole, LevelRole, StartRole, EndRole };
QColor lordColor(int lord, const QPalette& palette) {
    static const int hues[] = {275, 325, 35, 200, 5, 255, 45, 220, 145};
    return QColor::fromHsv(hues[std::clamp(lord, 0, 8)], 140, palette.base().color().lightness() < 128 ? 220 : 155);
}
QColor tint(QColor color, const QPalette& palette) {
    const auto base = palette.base().color();
    return QColor((base.red() * 7 + color.red()) / 8, (base.green() * 7 + color.green()) / 8,
                  (base.blue() * 7 + color.blue()) / 8);
}
QIcon marker(QColor color, bool metal = false) {
    QPixmap pix(32, 24); pix.fill(Qt::transparent); pix.setDevicePixelRatio(2);
    QPainter painter(&pix); painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(color.darker(135)); painter.setBrush(color);
    if (metal) painter.drawPolygon(QPolygonF{{1, 9}, {3, 4}, {12, 2}, {15, 8}, {6, 11}});
    else painter.drawEllipse(QRectF(3, 2, 9, 9));
    return QIcon(pix);
}
QString duration(qint64 ms) {
    const qint64 seconds = std::max<qint64>(0, ms) / 1000;
    if (seconds >= 86400) return QString("%1d %2h %3m").arg(seconds / 86400).arg(seconds / 3600 % 24).arg(seconds / 60 % 60);
    return QString("%1h %2m %3s").arg(seconds / 3600).arg(seconds / 60 % 60).arg(seconds % 60);
}
DashaPeriod period(const QTreeWidgetItem* item) {
    return {item->data(0, LordRole).toInt(), item->data(0, LevelRole).toInt(),
        item->data(0, StartRole).toLongLong(), item->data(0, EndRole).toLongLong()};
}
void setupTable(QTableWidget* table, const QStringList& headers) {
    table->setColumnCount(headers.size()); table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true); table->setShowGrid(false); table->verticalHeader()->hide();
    table->verticalHeader()->setMinimumSectionSize(20); table->verticalHeader()->setDefaultSectionSize(24);
    table->horizontalHeader()->setFixedHeight(24); table->horizontalHeader()->setMinimumSectionSize(40);
    table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
}
QLabel* caption(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent); auto font = label->font(); font.setWeight(QFont::DemiBold); label->setFont(font);
    return label;
}
}

DashaPanel::DashaPanel(SwissEph* swe, QWidget* parent) : QWidget(parent), swe_(swe) {
    setObjectName("dashaPanel");
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(4, 4, 4, 0); layout->setSpacing(4);
    auto* controls = new CompactControls;
    auto group = [&](const QString& title, QWidget* control) {
        auto* widget = new QWidget(this); auto* row = new QHBoxLayout(widget);
        row->setContentsMargins(0, 0, 0, 0); row->setSpacing(4);
        row->addWidget(new QLabel(title, widget)); row->addWidget(control); controls->addWidget(widget);
    };
    date_ = new QDateEdit(this); date_->setObjectName("dashaDate"); date_->setCalendarPopup(true);
    date_->setDisplayFormat("dd MMM yyyy"); date_->setDateRange(QDate(1, 1, 1), QDate(9999, 12, 31));
    time_ = new QTimeEdit(this); time_->setObjectName("dashaTime"); time_->setDisplayFormat("HH:mm:ss.zzz");
    year_ = new QComboBox(this); year_->setObjectName("dashaYear");
    year_->addItem("365.25 days", 365.25); year_->addItem("360 days", 360.0);
    year_->setToolTip("Fixed duration of a Vimshottari year. All five levels use this convention.");
    year_->setCurrentIndex(std::max(0, year_->findData(QSettings().value("vedic/dashaYearDays", 365.25).toDouble())));
    group("Inspect", date_); controls->addWidget(time_); group("Year", year_);
    auto* now = new QPushButton("Now", this); now->setObjectName("dashaNow");
    auto* birth = new QPushButton("Birth", this); birth->setObjectName("dashaBirth");
    run_ = new QPushButton("Calculate", this); run_->setObjectName("dashaCalculate");
    copy_ = new QPushButton("Copy active", this); copy_->setObjectName("dashaCopy");
    for (auto* button : {now, birth, run_, copy_}) controls->addWidget(button);
    layout->addLayout(controls);
    balance_ = new QLabel(this); balance_->setObjectName("dashaBalance"); balance_->setTextFormat(Qt::PlainText);
    balance_->setWordWrap(true); balance_->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(balance_);

    auto* navigation = new CompactControls;
    level_ = new QComboBox(this); level_->setObjectName("dashaChangeLevel");
    for (int i = 0; i < 5; ++i) level_->addItem(dashaLevelName(i), i);
    level_->setCurrentIndex(1);
    auto* previous = new QPushButton("Previous change", this); previous->setObjectName("dashaPrevious");
    auto* next = new QPushButton("Next change", this); next->setObjectName("dashaNext");
    auto* jump = new QPushButton("Show active path", this); jump->setObjectName("dashaJump");
    auto* inspect = new QPushButton("Inspect selected start", this); inspect->setObjectName("dashaInspectPeriod");
    auto* copySchedule = new QPushButton("Copy schedule", this); copySchedule->setObjectName("dashaCopySchedule");
    navigation->addWidget(level_);
    for (auto* button : {previous, next, jump, inspect, copySchedule}) navigation->addWidget(button);
    layout->addLayout(navigation);

    auto* splitter = new QSplitter(Qt::Horizontal, this); splitter->setObjectName("dashaSplitter");
    splitter->setChildrenCollapsible(false); splitter->setHandleWidth(6);
    auto* left = new QWidget(splitter); auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0); leftLayout->setSpacing(3);
    leftLayout->addWidget(caption("Vimshottari · period schedule", left));
    schedule_ = new QTreeWidget(left); schedule_->setObjectName("dashaSchedule");
    schedule_->setHeaderLabels({"Period / lord", "Start (local)", "End (local)", "Duration", "Age at start"});
    schedule_->setUniformRowHeights(true); schedule_->setAlternatingRowColors(true); schedule_->setIndentation(14);
    schedule_->setSelectionMode(QAbstractItemView::SingleSelection); schedule_->setSortingEnabled(false);
    schedule_->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    schedule_->header()->setStretchLastSection(false);
    schedule_->setToolTip("Expand a period for its nine subdivisions. Ends are exclusive. Double-click a period to inspect its start. Copy schedule exports expanded rows.");
    leftLayout->addWidget(schedule_, 1);
    auto* right = new QWidget(splitter); auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0); rightLayout->setSpacing(4);
    rightLayout->addWidget(caption("Active at inspection time", right));
    activeTable_ = new QTableWidget(right); activeTable_->setObjectName("dashaActive");
    setupTable(activeTable_, {"Level", "Lord", "Start (local)", "End (local)", "Remaining", "Elapsed"});
    activeTable_->setFixedHeight(164); rightLayout->addWidget(activeTable_);
    auto* heading = new QHBoxLayout; heading->addWidget(caption("Active lords · transit snapshot", right)); heading->addStretch();
    stop_ = new QPushButton("Stop lookup", right); stop_->setObjectName("dashaStop");
    openTransit_ = new QPushButton("Open Tara", right); openTransit_->setObjectName("dashaOpenTransit");
    heading->addWidget(stop_); heading->addWidget(openTransit_); rightLayout->addLayout(heading);
    transitTable_ = new QTableWidget(right); transitTable_->setObjectName("dashaTransits");
    setupTable(transitTable_, {"Planet", "Roles", "Transit sign", "H from Moon", "Nakshatra / pada", "Tara", "Moorthi", "Sign entry (local)"});
    transitTable_->horizontalHeaderItem(3)->setToolTip("Whole-sign count from the natal Moon to the transiting planet, 1–12.");
    transitTable_->horizontalHeaderItem(6)->setToolTip("Metal at this planet's latest sign entry in the selected zodiac, using the Moon at entry.");
    rightLayout->addWidget(transitTable_, 1);
    detail_ = new QLabel(right); detail_->setObjectName("dashaDetail"); detail_->setTextFormat(Qt::PlainText);
    detail_->setWordWrap(true); detail_->setTextInteractionFlags(Qt::TextSelectableByMouse); rightLayout->addWidget(detail_);
    status_ = new QLabel(right); status_->setObjectName("dashaStatus"); status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true); rightLayout->addWidget(status_);
    splitter->addWidget(left); splitter->addWidget(right); splitter->setSizes({700, 1050});
    splitter->restoreState(QSettings().value("vedic/dashaSplitter").toByteArray()); layout->addWidget(splitter, 1);
    connect(splitter, &QSplitter::splitterMoved, this, [splitter] { QSettings().setValue("vedic/dashaSplitter", splitter->saveState()); });
    timer_ = new QTimer(this); timer_->setInterval(0);
    connect(timer_, &QTimer::timeout, this, [this] { lookupStep(); });
    connect(date_, &QDateEdit::dateChanged, this, [this] { resolvedUtc_.reset(); invalidate(); });
    connect(time_, &QTimeEdit::timeChanged, this, [this] { resolvedUtc_.reset(); invalidate(); });
    connect(run_, &QPushButton::clicked, this, [this] { calculate(); });
    connect(now, &QPushButton::clicked, this, [this] { setInspectionTime(QDateTime::currentMSecsSinceEpoch()); });
    connect(birth, &QPushButton::clicked, this, [this] { if (hasContext_) setInspectionTime(birthMs_); });
    connect(year_, &QComboBox::currentIndexChanged, this, [this] {
        QSettings().setValue("vedic/dashaYearDays", year_->currentData().toDouble()); calculate();
    });
    connect(previous, &QPushButton::clicked, this, [this] { navigate(-1); });
    connect(next, &QPushButton::clicked, this, [this] { navigate(1); });
    connect(level_, &QComboBox::currentIndexChanged, this, [this](int level) {
        if (active_.size() == 5 && !timer_->isActive()) status_->setText("Next " + dashaLevelName(level) + ": " + localTime(active_[level].endMs, true));
    });
    connect(jump, &QPushButton::clicked, this, [this] { jumpToActive(); });
    auto inspectSelected = [this] {
        if (auto* item = schedule_->currentItem()) setInspectionTime(period(item).startMs);
    };
    connect(inspect, &QPushButton::clicked, this, inspectSelected);
    connect(schedule_, &QTreeWidget::itemDoubleClicked, this, inspectSelected);
    connect(schedule_, &QTreeWidget::itemExpanded, this, [this](auto* item) { expandPeriod(item); });
    connect(copy_, &QPushButton::clicked, this, [this] { copy(false); });
    connect(copySchedule, &QPushButton::clicked, this, [this] { copy(true); });
    connect(stop_, &QPushButton::clicked, this, [this] { stopLookup(); });
    connect(transitTable_, &QTableWidget::itemSelectionChanged, this, [this] { showTransitDetail(); });
    connect(activeTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row < 0 || row >= active_.size()) return;
        for (int i = 0; i < transits_.size(); ++i)
            if (transits_[i].lord == active_[row].lord) { transitTable_->selectRow(i); break; }
    });
    connect(openTransit_, &QPushButton::clicked, this, [this] {
        const int row = transitTable_->currentRow();
        if (row >= 0 && row < transits_.size() && onTransitRequested) onTransitRequested(transits_[row].name, inspectionMs_);
    });
    run_->setEnabled(false); invalidate();
}

double DashaPanel::yearDays() const { return year_->currentData().toDouble(); }
void DashaPanel::setContext(const NatalInput& input, const NatalChart& chart, const QString& facts) {
    input_ = input; input_.zodiacSystem = chart.zodiacSystem;
    ayanamsa_ = chart.siderealAyanamsa; birthFacts_ = facts;
    QString error, label;
    const auto moon = std::find_if(chart.bodies.cbegin(), chart.bodies.cend(), [](const auto& body) { return body.name == "Moon"; });
    hasContext_ = moon != chart.bodies.cend() && std::isfinite(moon->longitude) && chart.utcDateTime.isValid()
        && parseTimezoneInput(input.timezone, &zone_, &label, &error);
    run_->setEnabled(hasContext_);
    if (!hasContext_) { invalidate(); balance_->setText("Birth Moon or timezone unavailable."); return; }
    birthMoon_ = moon->longitude; birthMs_ = chart.utcDateTime.toMSecsSinceEpoch();
    date_->setToolTip("Inspection time in " + input.timezone); time_->setToolTip(date_->toolTip());
    setInspectionTime(resolvedUtc_.value_or(QDateTime::currentMSecsSinceEpoch()));
}
void DashaPanel::setInspectionTime(qint64 utcMs) {
    if (!hasContext_) return;
    const auto local = QDateTime::fromMSecsSinceEpoch(utcMs, zone_);
    if (local.date() < date_->minimumDate() || local.date() > date_->maximumDate()) {
        status_->setText("That boundary is outside the calendar input range. The full period remains in the schedule."); return;
    }
    const QSignalBlocker dateBlock(date_), timeBlock(time_);
    date_->setDate(local.date()); time_->setTime(local.time()); resolvedUtc_ = utcMs;
    calculate(isVisible());
}
void DashaPanel::invalidate() {
    timer_->stop(); snapshotPending_ = false; active_.clear(); transits_.clear();
    schedule_->clear(); activeTable_->setRowCount(0); transitTable_->setRowCount(0);
    copy_->setEnabled(false); stop_->setEnabled(false); openTransit_->setEnabled(false);
    balance_->clear(); detail_->clear(); status_->setText("Choose an inspection time, then Calculate.");
    if (onStateChanged) onStateChanged();
}
void DashaPanel::calculate(bool lookup) {
    invalidate();
    if (!hasContext_) return;
    // UTC navigation retains which occurrence of a repeated local hour was selected.
    // Manually entered ambiguous/skipped local times must not silently pick a side.
    const auto moment = resolvedUtc_ ? QDateTime::fromMSecsSinceEpoch(*resolvedUtc_, zone_)
        : QDateTime(date_->date(), time_->time(), zone_, QDateTime::TransitionResolution::Reject);
    if (!moment.isValid()) {
        status_->setText("This local time is skipped or repeated by daylight saving. Choose an unambiguous time."); return;
    }
    inspectionMs_ = moment.toMSecsSinceEpoch(); resolvedUtc_ = inspectionMs_;
    if (!engine_.initialize(birthMs_, birthMoon_, year_->currentData().toDouble())) {
        status_->setText("Cannot calculate Vimshottari from this birth Moon."); return;
    }
    active_ = engine_.activeAt(inspectionMs_);
    if (active_.size() != 5) {
        status_->setText("The inspection instant is outside the supported UTC calendar."); return;
    }
    const auto star = classifyVedicNakshatra(birthMoon_);
    balance_->setText(QString("Birth balance: %1 · %2 remaining · %3 / pada %4 · %5-day year · Times in %6")
        .arg(dashaLordName(engine_.birthMajor().lord), duration(engine_.birthMajor().endMs - birthMs_), star.name)
        .arg(star.pada).arg(engine_.yearDays()).arg(input_.timezone));
    balance_->setToolTip(QString("Moon %1° · %2 · Birth Mahadasha begins %3 · Ends %4\nEvery subdivision uses the full parent period. End timestamps are exclusive.")
        .arg(birthMoon_, 0, 'f', 9).arg(zodiacDescription(input_.zodiacSystem, ayanamsa_),
            localTime(engine_.birthMajor().startMs, true), localTime(engine_.birthMajor().endMs, true)));
    renderActive(); rebuildSchedule(); jumpToActive(); copy_->setEnabled(!active_.isEmpty());
    snapshotPending_ = true; status_->setText("Dasha periods ready.");
    if (onStateChanged) onStateChanged();
    if (lookup) startSnapshot();
}
QStringList DashaPanel::activeLords() const {
    QStringList result;
    for (const auto& p : active_) if (!result.contains(dashaLordName(p.lord))) result << dashaLordName(p.lord);
    return result;
}
QString DashaPanel::activeSummary() const {
    if (active_.isEmpty()) return {};
    QStringList roles;
    for (const auto& p : active_) roles << dashaLevelAbbreviation(p.level) + " " + dashaLordName(p.lord);
    return QString("%1 · %2 · %3-day year").arg(localTime(inspectionMs_, true), roles.join("  ›  ")).arg(engine_.yearDays());
}
QString DashaPanel::localTime(qint64 ms, bool precise) const {
    return QDateTime::fromMSecsSinceEpoch(ms, zone_).toString(precise ? "dd MMM yyyy HH:mm:ss.zzz ttt" : "dd MMM yyyy HH:mm:ss");
}
void DashaPanel::navigate(int direction) {
    const int level = level_->currentData().toInt();
    if (active_.size() != 5) return;
    if (direction > 0) setInspectionTime(active_[level].endMs);
    else if (inspectionMs_ > active_[level].startMs) setInspectionTime(active_[level].startMs);
    else {
        const auto previous = engine_.activeAt(active_[level].startMs - 1);
        if (previous.size() == 5) setInspectionTime(previous[level].startMs);
    }
}
void DashaPanel::renderActive() {
    activeTable_->setRowCount(active_.size());
    for (int row = 0; row < active_.size(); ++row) {
        const auto& p = active_[row];
        const QStringList cells = {dashaLevelName(row), dashaLordName(p.lord), localTime(p.startMs), localTime(p.endMs),
            duration(p.endMs - inspectionMs_), {}};
        for (int col = 0; col < cells.size(); ++col) {
            auto* item = new QTableWidgetItem(cells[col]); item->setData(LordRole, p.lord);
            if (col == 2 || col == 3) {
                const qint64 ms = col == 2 ? p.startMs : p.endMs;
                item->setData(StartRole, ms); item->setToolTip(localTime(ms, true) + "\n" + QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC).toString(Qt::ISODateWithMs));
            }
            if (row < 2) { auto font = item->font(); font.setWeight(QFont::DemiBold); item->setFont(font); }
            activeTable_->setItem(row, col, item);
        }
        auto* progress = new QProgressBar(activeTable_); progress->setRange(0, 1000);
        progress->setValue(int((inspectionMs_ - p.startMs) * 1000.0L / (p.endMs - p.startMs)));
        progress->setFormat(QString::number(progress->value() / 10.0, 'f', 1) + "%");
        progress->setMinimumWidth(78); progress->setMaximumHeight(19); activeTable_->setCellWidget(row, 5, progress);
    }
    applyColors();
}
void DashaPanel::rebuildSchedule() {
    schedule_->clear();
    auto add = [&](const DashaPeriod& p, QTreeWidgetItem* parent) {
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(schedule_);
        item->setData(0, LordRole, p.lord); item->setData(0, LevelRole, p.level);
        item->setData(0, StartRole, p.startMs); item->setData(0, EndRole, p.endMs);
        return item;
    };
    for (const auto& p : engine_.majorCycle(inspectionMs_)) add(p, nullptr);
    // Text and lazy-expansion flags are also set for newly created children.
    applyColors();
}
void DashaPanel::expandPeriod(QTreeWidgetItem* item) {
    if (item->childCount() || period(item).level >= 4) return;
    for (const auto& p : Vimshottari::children(period(item))) {
        auto* child = new QTreeWidgetItem(item);
        child->setData(0, LordRole, p.lord); child->setData(0, LevelRole, p.level);
        child->setData(0, StartRole, p.startMs); child->setData(0, EndRole, p.endMs);
    }
    applyColors();
}
void DashaPanel::jumpToActive() {
    if (active_.isEmpty()) return;
    QTreeWidgetItem* selected = nullptr;
    for (int i = 0; i < schedule_->topLevelItemCount(); ++i)
        if (period(schedule_->topLevelItem(i)).contains(inspectionMs_)) selected = schedule_->topLevelItem(i);
    if (!selected) return;
    for (int level = 0; level < 4; ++level) {
        expandPeriod(selected); selected->setExpanded(true);
        for (int i = 0; i < selected->childCount(); ++i)
            if (period(selected->child(i)).contains(inspectionMs_)) { selected = selected->child(i); break; }
    }
    schedule_->setCurrentItem(selected); schedule_->scrollToItem(selected, QAbstractItemView::PositionAtCenter);
}
void DashaPanel::applyColors() {
    if (!schedule_ || !activeTable_) return;
    for (QTreeWidgetItemIterator it(schedule_); *it; ++it) {
        auto* item = *it; const auto p = period(item); const auto color = lordColor(p.lord, palette());
        item->setText(0, dashaLevelAbbreviation(p.level) + " · " + dashaLordName(p.lord));
        item->setIcon(0, marker(color));
        for (int col : {1, 2}) {
            const auto ms = col == 1 ? p.startMs : p.endMs;
            item->setText(col, localTime(ms)); item->setToolTip(col, localTime(ms, true) + "\n" + QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC).toString(Qt::ISODateWithMs));
        }
        item->setText(3, duration(p.endMs - p.startMs));
        const auto birth = QDateTime::fromMSecsSinceEpoch(birthMs_, zone_);
        const auto start = QDateTime::fromMSecsSinceEpoch(p.startMs, zone_);
        int years = start.date().year() - birth.date().year();
        if (birth.addYears(years) > start) --years;
        item->setText(4, p.startMs < birthMs_ ? "Before birth" : QString("%1y %2d").arg(years).arg(birth.addYears(years).daysTo(start)));
        item->setChildIndicatorPolicy(p.level < 4 ? QTreeWidgetItem::ShowIndicator : QTreeWidgetItem::DontShowIndicator);
        const bool active = p.contains(inspectionMs_);
        for (int col = 0; col < 5; ++col) {
            auto font = item->font(col); font.setBold(active); item->setFont(col, font);
            item->setBackground(col, active ? QBrush(tint(color, palette())) : QBrush());
        }
    }
    for (int row = 0; row < activeTable_->rowCount(); ++row) {
        const auto color = lordColor(active_[row].lord, palette());
        for (int col : {0, 1}) activeTable_->item(row, col)->setBackground(tint(color, palette()));
        activeTable_->item(row, 1)->setIcon(marker(color));
        if (auto* progress = activeTable_->cellWidget(row, 5))
            progress->setStyleSheet(QString("QProgressBar { border: 1px solid palette(mid); border-radius: 2px; text-align: center; background: %2; color: %3; } QProgressBar::chunk { background: %1; }")
                .arg(tint(color, palette()).name(), palette().base().color().name(), palette().text().color().name()));
    }
}
void DashaPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event); if (snapshotPending_) startSnapshot();
}
void DashaPanel::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange && schedule_) { applyColors(); if (!transits_.isEmpty()) renderSnapshot(); }
}

void DashaPanel::startSnapshot() {
    if (active_.isEmpty()) return;
    snapshotPending_ = false; timer_->stop(); transits_.clear();
    if (!swe_ || !swe_->isLoaded()) { status_->setText("Periods ready · transit ephemeris unavailable."); return; }
    const int bodyIds[] = {SE_MEAN_NODE, SE_VENUS, SE_SUN, SE_MOON, SE_MARS, SE_MEAN_NODE, SE_JUPITER, SE_SATURN, SE_MERCURY};
    for (const auto& name : activeLords()) {
        const int lord = std::find_if(active_.cbegin(), active_.cend(), [&](const auto& p) { return dashaLordName(p.lord) == name; })->lord;
        QStringList roles;
        for (const auto& p : active_) if (p.lord == lord) roles << dashaLevelAbbreviation(p.level);
        if (lord == 0 || lord == 5) {
            for (auto type : {LunarNodeType::Mean, LunarNodeType::True}) {
                if (lunarNodePolicyIncludes(input_.lunarNodePolicy, type))
                    transits_.push_back({name + " (" + lunarNodeTypeToString(type) + ")", roles.join(" / "), lord,
                        type == LunarNodeType::Mean ? SE_MEAN_NODE : SE_TRUE_NODE, lord == 0 ? 180.0 : 0.0});
            }
        } else transits_.push_back({name, roles.join(" / "), lord, bodyIds[lord]});
    }
    const double jd = epochJd + inspectionMs_ / double(dayMs);
    QString error; bool valid = true;
    const int zodiacFlags = input_.zodiacSystem == ZodiacSystem::Sidereal ? SEFLG_SIDEREAL : 0;
    swe_->setSidMode(siderealAyanamsaSwissMode(ayanamsa_));
    for (auto& transit : transits_) {
        double lon = 0;
        if (!swe_->calcUt(jd, transit.body, zodiacFlags, &lon, &error) || !std::isfinite(lon)) { valid = false; break; }
        transit.longitude = normalizeDegrees(lon + transit.offset); transit.lookupStatus = "Searching…";
    }
    swe_->setSidMode(siderealAyanamsaSwissMode(input_.siderealAyanamsa));
    if (!valid) { transits_.clear(); renderSnapshot(); status_->setText("Transit calculation failed: " + error); return; }
    lookupRow_ = 0; cursorJd_ = jd;
    minimumJd_ = epochJd + QDateTime(QDate(1, 1, 1), QTime(0, 0), QTimeZone::UTC).toMSecsSinceEpoch() / double(dayMs);
    renderSnapshot(); if (!transits_.isEmpty()) transitTable_->selectRow(0);
    stop_->setEnabled(true); timer_->start(); status_->setText("Resolving latest sign entries…");
}
void DashaPanel::lookupStep() {
    QElapsedTimer elapsed; elapsed.start(); bool changed = false;
    const int zodiacFlags = input_.zodiacSystem == ZodiacSystem::Sidereal ? SEFLG_SIDEREAL : 0;
    swe_->setSidMode(siderealAyanamsaSwissMode(ayanamsa_));
    while (elapsed.elapsed() < 12 && lookupRow_ < transits_.size()) {
        auto& transit = transits_[lookupRow_];
        if (cursorJd_ <= minimumJd_) {
            transit.lookupStatus = "No earlier entry within the calendar range"; ++lookupRow_; cursorJd_ = epochJd + inspectionMs_ / double(dayMs); changed = true; continue;
        }
        const double from = std::max(minimumJd_, cursorJd_ - 0.25);
        QVector<MoorthiEntry> entries; QString error;
        if (!findMoorthiEntries(*swe_, transit.body, transit.offset, from, cursorJd_, signIndex(birthMoon_), &entries, &error, zodiacFlags)) {
            transit.lookupStatus = "Unavailable: " + error; ++lookupRow_; cursorJd_ = epochJd + inspectionMs_ / double(dayMs); changed = true; continue;
        }
        // Scan backwards and choose the newest crossing into the current sign;
        // a retrograde re-entry supersedes the earlier direct entry.
        for (auto it = entries.crbegin(); it != entries.crend(); ++it) {
            if (it->newSign == signIndex(transit.longitude) && it->jd <= epochJd + inspectionMs_ / double(dayMs)) {
                transit.entry = *it; transit.lookupStatus.clear(); break;
            }
        }
        cursorJd_ = from;
        if (transit.entry) { ++lookupRow_; cursorJd_ = epochJd + inspectionMs_ / double(dayMs); changed = true; }
    }
    swe_->setSidMode(siderealAyanamsaSwissMode(input_.siderealAyanamsa));
    if (changed) renderSnapshot();
    if (lookupRow_ >= transits_.size()) {
        timer_->stop(); stop_->setEnabled(false);
        const int found = int(std::count_if(transits_.cbegin(), transits_.cend(), [](const auto& t) { return t.entry.has_value(); }));
        status_->setText(QString("%1 active planets · %2 transit rows · %3/%2 latest entries resolved · Next %4: %5")
            .arg(activeLords().size()).arg(transits_.size()).arg(found).arg(dashaLevelName(level_->currentIndex()), localTime(active_[level_->currentIndex()].endMs)));
    } else status_->setText(QString("Latest entry: %1 · searching before %2").arg(transits_[lookupRow_].name,
        localTime(qRound64((cursorJd_ - epochJd) * dayMs))));
}
void DashaPanel::stopLookup() {
    timer_->stop(); stop_->setEnabled(false);
    for (auto& transit : transits_) if (!transit.entry && transit.lookupStatus == "Searching…") transit.lookupStatus = "Stopped";
    renderSnapshot(); status_->setText("Lookup stopped · unresolved Moorthi cells are unavailable. Calculate to retry.");
}
void DashaPanel::renderSnapshot() {
    const int selected = transitTable_->currentRow();
    const QSignalBlocker block(transitTable_); transitTable_->setRowCount(transits_.size());
    for (int row = 0; row < transits_.size(); ++row) {
        const auto& t = transits_[row]; const auto star = classifyVedicNakshatra(t.longitude);
        const auto tara = classifyVedicTara(classifyVedicNakshatra(birthMoon_).index, star.index);
        const QStringList cells = {t.name, t.roles, signName(signIndex(t.longitude)),
            QString::number((signIndex(t.longitude) - signIndex(birthMoon_) + 12) % 12 + 1),
            star.name + " / " + QString::number(star.pada), QString("%1 · %2").arg(tara.number).arg(tara.name),
            t.body == SE_MOON ? "Not applicable" : t.entry ? moorthiName(t.entry->moorthi) : t.lookupStatus == "Searching…" ? "Searching…" : "Unavailable",
            t.entry ? localTime(qRound64((t.entry->jd - epochJd) * dayMs)) : "—"};
        for (int col = 0; col < cells.size(); ++col) {
            auto* item = new QTableWidgetItem(cells[col]); item->setToolTip(cells[col]);
            if (col < 2) item->setBackground(tint(lordColor(t.lord, palette()), palette()));
            if (col == 0) item->setIcon(marker(lordColor(t.lord, palette())));
            if (col == 2) item->setToolTip(QString::number(t.longitude, 'f', 9) + "° · " + zodiacDescription(input_.zodiacSystem, ayanamsa_) + " · " + formatDegInSign(t.longitude));
            if (col == 3) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (col == 5) item->setToolTip(QString("From natal star: %1 · Cycle %2 · Tara %3").arg(tara.count).arg((tara.count - 1) / 9 + 1).arg(tara.name));
            if (col == 6) {
                if (t.body == SE_MOON) item->setToolTip("Moon is the reference for Moorthi and has no Moorthi of its own. Its Tara and sign-entry time remain available.");
                else if (t.entry && t.entry->moorthi != Moorthi::NotApplicable) {
                    const QColor metals[] = {QColor("#c69c27"), QColor("#a6b2be"), QColor("#be7850"), palette().base().color().lightness() < 128 ? QColor("#a0aab5") : QColor("#505a65")};
                    item->setIcon(marker(metals[int(t.entry->moorthi)], true));
                    item->setToolTip(QString("Moon at entry: %1 · From natal Moon: %2").arg(signName(t.entry->moonSign)).arg(t.entry->count));
                } else item->setToolTip(t.lookupStatus);
            }
            if (col == 7 && t.entry) item->setToolTip(localTime(qRound64((t.entry->jd - epochJd) * dayMs), true)
                + (t.entry->retrograde ? " · Retrograde entry" : " · Direct entry"));
            transitTable_->setItem(row, col, item);
        }
    }
    if (selected >= 0 && selected < transits_.size()) transitTable_->selectRow(selected);
    showTransitDetail();
}
void DashaPanel::showTransitDetail() {
    const int row = transitTable_->currentRow(); openTransit_->setEnabled(row >= 0 && row < transits_.size());
    if (row < 0 || row >= transits_.size()) { detail_->clear(); return; }
    const auto& t = transits_[row];
    QString text = QString("%1 · %2 · %3 · %4").arg(t.name, t.roles, formatDegInSign(t.longitude), localTime(inspectionMs_, true));
    if (t.body == SE_MOON) {
        text += "\nMoorthi: Not applicable";
        if (t.entry) text += " · Sign entry: " + localTime(qRound64((t.entry->jd - epochJd) * dayMs), true);
    } else if (t.entry) text += QString("\n%1 sign entry: %2 · Moon %3 %4 · Count %5 → %6")
        .arg(t.entry->retrograde ? "Retrograde" : "Direct", localTime(qRound64((t.entry->jd - epochJd) * dayMs), true),
             signName(t.entry->moonSign), formatDegInSign(t.entry->moonLongitude)).arg(t.entry->count).arg(moorthiName(t.entry->moorthi));
    else text += "\nMoorthi: " + t.lookupStatus;
    detail_->setText(text);
}
QString DashaPanel::reportContext() const {
    return QString("Vimshottari · Moon-based · %1-day year\n%2\nInspection: %3 · UTC: %4\n%5\n%6\nPeriod ends are exclusive; fractional boundaries are retained to milliseconds.")
        .arg(engine_.yearDays()).arg(birthFacts_, localTime(inspectionMs_, true),
            QDateTime::fromMSecsSinceEpoch(inspectionMs_, QTimeZone::UTC).toString(Qt::ISODateWithMs), balance_->text(), balance_->toolTip());
}
void DashaPanel::copy(bool schedule) {
    if (active_.isEmpty()) return;
    QStringList lines{reportContext()};
    if (schedule) {
        lines << "Expanded schedule · full 120-year cycle containing the inspection time";
        QStringList headers;
        for (int col = 0; col < schedule_->columnCount(); ++col) headers << schedule_->headerItem()->text(col);
        lines << headers.join('\t');
        for (QTreeWidgetItemIterator it(schedule_); *it; ++it) {
            bool visible = true;
            for (auto* parent = (*it)->parent(); parent; parent = parent->parent()) if (!parent->isExpanded()) visible = false;
            if (!visible) continue;
            const auto p = period(*it);
            lines << QStringList{QString(p.level * 2, ' ') + (*it)->text(0), localTime(p.startMs, true),
                localTime(p.endMs, true), (*it)->text(3), (*it)->text(4)}.join('\t');
        }
    } else {
        QStringList headers;
        for (int col = 0; col < activeTable_->columnCount(); ++col) headers << activeTable_->horizontalHeaderItem(col)->text();
        lines << headers.join('\t');
        for (const auto& p : active_) lines << QStringList{dashaLevelName(p.level), dashaLordName(p.lord),
            localTime(p.startMs, true), localTime(p.endMs, true), duration(p.endMs - inspectionMs_),
            QString::number((inspectionMs_ - p.startMs) * 100.0L / (p.endMs - p.startMs), 'f', 3) + "%"}.join('\t');
        lines << "" << "Active-lord transit snapshot";
        headers.clear();
        for (int col = 0; col < transitTable_->columnCount(); ++col) headers << transitTable_->horizontalHeaderItem(col)->text();
        lines << headers.join('\t');
        for (int row = 0; row < transits_.size(); ++row) {
            QStringList cells;
            for (int col = 0; col < transitTable_->columnCount(); ++col) cells << transitTable_->item(row, col)->text();
            if (transits_[row].entry) cells[7] = localTime(qRound64((transits_[row].entry->jd - epochJd) * dayMs), true);
            lines << cells.join('\t');
            const auto& t = transits_[row];
            if (t.body == SE_MOON) lines << "  Moon: Moorthi not applicable; sign-entry time is shown separately.";
            else if (t.entry) lines << QString("  %1: Moon at entry %2 · %3° · Count %4 · %5 entry")
                .arg(t.name, signName(t.entry->moonSign)).arg(t.entry->moonLongitude, 0, 'f', 9).arg(t.entry->count).arg(t.entry->retrograde ? "Retrograde" : "Direct");
            else lines << "  " + t.name + ": " + t.lookupStatus;
        }
        if (transits_.isEmpty()) lines << "Transit snapshot unavailable or not calculated.";
    }
    QApplication::clipboard()->setText(lines.join('\n')); status_->setText(schedule ? "Expanded schedule copied." : "Active periods and transit snapshot copied.");
}
} // namespace dracoved
