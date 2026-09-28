#include "tara_panel.h"
#include "compact_controls.h"
#include "../core/formatting.h"
#include "../core/lunar_nodes.h"
#include "../core/swiss_eph.h"
#include "../core/timezone_utils.h"
#include "../core/vedic_nakshatra.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateEdit>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace dracoved {
namespace {
class TaraItem : public QTableWidgetItem {
public:
    using QTableWidgetItem::QTableWidgetItem;
    bool operator<(const QTableWidgetItem& other) const override {
        if (data(Qt::UserRole).isValid() && other.data(Qt::UserRole).isValid())
            return data(Qt::UserRole).toInt() < other.data(Qt::UserRole).toInt();
        return text().localeAwareCompare(other.text()) < 0;
    }
};
}

TaraPanel::TaraPanel(SwissEph* swe, QWidget* parent) : QWidget(parent), swe_(swe) {
    auto* layout = new QVBoxLayout(this);
    setObjectName("taraPanel");
    layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(4);
    auto* heading = new QHBoxLayout;
    auto* title = new QLabel("Transit Tara", this);
    QFont font = title->font();
    font.setWeight(QFont::DemiBold); title->setFont(font);
    heading->addWidget(title); heading->addStretch();
    copy_ = new QPushButton("Copy results", this); copy_->setObjectName("taraCopy");
    layout->addLayout(heading);
    auto* controls = new CompactControls;
    date_ = new QDateEdit(QDate::currentDate(), this); date_->setObjectName("taraDate");
    date_->setCalendarPopup(true); date_->setDisplayFormat("dd MMM yyyy"); date_->setMinimumWidth(112);
    date_->setDateRange(QDate(1, 1, 1), QDate(9999, 12, 31));
    time_ = new QTimeEdit(QTime::currentTime(), this); time_->setObjectName("taraTime");
    time_->setDisplayFormat("HH:mm:ss");
    auto* now = new QPushButton("Now", this); now->setObjectName("taraNow");
    planet_ = new QComboBox(this); planet_->setObjectName("taraPlanet");
    planet_->addItem("All planets"); planet_->setMinimumWidth(106);
    run_ = new QPushButton("Calculate", this); run_->setObjectName("taraCalculate");
    auto group = [&](const QString& label, QWidget* control) {
        auto* widget = new QWidget(this); auto* row = new QHBoxLayout(widget);
        row->setContentsMargins(0, 0, 0, 0); row->setSpacing(4);
        row->addWidget(new QLabel(label, widget)); row->addWidget(control); controls->addWidget(widget);
    };
    group("Date", date_); group("Time", time_); controls->addWidget(now); group("Planet", planet_);
    auto* actions = new QWidget(this); auto* actionRow = new QHBoxLayout(actions);
    actionRow->setContentsMargins(0, 0, 0, 0); actionRow->setSpacing(4);
    copy_->setText("Copy"); actionRow->addWidget(run_); actionRow->addWidget(copy_);
    controls->addWidget(actions); layout->addLayout(controls);
    context_ = new QLabel("Load a birth chart to calculate.", this);
    context_->setObjectName("hintLabel"); context_->setTextFormat(Qt::PlainText); context_->setWordWrap(true);
    layout->addWidget(context_);
    table_ = new QTableWidget(0, 6, this); table_->setObjectName("taraTable");
    table_->setHorizontalHeaderLabels({"Planet", "Transit sign", "Nakshatra", "Pada", "Count", "Tara"});
    table_->horizontalHeaderItem(4)->setToolTip("Inclusive nakshatra count from natal Moon, 1–27.");
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true); table_->setShowGrid(false);
    table_->verticalHeader()->hide(); table_->verticalHeader()->setMinimumSectionSize(20);
    table_->verticalHeader()->setDefaultSectionSize(22);
    auto* header = table_->horizontalHeader(); header->setFixedHeight(24); header->setMinimumSectionSize(42);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    header->setSectionResizeMode(2, QHeaderView::Stretch);
    for (int col : {3, 4}) {
        table_->horizontalHeaderItem(col)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    }
    table_->setSortingEnabled(true); table_->sortItems(0, Qt::AscendingOrder);
    layout->addWidget(table_, 1);
    status_ = new QLabel(this); status_->setObjectName("taraStatus");
    status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true);
    layout->addWidget(status_);
    connect(date_, &QDateEdit::dateChanged, this, [this] { resolvedUtc_.reset(); invalidate(); });
    connect(time_, &QTimeEdit::timeChanged, this, [this] { resolvedUtc_.reset(); invalidate(); });
    connect(now, &QPushButton::clicked, this, [this] { setNow(); });
    connect(run_, &QPushButton::clicked, this, [this] { calculate(); });
    connect(copy_, &QPushButton::clicked, this, [this] { copy(); });
    connect(planet_, &QComboBox::currentIndexChanged, this, [this] { filterRows(); });
    connect(table_->model(), &QAbstractItemModel::layoutChanged, this, [this] { filterRows(); });
    run_->setEnabled(false); invalidate();
}

void TaraPanel::setContext(const NatalInput& input, const NatalChart& chart) {
    resolvedUtc_.reset();
    input_ = input; input_.zodiacSystem = chart.zodiacSystem;
    ayanamsa_ = chart.siderealAyanamsa; natalStar_ = -1;
    for (const auto& body : chart.bodies)
        if (body.name == "Moon") natalStar_ = classifyVedicNakshatra(body.longitude).index;
    QString error, label;
    const bool validZone = parseTimezoneInput(input.timezone, &zone_, &label, &error);
    if (!validZone) zone_ = QTimeZone();
    run_->setEnabled(natalStar_ >= 0 && validZone);
    context_->setText(natalStar_ < 0 ? "Natal Moon unavailable." : !validZone ? error :
        QString("Natal star: %1 · Time in %2").arg(vedicNakshatraNames()[natalStar_], input.timezone));
    invalidate();
    if (!initialized_ && validZone) { setNow(); initialized_ = true; }
}

void TaraPanel::setNow() {
    if (!zone_.isValid()) return;
    setInspectionTime(QDateTime::currentMSecsSinceEpoch() / 1000 * 1000);
}
void TaraPanel::setInspectionTime(qint64 utcMs) {
    if (!zone_.isValid()) return;
    const auto moment = QDateTime::fromMSecsSinceEpoch(utcMs, zone_);
    if (moment.date() < date_->minimumDate() || moment.date() > date_->maximumDate()) return;
    if (resolvedUtc_ == utcMs && date_->date() == moment.date() && time_->time() == moment.time()) return;
    const QSignalBlocker dateBlock(date_), timeBlock(time_);
    date_->setDate(moment.date()); time_->setDisplayFormat(moment.time().msec() ? "HH:mm:ss.zzz" : "HH:mm:ss");
    time_->setTime(moment.time()); resolvedUtc_ = utcMs; invalidate();
}
void TaraPanel::inspectPlanet(const QString& name, qint64 utcMs) {
    setInspectionTime(utcMs); calculate();
    planet_->setCurrentIndex(std::max(0, planet_->findText(name))); filterRows();
    for (int row = 0; row < table_->rowCount(); ++row)
        if (!table_->isRowHidden(row) && table_->item(row, 0)->text() == name) { table_->selectRow(row); break; }
}
void TaraPanel::setActiveLords(const QStringList& lords, const QString& context, bool only) {
    activeLords_ = QSet<QString>(lords.begin(), lords.end()); activeContext_ = context; activeOnly_ = only; filterRows();
}

void TaraPanel::invalidate() {
    table_->setRowCount(0); copy_->setEnabled(false); resultContext_.clear();
    status_->setToolTip({}); status_->setText("Choose a moment, then Calculate.");
}

void TaraPanel::calculate() {
    invalidate();
    if (natalStar_ < 0 || !zone_.isValid() || !swe_ || !swe_->isLoaded()) {
        status_->setText("Natal Moon, timezone or ephemeris unavailable."); return;
    }
    const QDateTime moment = resolvedUtc_ ? QDateTime::fromMSecsSinceEpoch(*resolvedUtc_, zone_)
        : QDateTime(date_->date(), time_->time(), zone_, QDateTime::TransitionResolution::Reject);
    if (!moment.isValid()) {
        status_->setText("This local time is skipped or repeated by daylight saving. Choose an unambiguous time."); return;
    }
    resolvedUtc_ = moment.toMSecsSinceEpoch();
    struct Target { QString name; int id; double offset; };
    QVector<Target> targets = {{"Sun", SE_SUN, 0}, {"Moon", SE_MOON, 0}, {"Mars", SE_MARS, 0},
        {"Mercury", SE_MERCURY, 0}, {"Jupiter", SE_JUPITER, 0}, {"Venus", SE_VENUS, 0}, {"Saturn", SE_SATURN, 0}};
    for (auto type : {LunarNodeType::Mean, LunarNodeType::True}) {
        if (!lunarNodePolicyIncludes(input_.lunarNodePolicy, type)) continue;
        const int id = type == LunarNodeType::Mean ? SE_MEAN_NODE : SE_TRUE_NODE;
        const QString suffix = QString(" (%1)").arg(lunarNodeTypeToString(type));
        targets.push_back({"Rahu" + suffix, id, 0}); targets.push_back({"Ketu" + suffix, id, 180});
    }
    const double jd = 2440587.5 + moment.toMSecsSinceEpoch() / 86400000.0;
    QVector<double> longitudes;
    QString error;
    // The shared ephemeris mode must be restored before returning, including failures.
    const int zodiacFlags = input_.zodiacSystem == ZodiacSystem::Sidereal ? SEFLG_SIDEREAL : 0;
    swe_->setSidMode(siderealAyanamsaSwissMode(ayanamsa_));
    for (const auto& target : targets) {
        double lon = 0;
        if (!swe_->calcUt(jd, target.id, zodiacFlags, &lon, &error) || !std::isfinite(lon)) break;
        longitudes.push_back(normalizeDegrees(lon + target.offset));
    }
    swe_->setSidMode(siderealAyanamsaSwissMode(input_.siderealAyanamsa));
    if (longitudes.size() != targets.size()) {
        status_->setText("Calculation failed: " + (error.isEmpty() ? QString("Invalid planetary position.") : error)); return;
    }
    const QString previous = planet_->currentText();
    const QSignalBlocker block(planet_);
    planet_->clear(); planet_->addItem("All planets");
    table_->setSortingEnabled(false); table_->setRowCount(targets.size());
    for (int row = 0; row < targets.size(); ++row) {
        planet_->addItem(targets[row].name);
        const auto star = classifyVedicNakshatra(longitudes[row]);
        const auto tara = classifyVedicTara(natalStar_, star.index);
        const int sign = signIndex(longitudes[row]);
        const QStringList cells = {targets[row].name, signName(sign), star.name, QString::number(star.pada),
            QString::number(tara.count), QString("%1 · %2").arg(tara.number).arg(tara.name)};
        const int keys[] = {0, sign, star.index, star.pada, tara.count, tara.number};
        for (int col = 0; col < cells.size(); ++col) {
            auto* item = new TaraItem(cells[col]);
            if (col > 0) item->setData(Qt::UserRole, keys[col]);
            if (col == 3 || col == 4) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            item->setToolTip(cells[col]);
            if (col == 5) item->setToolTip(QString("Count %1 · Cycle %2 · Tara %3: %4")
                .arg(tara.count).arg((tara.count - 1) / 9 + 1).arg(tara.number).arg(tara.name));
            table_->setItem(row, col, item);
        }
    }
    table_->setSortingEnabled(true);
    planet_->setCurrentIndex(std::max(0, planet_->findText(previous)));
    resultContext_ = QString("Transit Tara · %1\nBirth: %2 %3 · %4\n%5\nLocal: %6 · UTC: %7")
        .arg(input_.name, input_.date.toString(Qt::ISODate), input_.time.toString("HH:mm:ss"), input_.timezone,
             context_->text(), moment.toString(moment.time().msec() ? Qt::ISODateWithMs : Qt::ISODate),
             moment.toUTC().toString(moment.time().msec() ? Qt::ISODateWithMs : Qt::ISODate));
    resultContext_ += QString("\n%1 · %2\n%3").arg(zodiacDescription(input_.zodiacSystem, ayanamsa_),
        lunarNodePolicySummary(input_.lunarNodePolicy), birthFacts_);
    filterRows();
    if (onMomentCalculated) onMomentCalculated(moment.toMSecsSinceEpoch());
}

void TaraPanel::filterRows() {
    int visible = 0;
    const bool filterActive = activeOnly_ && planet_->currentIndex() == 0;
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto* body = table_->item(row, 0);
        if (!body) continue;
        const bool active = activeLords_.contains(body->text().section(" (", 0, 0));
        auto font = body->font(); font.setBold(active); body->setFont(font);
        body->setToolTip(active ? "Active dasha lord · " + activeContext_ : body->text());
        const bool hide = (planet_->currentIndex() > 0 && body->text() != planet_->currentText()) || (filterActive && !active);
        table_->setRowHidden(row, hide); if (!hide) ++visible;
    }
    copy_->setEnabled(visible > 0);
    if (!resultContext_.isEmpty()) {
        QStringList parts{QString("%1 of %2 planets shown").arg(visible).arg(table_->rowCount()),
            date_->date().toString("dd MMM yyyy") + " " + time_->time().toString("HH:mm:ss")};
        if (filterActive) parts << "Active lords only at " + activeContext_.section(" · ", 0, 0);
        else if (activeOnly_) parts << "Selected planet takes priority over Active lords only.";
        if (visible == 0 && table_->rowCount() > 0) parts << "Results are hidden by the filters above.";
        status_->setText(parts.join(" · "));
        status_->setToolTip(filterActive ? activeContext_ : QString());
    }
}

void TaraPanel::copy() {
    if (resultContext_.isEmpty() || !copy_->isEnabled()) return;
    QStringList lines{resultContext_ + "\nPlanet filter: " + planet_->currentText()};
    if (activeOnly_ && planet_->currentIndex() == 0) lines << "Active lords only · " + activeContext_;
    else if (activeOnly_) lines << "Dasha filter not applied: selected planet takes priority.";
    QStringList headers;
    for (int col = 0; col < table_->columnCount(); ++col) headers << table_->horizontalHeaderItem(col)->text();
    lines << headers.join('\t');
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (table_->isRowHidden(row)) continue;
        QStringList cells;
        for (int col = 0; col < table_->columnCount(); ++col) cells << table_->item(row, col)->text();
        lines << cells.join('\t');
    }
    QApplication::clipboard()->setText(lines.join('\n'));
    status_->setText("Transit Tara results copied.");
}
} // namespace dracoved
