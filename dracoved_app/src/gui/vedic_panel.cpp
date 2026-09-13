#include "vedic_panel.h"
#include "moorthi_panel.h"
#include "tara_panel.h"

#include "../core/formatting.h"
#include "../core/lunar_nodes.h"
#include "../core/swiss_eph.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QClipboard>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTabWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace dracoved {
namespace {

constexpr int kSortRole = Qt::UserRole;
constexpr int kBodyKeyRole = Qt::UserRole + 1;

class VedicTableItem final : public QTableWidgetItem {
public:
    using QTableWidgetItem::QTableWidgetItem;

    bool operator<(const QTableWidgetItem& other) const override {
        const QVariant left = data(kSortRole);
        const QVariant right = other.data(kSortRole);
        bool leftNumeric = false;
        bool rightNumeric = false;
        const double a = left.toDouble(&leftNumeric);
        const double b = right.toDouble(&rightNumeric);
        if (leftNumeric && rightNumeric) {
            if (a != b) return a < b;
        } else {
            const int compared = QString::localeAwareCompare(left.toString(), right.toString());
            if (compared != 0) return compared < 0;
        }
        return QString::localeAwareCompare(data(kBodyKeyRole).toString(),
                                           other.data(kBodyKeyRole).toString()) < 0;
    }
};

QString formatDegree(double value) {
    return std::isfinite(value) ? formatDegOnly(value) : QStringLiteral("N/A");
}

QString formatContext(const NatalInput& input, const NatalChart& chart, const QString& location,
                     SiderealAyanamsa ayanamsa) {
    const QString name = input.name.trimmed().isEmpty() ? QStringLiteral("Untitled") : input.name.trimmed();
    const QString date = input.date.isValid() ? input.date.toString(QStringLiteral("yyyy-MM-dd")) : QStringLiteral("Unknown date");
    const QString time = input.time.isValid() ? input.time.toString(QStringLiteral("HH:mm:ss")) : QStringLiteral("Unknown time");
    const QString tz = input.timezone.trimmed().isEmpty() ? QStringLiteral("UTC") : input.timezone.trimmed();
    const QString place = location.trimmed().isEmpty()
        ? QStringLiteral("%1, %2").arg(QString::number(input.latitude, 'f', 4), QString::number(input.longitude, 'f', 4))
        : location.trimmed();
    const QString chartMoment = chart.utcDateTime.isValid() ? chart.utcDateTime.toString(Qt::ISODate) : QStringLiteral("unavailable");
    return QStringLiteral("%1  |  %2 %3 (%4)  |  %5  |  UTC %6\nSidereal D1 · Ayanamsa: %7  |  Nodes: %8")
        .arg(name, date, time, tz, place, chartMoment, siderealAyanamsaToString(ayanamsa),
             lunarNodePolicySummary(input.lunarNodePolicy));
}

const BodyPosition* findBody(const NatalChart& chart, const QString& name) {
    for (const auto& body : chart.bodies) {
        if (body.name == name) {
            return &body;
        }
    }
    return nullptr;
}

struct NodeChoice {
    const BodyPosition* position = nullptr;
    LunarNodeType type = LunarNodeType::Mean;
};

QVector<NodeChoice> findNodes(const NatalChart& chart, bool north) {
    QVector<NodeChoice> nodes;
    for (const auto& body : chart.bodies) {
        if (body.isLunarNode && body.isNorthLunarNode == north) {
            nodes.push_back({&body, body.lunarNodeType});
        }
    }
    std::sort(nodes.begin(), nodes.end(), [](const NodeChoice& a, const NodeChoice& b) {
        return static_cast<int>(a.type) < static_cast<int>(b.type);
    });
    return nodes;
}

}  // namespace

VedicPanel::VedicPanel(TropicalNatalEngine* engine, SwissEph* swe, QWidget* parent)
    : QWidget(parent), engine_(engine), swe_(swe) {
    auto* outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(12, 8, 12, 8);
    auto* content = new QWidget(this);
    outerLayout->addStretch(1);
    outerLayout->addWidget(content, 100);
    outerLayout->addStretch(1);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* title = new QLabel(QStringLiteral("Nakshatra placements"), this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 2);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    auto* toolbar = new QWidget(this);
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(10);
    toolbarLayout->addWidget(title);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(new QLabel(QStringLiteral("Ayanamsa"), toolbar));
    ayanamsaCombo_ = new QComboBox(toolbar);
    ayanamsaCombo_->setObjectName("vedicAyanamsa");
    for (const SiderealAyanamsa value : {SiderealAyanamsa::Lahiri, SiderealAyanamsa::Raman,
                                         SiderealAyanamsa::Krishnamurti, SiderealAyanamsa::FaganBradley,
                                         SiderealAyanamsa::Yukteshwar, SiderealAyanamsa::TrueCitra,
                                         SiderealAyanamsa::TrueRevati, SiderealAyanamsa::PushyaPaksha}) {
        ayanamsaCombo_->addItem(siderealAyanamsaToString(value), static_cast<int>(value));
    }
    toolbarLayout->addWidget(ayanamsaCombo_);
    copyButton_ = new QPushButton(QStringLiteral("Copy table"), toolbar);
    copyButton_->setToolTip(QStringLiteral("Copy the displayed Vedic D1 table with chart context."));
    toolbarLayout->addWidget(copyButton_);

    contextLabel_ = new QLabel(QStringLiteral("No natal chart loaded."), this);
    contextLabel_->setObjectName(QStringLiteral("hintLabel"));
    contextLabel_->setWordWrap(true);
    contextLabel_->setTextFormat(Qt::PlainText);
    statusLabel_ = new QLabel(this);
    statusLabel_->setObjectName(QStringLiteral("hintLabel"));
    statusLabel_->setWordWrap(true);

    table_ = new QTableWidget(this);
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels({QStringLiteral("Body"), QStringLiteral("Sign"),
                                       QStringLiteral("Degree in sign"), QStringLiteral("Nakshatra"),
                                       QStringLiteral("Pada"), QStringLiteral("Nakshatra lord")});
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSortingEnabled(true);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setVisible(false);
    table_->setShowGrid(false);
    table_->verticalHeader()->setDefaultSectionSize(26);
    table_->horizontalHeader()->setFixedHeight(28);
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setMinimumSectionSize(90);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    table_->horizontalHeader()->resizeSection(4, 90);
    table_->horizontalHeaderItem(4)->setTextAlignment(Qt::AlignCenter);
    table_->setMinimumHeight(180);

    layout->addWidget(toolbar);
    layout->addWidget(contextLabel_);
    layout->addWidget(table_, 2);
    layout->addWidget(statusLabel_);
    layout->addSpacing(4);
    moorthiPanel_ = new MoorthiPanel(swe_, this);
    taraPanel_ = new TaraPanel(swe_, this);
    auto* transitTabs = new QTabWidget(this);
    transitTabs->setObjectName("vedicTransitTabs");
    transitTabs->addTab(moorthiPanel_, "Moorthi Nirnaya");
    transitTabs->addTab(taraPanel_, "Transit Tara");
    layout->addWidget(transitTabs, 2);

    ayanamsaCombo_->setEnabled(false);
    connect(ayanamsaCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!ayanamsaCombo_) return;
        ayanamsa_ = static_cast<SiderealAyanamsa>(ayanamsaCombo_->currentData().toInt());
        QSettings settings;
        settings.setValue(QStringLiteral("vedic/ayanamsa"), static_cast<int>(ayanamsa_));
        refresh();
    });
    connect(copyButton_, &QPushButton::clicked, this, &VedicPanel::copyTable);
}

void VedicPanel::setNatalContext(const NatalInput& input, const NatalChart& chart,
                                 const QString& location) {
    input_ = input;
    chart_ = chart;
    location_ = location;
    hasContext_ = true;
    if (!ayanamsaInitialized_) {
        QSettings settings;
        const QVariant saved = settings.value(QStringLiteral("vedic/ayanamsa"));
        ayanamsa_ = saved.isValid()
            ? static_cast<SiderealAyanamsa>(qBound(0, saved.toInt(), static_cast<int>(SiderealAyanamsa::PushyaPaksha)))
            : input.siderealAyanamsa;
        ayanamsaInitialized_ = true;
        settings.setValue(QStringLiteral("vedic/ayanamsa"), static_cast<int>(ayanamsa_));
        ayanamsaCombo_->setEnabled(true);
        updateAyanamsaControl();
    }
    contextLabel_->setText(formatContext(input_, chart_, location_, ayanamsa_));
    refresh();
}

void VedicPanel::updateAyanamsaControl() {
    if (!ayanamsaCombo_) return;
    const QSignalBlocker blocker(ayanamsaCombo_);
    const int index = ayanamsaCombo_->findData(static_cast<int>(ayanamsa_));
    ayanamsaCombo_->setCurrentIndex(index >= 0 ? index : 0);
}

void VedicPanel::refresh() {
    if (!hasContext_ || !engine_ || !swe_) {
        return;
    }
    const int sortColumn = table_->horizontalHeader()->sortIndicatorSection();
    const Qt::SortOrder sortOrder = table_->horizontalHeader()->sortIndicatorOrder();
    QString selectedKey;
    if (const auto rows = table_->selectionModel()->selectedRows(); !rows.isEmpty()) {
        selectedKey = table_->item(rows.first().row(), 0)->data(kBodyKeyRole).toString();
    }

    NatalInput vedicInput = input_;
    const SiderealAyanamsa restoreAyanamsa = input_.siderealAyanamsa;
    vedicInput.zodiacSystem = ZodiacSystem::Sidereal;
    vedicInput.siderealAyanamsa = ayanamsa_;
    vedicInput.houseSystem = HouseSystem::WholeSign;
    TropicalComputeOptions options;
    options.includeArabicLots = false;
    options.includePartOfFortune = false;
    options.includeFixedStars = false;
    options.includeAspectGrid = false;
    NatalChart vedicChart;
    QString error;
    const bool ok = engine_->compute(vedicInput, options, &vedicChart, &error);
    // TropicalNatalEngine configures Swiss Ephemeris' process-global sidereal
    // mode. Restore the active natal setting even when local computation fails.
    swe_->setSidMode(siderealAyanamsaSwissMode(restoreAyanamsa));
    if (!ok) {
        moorthiPanel_->setContext(input_, NatalChart{});
        taraPanel_->setContext(input_, NatalChart{});
        table_->setRowCount(0);
        statusLabel_->setText(QStringLiteral("Vedic chart unavailable: %1").arg(error));
        emit statusMessage(error);
        return;
    }

    QVector<Row> rows;
    rows.push_back({QStringLiteral("Ascendant"), QStringLiteral("Ascendant"), vedicChart.angles.asc, std::isfinite(vedicChart.angles.asc)});
    const QStringList planetNames = {"Sun", "Moon", "Mars", "Mercury", "Jupiter", "Venus", "Saturn"};
    for (const QString& name : planetNames) {
        const BodyPosition* position = findBody(vedicChart, name);
        rows.push_back({name, name, position ? position->longitude : 0.0,
                        position && std::isfinite(position->longitude)});
    }
    const bool bothNodes = vedicInput.lunarNodePolicy.mode == LunarNodeMode::Both;
    for (const bool north : {true, false}) {
        const QVector<NodeChoice> nodes = findNodes(vedicChart, north);
        if (nodes.isEmpty()) {
            rows.push_back({north ? QStringLiteral("Rahu") : QStringLiteral("Ketu"),
                            north ? QStringLiteral("Rahu") : QStringLiteral("Ketu"), 0.0, false});
        } else {
            for (const NodeChoice& node : nodes) {
                const QString base = north ? QStringLiteral("Rahu") : QStringLiteral("Ketu");
                const QString suffix = bothNodes
                    ? QStringLiteral(" (%1)").arg(lunarNodeTypeToString(node.type)) : QString();
                rows.push_back({base + suffix, base + suffix, node.position->longitude,
                                std::isfinite(node.position->longitude)});
            }
        }
    }

    table_->setSortingEnabled(false);
    table_->clearContents();
    table_->setRowCount(rows.size());
    for (int row = 0; row < rows.size(); ++row) {
        const Row& source = rows.at(row);
        const auto placement = source.valid ? classifyVedicNakshatra(source.longitude) : NakshatraPlacement{};
        const int sign = source.valid ? signIndex(normalizeDegrees(source.longitude)) : -1;
        const double deg = source.valid ? degInSign(normalizeDegrees(source.longitude)) : 0.0;
        const QString key = source.key;
        auto* bodyItem = new VedicTableItem(source.body);
        bodyItem->setData(kBodyKeyRole, key);
        bodyItem->setData(kSortRole, source.body);
        auto* signItem = new VedicTableItem(source.valid ? signName(sign) : QStringLiteral("N/A"));
        signItem->setData(kSortRole, sign);
        auto* degreeItem = new VedicTableItem(source.valid ? formatDegree(source.longitude) : QStringLiteral("N/A"));
        degreeItem->setData(kSortRole, source.valid ? deg : -1.0);
        auto* nakshatraItem = new VedicTableItem(placement.valid ? placement.name : QStringLiteral("N/A"));
        nakshatraItem->setData(kSortRole, placement.valid ? placement.index : -1);
        auto* padaItem = new VedicTableItem(placement.valid ? QString::number(placement.pada) : QStringLiteral("N/A"));
        padaItem->setTextAlignment(Qt::AlignCenter);
        padaItem->setData(kSortRole, placement.valid ? placement.pada : -1);
        auto* lordItem = new VedicTableItem(placement.valid ? placement.lord : QStringLiteral("N/A"));
        lordItem->setData(kSortRole, placement.valid ? placement.lord : QStringLiteral("N/A"));
        for (QTableWidgetItem* item : {bodyItem, signItem, degreeItem, nakshatraItem, padaItem, lordItem}) {
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            item->setData(kBodyKeyRole, key);
        }
        table_->setItem(row, 0, bodyItem);
        table_->setItem(row, 1, signItem);
        table_->setItem(row, 2, degreeItem);
        table_->setItem(row, 3, nakshatraItem);
        table_->setItem(row, 4, padaItem);
        table_->setItem(row, 5, lordItem);
    }
    table_->setSortingEnabled(true);
    if (sortColumn >= 0 && sortColumn < table_->columnCount()) {
        table_->sortItems(sortColumn, sortOrder);
    }
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (table_->item(row, 0)->data(kBodyKeyRole).toString() == selectedKey) {
            table_->selectRow(row);
            break;
        }
    }
    table_->setMaximumHeight(table_->horizontalHeader()->height()
        + table_->rowCount() * table_->verticalHeader()->defaultSectionSize()
        + 2 * table_->frameWidth());
    statusLabel_->setText(QStringLiteral("%1 placements · Click a column heading to sort; click again to reverse.")
                              .arg(table_->rowCount()));
    contextLabel_->setText(formatContext(input_, vedicChart, location_, ayanamsa_));
    moorthiPanel_->setContext(input_, vedicChart);
    taraPanel_->setContext(input_, vedicChart);
}

void VedicPanel::copyTable() {
    if (!table_ || table_->rowCount() == 0) {
        emit statusMessage(QStringLiteral("No Vedic table is available to copy."));
        return;
    }
    QString output = QStringLiteral("Birth chart · D1\n%1\n\nBody\tSign\tDegree in sign\tNakshatra\tPada\tNakshatra lord\n")
        .arg(contextLabel_->text());
    for (int row = 0; row < table_->rowCount(); ++row) {
        QStringList cells;
        for (int col = 0; col < table_->columnCount(); ++col) {
            cells.push_back(table_->item(row, col)->text());
        }
        output += cells.join('\t') + '\n';
    }
    QApplication::clipboard()->setText(output);
    emit statusMessage(QStringLiteral("Vedic D1 table copied."));
}

}  // namespace dracoved
