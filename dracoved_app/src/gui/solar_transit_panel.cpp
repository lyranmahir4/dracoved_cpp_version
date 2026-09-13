#include "solar_transit_panel.h"
#include "solar_transit_table_item.h"
#include "chart_wheel_widget.h"
#include "transit_calc_service.h"
#include "../core/formatting.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTabWidget>
#include <QThread>
#include <QTimeZone>
#include <QToolTip>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QCryptographicHash>
#include <QSignalBlocker>
#include <QLocale>
#include <QMap>
#include <QMenu>
#include <QAction>
#include <QDateEdit>
#include <algorithm>

namespace dracoved {
namespace {
const QStringList planets = {"Sun", "Moon", "Mercury", "Venus", "Mars", "Jupiter", "Saturn", "Uranus", "Neptune", "Pluto"};
const QStringList slowPlanets = {"Jupiter", "Saturn", "Uranus", "Neptune", "Pluto"};
QColor aspectColor(int angle) {
    switch (angle) {
    case 0: return QColor("#C99A2E");
    case 60: return QColor("#3FA7D6");
    case 90: return QColor("#E0533D");
    case 120: return QColor("#3FA66A");
    case 180: return QColor("#9B59B6");
    }
    return QColor("#6e859a");
}
QString laneKey(const QString& body, const QString& target, int aspect) {
    return body + " → " + target + " · " + solarTransitAspectLabel(aspect);
}
QString keyFor(const SolarTransitSource& source) {
    QString text = source.returnChart.utcDateTime.toString(Qt::ISODateWithMs)
        + source.returnInput.name + source.returnInput.timezone + source.natalInput.name
        + source.natalInput.date.toString(Qt::ISODate) + source.natalInput.time.toString(Qt::ISODate)
        + QString::number(source.returnInput.latitude, 'g', 17) + QString::number(source.returnInput.longitude, 'g', 17)
        + QString::number(int(source.returnChart.zodiacSystem)) + QString::number(int(source.returnChart.siderealAyanamsa))
        + QString::number(int(source.returnInput.houseSystem)) + QString::number(source.tajaka)
        + QString::number(source.natalSunLongitude, 'g', 17) + QString::number(source.returnYear)
        + lunarNodePolicySummary(source.returnChart.lunarNodePolicy);
    for (const auto& body : source.returnChart.bodies) text += body.name + QString::number(body.longitude, 'g', 17);
    for (const auto& cusp : source.returnChart.cusps) text += QString::number(cusp.longitude, 'g', 17);
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QTreeWidgetItem* category(QTreeWidget* tree, const QString& label) {
    auto* item = new QTreeWidgetItem(tree, {label});
    item->setFlags(Qt::ItemIsEnabled);
    QFont font = item->font(0); font.setBold(true); item->setFont(0, font);
    item->setExpanded(label == "Planets" || label == "Planets and nodes" || label == "Angles");
    return item;
}
void choice(QTreeWidgetItem* parent, const QString& label, const QString& name, bool checked, double longitude = 0) {
    auto* item = new QTreeWidgetItem(parent, {label});
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
    item->setData(0, Qt::UserRole, name);
    item->setData(0, Qt::UserRole + 1, longitude);
    item->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
}
template<class F> void eachChoice(QTreeWidget* tree, F action) {
    for (int group = 0; group < tree->topLevelItemCount(); ++group) {
        auto* parent = tree->topLevelItem(group);
        for (int row = 0; row < parent->childCount(); ++row) action(parent->child(row));
    }
}
QString motion(double speed) { return std::abs(speed) < 1e-5 ? "Station" : speed < 0 ? "Retrograde" : "Direct"; }
QString clean(QString value) { value.replace('\t', ' '); value.replace('\n', ' '); value.replace('\r', ' '); return value; }
QString localStamp(const QDateTime& utc, const SolarTransitSource& source) {
    return QLocale::c().toString(utc.toTimeZone(source.returnChart.localDateTime.timeZone()), "d MMM yyyy · h:mm:ss AP");
}
} // namespace

class SolarTransitTimeline final : public QWidget {
public:
    std::function<void(int)> eventSelected;
    explicit SolarTransitTimeline(QWidget* parent = nullptr) : QWidget(parent) {
        setMouseTracking(true);
        setMinimumWidth(520);
    }
    void setResult(std::shared_ptr<SolarTransitResult> result) {
        result_ = std::move(result);
        lanes_.clear();
        laneIndices_.clear();
        if (result_) {
            for (const auto& event : result_->events) addLane(event.houseCrossing ? event.body + " → SR houses" : laneKey(event.body, event.target, event.aspect));
            for (const auto& window : result_->windows) addLane(laneKey(window.body, window.target, window.aspect));
        }
        setMinimumHeight(std::max(160, 70 + int(lanes_.size()) * rowHeight));
        update();
    }
    void setSelected(int index) { selected_ = index; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), palette().base());
        painter.setPen(palette().text().color());
        if (!result_ || !result_->startUtc.isValid() || lanes_.isEmpty()) {
            painter.drawText(rect().adjusted(15, 15, -15, -15), Qt::AlignTop | Qt::TextWordWrap, result_
                ? "No qualifying contacts or orb periods for these selections."
                : "Generate the return-year contacts to see active periods and marked dates.");
            return;
        }
        painter.drawText(12, 19, "Bars: within orb    ◆ exact    ○ other dates    ■ station");
        const int ticks = width() - leftMargin < 520 ? 6 : 12;
        for (int month = 0; month <= ticks; ++month) {
            const double fraction = double(month) / ticks;
            const int x = leftMargin + qRound(fraction * (width() - leftMargin - 20));
            painter.setPen(palette().mid().color());
            painter.drawLine(x, 49, x, height());
            const auto date = result_->startUtc.addMSecs(qRound64(fraction * result_->startUtc.msecsTo(result_->endUtc)))
                .toTimeZone(result_->query.source.returnChart.localDateTime.timeZone());
            painter.setPen(palette().text().color());
            painter.drawText(QRect(x - 28, 27, 70, 20), Qt::AlignLeft, QLocale::c().toString(date, "d MMM"));
        }
        const QRect visible = visibleRegion().boundingRect();
        for (int lane = 0; lane < lanes_.size(); ++lane) {
            const int y = 65 + lane * rowHeight;
            if (y < visible.top() - rowHeight || y > visible.bottom() + rowHeight) continue;
            painter.setPen(palette().text().color());
            painter.drawText(QRect(8, y - 12, leftMargin - 16, 24), Qt::AlignVCenter,
                painter.fontMetrics().elidedText(lanes_[lane], Qt::ElideRight, leftMargin - 20));
            painter.setPen(palette().midlight().color());
            painter.drawLine(leftMargin, y + 13, width(), y + 13);
        }
        for (const auto& window : result_->windows) {
            const int lane = laneIndices_.value(laneKey(window.body, window.target, window.aspect));
            const int y = 65 + lane * rowHeight;
            if (y < visible.top() - rowHeight || y > visible.bottom() + rowHeight) continue;
            QColor color = aspectColor(window.aspect); color.setAlpha(100);
            painter.setPen(Qt::NoPen); painter.setBrush(color);
            painter.drawRoundedRect(QRectF(xFor(window.startUtc), y - 7, std::max(2.0, xFor(window.endUtc) - xFor(window.startUtc)), 14), 3, 3);
        }
        for (int index = 0; index < result_->events.size(); ++index) {
            const auto& event = result_->events[index];
            const int lane = laneIndices_.value(event.houseCrossing ? event.body + " → SR houses" : laneKey(event.body, event.target, event.aspect));
            const double x = xFor(event.utc), y = 65 + lane * rowHeight;
            if (y < visible.top() - rowHeight || y > visible.bottom() + rowHeight) continue;
            const QColor color = event.houseCrossing ? QColor("#43899c") : aspectColor(event.aspect);
            painter.setPen(QPen(index == selected_ ? palette().text().color() : color, index == selected_ ? 2 : 1));
            painter.setBrush(event.exact ? color : palette().base().color());
            if (event.exact) painter.drawPolygon(QPolygonF{QPointF(x, y - 5), QPointF(x + 5, y), QPointF(x, y + 5), QPointF(x - 5, y)});
            else if (event.kind.contains("Station")) painter.drawRect(QRectF(x - 3, y - 3, 6, 6));
            else painter.drawEllipse(QPointF(x, y), 3, 3);
        }
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        const int index = hit(event->position());
        if (index >= 0) {
            const auto& item = result_->events[index];
            QToolTip::showText(event->globalPosition().toPoint(), item.body + " → " + item.target + "\n" + item.kind
                + "\n" + localStamp(item.utc, result_->query.source) + QString("\nOrb %1°").arg(item.orb, 0, 'f', 6), this);
        } else {
            const int lane = int((event->position().y() - 50) / rowHeight);
            if (event->position().y() >= 50 && lane >= 0 && lane < lanes_.size())
                QToolTip::showText(event->globalPosition().toPoint(), lanes_[lane], this);
            else QToolTip::hideText();
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
        const int index = hit(event->position());
        if (index >= 0 && eventSelected) eventSelected(index);
    }
private:
    void addLane(const QString& key) {
        if (laneIndices_.contains(key)) return;
        laneIndices_.insert(key, lanes_.size()); lanes_ << key;
    }
    double xFor(const QDateTime& utc) const {
        return leftMargin + double(result_->startUtc.msecsTo(utc)) / result_->startUtc.msecsTo(result_->endUtc) * (width() - leftMargin - 20);
    }
    int hit(const QPointF& point) const {
        if (!result_ || point.y() < 50) return -1;
        const int lane = int((point.y() - 50) / rowHeight);
        int best = -1;
        double distance = 10;
        for (int i = 0; i < result_->events.size(); ++i) {
            const auto& event = result_->events[i];
            const int eventLane = laneIndices_.value(event.houseCrossing ? event.body + " → SR houses" : laneKey(event.body, event.target, event.aspect));
            if (eventLane != lane) continue;
            const double dx = std::abs(xFor(event.utc) - point.x());
            auto priority = [](const SolarTransitEvent& item) { return item.exact ? 3 : item.kind.contains("Near miss") ? 2 : item.kind.startsWith("Station") ? 1 : 0; };
            if (dx < distance || (std::abs(dx - distance) < 1e-4 && best >= 0 && priority(event) > priority(result_->events[best]))) {
                distance = dx; best = i;
            }
        }
        return best;
    }
    static constexpr int leftMargin = 220;
    static constexpr int rowHeight = 30;
    std::shared_ptr<SolarTransitResult> result_;
    QStringList lanes_;
    QMap<QString, int> laneIndices_;
    int selected_ = -1;
};

SolarTransitPanel::SolarTransitPanel(SourceProvider provider, QWidget* parent)
    : QWidget(parent), provider_(std::move(provider)) {
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    // The scroll viewport handles small sizes. Do not let this inactive page's
    // content minimum constrain every other page in the shared data stack.
    outerLayout->setSizeConstraint(QLayout::SetNoConstraint);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto* contentScroll = new QScrollArea(this);
    contentScroll->setObjectName("solarTransitContentScroll");
    contentScroll->setWidgetResizable(true);
    contentScroll->setFrameShape(QFrame::NoFrame);
    outerLayout->addWidget(contentScroll);
    auto* content = new QWidget(contentScroll);
    auto* layout = new QVBoxLayout(content);
    layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
    contentScroll->setWidget(content);
    sourceLabel_ = new QLabel("Calculate a Solar Return, then open Transits to Return.", this);
    sourceLabel_->setWordWrap(true); sourceLabel_->setTextFormat(Qt::PlainText);
    layout->addWidget(sourceLabel_);
    auto* filterDialog = new QDialog(this);
    filterDialog->setObjectName("solarTransitFilterDialog");
    filterDialog->setWindowTitle("Transits to Return — Filters");
    filterDialog->resize(380, 680);
    auto* filterDialogLayout = new QVBoxLayout(filterDialog);
    auto* filterScroll = new QScrollArea(filterDialog);
    filterDialogLayout->addWidget(filterScroll);
    filterScroll->setWidgetResizable(true);
    filters_ = new QWidget(filterScroll);
    auto* form = new QVBoxLayout(filters_);
    form->addWidget(new QLabel("Moving transit bodies", filters_));
    bodies_ = new QTreeWidget(filters_); bodies_->setObjectName("solarTransitBodies"); bodies_->setHeaderHidden(true); bodies_->setMinimumHeight(160);
    auto* planetGroup = category(bodies_, "Planets");
    auto* nodeGroup = category(bodies_, "Nodes");
    auto* otherGroup = category(bodies_, "Other bodies");
    for (const auto& body : transitcalc::transitCalculableBodyOrder()) {
        auto* group = planets.contains(body) ? planetGroup : isLunarNodeName(body) ? nodeGroup : otherGroup;
        choice(group, body, body, slowPlanets.contains(body) || body == "Mars");
    }
    form->addWidget(bodies_);
    auto* presets = new QHBoxLayout();
    for (const QString& name : {QString("All planets"), QString("Slow planets"), QString("Clear")}) {
        auto* button = new QPushButton(name, filters_); presets->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, name]() {
            eachChoice(bodies_, [&](QTreeWidgetItem* item) {
                const auto body = item->data(0, Qt::UserRole).toString();
                item->setCheckState(0, (name == "All planets" ? planets.contains(body) : name == "Slow planets" && slowPlanets.contains(body)) ? Qt::Checked : Qt::Unchecked);
            });
        });
    }
    form->addLayout(presets);
    form->addWidget(new QLabel("Fixed solar return targets", filters_));
    targets_ = new QTreeWidget(filters_); targets_->setObjectName("solarTransitTargets"); targets_->setHeaderHidden(true); targets_->setMinimumHeight(200);
    form->addWidget(targets_);
    auto* targetButtons = new QHBoxLayout();
    auto* allTargets = new QPushButton("Select all", filters_);
    auto* noTargets = new QPushButton("Clear", filters_);
    targetButtons->addWidget(allTargets); targetButtons->addWidget(noTargets); form->addLayout(targetButtons);
    connect(allTargets, &QPushButton::clicked, this, [this]() { eachChoice(targets_, [](auto* item) { item->setCheckState(0, Qt::Checked); }); });
    connect(noTargets, &QPushButton::clicked, this, [this]() { eachChoice(targets_, [](auto* item) { item->setCheckState(0, Qt::Unchecked); }); });
    auto* options = new QFormLayout();
    aspects_ = new QComboBox(filters_); aspects_->setObjectName("solarTransitSearchAspects");
    aspects_->addItem("Conjunction", 0); aspects_->addItem("Sextile", 60); aspects_->addItem("Square", 90);
    aspects_->addItem("Trine", 120); aspects_->addItem("Opposition", 180); aspects_->addItem("All major aspects", -1);
    aspects_->setCurrentIndex(aspects_->findData(-1));
    aspects_->setToolTip("Aspects to calculate when Generate return year is clicked. Activity filters only the saved results.");
    orb_ = new QDoubleSpinBox(filters_); orb_->setRange(.01, 10); orb_->setDecimals(2); orb_->setValue(1); orb_->setSuffix("°");
    options->addRow("Generate aspects", aspects_); options->addRow("Proximity orb", orb_); form->addLayout(options);
    houses_ = new QCheckBox("Also find SR house ingress / egress", filters_); form->addWidget(houses_);
    auto* hint = new QLabel("Exact contacts, orb periods and near misses are calculated separately. The year ends at the next exact return. Lots and house cusps are optional targets.", filters_);
    hint->setWordWrap(true); form->addWidget(hint); form->addStretch(); filterScroll->setWidget(filters_);
    auto* closeFilters = new QPushButton("Done", filterDialog);
    filterDialogLayout->addWidget(closeFilters);
    connect(closeFilters, &QPushButton::clicked, filterDialog, &QDialog::hide);
    auto* resultsPanel = new QWidget(content);
    resultsPanel->setMinimumWidth(520);
    layout->addWidget(resultsPanel, 1);
    auto* resultsLayout = new QVBoxLayout(resultsPanel);
    resultsLayout->setContentsMargins(0, 0, 0, 0);
    auto* actions = new QHBoxLayout();
    auto* editFilters = new QPushButton("Filters…", resultsPanel);
    editFilters->setObjectName("solarTransitEditFilters");
    actions->addWidget(editFilters);
    connect(editFilters, &QPushButton::clicked, filterDialog, [filterDialog]() {
        filterDialog->show(); filterDialog->raise(); filterDialog->activateWindow();
    });
    run_ = new QPushButton("Generate return year", resultsPanel); stop_ = new QPushButton("Stop", resultsPanel); stop_->setEnabled(false);
    auto* copyAll = new QPushButton("Copy results", resultsPanel);
    auto* copySelected = new QPushButton("Copy selected", resultsPanel);
    actions->addWidget(run_); actions->addWidget(stop_); actions->addStretch(); actions->addWidget(copySelected); actions->addWidget(copyAll);
    resultsLayout->addLayout(actions);
    progress_ = new QProgressBar(resultsPanel); progress_->setRange(0, 100); progress_->setValue(0); progress_->setMaximumHeight(16);
    resultsLayout->addWidget(progress_);
    statusLabel_ = new QLabel("Ready", resultsPanel); statusLabel_->setObjectName("solarTransitStatus"); statusLabel_->setWordWrap(true); statusLabel_->setTextFormat(Qt::PlainText); resultsLayout->addWidget(statusLabel_);
    summaryLabel_ = new QLabel(resultsPanel); summaryLabel_->setWordWrap(true); summaryLabel_->setTextFormat(Qt::PlainText); resultsLayout->addWidget(summaryLabel_);
    summaryLabel_->hide();
    auto* workspaceTools = new QHBoxLayout();
    auto* expandResults = new QPushButton("Expand results", resultsPanel);
    expandResults->setObjectName("solarTransitExpandResults"); expandResults->setCheckable(true);
    workspaceTools->addWidget(expandResults);
    connect(expandResults, &QPushButton::toggled, this, [this, expandResults](bool expanded) {
        expandResults->setText(expanded ? "Restore layout" : "Expand results");
        if (expandResultsChanged) expandResultsChanged(expanded);
    });
    auto* showSummary = new QCheckBox("Show summary", resultsPanel);
    workspaceTools->addWidget(showSummary); workspaceTools->addStretch();
    connect(showSummary, &QCheckBox::toggled, summaryLabel_, &QWidget::setVisible);
    resultsLayout->addLayout(workspaceTools);
    views_ = new QTabWidget(resultsPanel); views_->setMinimumHeight(320); resultsLayout->addWidget(views_, 1);
    auto* datePage = new QWidget(views_); auto* dateLayout = new QVBoxLayout(datePage);
    eventFilter_ = new QPushButton("Event types: all", datePage);
    eventFilter_->setObjectName("solarTransitEventTypes");
    eventMenu_ = new QMenu(eventFilter_); eventMenu_->setObjectName("solarTransitEventMenu");
    const QStringList categories{"Exact contacts", "Orb entries", "Orb exits", "Already within orb / period events",
        "Near misses", "Year-end approaches", "Stations", "House crossings"};
    for (int i = 0; i < categories.size(); ++i) {
        auto* action = eventMenu_->addAction(categories[i]); action->setCheckable(true); action->setChecked(true);
        action->setData(1u << i); action->setObjectName(QString("solarTransitEventType%1").arg(i));
        connect(action, &QAction::toggled, this, [this, i](bool checked) {
            if (checked) eventVisibility_ |= 1u << i; else eventVisibility_ &= ~(1u << i);
            renderResults();
        });
    }
    eventMenu_->addSeparator();
    for (const auto& preset : {qMakePair(QString("Exact only"), 1u), qMakePair(QString("Show all"), 255u), qMakePair(QString("Reset"), 255u)}) {
        auto* action = eventMenu_->addAction(preset.first);
        connect(action, &QAction::triggered, this, [this, preset]() { eventVisibility_ = preset.second; updateEventMenu(); renderResults(); });
    }
    eventMenu_->addSeparator();
    connect(eventMenu_->addAction("Copy visible dates"), &QAction::triggered, this, &SolarTransitPanel::copyVisibleDates);
    eventFilter_->setMenu(eventMenu_);
    dateLayout->addWidget(eventFilter_);
    dates_ = new QTableWidget(datePage); dates_->setObjectName("solarTransitDates"); dates_->setSelectionBehavior(QAbstractItemView::SelectRows);
    dates_->setSelectionMode(QAbstractItemView::SingleSelection); dates_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    dates_->setAlternatingRowColors(true); dates_->verticalHeader()->hide();
    dates_->setColumnCount(9);
    dates_->setHorizontalHeaderLabels({"Date", "Local time", "Transit", "SR target", "Aspect", "Event", "Orb", "Motion / pass", "Until"});
    dates_->horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
    dates_->setSortingEnabled(true);
    dates_->horizontalHeader()->setToolTip("Click a heading to sort; click again to reverse. Date restores chronological order.");
    dates_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(dates_, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        eventMenu_->popup(dates_->viewport()->mapToGlobal(position));
    });
    dateLayout->addWidget(dates_); views_->addTab(datePage, "Dates");
    auto* timelineScroll = new QScrollArea(views_); timelineScroll->setWidgetResizable(true);
    timeline_ = new SolarTransitTimeline(timelineScroll); timelineScroll->setWidget(timeline_); views_->addTab(timelineScroll, "Year timeline");
    views_->addTab(createActivityPage(), "Activity");
    detailLabel_ = new QLabel("Select a marked date to display Solar Return + Transit on the main chart.", resultsPanel);
    detailLabel_->setWordWrap(true); detailLabel_->setTextFormat(Qt::PlainText); detailLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    resultsLayout->addWidget(detailLabel_);
    connect(run_, &QPushButton::clicked, this, &SolarTransitPanel::startSearch);
    connect(stop_, &QPushButton::clicked, this, [this]() { if (cancelled_) cancelled_->store(true); statusLabel_->setText("Stopping..."); });
    connect(copyAll, &QPushButton::clicked, this, [this]() { copyResults(false); });
    connect(copySelected, &QPushButton::clicked, this, [this]() { copyResults(true); });
    connect(dates_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (row >= 0 && dates_->item(row, 0)) selectEvent(dates_->item(row, 0)->data(Qt::UserRole).toInt());
    });
    timeline_->eventSelected = [this](int index) {
        if (!result_ || index < 0 || index >= result_->events.size()) return;
        eventVisibility_ |= solarEventCategory(result_->events[index]);
        updateEventMenu(); renderResults();
        for (int row = 0; row < dates_->rowCount(); ++row) {
            if (dates_->item(row, 0)->data(Qt::UserRole).toInt() == index) { dates_->selectRow(row); break; }
        }
        selectEvent(index);
    };
    auto changed = [this]() { if (result_ && !worker_) statusLabel_->setText("Filters changed. Generate again to update the saved results."); };
    connect(bodies_, &QTreeWidget::itemChanged, this, [changed](QTreeWidgetItem*, int) { changed(); });
    connect(targets_, &QTreeWidget::itemChanged, this, [changed](QTreeWidgetItem*, int) { changed(); });
    connect(aspects_, &QComboBox::currentIndexChanged, this, [changed](int) { changed(); });
    connect(orb_, &QDoubleSpinBox::valueChanged, this, [changed](double) { changed(); });
    connect(houses_, &QCheckBox::toggled, this, [changed](bool) { changed(); });
}

SolarTransitPanel::~SolarTransitPanel() {
    if (cancelled_) cancelled_->store(true);
    if (worker_) worker_->wait();
}

void SolarTransitPanel::refreshSource() {
    SolarTransitSource next;
    QString error;
    sourceValid_ = provider_(&next, &error);
    if (!sourceValid_) {
        sourceLabel_->setText(error + (result_ ? "\nDisplayed results retain their previous return-year snapshot." : QString()));
        run_->setEnabled(false);
        return;
    }
    const QString key = keyFor(next);
    if (key != sourceKey_) {
        if (cancelled_ && worker_) cancelled_->store(true);
        sourceKey_ = key; source_ = next;
        result_.reset(); selectedEvent_ = -1; selectedContact_.reset(); timeline_->setResult(nullptr);
        renderActivity();
        { const QSignalBlocker block(dates_); dates_->setRowCount(0); }
        summaryLabel_->clear(); detailLabel_->setText("Generate contacts for this return year.");
        populateTargets();
    }
    sourceLabel_->setText(QString("Fixed reference: %1 · %2 · %3 · %4\n%5 · %6 · %7")
        .arg(source_.returnInput.name, localStamp(source_.returnChart.utcDateTime, source_), source_.returnChart.timezoneLabel, source_.location)
        .arg(source_.tajaka ? "Tajaka return timing" : "Standard return timing")
        .arg(source_.returnChart.zodiacSystem == ZodiacSystem::Sidereal ? "Sidereal · " + siderealAyanamsaToString(source_.returnChart.siderealAyanamsa) : "Tropical")
        .arg(source_.returnInput.houseSystem == HouseSystem::Placidus ? "Placidus houses" : "Whole Sign houses"));
    run_->setEnabled(!worker_);
}

void SolarTransitPanel::populateTargets() {
    const QSignalBlocker block(targets_); targets_->clear();
    auto* core = category(targets_, "Planets and nodes");
    auto* angles = category(targets_, "Angles");
    auto* other = category(targets_, "Other points");
    auto* cusps = category(targets_, "House cusps");
    auto* lots = category(targets_, "Arabic Lots");
    for (const auto& body : source_.returnChart.bodies) {
        auto* group = isArabicLotName(body.name) ? lots : planets.contains(body.name) || isLunarNodeName(body.name) ? core : other;
        choice(group, lunarNodeDisplayName(body.name, source_.returnChart.lunarNodePolicy) + " · " + formatDegInSign(body.longitude), body.name, group == core, body.longitude);
    }
    const auto& a = source_.returnChart.angles;
    for (const auto& target : QVector<SolarTransitTarget>{{"Ascendant", a.asc}, {"Midheaven", a.mc}, {"Descendant", a.desc}, {"IC", a.ic}})
        choice(angles, target.name + " · " + formatDegInSign(target.longitude), target.name, true, target.longitude);
    for (int house = 1; house <= 12; ++house) {
        double longitude = -1;
        if (source_.returnInput.houseSystem == HouseSystem::WholeSign)
            longitude = search_events::wrap(std::floor(a.asc / 30) * 30 + (house - 1) * 30);
        else for (const auto& cusp : source_.returnChart.cusps) if (cusp.number == house) longitude = cusp.longitude;
        if (longitude >= 0) choice(cusps, QString("House %1 · %2").arg(house).arg(formatDegInSign(longitude)), QString("House %1 cusp").arg(house), false, longitude);
    }
}

void SolarTransitPanel::startSearch() {
    if (worker_) return;
    refreshSource();
    if (!sourceValid_) return;
    SolarTransitQuery query; query.source = source_; query.orb = orb_->value(); query.houses = houses_->isChecked();
    eachChoice(bodies_, [&](auto* item) { if (item->checkState(0) == Qt::Checked) query.bodies << item->data(0, Qt::UserRole).toString(); });
    eachChoice(targets_, [&](auto* item) { if (item->checkState(0) == Qt::Checked) query.targets.push_back({item->data(0, Qt::UserRole).toString(), item->data(0, Qt::UserRole + 1).toDouble()}); });
    const int aspect = aspects_->currentData().toInt();
    query.aspects = aspect < 0 ? QVector<int>{0, 60, 90, 120, 180} : QVector<int>{aspect};
    if (query.bodies.isEmpty() || (query.targets.isEmpty() && !query.houses)) {
        statusLabel_->setText("Select moving bodies and at least one return target or house crossings."); return;
    }
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    const auto cancel = cancelled_;
    const QString key = sourceKey_;
    auto output = std::make_shared<SolarTransitResult>();
    filters_->setEnabled(false); run_->setEnabled(false); stop_->setEnabled(true); progress_->setValue(0);
    statusLabel_->setText("Calculating the return year...");
    worker_ = QThread::create([this, query, cancel, output]() {
        *output = calculateSolarTransits(query, [cancel]() { return cancel->load(); }, [this, cancel](int percent, const QString& status) {
            QMetaObject::invokeMethod(this, [this, cancel, percent, status]() {
                if (cancel != cancelled_ || cancel->load()) return;
                progress_->setValue(percent); statusLabel_->setText(status);
            }, Qt::QueuedConnection);
        });
    });
    worker_->setParent(this);
    connect(worker_, &QThread::finished, this, [this, output, key]() {
        worker_ = nullptr; filters_->setEnabled(true); stop_->setEnabled(false); run_->setEnabled(sourceValid_);
        if (output->cancelled || key != sourceKey_) { statusLabel_->setText("Cancelled. No partial year was committed."); return; }
        if (!output->error.isEmpty()) { statusLabel_->setText(output->error); return; }
        result_ = output; selectedEvent_ = -1; selectedContact_.reset(); progress_->setValue(100);
        {
            const QSignalBlocker fromBlock(activityFrom_), throughBlock(activityThrough_);
            const auto zone = output->query.source.returnChart.localDateTime.timeZone();
            activityFrom_->setDate(output->startUtc.toTimeZone(zone).date());
            activityThrough_->setDate(output->endUtc.addMSecs(-1).toTimeZone(zone).date());
        }
        timeline_->setResult(result_); renderResults();
        renderActivity();
        summaryLabel_->setText(QString("%1 → %2 (%3)\n%4 marked dates · %5 orb periods · %6 of %7 contacts never enter the selected %8° orb. End-of-year approaches are boundary measurements.")
            .arg(localStamp(output->startUtc, output->query.source), localStamp(output->endUtc, output->query.source), output->query.source.returnChart.timezoneLabel)
            .arg(output->events.size()).arg(output->windows.size()).arg(output->inactivePairs).arg(output->searchedPairs).arg(output->query.orb));
        statusLabel_->setText(output->warnings.isEmpty() ? "Done. Results retain the return chart and filters used for this search."
            : "Done with warnings: " + output->warnings.join("; "));
        if (dates_->rowCount() > 0) dates_->selectRow(0);
        else if (selectionChanged) selectionChanged();
    });
    connect(worker_, &QThread::finished, worker_, &QObject::deleteLater);
    worker_->start();
}

void SolarTransitPanel::renderResults() {
    QSignalBlocker block(dates_);
    dates_->setRowCount(0);
    if (!result_) { updateEventMenu(); return; }
    dates_->setSortingEnabled(false);
    QTableWidgetItem* selectedItem = nullptr;
    for (int index = 0; index < result_->events.size(); ++index) {
        const auto& event = result_->events[index];
        if (!(eventVisibility_ & solarEventCategory(event))) continue;
        const auto local = event.utc.toTimeZone(result_->query.source.returnChart.localDateTime.timeZone());
        const int row = dates_->rowCount(); dates_->insertRow(row);
        QString pass = motion(event.speed);
        if (event.totalPasses > 0) pass += QString(" · %1/%2").arg(event.pass).arg(event.totalPasses);
        const QStringList values{QLocale::c().toString(local, "d MMM yyyy"), local.toString("h:mm:ss AP"), event.body, event.target,
            event.houseCrossing ? "—" : solarTransitAspectLabel(event.aspect), event.kind,
            event.houseCrossing ? "—" : QString::number(event.orb, 'f', 6) + "°", pass,
            event.untilUtc.isValid() ? localStamp(event.untilUtc, result_->query.source) : QString()};
        for (int col = 0; col < values.size(); ++col) {
            const double missing = std::numeric_limits<double>::infinity();
            QTableWidgetItem* item = nullptr;
            if (col == 0) item = new SolarTransitTableItem(values[col], event.utc.toMSecsSinceEpoch());
            else if (col == 1) item = new SolarTransitTableItem(values[col], local.time().msecsSinceStartOfDay());
            else if (col == 4) item = new SolarTransitTableItem(values[col], event.houseCrossing ? missing : event.aspect);
            else if (col == 6) item = new SolarTransitTableItem(values[col], event.houseCrossing ? missing : event.orb);
            else if (col == 8) item = new SolarTransitTableItem(values[col], event.untilUtc.isValid() ? event.untilUtc.toMSecsSinceEpoch() : missing);
            else item = new QTableWidgetItem(values[col]);
            item->setToolTip(values[col]); dates_->setItem(row, col, item);
        }
        dates_->item(row, 0)->setData(Qt::UserRole, index);
        dates_->item(row, 4)->setForeground(aspectColor(event.aspect));
        if (index == selectedEvent_) selectedItem = dates_->item(row, 0);
    }
    dates_->setSortingEnabled(true);
    dates_->resizeColumnsToContents(); dates_->horizontalHeader()->setStretchLastSection(true);
    if (selectedItem) dates_->selectRow(selectedItem->row());
    block.unblock();
    if (selectedEvent_ >= 0 && !selectedItem) {
        selectedEvent_ = -1; selectedContact_.reset(); timeline_->setSelected(-1);
        detailLabel_->setText("The selected date is hidden. Select a visible contact to update the main chart.");
        if (selectionChanged) selectionChanged();
    }
    updateEventMenu();
}

void SolarTransitPanel::updateEventMenu() {
    int count = 0;
    for (auto* action : eventMenu_->actions()) {
        const unsigned bit = action->data().toUInt();
        if (!bit) continue;
        const QSignalBlocker block(action); action->setChecked(eventVisibility_ & bit);
        if (eventVisibility_ & bit) ++count;
    }
    eventFilter_->setText(QString("Event types: %1 · %2 / %3 dates")
        .arg(count == 8 ? "all" : eventVisibility_ == 1 ? "exact only" : QString("%1 of 8").arg(count))
        .arg(dates_->rowCount()).arg(result_ ? result_->events.size() : 0));
    eventFilter_->setToolTip("Choose event types here or right-click the table. Activity counts are independent of hidden date rows.");
}

void SolarTransitPanel::copyVisibleDates() {
    QStringList lines;
    QStringList headers;
    for (int column = 0; column < dates_->columnCount(); ++column) headers << dates_->horizontalHeaderItem(column)->text();
    lines << headers.join('\t');
    for (int row = 0; row < dates_->rowCount(); ++row) {
        QStringList fields;
        for (int column = 0; column < dates_->columnCount(); ++column) fields << clean(dates_->item(row, column)->text());
        lines << fields.join('\t');
    }
    QApplication::clipboard()->setText(lines.join('\n'));
    statusLabel_->setText(QString("Copied %1 visible dates.").arg(dates_->rowCount()));
}

void SolarTransitPanel::selectEvent(int index) {
    if (!result_ || index < 0 || index >= result_->events.size()) return;
    if (selectedEvent_ == index) return;
    selectContact(result_->events[index], index);
}

void SolarTransitPanel::selectContact(const SolarTransitEvent& event, int index) {
    if (!result_) return;
    selectedEvent_ = index; selectedContact_ = event; timeline_->setSelected(index);
    NatalChart transit; QString error;
    if (!computeSolarTransitMoment(result_->query.source, event.utc, &transit, &error)) {
        selectedEvent_ = -1;
        selectedContact_.reset();
        statusLabel_->setText("Could not render the selected moment: " + error);
        if (selectionChanged) selectionChanged();
        return;
    }
    selectedTransit_ = std::move(transit);
    if (selectedContact_->kind == "Active within orb") {
        selectedContact_->orb = std::numeric_limits<double>::quiet_NaN();
        selectedContact_->speed = std::numeric_limits<double>::quiet_NaN();
        for (const auto& body : selectedTransit_.bodies) if (body.name == selectedContact_->body) {
            const double separation = std::abs(search_events::signedAngle(body.longitude - selectedContact_->targetLongitude));
            selectedContact_->orb = std::abs(separation - selectedContact_->aspect);
            if (body.hasSpeed) selectedContact_->speed = body.speed;
            break;
        }
    }
    const auto& selected = *selectedContact_;
    detailLabel_->setText(QString("%1 · %2 (%3)\n%4 → SR %5 at %6 · %7\nOrb %8 · %9. %10")
        .arg(selected.kind, localStamp(selected.utc, result_->query.source), result_->query.source.returnChart.timezoneLabel)
        .arg(selected.body, selected.target, formatDegInSign(selected.targetLongitude), selected.houseCrossing ? "House boundary" : solarTransitAspectLabel(selected.aspect))
        .arg(std::isfinite(selected.orb) ? QString::number(selected.orb, 'f', 6) + "°" : "unavailable")
        .arg(std::isfinite(selected.speed) ? motion(selected.speed) : "Motion unavailable", selected.note));
    if (selectionChanged) selectionChanged();
}

bool SolarTransitPanel::renderSelectedChart(ChartWheelWidget* wheel, const AspectOrbs& orbs, double maxOrb) const {
    if (!wheel || !sourceValid_ || !result_ || !selectedContact_) return false;
    const auto& event = *selectedContact_;
    wheel->setOverlayCharts(result_->query.source.returnChart, selectedTransit_, result_->query.source.returnInput.houseSystem, orbs);
    wheel->setBaseLabel("Solar Return"); wheel->setOverlayLabel("Transit");
    wheel->setOverlayAspectScopes(true, false, false); wheel->setAspectDisplayMaxOrb(maxOrb);
    wheel->setChartNote(event.body + " → SR " + event.target + " · " + event.kind);
    wheel->setHighlight(event.body, true, event.kind, aspectColor(event.aspect));
    return true;
}

const NatalChart* SolarTransitPanel::selectedTransitChart() const {
    return sourceValid_ && result_ && selectedContact_ ? &selectedTransit_ : nullptr;
}

const SolarTransitSource* SolarTransitPanel::selectedSource() const {
    return selectedTransitChart() ? &result_->query.source : nullptr;
}

void SolarTransitPanel::copyResults(bool selectedOnly) {
    if (!result_ || (selectedOnly && !selectedContact_)) { statusLabel_->setText("Generate contacts and select a result first."); return; }
    const auto& source = result_->query.source;
    QStringList lines{QString("Transits to %1 · %2 · %3").arg(source.returnInput.name, source.location, source.returnChart.timezoneLabel),
        QString("Return-year UTC: %1 to %2 (end excluded; year-end approaches explicitly marked)").arg(result_->startUtc.toString(Qt::ISODateWithMs), result_->endUtc.toString(Qt::ISODateWithMs)),
        QString("%1; %2; house system %3; proximity orb %4 degrees; %5")
            .arg(source.tajaka ? "Tajaka timing" : "Standard timing", source.returnChart.zodiacSystem == ZodiacSystem::Sidereal ? "Sidereal · " + siderealAyanamsaToString(source.returnChart.siderealAyanamsa) : "Tropical",
                 source.returnInput.houseSystem == HouseSystem::Placidus ? "Placidus" : "Whole Sign").arg(result_->query.orb).arg(lunarNodePolicySummary(source.returnChart.lunarNodePolicy)),
        "UTC\tLocal time\tTimezone\tTransit\tSR target\tTarget longitude\tAspect\tEvent\tOrb degrees\tSpeed degrees/day\tPass\tUntil UTC\tNotes"};
    const int exportCount = selectedOnly ? 1 : result_->events.size();
    for (int i = 0; i < exportCount; ++i) {
        const auto& event = selectedOnly ? *selectedContact_ : result_->events[i];
        QStringList fields{event.utc.toString(Qt::ISODateWithMs), localStamp(event.utc, source), source.returnChart.timezoneLabel,
            event.body, event.target, QString::number(event.targetLongitude, 'f', 8), event.houseCrossing ? "House crossing" : solarTransitAspectLabel(event.aspect),
            event.kind, QString::number(event.orb, 'f', 8), QString::number(event.speed, 'f', 8),
            event.totalPasses ? QString("%1/%2").arg(event.pass).arg(event.totalPasses) : QString(), event.untilUtc.toString(Qt::ISODateWithMs), event.note};
        for (auto& field : fields) field = clean(field);
        lines << fields.join('\t');
    }
    if (!selectedOnly) {
        lines << "" << "Orb periods" << "Transit\tSR target\tAspect\tStart UTC\tEnd UTC\tAlready active at start\tContinues at end";
        for (const auto& window : result_->windows) lines << QStringList{window.body, window.target, solarTransitAspectLabel(window.aspect),
            window.startUtc.toString(Qt::ISODateWithMs), window.endUtc.toString(Qt::ISODateWithMs), window.clippedStart ? "Yes" : "No", window.clippedEnd ? "Yes" : "No"}.join('\t');
    }
    if (!result_->warnings.isEmpty()) lines << "Warnings: " + result_->warnings.join("; ");
    QApplication::clipboard()->setText(lines.join('\n'));
    statusLabel_->setText(selectedOnly ? "Selected contact copied." : "All contacts and orb periods copied as tab-separated text.");
}
} // namespace dracoved
