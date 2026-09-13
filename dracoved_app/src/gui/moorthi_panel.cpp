#include "moorthi_panel.h"
#include "../core/formatting.h"
#include "../core/timezone_utils.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateEdit>
#include <QElapsedTimer>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace dracoved {
namespace {
constexpr double epochJd = 2440587.5;
class EntryItem : public QTableWidgetItem {
public:
    using QTableWidgetItem::QTableWidgetItem;
    bool operator<(const QTableWidgetItem& other) const override {
        if (data(Qt::UserRole).isValid() && other.data(Qt::UserRole).isValid())
            return data(Qt::UserRole).toDouble() < other.data(Qt::UserRole).toDouble();
        return text().localeAwareCompare(other.text()) < 0;
    }
};
QIcon metalIcon(Moorthi value) {
    const QColor colors[] = {QColor("#d8a521"), QColor("#aeb8c2"), QColor("#b8683d"), QColor("#505a65")};
    const QColor color = colors[int(value)];
    QPixmap pix(40, 28); pix.fill(Qt::transparent); pix.setDevicePixelRatio(2);
    QPainter p(&pix); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(color.darker(160)); p.setBrush(color);
    p.drawPolygon(QPolygonF{QPointF(2, 11), QPointF(4, 5), QPointF(15, 3), QPointF(18, 9), QPointF(8, 12)});
    p.setBrush(color.lighter(135));
    p.drawPolygon(QPolygonF{QPointF(4, 5), QPointF(15, 3), QPointF(16, 6), QPointF(6, 8)});
    return QIcon(pix);
}
}

MoorthiPanel::MoorthiPanel(SwissEph* swe, QWidget* parent) : QWidget(parent), swe_(swe) {
    setObjectName("moorthiPanel");
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(6);
    auto* title = new QLabel("Moorthi Nirnaya · sign entries", this);
    QFont font = title->font(); font.setPointSizeF(font.pointSizeF() + 2);
    font.setWeight(QFont::DemiBold); title->setFont(font);
    auto* heading = new QHBoxLayout;
    heading->addWidget(title);
    heading->addStretch(1);
    layout->addLayout(heading);
    auto* controls = new QGridLayout; controls->setHorizontalSpacing(8);
    from_ = new QDateEdit(QDate(QDate::currentDate().year(), 1, 1), this);
    through_ = new QDateEdit(QDate(QDate::currentDate().year(), 12, 31), this);
    for (auto* date : {from_, through_}) {
        date->setCalendarPopup(true); date->setDisplayFormat("dd MMM yyyy");
        date->setMinimumWidth(130);
    }
    from_->setObjectName("moorthiFrom"); through_->setObjectName("moorthiThrough");
    planet_ = new QComboBox(this); planet_->setObjectName("moorthiPlanet");
    filter_ = new QComboBox(this); filter_->setObjectName("moorthiFilter");
    planet_->setMinimumWidth(120);
    filter_->setMinimumWidth(110);
    filter_->addItem("All metals", -1);
    for (int i = 0; i < 4; ++i) filter_->addItem(metalIcon(Moorthi(i)), moorthiName(Moorthi(i)), i);
    run_ = new QPushButton("Find entries", this); run_->setObjectName("moorthiRun");
    stop_ = new QPushButton("Stop", this); stop_->setEnabled(false);
    copy_ = new QPushButton("Copy results", this); copy_->setObjectName("moorthiCopy"); copy_->setEnabled(false);
    heading->addWidget(copy_);
    controls->addWidget(new QLabel("From", this), 0, 0); controls->addWidget(from_, 0, 1);
    controls->addWidget(new QLabel("Through", this), 0, 2); controls->addWidget(through_, 0, 3);
    controls->addWidget(new QLabel("Planet", this), 0, 4); controls->addWidget(planet_, 0, 5);
    controls->addWidget(new QLabel("Metal", this), 0, 6); controls->addWidget(filter_, 0, 7);
    controls->setColumnStretch(8, 1);
    controls->addWidget(run_, 0, 9); controls->addWidget(stop_, 0, 10);
    context_ = new QLabel("Load a birth chart to calculate.", this); context_->setTextFormat(Qt::PlainText);
    context_->setObjectName("hintLabel");
    context_->setWordWrap(true); controls->addWidget(context_, 1, 0, 1, 11);
    layout->addLayout(controls);
    table_ = new QTableWidget(0, 7, this); table_->setObjectName("moorthiTable");
    table_->setHorizontalHeaderLabels({"Planet", "Entry time (local)", "New sign", "Motion", "Moon at entry", "From natal Moon", "Moorthi"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true); table_->verticalHeader()->hide();
    table_->setShowGrid(false);
    table_->verticalHeader()->setDefaultSectionSize(26);
    table_->horizontalHeader()->setFixedHeight(28);
    table_->horizontalHeader()->setMinimumSectionSize(90);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    table_->horizontalHeader()->resizeSection(5, 140);
    table_->horizontalHeaderItem(5)->setTextAlignment(Qt::AlignCenter);
    table_->setSortingEnabled(true); table_->sortItems(1, Qt::AscendingOrder);
    layout->addWidget(table_, 1);
    status_ = new QLabel("", this); status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true);
    layout->addWidget(status_);
    timer_ = new QTimer(this); timer_->setInterval(0);
    connect(timer_, &QTimer::timeout, this, [this] { step(); });
    connect(run_, &QPushButton::clicked, this, [this] { start(); });
    connect(stop_, &QPushButton::clicked, this, [this] { stop("Stopped · partial results"); });
    connect(copy_, &QPushButton::clicked, this, [this] { copy(); });
    connect(filter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { display(); });
    connect(from_, &QDateEdit::dateChanged, this, [this] { invalidate(); });
    connect(through_, &QDateEdit::dateChanged, this, [this] { invalidate(); });
    connect(planet_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { invalidate(); });
    run_->setEnabled(false);
}

void MoorthiPanel::setContext(const NatalInput& input, const NatalChart& chart) {
    input_ = input;
    const auto moon = std::find_if(chart.bodies.begin(), chart.bodies.end(), [](const auto& p) { return p.name == "Moon"; });
    const QString key = QString("%1|%2|%3|%4|%5|%6|%7")
        .arg(input.name, input.date.toString(Qt::ISODate), input.time.toString("HH:mm:ss.zzz"), input.timezone,
             QString::number(int(chart.siderealAyanamsa)), lunarNodePolicySummary(input.lunarNodePolicy),
             moon == chart.bodies.end() ? "" : QString::number(moon->longitude, 'g', 17));
    if (key == sourceKey_) return;
    sourceKey_ = key;
    ayanamsa_ = chart.siderealAyanamsa;
    natalMoonSign_ = moon == chart.bodies.end() ? -1 : signIndex(moon->longitude);
    invalidate();
    const QString previous = planet_->currentText();
    const QSignalBlocker block(planet_);
    targets_ = {{"Sun", SE_SUN, 0}, {"Moon", SE_MOON, 0}, {"Mars", SE_MARS, 0},
                {"Mercury", SE_MERCURY, 0}, {"Jupiter", SE_JUPITER, 0}, {"Venus", SE_VENUS, 0}, {"Saturn", SE_SATURN, 0}};
    for (const auto type : {LunarNodeType::Mean, LunarNodeType::True}) {
        if (!lunarNodePolicyIncludes(input.lunarNodePolicy, type)) continue;
        const int id = type == LunarNodeType::Mean ? SE_MEAN_NODE : SE_TRUE_NODE;
        const QString suffix = QString(" (%1)").arg(lunarNodeTypeToString(type));
        targets_.push_back({"Rahu" + suffix, id, 0}); targets_.push_back({"Ketu" + suffix, id, 180});
    }
    planet_->clear(); planet_->addItem("All planets", -1);
    for (int i = 0; i < targets_.size(); ++i) planet_->addItem(targets_[i].name, i);
    const int old = planet_->findText(previous); planet_->setCurrentIndex(old >= 0 ? old : 0);
    context_->setText(QString("Natal Moon: %1 · %2 · %3").arg(signName(natalMoonSign_), siderealAyanamsaToString(ayanamsa_), input.timezone));
    run_->setEnabled(natalMoonSign_ >= 0);
}

void MoorthiPanel::invalidate() {
    if (timer_->isActive()) stop("Search cancelled: inputs changed.");
    results_.clear(); table_->setRowCount(0); copy_->setEnabled(false);
    resultContext_.clear(); status_->setText("Choose a range, then Find entries.");
}

void MoorthiPanel::start() {
    if (natalMoonSign_ < 0 || !swe_ || !swe_->isLoaded()) return;
    QString error, zoneLabel;
    if (!parseTimezoneInput(input_.timezone, &zone_, &zoneLabel, &error)) { status_->setText(error); return; }
    if (from_->date() > through_->date() || from_->date().daysTo(through_->date()) > 3660) {
        status_->setText("Choose an ordered range of at most 10 years."); return;
    }
    const auto start = from_->date().startOfDay(zone_);
    const auto end = through_->date().addDays(1).startOfDay(zone_);
    if (!start.isValid() || !end.isValid()) { status_->setText("Invalid date range for this timezone."); return; }
    invalidate();
    startJd_ = epochJd + start.toMSecsSinceEpoch() / 86400000.0;
    endJd_ = epochJd + end.toMSecsSinceEpoch() / 86400000.0;
    // Include a crossing exactly at the start, but not one exactly at the end.
    cursorJd_ = startJd_ - 0.1 / 86400.0; targetIndex_ = 0;
    selected_.clear();
    const int selected = planet_->currentData().toInt();
    if (selected < 0) selected_ = targets_; else selected_.push_back(targets_.at(selected));
    resultContext_ = QString("Moorthi Nirnaya\n%1 · Birth %2 %3 (%4)\nNatal Moon: %5 · Sidereal %6 · %7\n%8 through %9 (inclusive) · %10\n")
        .arg(input_.name, input_.date.toString(Qt::ISODate), input_.time.toString("HH:mm:ss"), input_.timezone,
             signName(natalMoonSign_), siderealAyanamsaToString(ayanamsa_), lunarNodePolicySummary(input_.lunarNodePolicy),
             from_->date().toString(Qt::ISODate), through_->date().toString(Qt::ISODate), planet_->currentText());
    run_->setEnabled(false); stop_->setEnabled(true);
    from_->setEnabled(false); through_->setEnabled(false); planet_->setEnabled(false);
    timer_->start();
}

void MoorthiPanel::step() {
    QElapsedTimer elapsed; elapsed.start();
    swe_->setSidMode(siderealAyanamsaSwissMode(ayanamsa_));
    QString error;
    while (elapsed.elapsed() < 12 && targetIndex_ < selected_.size()) {
        const auto& target = selected_[targetIndex_];
        const double next = std::min(cursorJd_ + 0.25, endJd_);
        QVector<MoorthiEntry> entries;
        if (!findMoorthiEntries(*swe_, target.id, target.offset, cursorJd_, next, natalMoonSign_, &entries, &error)) break;
        for (const auto& entry : entries) {
            if (entry.jd < startJd_ - 0.005 / 86400.0 || entry.jd >= endJd_) continue;
            if (!results_.isEmpty() && results_.last().planet == target.name
                && std::abs(results_.last().entry.jd - entry.jd) < 0.1 / 86400.0) continue;
            results_.push_back({target.name, entry});
        }
        cursorJd_ = next;
        if (cursorJd_ >= endJd_) { ++targetIndex_; cursorJd_ = startJd_ - 0.1 / 86400.0; }
    }
    swe_->setSidMode(siderealAyanamsaSwissMode(input_.siderealAyanamsa));
    if (!error.isEmpty()) { stop("Search failed · partial results: " + error); return; }
    if (targetIndex_ >= selected_.size()) { stop("Complete"); return; }
    const double fraction = (targetIndex_ + (cursorJd_ - startJd_) / (endJd_ - startJd_)) / selected_.size();
    status_->setText(QString("Searching %1 · %2% · %3 entries").arg(selected_[targetIndex_].name).arg(int(fraction * 100)).arg(results_.size()));
}

void MoorthiPanel::stop(const QString& message) {
    timer_->stop(); run_->setEnabled(natalMoonSign_ >= 0); stop_->setEnabled(false);
    from_->setEnabled(true); through_->setEnabled(true); planet_->setEnabled(true);
    display(); status_->setText(QString("%1 · %2 entries").arg(message).arg(results_.size()));
    if (message != "Complete") resultContext_ += message + '\n';
}

void MoorthiPanel::display() {
    const int sortColumn = table_->horizontalHeader()->sortIndicatorSection();
    const auto order = table_->horizontalHeader()->sortIndicatorOrder();
    table_->setSortingEnabled(false); table_->setRowCount(0);
    const int filter = filter_->currentData().toInt();
    for (const auto& result : results_) {
        const auto& e = result.entry;
        if (filter >= 0 && int(e.moorthi) != filter) continue;
        const auto utc = QDateTime::fromMSecsSinceEpoch(qRound64((e.jd - epochJd) * 86400000.0), QTimeZone::UTC);
        const QStringList texts = {result.planet, utc.toTimeZone(zone_).toString("dd MMM yyyy HH:mm:ss"), signName(e.newSign),
            e.retrograde ? "Retrograde" : "Direct", signName(e.moonSign), QString::number(e.count), moorthiName(e.moorthi)};
        const int row = table_->rowCount(); table_->insertRow(row);
        for (int col = 0; col < texts.size(); ++col) {
            auto* item = new EntryItem(texts[col]); item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            if (col == 1) { item->setData(Qt::UserRole, e.jd); item->setToolTip(utc.toString("yyyy-MM-dd HH:mm:ss.zzz 'UTC'")); }
            if (col == 2) item->setData(Qt::UserRole, e.newSign);
            if (col == 4) { item->setData(Qt::UserRole, e.moonSign); item->setToolTip(formatDegInSign(e.moonLongitude)); }
            if (col == 5) { item->setData(Qt::UserRole, e.count); item->setTextAlignment(Qt::AlignCenter); }
            if (col == 6) {
                item->setIcon(metalIcon(e.moorthi)); item->setData(Qt::UserRole, int(e.moorthi));
                item->setToolTip(QStringList{"Gold", "Silver", "Copper", "Iron"}.at(int(e.moorthi)));
            }
            table_->setItem(row, col, item);
        }
    }
    table_->setSortingEnabled(true); table_->sortItems(sortColumn, order);
    copy_->setEnabled(table_->rowCount() > 0);
}

void MoorthiPanel::copy() {
    QString text = resultContext_ + "Filter: " + filter_->currentText() + "\n";
    text += "Planet\tEntry time (" + input_.timezone + ")\tNew sign\tMotion\tMoon at entry\tFrom natal Moon\tMoorthi\n";
    for (int row = 0; row < table_->rowCount(); ++row) {
        QStringList cells;
        for (int col = 0; col < table_->columnCount(); ++col) cells << table_->item(row, col)->text();
        text += cells.join('\t') + '\n';
    }
    QApplication::clipboard()->setText(text);
    status_->setText(QString("Copied %1 visible entries.").arg(table_->rowCount()));
}
}  // namespace dracoved
