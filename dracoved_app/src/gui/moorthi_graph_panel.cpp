#include "moorthi_graph_panel.h"
#include "compact_controls.h"
#include "time_graph_range.h"
#include "vedic_benchmark_panel.h"
#include "../core/formatting.h"
#include "../core/timezone_utils.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDateEdit>
#include <QElapsedTimer>
#include <QHelpEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidgetAction>
#include <algorithm>
#include <cmath>

namespace dracoved {
namespace {
constexpr double epochJd = 2440587.5;
constexpr double dayMs = 86400000.0;
constexpr double rootTolerance = 0.1 / 86400.0;
double julian(const QDateTime& time) { return epochJd + time.toMSecsSinceEpoch() / dayMs; }
QDateTime local(double jd, const QTimeZone& zone) {
    return QDateTime::fromMSecsSinceEpoch(qRound64((jd - epochJd) * dayMs), zone);
}
QColor planetColor(int index, const QPalette& palette) {
    static const char* colors[] = {"#ac6900", "#287c90", "#c33e44", "#268456", "#927718",
                                   "#b14685", "#355fb1", "#764cbc", "#985832"};
    QColor color(colors[index % 9]);
    if (palette.base().color().lightness() < 128) color = color.lighter(160);
    return color;
}
QString metalLabel(Moorthi metal) {
    if (metal == Moorthi::NotApplicable) return moorthiName(metal);
    static const char* names[] = {"Gold", "Silver", "Copper", "Iron"};
    return QString("%1 · %2").arg(names[int(metal)], moorthiName(metal));
}
Qt::PenStyle planetLine(const QString& name) {
    // Rahu and Ketu enter opposing signs together and can share the entire
    // metal trace. Gaps in Ketu's stroke keep Rahu visible underneath it.
    if (name.startsWith("Ketu")) return name.contains("(Mean)") ? Qt::DashDotDotLine : Qt::DashLine;
    return name.contains("(Mean)") ? Qt::DashLine : Qt::SolidLine;
}
}

class MoorthiGraphPlot final : public QWidget {
public:
    MoorthiGraphPlot(const QVector<MoorthiGraphSeries>& series, QWidget* parent)
        : QWidget(parent), series_(series) {
        setObjectName("moorthiGraphPlot");
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setAccessibleName("Moorthi changes over time");
        setAccessibleDescription("Ctrl + mouse wheel zooms time at the cursor. Drag left/right to pan when zoomed. Right-click to fit the full graph.");
    }
    QSize minimumSizeHint() const override { return {340, 230}; }
    QSize sizeHint() const override { return {1000, 480}; }
    void setRange(double start, double end, const QTimeZone& zone) {
        pressed_=dragging_=false; unsetCursor();
        start_ = start; end_ = end; range_.setRange(start,end); zone_ = zone; hits_.clear(); update();
    }
    void clearHits() { pressed_=dragging_=false; unsetCursor(); hits_.clear(); range_.reset(); QToolTip::hideText(); update(); }

protected:
    void wheelEvent(QWheelEvent* event) override {
        if (pressed_ || !(event->modifiers() & Qt::ControlModifier) || !area_.contains(event->position()) || series_.isEmpty()) {
            event->ignore(); return;
        }
        const double steps = event->angleDelta().y() ? event->angleDelta().y()/120.0 : event->pixelDelta().y()/40.0;
        range_.zoom((event->position().x()-area_.left())/area_.width(),steps);
        hits_.clear(); setCursor(range_.zoomed()?Qt::OpenHandCursor:Qt::ArrowCursor); QToolTip::hideText(); update(); event->accept();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button()==Qt::LeftButton && range_.zoomed() && area_.contains(event->position())) {
            pressed_=true; dragging_=false; pressPosition_=event->position(); panStart_=range_.start();
            QToolTip::hideText(); event->accept();
        } else event->ignore();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (pressed_ && (event->buttons() & Qt::LeftButton)) {
            const double dx=event->position().x()-pressPosition_.x();
            if (std::abs(dx)>=QApplication::startDragDistance()) dragging_=true;
            if (dragging_) {
                range_.panTo(panStart_-dx/area_.width()*(range_.end()-range_.start()));
                hits_.clear(); setCursor(Qt::ClosedHandCursor); QToolTip::hideText(); update();
            }
            event->accept(); return;
        }
        pressed_=dragging_=false;
        setCursor(range_.zoomed() && area_.contains(event->position())?Qt::OpenHandCursor:Qt::ArrowCursor);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button()!=Qt::LeftButton || !pressed_) {event->ignore(); return;}
        pressed_=dragging_=false;
        setCursor(range_.zoomed() && area_.contains(event->position())?Qt::OpenHandCursor:Qt::ArrowCursor);
        event->accept();
    }
    void leaveEvent(QEvent*) override {QToolTip::hideText(); if(!pressed_) unsetCursor();}
    void contextMenuEvent(QContextMenuEvent* event) override {
        pressed_=dragging_=false; unsetCursor();
        QMenu menu(this);
        auto* fit=menu.addAction("Fit graph / Reset zoom"); fit->setEnabled(range_.zoomed());
        if (menu.exec(event->globalPos())==fit) {range_.reset(); hits_.clear(); QToolTip::hideText(); update();}
        event->accept();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette().base());
        hits_.clear();
        const auto fm = fontMetrics();
        const int rowHeight = fm.height() + 10;
        int lx = 12, ly = 8;
        for (const auto& series : series_) {
            if (series.body == SE_MOON) continue;
            const int width = fm.horizontalAdvance(series.planet) + 44;
            if (lx > 12 && lx + width > this->width() - 12) { lx = 12; ly += rowHeight; }
            p.setPen(QPen(planetColor(series.colorIndex, palette()), 2, planetLine(series.planet)));
            p.drawLine(lx, ly + fm.height() / 2, lx + 20, ly + fm.height() / 2);
            p.setPen(palette().text().color());
            p.drawText(lx + 26, ly + fm.ascent(), series.planet);
            lx += width;
        }
        if (series_.isEmpty() || end_ <= start_) {
            p.setPen(palette().text().color());
            p.drawText(rect().adjusted(16, rowHeight + 12, -16, -16), Qt::AlignCenter,
                       "Choose a range and planets, then Calculate.");
            return;
        }
        if (std::all_of(series_.cbegin(), series_.cend(), [](const auto& s) { return s.body == SE_MOON; })) {
            p.setPen(palette().text().color());
            p.drawText(rect().adjusted(16, rowHeight + 12, -16, -16), Qt::AlignCenter | Qt::TextWordWrap,
                       "Moon has no Moorthi. Use Combined score for its Tara and BAV.");
            return;
        }
        int left = 0;
        for (int i = 0; i < 4; ++i) left = std::max(left, fm.horizontalAdvance(metalLabel(Moorthi(i))));
        area_ = QRectF(left + 24, ly + rowHeight + 6, width() - left - 42,
                       height() - ly - rowHeight - 66);
        if (area_.width() < 40 || area_.height() < 40) return;
        const QColor metals[] = {QColor("#c5a334"), QColor("#849bad"), QColor("#b87851"), QColor("#7c8490")};
        QColor grid = palette().text().color(); grid.setAlpha(40);
        for (int i = 0; i < 4; ++i) {
            const double top = area_.top() + area_.height() * i / 4;
            QColor tint = metals[i]; tint.setAlpha(20);
            p.fillRect(QRectF(area_.left(), top, area_.width(), area_.height() / 4), tint);
            const double y = ordinate(Moorthi(i));
            p.setPen(grid); p.drawLine(QPointF(area_.left(), y), QPointF(area_.right(), y));
            p.setPen(palette().text().color());
            p.drawText(QRectF(4, y - rowHeight / 2.0, area_.left() - 14, rowHeight),
                       Qt::AlignRight | Qt::AlignVCenter, metalLabel(Moorthi(i)));
        }
        drawDates(p, grid);
        p.setPen(palette().mid().color()); p.drawRect(area_);
        p.save(); p.setClipRect(area_.adjusted(-4, -4, 4, 4));
        bool havePoints = false;
        for (int s = 0; s < series_.size(); ++s) {
            const auto& series = series_[s];
            if (series.body == SE_MOON || series.entries.isEmpty()) continue;
            havePoints = true;
            QPainterPath path;
            for (int i = 0; i < series.entries.size(); ++i) {
                const auto& entry = series.entries[i];
                const QPointF point(abscissa(std::max(entry.jd, start_)), ordinate(entry.moorthi));
                if (i == 0) path.moveTo(point); else path.lineTo(point);
                hits_.push_back({point, s, i});
            }
            // A range with no entries still shows its carried-in metal. Extend
            // only through time actually scanned, including stopped searches.
            path.lineTo(abscissa(std::min(series.calculatedThrough, end_)),
                        ordinate(series.entries.last().moorthi));
            const QColor color = planetColor(series.colorIndex, palette());
            p.setPen(QPen(color, 1.8, planetLine(series.planet)));
            p.setBrush(Qt::NoBrush); p.drawPath(path);
            p.setPen(QPen(color, 1.4));
            for (const auto& hit : hits_) {
                if (hit.series != s) continue;
                const bool carried = series.entries[hit.entry].jd < start_;
                p.setBrush(carried ? palette().base().color() : color);
                const double radius = series.planet.startsWith("Ketu") ? 2.3 : 3.3;
                p.drawEllipse(hit.position, radius, radius);
            }
        }
        p.restore();
        if (!havePoints) {
            p.setPen(palette().text().color());
            p.drawText(area_, Qt::AlignCenter, "No calculated points yet.");
        }
        p.setPen(palette().text().color());
        p.drawText(QRectF(area_.left(), height() - fm.height() - 5, area_.width(), fm.height()),
                   Qt::AlignCenter, "Time · " + QString::fromUtf8(zone_.id()));
    }

    bool event(QEvent* event) override {
        if (event->type() != QEvent::ToolTip) return QWidget::event(event);
        if (pressed_) {QToolTip::hideText(); event->ignore(); return true;}
        const auto* help = static_cast<QHelpEvent*>(event);
        QStringList details;
        for (const auto& hit : hits_) {
            if (!area_.contains(hit.position) || QLineF(help->pos(), hit.position).length() > 8 || hit.series >= series_.size()) continue;
            const auto& series = series_[hit.series];
            if (hit.entry >= series.entries.size()) continue;
            const auto& entry = series.entries[hit.entry];
            QString text = series.planet + " · " + metalLabel(entry.moorthi);
            if (entry.jd < start_) text += "\nIn effect at range start; entered earlier:";
            text += QString("\n%1\n%2 · %3 entry\nMoon at entry: %4 · Count: %5")
                .arg(local(entry.jd, zone_).toString("dd MMM yyyy HH:mm:ss.zzz ttt"),
                     signName(entry.newSign), entry.retrograde ? "Retrograde" : "Direct",
                     signName(entry.moonSign)).arg(entry.count);
            details << text;
        }
        if (details.isEmpty()) { QToolTip::hideText(); event->ignore(); }
        else QToolTip::showText(help->globalPos(), details.join("\n\n"), this);
        return true;
    }

private:
    double abscissa(double jd) const { return area_.left() + (jd - range_.start()) / (range_.end() - range_.start()) * area_.width(); }
    double ordinate(Moorthi metal) const { return area_.top() + (int(metal) + 0.5) * area_.height() / 4; }
    void drawDates(QPainter& p, const QColor& grid) {
        const double start=range_.start(), end=range_.end(), days=end-start;
        const int count = std::clamp(int(area_.width() / 100), 2, 16);
        QVector<QPair<double, QString>> ticks;
        const QDate first = local(start, zone_).date();
        auto addDate = [&](const QDate& date, const QString& format) {
            const auto time = date.startOfDay(zone_);
            if (time.isValid()) ticks.push_back({julian(time), date.toString(format)});
        };
        if (days > 730) {
            const double desired = days / 365.25 / count;
            const double power = std::pow(10.0, std::floor(std::log10(std::max(1.0, desired))));
            const int step = int((desired / power <= 1 ? 1 : desired / power <= 2 ? 2 : desired / power <= 5 ? 5 : 10) * power);
            for (int year = ((first.year() + step - 1) / step) * step; year <= 9999; year += step) {
                const QDate date(year, 1, 1);
                if (julian(date.startOfDay(zone_)) > end) break;
                addDate(date, "yyyy");
            }
        } else if (days > 60) {
            const double desired = days / 30.44 / count;
            const int step = desired <= 1 ? 1 : desired <= 2 ? 2 : desired <= 3 ? 3 : desired <= 6 ? 6 : 12;
            for (QDate date(first.year(), first.month(), 1); date.isValid() && julian(date.startOfDay(zone_)) <= end; date = date.addMonths(step))
                addDate(date, "MMM yyyy");
        } else if (days > 2) {
            const int step = std::max(1, int(std::ceil(days / count)));
            for (QDate date = first; date.isValid() && julian(date.startOfDay(zone_)) <= end; date = date.addDays(step))
                addDate(date, "dd MMM");
        } else {
            const double hours = days * 24 / count;
            for (int i = 0; i <= count; ++i) {
                const double jd = start + i * hours / 24;
                ticks.push_back({jd, local(jd, zone_).toString("dd MMM HH:mm")});
            }
        }
        for (const auto& tick : ticks) {
            if (tick.first < start || tick.first > end) continue;
            const double x = abscissa(tick.first);
            p.setPen(grid); p.drawLine(QPointF(x, area_.top()), QPointF(x, area_.bottom()));
            p.setPen(palette().text().color());
            const int w = fontMetrics().horizontalAdvance(tick.second) + 8;
            const double labelX = std::clamp(x - w / 2.0, 1.0, double(width() - w - 1));
            p.drawText(QRectF(labelX, area_.bottom() + 6, w, fontMetrics().height()), Qt::AlignCenter, tick.second);
        }
    }
    struct Hit { QPointF position; int series; int entry; };
    const QVector<MoorthiGraphSeries>& series_;
    QVector<Hit> hits_;
    QRectF area_;
    QTimeZone zone_;
    double start_ = 0.0, end_ = 0.0;
    TimeGraphRange range_;
    bool pressed_=false, dragging_=false;
    QPointF pressPosition_;
    double panStart_=0;
};

MoorthiGraphPanel::MoorthiGraphPanel(SwissEph* swe, QWidget* parent) : QWidget(parent), swe_(swe) {
    setObjectName("moorthiGraphPanel");
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(6, 6, 6, 4); layout->setSpacing(5);
    auto* controls = new CompactControls;
    from_ = new QDateEdit(QDate(QDate::currentDate().year(), 1, 1), this);
    through_ = new QDateEdit(QDate(QDate::currentDate().year(), 12, 31), this);
    from_->setObjectName("moorthiGraphFrom"); through_->setObjectName("moorthiGraphThrough");
    for (auto* date : {from_, through_}) {
        date->setDateRange(QDate(1, 1, 1), QDate(9999, 12, 31));
        date->setCalendarPopup(true); date->setDisplayFormat("dd MMM yyyy"); date->setMinimumWidth(118);
    }
    planets_ = new QToolButton(this); planets_->setObjectName("moorthiGraphPlanets");
    planets_->setText("All planets"); planets_->setPopupMode(QToolButton::InstantPopup);
    planets_->setMinimumWidth(154);
    menu_ = new QMenu(planets_); planets_->setMenu(menu_);
    auto group = [&](const QString& label, QWidget* control) {
        auto* widget = new QWidget(this); auto* row = new QHBoxLayout(widget);
        row->setContentsMargins(0, 0, 0, 0); row->setSpacing(4);
        row->addWidget(new QLabel(label, widget)); row->addWidget(control); controls->addWidget(widget);
    };
    group("From", from_); group("Through", through_); group("Planets", planets_);
    view_ = new QComboBox(this); view_->setObjectName("moorthiGraphView");
    view_->addItems({"Moorthi metals", "Combined score"}); group("View", view_);
    run_ = new QPushButton("Calculate", this); run_->setObjectName("moorthiGraphCalculate");
    stop_ = new QPushButton("Stop", this); stop_->setObjectName("moorthiGraphStop");
    run_->setEnabled(false); stop_->setEnabled(false);
    auto* actions = new QWidget(this); auto* buttons = new QHBoxLayout(actions);
    buttons->setContentsMargins(0, 0, 0, 0); buttons->setSpacing(4);
    buttons->addWidget(run_); buttons->addWidget(stop_);
    controls->addWidget(actions); layout->addLayout(controls);
    context_ = new QLabel("Load a birth chart to calculate.", this);
    context_->setTextFormat(Qt::PlainText); context_->setWordWrap(true); layout->addWidget(context_);
    plot_ = new MoorthiGraphPlot(series_, this); layout->addWidget(plot_, 1);
    benchmark_ = new VedicBenchmarkPanel(swe_, this); layout->addWidget(benchmark_, 1); benchmark_->hide();
    hint_ = new QLabel("Dots: sign entries · Hollow dot: metal at range start · Lines connect events; intermediate heights are not metal values.", this);
    hint_->setObjectName("hintLabel"); hint_->setWordWrap(true); layout->addWidget(hint_);
    status_ = new QLabel(this); status_->setObjectName("moorthiGraphStatus");
    status_->setTextFormat(Qt::PlainText); status_->setWordWrap(true); layout->addWidget(status_);
    timer_ = new QTimer(this); timer_->setInterval(0); timer_->setObjectName("moorthiGraphTimer");
    connect(timer_, &QTimer::timeout, this, [this] { step(); });
    connect(run_, &QPushButton::clicked, this, [this] { start(); });
    connect(stop_, &QPushButton::clicked, this, [this] {
        if (benchmark_->isRunning()) benchmark_->stop(); else finish("Stopped · partial results");
    });
    benchmark_->onBusyChanged = [this](bool busy) { setBusy(busy); };
    benchmark_->onMomentSelected = [this](qint64 ms) { if (onMomentSelected) onMomentSelected(ms); };
    benchmark_->onPlanetScopeChanged = [this] { updatePlanetLabel(); invalidate(); };
    connect(view_, &QComboBox::currentIndexChanged, this, [this] {
        const bool score = view_->currentIndex() == 1;
        plot_->setVisible(!score); hint_->setVisible(!score); benchmark_->setVisible(score);
        if (benchmark_->activeDashasOnly()) { updatePlanetLabel(); invalidate(); }
        if (score && benchmark_->hasSource() && benchmark_->samples().isEmpty()) benchmark_->calculate();
    });
    connect(from_, &QDateEdit::dateChanged, this, [this] { invalidate(); });
    connect(through_, &QDateEdit::dateChanged, this, [this] { invalidate(); });
}

void MoorthiGraphPanel::setContext(const NatalInput& input, const NatalChart& chart) {
    const auto moon = std::find_if(chart.bodies.cbegin(), chart.bodies.cend(), [](const auto& b) { return b.name == "Moon"; });
    QString key = QString("%1|%2|%3|%4|%5|%6|%7")
        .arg(input.name, input.date.toString(Qt::ISODate), input.time.toString("HH:mm:ss.zzz"), input.timezone,
             QString::number(int(chart.siderealAyanamsa)), lunarNodePolicySummary(input.lunarNodePolicy),
             moon == chart.bodies.cend() ? "" : QString::number(moon->longitude, 'g', 17));
    // BAV depends on every natal planet and Lagna, not just the Moon.
    key += "|" + QString::number(chart.angles.asc, 'g', 17);
    key += "|" + chart.utcDateTime.toString(Qt::ISODateWithMs);
    key += QString("|%1|%2|%3|%4").arg(int(input.siderealAyanamsa)).arg(int(chart.zodiacSystem))
        .arg(input.latitude, 0, 'g', 17).arg(input.longitude, 0, 'g', 17);
    for (const auto& body : chart.bodies) key += "|" + body.name + ":" + QString::number(body.longitude, 'g', 17);
    input_ = input; input_.zodiacSystem = chart.zodiacSystem;
    if (key == sourceKey_) return;
    sourceKey_ = key; ayanamsa_ = chart.siderealAyanamsa;
    benchmark_->setContext(input, chart);
    natalMoonSign_ = moon == chart.bodies.cend() || !std::isfinite(moon->longitude) ? -1 : signIndex(moon->longitude);
    invalidate();
    QSet<QString> chosen, previous;
    const bool allSelected = choices_.isEmpty() || std::all_of(choices_.cbegin(), choices_.cend(), [](auto* c) { return c->isChecked(); });
    for (auto* choice : choices_) {
        previous.insert(choice->text());
        if (choice->isChecked()) chosen.insert(choice->text());
    }
    choices_.clear(); menu_->clear();
    targets_ = {{"Sun", SE_SUN, 0, 0}, {"Moon", SE_MOON, 0, 1}, {"Mars", SE_MARS, 0, 2},
                {"Mercury", SE_MERCURY, 0, 3}, {"Jupiter", SE_JUPITER, 0, 4},
                {"Venus", SE_VENUS, 0, 5}, {"Saturn", SE_SATURN, 0, 6}};
    for (const auto type : {LunarNodeType::Mean, LunarNodeType::True}) {
        if (!lunarNodePolicyIncludes(input.lunarNodePolicy, type)) continue;
        const int id = type == LunarNodeType::Mean ? SE_MEAN_NODE : SE_TRUE_NODE;
        const QString suffix = QString(" (%1)").arg(lunarNodeTypeToString(type));
        targets_.push_back({"Rahu" + suffix, id, 0, 7}); targets_.push_back({"Ketu" + suffix, id, 180, 8});
    }
    auto addChoice = [&](const QString& name) {
        auto* action = new QWidgetAction(menu_);
        auto* check = new QCheckBox(name, menu_);
        check->setContentsMargins(8, 3, 12, 3);
        action->setDefaultWidget(check); menu_->addAction(action);
        return check;
    };
    all_ = addChoice("All planets"); all_->setObjectName("moorthiGraphAll"); menu_->addSeparator();
    for (const auto& target : targets_) {
        auto* choice = addChoice(target.planet);
        choice->setObjectName("moorthiGraphChoice"); choice->setProperty("planet", target.planet);
        if (target.body == SE_MOON) choice->setToolTip("No Moorthi. Included in Combined score through Tara/BAV; sign entries remain available as sampling anchors.");
        bool selected = allSelected || chosen.contains(target.planet);
        if (!previous.contains(target.planet))
            for (const auto& name : chosen) selected |= name.section(" (", 0, 0) == target.planet.section(" (", 0, 0);
        choice->setChecked(selected); choices_.push_back(choice);
        connect(choice, &QCheckBox::toggled, this, [this] { selectionChanged(); });
    }
    connect(all_, &QCheckBox::clicked, this, [this](bool checked) {
        for (auto* choice : choices_) { const QSignalBlocker block(choice); choice->setChecked(checked); }
        selectionChanged();
    });
    context_->setText(natalMoonSign_ < 0 ? "Natal Moon unavailable." :
        QString("Natal Moon: %1 · %2 · %3 · Dates in %4")
            .arg(signName(natalMoonSign_), zodiacDescription(input_.zodiacSystem, ayanamsa_),
                 lunarNodePolicySummary(input.lunarNodePolicy), input.timezone));
    selectionChanged();
}

void MoorthiGraphPanel::setDashaYearDays(double days) { benchmark_->setDashaYearDays(days); }
bool MoorthiGraphPanel::automaticPlanets() const {
    return view_->currentIndex() == 1 && benchmark_->activeDashasOnly();
}
void MoorthiGraphPanel::updatePlanetLabel() {
    QStringList names;
    for (auto* choice : choices_) if (choice->isChecked()) names << choice->text();
    if (all_) {
        const QSignalBlocker block(all_);
        all_->setCheckState(names.isEmpty() ? Qt::Unchecked : names.size() == choices_.size() ? Qt::Checked : Qt::PartiallyChecked);
    }
    planets_->setText(names.size() == choices_.size() ? "All planets" : names.isEmpty() ? "Select planets" :
                      names.size() == 1 ? names.first() : QString("%1 planets selected").arg(names.size()));
    planets_->setToolTip(names.join(", "));
    if (automaticPlanets()) {
        planets_->setText("Active dasha lords");
        planets_->setToolTip("Automatically follows the enabled dasha levels at each graph date. Manual planet selection is retained for normal mode.");
    }
    planets_->setEnabled(!automaticPlanets() && !timer_->isActive() && !benchmark_->isRunning());
}
void MoorthiGraphPanel::selectionChanged() {
    updatePlanetLabel();
    invalidate();
}

void MoorthiGraphPanel::invalidate() {
    if (timer_->isActive()) finish("Calculation cancelled: inputs changed.");
    benchmark_->clear();
    series_.clear(); plot_->clearHits();
    status_->setText("Choose a range and planets, then Calculate.");
    const bool selected = std::any_of(choices_.cbegin(), choices_.cend(), [](auto* c) { return c->isChecked(); });
    run_->setEnabled(natalMoonSign_ >= 0 && (selected || automaticPlanets()));
}

void MoorthiGraphPanel::start() {
    if (timer_->isActive() || benchmark_->isRunning()) return;
    if (view_->currentIndex() == 1 && benchmark_->hasSource()) { benchmark_->calculate(); return; }
    if (!swe_ || !swe_->isLoaded() || natalMoonSign_ < 0) { status_->setText("Natal Moon or ephemeris unavailable."); return; }
    if (from_->date() > through_->date()) { status_->setText("From must be on or before Through."); return; }
    QString error, label;
    if (!parseTimezoneInput(input_.timezone, &zone_, &label, &error)) { status_->setText(error); return; }
    const auto start = from_->date().startOfDay(zone_);
    const auto end = through_->date().addDays(1).startOfDay(zone_);
    if (!start.isValid() || !end.isValid() || end <= start) { status_->setText("Invalid date range for this timezone."); return; }
    invalidate();
    startJd_ = julian(start); endJd_ = julian(end);
    minimumJd_ = julian(QDateTime(QDate(1, 1, 1), QTime(0, 0), QTimeZone::UTC));
    for (int i = 0; i < choices_.size(); ++i) {
        if (!automaticPlanets() && !choices_[i]->isChecked()) continue;
        series_.push_back(targets_[i]); series_.last().calculatedThrough = startJd_;
    }
    if (series_.isEmpty()) { status_->setText("Select at least one planet."); return; }
    seriesIndex_ = 0; lookingBack_ = true; cursorJd_ = startJd_ + rootTolerance;
    plot_->setRange(startJd_, endJd_, zone_);
    setBusy(true);
    lastProgressMs_ = 0; updateProgress(); timer_->start();
}

void MoorthiGraphPanel::step() {
    QElapsedTimer elapsed; elapsed.start();
    QString error;
    const int zodiacFlags = input_.zodiacSystem == ZodiacSystem::Sidereal ? SEFLG_SIDEREAL : 0;
    swe_->setSidMode(siderealAyanamsaSwissMode(ayanamsa_));
    while (elapsed.elapsed() < 12 && seriesIndex_ < series_.size()) {
        auto& series = series_[seriesIndex_];
        if (lookingBack_ && cursorJd_ <= minimumJd_) {
            error = "No earlier entry within the calendar range."; break;
        }
        const double first = lookingBack_ ? std::max(minimumJd_, cursorJd_ - 0.25) : cursorJd_;
        const double last = lookingBack_ ? cursorJd_ : std::min(endJd_, cursorJd_ + 0.25);
        QVector<MoorthiEntry> entries;
        if (!findMoorthiEntries(*swe_, series.body, series.offset, first, last, natalMoonSign_, &entries, &error, zodiacFlags)) {
            if (error.isEmpty()) error = "Ephemeris could not calculate this interval.";
            break;
        }
        if (lookingBack_) {
            for (auto it = entries.crbegin(); it != entries.crend(); ++it) {
                if (it->jd <= startJd_ + 0.005 / 86400.0) { series.entries.push_back(*it); break; }
            }
            cursorJd_ = first;
            if (!series.entries.isEmpty()) { lookingBack_ = false; cursorJd_ = startJd_; }
        } else {
            for (const auto& entry : entries) {
                if (entry.jd >= endJd_ || entry.jd <= series.entries.last().jd + rootTolerance) continue;
                series.entries.push_back(entry);
            }
            series.calculatedThrough = cursorJd_ = last;
            if (cursorJd_ >= endJd_) { ++seriesIndex_; lookingBack_ = true; cursorJd_ = startJd_ + rootTolerance; }
        }
    }
    // The shared Swiss wrapper must be restored before yielding to other tabs.
    swe_->setSidMode(siderealAyanamsaSwissMode(input_.siderealAyanamsa));
    if (!error.isEmpty()) {
        finish(QString("Unavailable: %1 near %2 · %3 · Partial results")
            .arg(series_[seriesIndex_].planet, local(cursorJd_, zone_).toString("dd MMM yyyy"), error));
    } else if (seriesIndex_ >= series_.size()) finish("Complete");
    else if (QDateTime::currentMSecsSinceEpoch() - lastProgressMs_ >= 150) updateProgress();
}

void MoorthiGraphPanel::updateProgress() {
    lastProgressMs_ = QDateTime::currentMSecsSinceEpoch();
    if (seriesIndex_ < series_.size()) {
        const QString phase = lookingBack_ ? "Finding sign entry at range start" :
            QString("Calculating · %1%").arg(int(100 * (cursorJd_ - startJd_) / (endJd_ - startJd_)));
        status_->setText(QString("%1 · %2 · Planet %3 of %4").arg(series_[seriesIndex_].planet, phase)
                         .arg(seriesIndex_ + 1).arg(series_.size()));
    }
    plot_->update();
}

void MoorthiGraphPanel::finish(const QString& message) {
    timer_->stop(); setBusy(false);
    int entries = 0, complete = 0;
    for (const auto& series : series_) {
        if (series.calculatedThrough >= endJd_) ++complete;
        for (const auto& entry : series.entries) if (entry.jd >= startJd_ - 0.005 / 86400.0 && entry.jd < endJd_) ++entries;
    }
    status_->setText(QString("%1 · %2 of %3 planets complete · %4 sign entries in range")
                     .arg(message).arg(complete).arg(series_.size()).arg(entries));
    plot_->update();
    if (message == "Complete" && complete == series_.size() && complete > 0) {
        benchmark_->setSeries(series_, startJd_, endJd_, zone_);
        if (view_->currentIndex() == 1) benchmark_->calculate();
    }
}
void MoorthiGraphPanel::setBusy(bool busy) {
    stop_->setEnabled(busy); run_->setEnabled(!busy && natalMoonSign_ >= 0);
    from_->setEnabled(!busy); through_->setEnabled(!busy); planets_->setEnabled(!busy && !automaticPlanets()); view_->setEnabled(!busy);
    benchmark_->setControlsBusy(busy);
}
} // namespace dracoved
