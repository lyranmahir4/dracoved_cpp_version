#include "ashtakavarga_panel.h"
#include "compact_controls.h"
#include "../core/formatting.h"
#include "../core/swiss_eph.h"
#include "../core/timezone_utils.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateEdit>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <cmath>
#include <numeric>

namespace dracoved {
namespace {
QTableWidget* makeTable(QWidget* parent, const QString& name, int columns) {
    auto* table = new QTableWidget(0, columns, parent);
    table->setObjectName(name);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->hide();
    table->verticalHeader()->setMinimumSectionSize(20);
    table->verticalHeader()->setDefaultSectionSize(25);
    table->horizontalHeader()->setMinimumSectionSize(36);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->setMinimumSize(260, 155);
    return table;
}
QLabel* caption(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    auto font = label->font(); font.setWeight(QFont::DemiBold); label->setFont(font);
    return label;
}
void cell(QTableWidget* table, int row, int col, const QString& text, const QString& tip = {}, bool bold = false) {
    auto* item = new QTableWidgetItem(text);
    item->setToolTip(tip);
    item->setTextAlignment(col ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter);
    auto font = item->font(); font.setBold(bold); item->setFont(font);
    table->setItem(row, col, item);
}
void scoreCell(QTableWidget* table, int row, int col, int score, const QString& tip, bool total = false) {
    cell(table, row, col, QString::number(score), tip, total);
    // BAV comparison is against four of eight donors; SAV is an unclassified sum.
    if (!total) {
        if (score > 4) table->item(row, col)->setForeground(QColor("#26734B"));
        if (score < 4) table->item(row, col)->setForeground(QColor("#B44839"));
    }
}
QString tableText(QTableWidget* table) {
    QStringList lines, headers;
    for (int c = 0; c < table->columnCount(); ++c)
        headers << QString(table->horizontalHeaderItem(c)->text()).replace('\n', ' ');
    lines << headers.join('\t');
    for (int r = 0; r < table->rowCount(); ++r) {
        QStringList cells;
        for (int c = 0; c < table->columnCount(); ++c) cells << table->item(r, c)->text();
        lines << cells.join('\t');
    }
    return lines.join('\n');
}
}

AshtakavargaPanel::AshtakavargaPanel(SwissEph* swe, QWidget* parent) : QWidget(parent), swe_(swe) {
    setObjectName("ashtakavargaPanel");
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(4, 4, 4, 4); layout->setSpacing(4);
    auto* heading = new QHBoxLayout;
    auto* title = caption("Natal Ashtakavarga · unreduced", this);
    title->setToolTip("BAV: 0–8 benefic points from seven natal planets and Lagna. SAV: sum of seven BAVs.\n"
        "P.V.R. Narasimha Rao, Vedic Astrology: An Integrated Approach, chapter 12, tables 19–25.\n"
        "No Trikona/Ekadhipatya reductions. Rahu and Ketu have no BAV in this method.");
    heading->addWidget(title); heading->addStretch();
    order_ = new QComboBox(this); order_->setObjectName("ashtakavargaOrder");
    order_->addItems({"Aries first", "Lagna first"}); heading->addWidget(order_);
    copy_ = new QPushButton("Copy all", this); copy_->setObjectName("ashtakavargaCopy"); heading->addWidget(copy_);
    layout->addLayout(heading);
    auto* vertical = new QSplitter(Qt::Vertical, this); vertical->setChildrenCollapsible(false);
    auto* top = new QWidget(vertical); auto* topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0); topLayout->setSpacing(3);
    matrix_ = makeTable(top, "ashtakavargaMatrix", 14); topLayout->addWidget(matrix_, 1);
    natalStatus_ = new QLabel("Load a natal chart.", top); topLayout->addWidget(natalStatus_);
    auto* bottom = new QSplitter(Qt::Horizontal, vertical); bottom->setChildrenCollapsible(false);
    auto* details = new QWidget(bottom); auto* detailLayout = new QVBoxLayout(details);
    detailLayout->setContentsMargins(0, 0, 0, 0); detailLayout->setSpacing(4);
    auto* detailHeading = new QHBoxLayout;
    detailHeading->addWidget(caption("Prastara · contributions", details)); detailHeading->addStretch();
    planet_ = new QComboBox(details); planet_->setObjectName("ashtakavargaPlanet");
    planet_->addItems(ashtakavargaPlanets()); detailHeading->addWidget(planet_);
    detailLayout->addLayout(detailHeading);
    contributions_ = makeTable(details, "ashtakavargaContributions", 14); detailLayout->addWidget(contributions_, 1);
    auto* hint = new QLabel("1 = contributes a point · 0 = no point", details); detailLayout->addWidget(hint);

    auto* transit = new QWidget(bottom); auto* transitLayout = new QVBoxLayout(transit);
    transitLayout->setContentsMargins(0, 0, 0, 0); transitLayout->setSpacing(4);
    transitLayout->addWidget(caption("Transit scores · natal BAV / SAV / Kaksha", transit));
    auto* controls = new CompactControls;
    date_ = new QDateEdit(QDate::currentDate(), transit); date_->setObjectName("ashtakavargaDate");
    date_->setCalendarPopup(true); date_->setDateRange(QDate(1,1,1), QDate(9999,12,31)); date_->setDisplayFormat("dd MMM yyyy");
    time_ = new QTimeEdit(QTime::currentTime(), transit); time_->setObjectName("ashtakavargaTime"); time_->setDisplayFormat("HH:mm:ss");
    auto* now = new QPushButton("Now", transit);
    run_ = new QPushButton("Calculate", transit); run_->setObjectName("ashtakavargaCalculate");
    controls->addWidget(new QLabel("Date", transit)); controls->addWidget(date_);
    controls->addWidget(new QLabel("Time", transit)); controls->addWidget(time_);
    controls->addWidget(now); controls->addWidget(run_); transitLayout->addLayout(controls);
    transits_ = makeTable(transit, "ashtakavargaTransits", 7);
    transits_->setHorizontalHeaderLabels({"Planet", "Transit sign", "House", "BAV / 8", "SAV", "Kaksha", "Bindu"});
    transits_->horizontalHeaderItem(2)->setToolTip("Whole-sign house from natal Lagna.");
    transits_->horizontalHeaderItem(3)->setToolTip("This planet's natal BAV score in its transit sign. Green >4, red <4, neutral 4.");
    transits_->horizontalHeaderItem(4)->setToolTip("Sum of the seven natal BAVs in the transit sign.");
    transits_->horizontalHeaderItem(5)->setToolTip("3°45' section of the current sign · Saturn, Jupiter, Mars, Sun, Venus, Mercury, Moon, Lagna.");
    transits_->horizontalHeaderItem(6)->setToolTip("This planet's own natal Prastara contribution from the current Kaksha ruler: 0 or 1.");
    transits_->setSelectionBehavior(QAbstractItemView::SelectRows); transitLayout->addWidget(transits_, 1);
    transitStatus_ = new QLabel(transit); transitStatus_->setWordWrap(true); transitLayout->addWidget(transitStatus_);
    vertical->setSizes({285, 385}); bottom->setSizes({920, 720}); layout->addWidget(vertical, 1);

    connect(order_, &QComboBox::currentIndexChanged, this, [this] { fillScores(); fillContributions(); });
    connect(planet_, &QComboBox::currentIndexChanged, this, [this] { fillContributions(); });
    connect(matrix_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (row >= 0 && row < 7) planet_->setCurrentIndex(row);
    });
    connect(transits_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (row < 0 || row >= 7 || !transits_->item(row, 1)) return;
        planet_->setCurrentIndex(row);
        const int sign = transits_->item(row, 1)->data(Qt::UserRole).toInt();
        for (int col = 1; col <= 12; ++col) if (signForColumn(col) == sign) matrix_->setCurrentCell(row, col);
    });
    connect(date_, &QDateEdit::dateChanged, this, [this] { resolvedUtc_.reset(); clearTransits(); });
    connect(time_, &QTimeEdit::timeChanged, this, [this] { resolvedUtc_.reset(); clearTransits(); });
    connect(now, &QPushButton::clicked, this, [this] { setInspectionTime(QDateTime::currentMSecsSinceEpoch() / 1000 * 1000); });
    connect(run_, &QPushButton::clicked, this, [this] { calculateTransits(); });
    connect(copy_, &QPushButton::clicked, this, [this] { copy(); });
    run_->setEnabled(false); copy_->setEnabled(false); clearTransits();
}

void AshtakavargaPanel::setContext(const NatalInput& input, const NatalChart& chart, const QString& facts) {
    input_ = input; input_.zodiacSystem = chart.zodiacSystem;
    ayanamsa_ = chart.siderealAyanamsa; facts_ = facts;
    QString error, zoneError, label;
    valid_ = computeAshtakavarga(chart, &scores_, &error);
    if (!parseTimezoneInput(input.timezone, &zone_, &label, &zoneError)) zone_ = QTimeZone();
    run_->setEnabled(valid_ && zone_.isValid()); copy_->setEnabled(valid_);
    fillScores(); fillContributions(); clearTransits();
    if (!valid_) natalStatus_->setText(error);
    if (!zone_.isValid()) transitStatus_->setText(zoneError);
    else setInspectionTime(resolvedUtc_.value_or(QDateTime::currentMSecsSinceEpoch() / 1000 * 1000));
}

int AshtakavargaPanel::signForColumn(int column) const {
    return (column - 1 + (order_->currentIndex() == 1 ? scores_.referenceSigns[7] : 0)) % 12;
}
void AshtakavargaPanel::setSignHeaders(QTableWidget* table) {
    QStringList headers{table == matrix_ ? "Planet" : "Contributor"};
    for (int col = 1; col <= 12; ++col) {
        const int sign = signForColumn(col), house = (sign - scores_.referenceSigns[7] + 12) % 12 + 1;
        headers << signName(sign).left(3) + "\nH" + QString::number(house);
    }
    headers << "Total"; table->setHorizontalHeaderLabels(headers);
    for (int col = 1; col <= 12; ++col) table->horizontalHeaderItem(col)->setToolTip(signName(signForColumn(col)));
}
void AshtakavargaPanel::fillScores() {
    const QSignalBlocker block(matrix_);
    matrix_->setRowCount(0); if (!valid_) return;
    setSignHeaders(matrix_); matrix_->setRowCount(8);
    for (int row = 0; row < 8; ++row) {
        const bool total = row == 7;
        cell(matrix_, row, 0, total ? "SAV" : ashtakavargaPlanets()[row], {}, total);
        const auto& values = total ? scores_.sav : scores_.bav[row];
        for (int col = 1; col <= 12; ++col) {
            const int sign = signForColumn(col);
            QStringList donors;
            if (!total) for (int d = 0; d < 8; ++d) if (scores_.contributions[row][d][sign])
                donors << (d == 7 ? QString("Lagna") : ashtakavargaPlanets()[d]);
            const QString tip = total ? signName(sign) + " · Sum of seven planetary BAV scores" :
                QString("%1 in %2: %3/8\nContributors: %4\nGreen >4 · neutral 4 · red <4")
                    .arg(ashtakavargaPlanets()[row], signName(sign)).arg(values[sign]).arg(donors.isEmpty() ? "None" : donors.join(", "));
            scoreCell(matrix_, row, col, values[sign], tip, total);
        }
        cell(matrix_, row, 13, QString::number(std::accumulate(values.begin(), values.end(), 0)), "Total across all twelve signs", true);
    }
    natalStatus_->setText("SAV total: 337 · Seven planets + Lagna as contributors · Select a planet for its breakdown");
}
void AshtakavargaPanel::fillContributions() {
    contributions_->setRowCount(0); if (!valid_) return;
    const int planet = planet_->currentIndex();
    setSignHeaders(contributions_); contributions_->setRowCount(9);
    for (int donor = 0; donor < 9; ++donor) {
        const bool total = donor == 8;
        const QString name = total ? "BAV" : donor == 7 ? "Lagna" : ashtakavargaPlanets()[donor];
        cell(contributions_, donor, 0, name, total ? QString() : "Natal sign: " + signName(scores_.referenceSigns[donor]), total);
        const auto& values = total ? scores_.bav[planet] : scores_.contributions[planet][donor];
        for (int col = 1; col <= 12; ++col) {
            const int sign = signForColumn(col);
            const QString tip = total ? "Sum of the eight contributors" :
                QString("%1 from natal %2 in %3 · House %4 → %5")
                    .arg(ashtakavargaPlanets()[planet], name, signName(scores_.referenceSigns[donor]))
                    .arg((sign - scores_.referenceSigns[donor] + 12) % 12 + 1).arg(values[sign]);
            cell(contributions_, donor, col, QString::number(values[sign]), tip, total);
        }
        cell(contributions_, donor, 13, QString::number(std::accumulate(values.begin(), values.end(), 0)), {}, true);
    }
}

void AshtakavargaPanel::setInspectionTime(qint64 utcMs) {
    if (!zone_.isValid()) return;
    const auto moment = QDateTime::fromMSecsSinceEpoch(utcMs, zone_);
    if (moment.date() < date_->minimumDate() || moment.date() > date_->maximumDate()) return;
    if (resolvedUtc_ == utcMs && date_->date() == moment.date() && time_->time() == moment.time()) return;
    const QSignalBlocker dateBlock(date_), timeBlock(time_);
    date_->setDate(moment.date()); time_->setDisplayFormat(moment.time().msec() ? "HH:mm:ss.zzz" : "HH:mm:ss");
    time_->setTime(moment.time()); resolvedUtc_ = utcMs; clearTransits();
}
void AshtakavargaPanel::setActiveLords(const QStringList& lords) {
    activeLords_ = lords;
    for (int row = 0; row < transits_->rowCount(); ++row) {
        auto* item = transits_->item(row, 0); auto font = item->font();
        font.setBold(activeLords_.contains(item->text())); item->setFont(font);
    }
}
void AshtakavargaPanel::clearTransits() {
    transits_->setRowCount(0); transitContext_.clear();
    transitStatus_->setText("Choose a moment, then Calculate. Time: " + input_.timezone);
}
void AshtakavargaPanel::calculateTransits() {
    clearTransits();
    if (!valid_ || !zone_.isValid() || !swe_ || !swe_->isLoaded()) {
        transitStatus_->setText("Natal scores, timezone or ephemeris unavailable."); return;
    }
    const QDateTime moment = resolvedUtc_ ? QDateTime::fromMSecsSinceEpoch(*resolvedUtc_, zone_) :
        QDateTime(date_->date(), time_->time(), zone_, QDateTime::TransitionResolution::Reject);
    if (!moment.isValid()) {
        transitStatus_->setText("This local time is skipped or repeated by daylight saving. Choose an unambiguous time."); return;
    }
    constexpr int bodies[] = {SE_SUN, SE_MOON, SE_MARS, SE_MERCURY, SE_JUPITER, SE_VENUS, SE_SATURN};
    std::array<double, 7> longitudes{};
    QString error; bool ok = true;
    const int zodiacFlags = input_.zodiacSystem == ZodiacSystem::Sidereal ? SEFLG_SIDEREAL : 0;
    swe_->setSidMode(siderealAyanamsaSwissMode(ayanamsa_));
    for (int p = 0; p < 7; ++p) {
        if (!swe_->calcUt(2440587.5 + moment.toMSecsSinceEpoch() / 86400000.0, bodies[p], zodiacFlags, &longitudes[p], &error)
            || !std::isfinite(longitudes[p])) { ok = false; error = ashtakavargaPlanets()[p] + ": " + error; break; }
    }
    swe_->setSidMode(siderealAyanamsaSwissMode(input_.siderealAyanamsa));
    if (!ok) { transitStatus_->setText("Calculation failed: " + error); return; }
    resolvedUtc_ = moment.toMSecsSinceEpoch();
    const QSignalBlocker block(transits_);
    transits_->setRowCount(7);
    for (int p = 0; p < 7; ++p) {
        const int sign = signIndex(normalizeDegrees(longitudes[p]));
        cell(transits_, p, 0, ashtakavargaPlanets()[p]);
        cell(transits_, p, 1, signName(sign), QString::number(normalizeDegrees(longitudes[p]), 'f', 8) + "° · " + zodiacDescription(input_.zodiacSystem, ayanamsa_));
        transits_->item(p, 1)->setData(Qt::UserRole, sign);
        cell(transits_, p, 2, QString::number((sign - scores_.referenceSigns[7] + 12) % 12 + 1));
        scoreCell(transits_, p, 3, scores_.bav[p][sign], "Own natal BAV in the transit sign · out of 8");
        scoreCell(transits_, p, 4, scores_.sav[sign], "Natal SAV in the transit sign", true);
        KakshaBindu kaksha;
        if (kakshaBinduAt(scores_, p, longitudes[p], &kaksha)) {
            const QString ruler=kakshaDonorName(kaksha.donor);
            cell(transits_, p, 5, QString("%1 · %2").arg(kaksha.section+1).arg(ruler),
                 QString("Kaksha %1/8 · %2° to %3° of %4")
                    .arg(kaksha.section+1).arg(kaksha.section*3.75,0,'f',2)
                    .arg((kaksha.section+1)*3.75,0,'f',2).arg(signName(kaksha.sign)));
            cell(transits_, p, 6, QString::number(kaksha.bindu),
                 ruler+" contributes "+QString::number(kaksha.bindu)+" bindu to "+ashtakavargaPlanets()[p]+"'s natal Prastara in "+signName(sign));
        }
    }
    transitContext_ = "Local: " + moment.toString(Qt::ISODateWithMs) + " · UTC: " + moment.toUTC().toString(Qt::ISODateWithMs);
    transitStatus_->setText(moment.toString("dd MMM yyyy HH:mm:ss") + " · " + input_.timezone + " · 7 planets · Active dasha lords in bold");
    if (onMomentCalculated) onMomentCalculated(*resolvedUtc_);
    setActiveLords(activeLords_);
}
void AshtakavargaPanel::copy() {
    if (!valid_) return;
    QStringList lines{facts_, "Ashtakavarga · unreduced natal D1 · BAV 0–8 · SAV total 337",
        "Method: P.V.R. Narasimha Rao, Vedic Astrology: An Integrated Approach, chapter 12, tables 19–25.",
        "Kaksha: Gochar Phaladeepika, PDF pp. 234–235; eight 3°45' sections per sign, using the transiting planet's own natal Prastara contribution.",
        "https://vedicastrologer.org/articles/vedic_astro_textbook.pdf",
        tableText(matrix_), "", "Prastara: " + planet_->currentText(), tableText(contributions_)};
    if (transits_->rowCount()) lines << "" << "Transit sign scores" << transitContext_ << tableText(transits_);
    QApplication::clipboard()->setText(lines.join('\n'));
    natalStatus_->setText("Ashtakavarga tables copied · SAV total: 337");
}
} // namespace dracoved
