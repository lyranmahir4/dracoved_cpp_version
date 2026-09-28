#include "vedic_panel.h"
#include "moorthi_panel.h"
#include "moorthi_graph_panel.h"
#include "tara_panel.h"
#include "dasha_panel.h"
#include "ashtakavarga_panel.h"
#include "../core/formatting.h"
#include "../core/lunar_nodes.h"
#include "../core/tajaka.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCheckBox>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QToolButton>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace dracoved {
namespace {
constexpr int bodyKeyRole = Qt::UserRole + 1;
class DenseColumns final : public QObject {
public:
    DenseColumns(QTableWidget* table, QList<int> flexible) : QObject(table), table_(table), flexible_(flexible) {
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        table_->viewport()->installEventFilter(this);
        connect(table_->model(), &QAbstractItemModel::dataChanged, this, [this] { schedule(); });
        connect(table_->model(), &QAbstractItemModel::rowsInserted, this, [this] { schedule(); });
        connect(table_->model(), &QAbstractItemModel::modelReset, this, [this] { schedule(); });
        schedule();
    }
protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        if (event->type() == QEvent::Resize) schedule();
        return QObject::eventFilter(object, event);
    }
private:
    void schedule() {
        if (pending_) return;
        pending_ = true;
        QTimer::singleShot(0, this, [this] {
            pending_ = false;
            table_->resizeColumnsToContents();
            int total = 0;
            for (int col = 0; col < table_->columnCount(); ++col) total += table_->columnWidth(col);
            const int spare = std::max(0, table_->viewport()->width() - total);
            for (int i = 0; i < flexible_.size(); ++i) {
                const int extra = spare / flexible_.size() + (i < spare % flexible_.size() ? 1 : 0);
                table_->setColumnWidth(flexible_[i], table_->columnWidth(flexible_[i]) + extra);
            }
        });
    }
    QTableWidget* table_;
    QList<int> flexible_;
    bool pending_ = false;
};
class VedicItem : public QTableWidgetItem {
public:
    using QTableWidgetItem::QTableWidgetItem;
    bool operator<(const QTableWidgetItem& other) const override {
        const auto left = data(Qt::UserRole), right = other.data(Qt::UserRole);
        if (!left.isValid() || !right.isValid()) return !left.isValid() && right.isValid();
        if (left.metaType().id() == QMetaType::QString) return left.toString().localeAwareCompare(right.toString()) < 0;
        return left.toDouble() < right.toDouble();
    }
};
const BodyPosition* findBody(const NatalChart& chart, const QString& name) {
    for (const auto& body : chart.bodies) if (body.name == name) return &body;
    return nullptr;
}
QString dms(double degrees, bool precise = false) {
    // Compact cells truncate subsecond detail, so a sign's final second never
    // displays as 30 degrees in the preceding sign. Tooltips retain precision.
    const qint64 total = precise ? qRound64(degrees * 360000.0) : qint64(std::floor(degrees * 3600.0)) * 100;
    return QString("%1°%2′%3″").arg(total / 360000, 2, 10, QChar('0'))
        .arg(total / 6000 % 60, 2, 10, QChar('0'))
        .arg(precise ? QString::number((total % 6000) / 100.0, 'f', 2).rightJustified(5, '0')
                     : QString::number(total / 100 % 60).rightJustified(2, '0'));
}
QString signLord(int sign) {
    static const QStringList lords = {"Mars", "Venus", "Mercury", "Moon", "Sun", "Mercury",
        "Venus", "Mars", "Jupiter", "Saturn", "Saturn", "Jupiter"};
    return sign >= 0 && sign < 12 ? lords[sign] : QString();
}
}

QStringList VedicPanel::columnHeaders() {
    return {"Body", "Sign", "Degree in sign", "Longitude", "House", "Sign lord", "Motion · °/day",
        "Nakshatra", "Pada", "In star", "Star lord", "D9 sign", "Dignity", "From Sun"};
}

VedicPanel::VedicPanel(TropicalNatalEngine* engine, SwissEph* swe, QWidget* parent)
    : QWidget(parent), engine_(engine), swe_(swe) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6); layout->setSpacing(4);
    auto* controls = new QHBoxLayout; controls->setSpacing(6);
    auto* caption = new QLabel("Vedic research", this);
    QFont bold = caption->font(); bold.setWeight(QFont::DemiBold); caption->setFont(bold);
    controls->addWidget(caption); controls->addStretch();
    controls->addWidget(new QLabel("Zodiac / Ayanamsa", this));
    ayanamsaCombo_ = new QComboBox(this); ayanamsaCombo_->setObjectName("vedicAyanamsa");
    for (auto value : {SiderealAyanamsa::Lahiri, SiderealAyanamsa::Raman, SiderealAyanamsa::Krishnamurti,
            SiderealAyanamsa::FaganBradley, SiderealAyanamsa::Yukteshwar, SiderealAyanamsa::TrueCitra,
            SiderealAyanamsa::TrueRevati, SiderealAyanamsa::PushyaPaksha})
        ayanamsaCombo_->addItem(siderealAyanamsaToString(value), int(value));
    ayanamsaCombo_->addItem("Tropical", -1);
    ayanamsaCombo_->setToolTip("Zodiac for all Vedic calculations. Tropical uses unshifted longitudes, with nakshatras starting at 0° tropical Aries.");
    controls->addWidget(ayanamsaCombo_);
    orderButton_ = new QToolButton(this); orderButton_->setText("Natural order");
    orderButton_->setObjectName("vedicNaturalOrder"); orderButton_->setToolTip("Restore Ascendant-first planetary order.");
    controls->addWidget(orderButton_);
    copyButton_ = new QPushButton("Copy table", this); controls->addWidget(copyButton_);
    layout->addLayout(controls);
    contextLabel_ = new QLabel("No natal chart loaded.", this);
    contextLabel_->setObjectName("hintLabel"); contextLabel_->setTextFormat(Qt::PlainText);
    contextLabel_->setWordWrap(true); contextLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(contextLabel_);

    auto* activeRow = new QHBoxLayout; activeRow->setSpacing(8);
    activeLabel_ = new QLabel("Dashas: load a birth chart.", this); activeLabel_->setObjectName("vedicActiveLords");
    activeLabel_->setTextFormat(Qt::PlainText); activeLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    activeLabel_->setWordWrap(true); activeLabel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    activeRow->addWidget(activeLabel_, 1);
    activeOnly_ = new QCheckBox("Active lords only", this); activeOnly_->setObjectName("vedicActiveOnly");
    activeOnly_->setToolTip("With All planets selected, filter Moorthi and Tara to lords active at the inspection time above. A specific planet selection takes priority. For a Moorthi range, this uses that single inspection time, not each entry date.");
    activeRow->addWidget(activeOnly_); layout->addLayout(activeRow);
    views_ = new QTabWidget(this); views_->setObjectName("vedicViews");

    mainSplitter_ = new QSplitter(Qt::Vertical, this); mainSplitter_->setObjectName("vedicMainSplitter");
    mainSplitter_->setChildrenCollapsible(false); mainSplitter_->setHandleWidth(6);
    auto* natal = new QWidget(mainSplitter_); auto* natalLayout = new QVBoxLayout(natal);
    natalLayout->setContentsMargins(0, 0, 0, 0); natalLayout->setSpacing(2);
    table_ = new QTableWidget(0, ColumnCount, natal); table_->setHorizontalHeaderLabels(columnHeaders());
    table_->setObjectName("vedicTable"); table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setAlternatingRowColors(true); table_->setShowGrid(false);
    table_->verticalHeader()->hide(); table_->verticalHeader()->setMinimumSectionSize(20);
    table_->verticalHeader()->setDefaultSectionSize(22);
    auto* header = table_->horizontalHeader(); header->setFixedHeight(24); header->setMinimumSectionSize(40);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColNakshatra, QHeaderView::Stretch);
    header->setSortIndicator(-1, Qt::AscendingOrder); table_->setSortingEnabled(true);
    for (int col : {ColDegree, ColLongitude, ColHouse, ColMotion, ColPada, ColInStar, ColFromSun})
        table_->horizontalHeaderItem(col)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->horizontalHeaderItem(ColDignity)->setToolTip("Existing app dignity convention: Ruler, Exalt, Fall, Detriment. Nodes are unclassified.");
    table_->horizontalHeaderItem(ColFromSun)->setToolTip("Shortest ecliptic longitude distance from the Sun, for Mercury through Saturn. No combustion thresholds applied.");
    table_->horizontalHeaderItem(ColHouse)->setToolTip("Whole-sign house counted from the Ascendant in the selected zodiac.");
    table_->horizontalHeaderItem(ColNavamsa)->setToolTip("Navamsa sign only; * marks the same sign in D1 and D9 (vargottama).");
    natalLayout->addWidget(table_, 1);
    statusLabel_ = new QLabel(this); statusLabel_->setObjectName("hintLabel"); natalLayout->addWidget(statusLabel_);
    natal->setMinimumHeight(140);

    transitSplitter_ = new QSplitter(Qt::Horizontal, mainSplitter_);
    transitSplitter_->setObjectName("vedicTransitSplitter"); transitSplitter_->setChildrenCollapsible(false);
    transitSplitter_->setHandleWidth(6);
    moorthiPanel_ = new MoorthiPanel(swe_, transitSplitter_);
    taraPanel_ = new TaraPanel(swe_, transitSplitter_);
    new DenseColumns(table_, {ColBody, ColSign, ColSignLord, ColNakshatra, ColStarLord, ColNavamsa});
    new DenseColumns(moorthiPanel_->findChild<QTableWidget*>("moorthiTable"), {0, 1, 2, 4, 6});
    new DenseColumns(taraPanel_->findChild<QTableWidget*>("taraTable"), {0, 1, 2, 5});
    mainSplitter_->addWidget(natal); mainSplitter_->addWidget(transitSplitter_);
    transitSplitter_->setStretchFactor(0, 3); transitSplitter_->setStretchFactor(1, 2);
    views_->addTab(mainSplitter_, "Placements && transits");
    dashaPanel_ = new DashaPanel(swe_, views_); views_->addTab(dashaPanel_, "Dashas");
    moorthiGraphPanel_ = new MoorthiGraphPanel(swe_, views_);
    views_->addTab(moorthiGraphPanel_, "Vedic transit graph");
    ashtakavargaPanel_ = new AshtakavargaPanel(swe_, views_);
    views_->addTab(ashtakavargaPanel_, "Ashtakavarga");
    layout->addWidget(views_, 1); restoreSplitters();
    dashaPanel_->onStateChanged = [this] { updateActiveLords(); };
    dashaPanel_->onTransitRequested = [this](const QString& name, qint64 utcMs) {
        views_->setCurrentIndex(0); taraPanel_->inspectPlanet(name, utcMs);
    };
    taraPanel_->onMomentCalculated = [this](qint64 utcMs) {
        if (dashaPanel_->activeLords().isEmpty() || dashaPanel_->inspectionMs() != utcMs) dashaPanel_->setInspectionTime(utcMs);
    };
    ashtakavargaPanel_->onMomentCalculated = taraPanel_->onMomentCalculated;
    moorthiGraphPanel_->onMomentSelected = taraPanel_->onMomentCalculated;
    connect(activeOnly_, &QCheckBox::toggled, this, [this] { updateActiveLords(false); });
    connect(views_, &QTabWidget::currentChanged, this, [this](int index) {
        copyButton_->setVisible(index == 0); orderButton_->setVisible(index == 0);
        activeOnly_->setVisible(index < 2); activeLabel_->setVisible(index != 2);
    });
    connect(mainSplitter_, &QSplitter::splitterMoved, this, [this] { persistSplitters(); });
    connect(transitSplitter_, &QSplitter::splitterMoved, this, [this] { persistSplitters(); });
    connect(header, &QHeaderView::sortIndicatorChanged, this, [this](int column, Qt::SortOrder order) {
        if (!populating_) { sortColumn_ = column; sortOrder_ = order; updateStatus(); }
    });
    connect(orderButton_, &QToolButton::clicked, this, &VedicPanel::resetOrder);
    connect(copyButton_, &QPushButton::clicked, this, &VedicPanel::copyTable);
    connect(ayanamsaCombo_, &QComboBox::currentIndexChanged, this, [this] {
        const int value = ayanamsaCombo_->currentData().toInt();
        zodiac_ = value == -1 ? ZodiacSystem::Tropical : ZodiacSystem::Sidereal;
        if (value >= 0) ayanamsa_ = SiderealAyanamsa(value);
        QSettings settings;
        settings.setValue("vedic/zodiacSystem", zodiacSystemToString(zodiac_));
        settings.setValue("vedic/ayanamsa", int(ayanamsa_)); refresh();
    });
    ayanamsaCombo_->setEnabled(false); copyButton_->setEnabled(false); updateStatus();
}

void VedicPanel::restoreSplitters() {
    QSettings settings;
    mainSplitter_->setSizes({280, 480}); transitSplitter_->setSizes({1000, 760});
    mainSplitter_->restoreState(settings.value("vedic/mainSplitter").toByteArray());
    transitSplitter_->restoreState(settings.value("vedic/transitSplitter").toByteArray());
}
void VedicPanel::persistSplitters() {
    QSettings settings;
    settings.setValue("vedic/mainSplitter", mainSplitter_->saveState());
    settings.setValue("vedic/transitSplitter", transitSplitter_->saveState());
}
void VedicPanel::updateActiveLords(bool syncTime) {
    moorthiGraphPanel_->setDashaYearDays(dashaPanel_->yearDays());
    const auto lords = dashaPanel_->activeLords();
    const auto summary = dashaPanel_->activeSummary();
    activeLabel_->setText(summary.isEmpty() ? "Dashas: choose a valid inspection time and Calculate." : summary);
    activeLabel_->setToolTip(summary);
    activeOnly_->setEnabled(!lords.isEmpty());
    moorthiPanel_->setActiveLords(lords, summary, activeOnly_->isChecked());
    taraPanel_->setActiveLords(lords, summary, activeOnly_->isChecked());
    ashtakavargaPanel_->setActiveLords(lords);
    if (syncTime && !lords.isEmpty()) {
        taraPanel_->setInspectionTime(dashaPanel_->inspectionMs());
        ashtakavargaPanel_->setInspectionTime(dashaPanel_->inspectionMs());
    }
}
void VedicPanel::setNatalContext(const NatalInput& input, const NatalChart& chart, const QString& location) {
    input_ = input; chart_ = chart; location_ = location; hasContext_ = true;
    if (!ayanamsaInitialized_) {
        const int saved = QSettings().value("vedic/ayanamsa", int(SiderealAyanamsa::Lahiri)).toInt();
        const int index = ayanamsaCombo_->findData(saved);
        ayanamsa_ = saved >= 0 && index >= 0 ? SiderealAyanamsa(saved) : SiderealAyanamsa::Lahiri;
        zodiac_ = QSettings().value("vedic/zodiacSystem", "Sidereal").toString() == "Tropical"
            ? ZodiacSystem::Tropical : ZodiacSystem::Sidereal;
        ayanamsaInitialized_ = true;
    }
    updateAyanamsaControl(); ayanamsaCombo_->setEnabled(true); refresh();
}
void VedicPanel::updateAyanamsaControl() {
    const QSignalBlocker block(ayanamsaCombo_);
    ayanamsaCombo_->setCurrentIndex(ayanamsaCombo_->findData(zodiac_ == ZodiacSystem::Tropical ? -1 : int(ayanamsa_)));
}
void VedicPanel::updateFacts(const NatalChart& chart, double value, bool validValue) {
    const auto* moon = findBody(chart, "Moon");
    const auto star = moon ? classifyVedicNakshatra(moon->longitude) : NakshatraPlacement{};
    const QString zodiac = zodiac_ == ZodiacSystem::Tropical ? "Tropical D1 · Ayanamsa: 0°"
        : QString("Sidereal D1 · %1: %2").arg(siderealAyanamsaToString(ayanamsa_), validValue ? dms(value, true) : "N/A");
    contextPlain_ = QString("%1 · Birth: %2 · UTC: %3 · %4 · %5\n%6 · Nodes: %7 · Lagna: %8 · Moon / Janma: %9")
        .arg(input_.name, chart.localDateTime.toString("dd MMM yyyy HH:mm:ss"), chart.utcDateTime.toString(Qt::ISODate),
             location_, input_.timezone, zodiac,
             lunarNodePolicySummary(input_.lunarNodePolicy), std::isfinite(chart.angles.asc) ? signName(signIndex(chart.angles.asc)) : "N/A",
             star.valid ? QString("%1 · pada %2/4").arg(star.name).arg(star.pada) : "N/A");
    contextLabel_->setText(QString(contextPlain_).replace('\n', " · "));
    contextLabel_->setToolTip(contextPlain_);
    moorthiPanel_->setBirthFacts(contextPlain_); taraPanel_->setBirthFacts(contextPlain_);
}
void VedicPanel::refresh() {
    if (!hasContext_ || !engine_ || !swe_) return;
    QString selected;
    if (auto* item = table_->item(table_->currentRow(), ColBody)) selected = item->data(bodyKeyRole).toString();
    NatalInput input = input_; input.zodiacSystem = zodiac_;
    input.siderealAyanamsa = ayanamsa_; input.houseSystem = HouseSystem::WholeSign;
    TropicalComputeOptions options; options.includeArabicLots = false; options.includePartOfFortune = false;
    options.includeFixedStars = false; options.includeAspectGrid = false;
    NatalChart chart; QString error;
    const bool ok = engine_->compute(input, options, &chart, &error);
    double ayanamsaValue = 0;
    const bool hasValue = ok && (zodiac_ == ZodiacSystem::Tropical ||
        (swe_->getAyanamsaUt(2440587.5 + chart.utcDateTime.toMSecsSinceEpoch() / 86400000.0, &ayanamsaValue, &error)
         && std::isfinite(ayanamsaValue)));
    swe_->setSidMode(siderealAyanamsaSwissMode(input_.siderealAyanamsa));
    if (!ok) {
        table_->setRowCount(0); copyButton_->setEnabled(false); contextPlain_.clear();
        contextLabel_->setText("Vedic chart unavailable."); contextLabel_->setToolTip({});
        statusLabel_->setText(error); moorthiPanel_->setContext(input_, {}); taraPanel_->setContext(input_, {});
        dashaPanel_->setContext(input_, {}, {});
        moorthiGraphPanel_->setContext(input_, {});
        ashtakavargaPanel_->setContext(input_, {}, {});
        moorthiPanel_->setBirthFacts({}); taraPanel_->setBirthFacts({}); emit statusMessage(error); return;
    }
    QVector<Row> rows;
    rows.push_back({"Ascendant", "Ascendant", chart.angles.asc, std::isfinite(chart.angles.asc)});
    auto addBody = [&](const QString& label, const BodyPosition* p, bool node) {
        rows.push_back({label, label, p ? p->longitude : 0, p && std::isfinite(p->longitude),
            p && p->hasSpeed && std::isfinite(p->speed), p ? p->speed : 0, p && p->retrograde, node});
    };
    for (const auto& name : QStringList{"Sun", "Moon", "Mars", "Mercury", "Jupiter", "Venus", "Saturn"})
        addBody(name, findBody(chart, name), false);
    for (bool north : {true, false}) for (auto type : {LunarNodeType::Mean, LunarNodeType::True}) {
        if (!lunarNodePolicyIncludes(input.lunarNodePolicy, type)) continue;
        const auto* p = findBody(chart, north ? internalNorthNodeName(type, input.lunarNodePolicy) : internalSouthNodeName(type, input.lunarNodePolicy));
        const QString label = QString(north ? "Rahu" : "Ketu") + (input.lunarNodePolicy.mode == LunarNodeMode::Both ? QString(" (%1)").arg(lunarNodeTypeToString(type)) : QString());
        addBody(label, p, true);
    }
    const int lagna = rows[0].valid ? signIndex(rows[0].longitude) : -1;
    const auto* sun = findBody(chart, "Sun");
    QFont digits = QFontDatabase::systemFont(QFontDatabase::FixedFont); digits.setPointSizeF(table_->font().pointSizeF());
    populating_ = true; table_->setSortingEnabled(false); table_->setRowCount(0); table_->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const auto& source = rows[row];
        auto put = [&](int col, const QString& text, const QVariant& key = {}, const QString& tip = {}, bool numeric = false) {
            auto* item = new VedicItem(text); item->setData(Qt::UserRole, key); item->setData(bodyKeyRole, source.key);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            item->setToolTip(tip.isEmpty() ? text : tip);
            if (numeric) { item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter); item->setFont(digits); }
            table_->setItem(row, col, item);
        };
        put(ColBody, source.body, source.body);
        if (!source.valid) { for (int col = 1; col < ColumnCount; ++col) put(col, "N/A"); continue; }
        const double lon = normalizeDegrees(source.longitude);
        const int sign = signIndex(lon); const auto star = classifyVedicNakshatra(lon);
        const int d9 = tajaka::divisionSign(9, lon);
        const double inStar = std::max(0.0, lon - star.index * (40.0 / 3.0));
        const QString position = signName(sign) + " " + dms(degInSign(lon), true) + " · " + QString::number(lon, 'f', 8) + "°";
        put(ColSign, signName(sign), sign, position);
        put(ColDegree, dms(degInSign(lon)), degInSign(lon), position, true);
        put(ColLongitude, QString::number(lon, 'f', 4) + "°", lon, position, true);
        put(ColHouse, lagna >= 0 ? QString::number((sign - lagna + 12) % 12 + 1) : "N/A",
            lagna >= 0 ? QVariant((sign - lagna + 12) % 12 + 1) : QVariant(), "Whole-sign house from Lagna", true);
        put(ColSignLord, signLord(sign), signLord(sign));
        put(ColMotion, source.hasSpeed ? QString("%1 %2").arg(source.retrograde ? "R" : "D", QString::number(source.speed, 'f', 4)) : "—",
            source.hasSpeed ? QVariant(source.speed) : QVariant(), source.hasSpeed ? QString("%1°/day · %2").arg(source.speed, 0, 'f', 8).arg(source.retrograde ? "Retrograde" : "Direct") : "Motion unavailable", true);
        const QString starTip = QString("%1 · absolute span %2° to %3° (end excluded)")
            .arg(star.name).arg(star.index * (40.0 / 3.0), 0, 'f', 6).arg((star.index + 1) * (40.0 / 3.0), 0, 'f', 6);
        put(ColNakshatra, star.name, star.index, starTip);
        put(ColPada, QString("%1/4").arg(star.pada), star.pada, starTip + QString(" · Pada %1").arg(star.pada), true);
        put(ColInStar, QString::number(inStar / (40.0 / 3.0) * 100.0, 'f', 1) + "%", inStar,
            dms(inStar, true) + " / 13°20′00″", true);
        put(ColStarLord, star.lord, star.lord);
        put(ColNavamsa, signName(d9) + (d9 == sign ? " *" : ""), d9,
            d9 == sign ? "Vargottama: same sign in D1 and D9" : "Navamsa sign");
        put(ColDignity, row == 0 || source.isNode ? "—" : dignityLabel(source.body, signName(sign)),
            row == 0 || source.isNode ? QVariant() : QVariant(dignityLabel(source.body, signName(sign))),
            "Existing app convention: Ruler, Exalt, Fall, Detriment. Nodes and Ascendant unclassified.");
        const bool solarDistance = row >= 3 && row <= 7 && sun && std::isfinite(sun->longitude);
        const double distance = solarDistance ? std::abs(std::remainder(lon - sun->longitude, 360.0)) : 0;
        put(ColFromSun, solarDistance ? QString::number(distance, 'f', 2) + "°" : "—", solarDistance ? QVariant(distance) : QVariant(),
            solarDistance ? dms(distance, true) + " · Ecliptic separation; no combustion threshold applied" : "Not applicable", true);
    }
    table_->horizontalHeader()->setSortIndicator(sortColumn_, sortOrder_); table_->setSortingEnabled(true);
    for (int row = 0; row < table_->rowCount(); ++row)
        if (table_->item(row, ColBody)->data(bodyKeyRole).toString() == selected) { table_->selectRow(row); break; }
    populating_ = false; copyButton_->setEnabled(!rows.isEmpty()); updateStatus();
    moorthiPanel_->setContext(input_, chart); taraPanel_->setContext(input_, chart);
    moorthiGraphPanel_->setContext(input_, chart);
    updateFacts(chart, ayanamsaValue, hasValue);
    ashtakavargaPanel_->setContext(input_, chart, contextPlain_);
    dashaPanel_->setContext(input_, chart, contextPlain_);
}
void VedicPanel::updateStatus() {
    statusLabel_->setText(QString("%1 placements · %2 · * Vargottama").arg(table_->rowCount())
        .arg(sortColumn_ < 0 ? "Natural order" : "Sorted by " + columnHeaders().value(sortColumn_)));
    orderButton_->setEnabled(sortColumn_ >= 0);
}
void VedicPanel::resetOrder() {
    // Reorder the existing cells without recalculating or clearing transit results.
    const QStringList order = {"Ascendant", "Sun", "Moon", "Mars", "Mercury", "Jupiter", "Venus", "Saturn",
        "Rahu", "Rahu (Mean)", "Rahu (True)", "Ketu", "Ketu (Mean)", "Ketu (True)"};
    populating_ = true; table_->setSortingEnabled(false);
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto* item = table_->item(row, ColBody); item->setData(Qt::UserRole, order.indexOf(item->text()));
    }
    table_->sortItems(ColBody, Qt::AscendingOrder);
    for (int row = 0; row < table_->rowCount(); ++row) {
        auto* item = table_->item(row, ColBody); item->setData(Qt::UserRole, item->text());
    }
    sortColumn_ = -1; sortOrder_ = Qt::AscendingOrder;
    table_->horizontalHeader()->setSortIndicator(-1, sortOrder_); table_->setSortingEnabled(true);
    populating_ = false; updateStatus();
}
void VedicPanel::copyTable() {
    if (!table_->rowCount()) return;
    QStringList lines{contextPlain_, columnHeaders().join('\t')};
    for (int row = 0; row < table_->rowCount(); ++row) {
        QStringList cells;
        for (int col = 0; col < ColumnCount; ++col) cells << table_->item(row, col)->text();
        lines << cells.join('\t');
    }
    QApplication::clipboard()->setText(lines.join('\n'));
    emit statusMessage("Vedic D1 table copied.");
}
} // namespace dracoved
