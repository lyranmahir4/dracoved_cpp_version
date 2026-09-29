#include "vedic_benchmark_panel.h"
#include "compact_controls.h"
#include "time_graph_range.h"
#include "../core/formatting.h"
#include "../core/vedic_nakshatra.h"
#include "../core/vedic_planet_nature.h"
#include "../core/lunar_nodes.h"
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimeEdit>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace dracoved {
namespace {
constexpr double epoch = 2440587.5, dayMs = 86400000.0;
double jd(const QDateTime& time) { return epoch + time.toMSecsSinceEpoch() / dayMs; }
QDateTime localTime(double value, const QTimeZone& zone) { return QDateTime::fromMSecsSinceEpoch(qRound64((value-epoch)*dayMs), zone); }
const QStringList planets = {"Sun", "Moon", "Mars", "Mercury", "Jupiter", "Venus", "Saturn", "Rahu", "Ketu"};
const QStringList metals = {"Gold", "Silver", "Copper", "Iron"};
QString number(double value) { return QString::number(std::abs(value) < .00001 ? 0 : value, 'f', 1); }
bool findNatalCrossings(SwissEph& swe,int body,double offset,double from,double to,
                        const std::array<double,9>& natal,QVector<double>* events,QString* error,int zodiacFlags) {
    auto position=[&](double time,double* longitude,double* speed) {
        double values[6]{};
        if(!swe.calcUtFull(time,body,zodiacFlags|SEFLG_SPEED,values,error)) return false;
        *longitude=normalizeDegrees(values[0]+offset);*speed=values[3];
        if(std::isfinite(*longitude) && std::isfinite(*speed)) return true;
        if(error) *error="Invalid ephemeris position during natal-crossing search.";
        return false;
    };
    double a=0,b=0,sa=0,sb=0;
    if(!position(from,&a,&sa) || !position(to,&b,&sb)) return false;
    if(sa*sb<0 && to-from>1.0/86400) {
        const double middle=(from+to)/2;
        return findNatalCrossings(swe,body,offset,from,middle,natal,events,error,zodiacFlags) &&
               findNatalCrossings(swe,body,offset,middle,to,natal,events,error,zodiacFlags);
    }
    for(double target:natal) {
        if(!std::isfinite(target)) continue;
        const double da=std::remainder(a-target,360.0),db=std::remainder(b-target,360.0);
        if(std::abs(da-db)>90 || (da>0)==(db>0)) continue;
        double low=from,high=to;
        for(int i=0;i<40 && high-low>.01/86400.0;++i) {
            const double middle=(low+high)/2;
            double lon=0,speed=0;if(!position(middle,&lon,&speed)) return false;
            if((std::remainder(lon-target,360.0)>0)==(da>0)) low=middle;
            else high=middle;
        }
        events->push_back((low+high)/2);
    }
    return true;
}
void scoreSample(VedicBenchmarkSample& sample, const VedicBenchmarkRules& rules,
                 const GocharRules& gocharRules, int mode, int scope,
                 const NavamsaTransitRules& d9Rules, const std::array<double,9>& natal, double ascendant) {
    std::array<double, 9> sums{}, weights{}; std::array<int, 9> counts{};
    sample.overall = 0; sample.scoredPlanets = 0;
    sample.d9Unresolved.clear(); sample.d9NotCovered.clear();
    sample.d1Contribution=sample.d9Contribution=0;
    sample.effectiveD1Share=sample.effectiveD9Share=0;
    auto modelRules=gocharRules;modelRules.useMalefic=rules.useMalefic;
    const auto gochar = mode && scope!=1 ? calculateGochar(sample.gochar,modelRules) : std::array<GocharReading,9>{};
    for (auto& reading : sample.readings) {
        reading.gochar = {};
        if(scope==1) {
            reading.score = {}; // D9 has no dependency on D1 score components.
        } else if(mode) {
            reading.gochar=gochar[reading.planetIndex];
            if(std::isfinite(sample.gochar.transit[reading.planetIndex]) &&
               (signIndex(reading.longitude)!=signIndex(sample.gochar.transit[reading.planetIndex]) ||
                std::abs(std::remainder(reading.longitude-sample.gochar.transit[reading.planetIndex],360.0))>1e-6)) {
                auto alternate=sample.gochar; alternate.transit[reading.planetIndex]=reading.longitude;
                reading.gochar=calculateGochar(alternate,modelRules)[reading.planetIndex];
            }
            reading.score={};
            reading.score.net=gocharViewScore(reading.gochar,mode);
            reading.score.valid=std::isfinite(reading.score.net);
        } else {
            reading.score = scoreVedicBenchmark(reading.planetIndex, int(reading.entry.moorthi), reading.tara, reading.bav, rules, reading.kaksha.bindu);
        }
        reading.d1Score = reading.score;
        reading.d9 = scope ? calculateNavamsaTransit(reading.planetIndex, reading.longitude, natal, ascendant, d9Rules)
                           : NavamsaTransitReading{};
        if (scope == 1) {
            reading.score.valid = reading.d9.valid;
            reading.score.net = reading.d9.score;
        } else if (scope == 2 && reading.d9.valid) {
            const double a = reading.d1Score.valid ? 1-d9Rules.blendShare : 0;
            const double b = d9Rules.blendShare;
            reading.score.valid = a+b>0;
            reading.score.net = a+b>0 ? ((a>0?a*reading.d1Score.net:0)+b*reading.d9.score)/(a+b) : 0;
        } else if (scope == 2 && d9Rules.blendShare == 1) reading.score.valid = false;
        reading.planetWeight=vedicPlanetWeight(reading.planetIndex,reading.roleMask,rules);
        if(scope && reading.planetWeight>0) {
            if(reading.d9.incomplete && !sample.d9Unresolved.contains(reading.planet)) sample.d9Unresolved<<reading.planet;
            if(!reading.d9.supported && !sample.d9NotCovered.contains(reading.planet)) sample.d9NotCovered<<reading.planet;
        }
        if (!reading.score.valid || reading.planetWeight<=0) continue;
        weights[reading.planetIndex]=reading.planetWeight;
        sums[reading.planetIndex] += reading.score.net; ++counts[reading.planetIndex];
    }
    // Mean/True variants share one planet's weight rather than counting twice.
    double denominator=0;
    for (int p=0; p<9; ++p) if (counts[p]) {
        sample.overall += weights[p]*sums[p]/counts[p]; denominator+=weights[p]; ++sample.scoredPlanets;
    }
    if (denominator>0) sample.overall /= denominator;
    auto layerMean = [&](bool d9) {
        std::array<double,9> totals{}, importance{};
        std::array<int,9> n{};
        for (const auto& r:sample.readings) {
            if (!(d9 ? r.d9.valid : r.d1Score.valid) || r.planetWeight<=0) continue;
            totals[r.planetIndex] += d9 ? r.d9.score : r.d1Score.net;
            importance[r.planetIndex] = r.planetWeight; ++n[r.planetIndex];
        }
        double sum=0, weight=0;
        for (int p=0;p<9;++p) if(n[p]) {sum+=importance[p]*totals[p]/n[p]; weight+=importance[p];}
        return weight>0 ? sum/weight : std::numeric_limits<double>::quiet_NaN();
    };
    sample.d1Overall = layerMean(false);
    sample.d9Overall = scope ? layerMean(true) : std::numeric_limits<double>::quiet_NaN();
    if (scope == 2) {
        // Blend layer means so unsupported D9 planets do not dilute the D9 layer.
        const double a=std::isfinite(sample.d1Overall)?1-d9Rules.blendShare:0;
        const double b=std::isfinite(sample.d9Overall)?d9Rules.blendShare:0;
        sample.effectiveD1Share=a+b>0?a/(a+b):0;
        sample.effectiveD9Share=a+b>0?b/(a+b):0;
        sample.d1Contribution=a>0?sample.effectiveD1Share*sample.d1Overall:0;
        sample.d9Contribution=b>0?sample.effectiveD9Share*sample.d9Overall:0;
        sample.overall = sample.d1Contribution+sample.d9Contribution;
        if (a+b<=0) sample.scoredPlanets=0;
    }
}
void storeNavamsaRules(NavamsaTransitRules& rules, bool save) {
    QSettings settings;
    if(save) {
        settings.setValue("vedic/d9/contactPoints",rules.contactPoints);
        settings.setValue("vedic/d9/blendShare",rules.blendShare);
        settings.setValue("vedic/d9/jupiterReference",rules.jupiterDusthanaReference);
    } else {
        const double points=settings.value("vedic/d9/contactPoints",50).toDouble();
        const double share=settings.value("vedic/d9/blendShare",.5).toDouble();
        if(std::isfinite(points)) rules.contactPoints=std::clamp(points,0.0,100.0);
        if(std::isfinite(share)) rules.blendShare=std::clamp(share,0.0,1.0);
        rules.jupiterDusthanaReference=std::clamp(settings.value("vedic/d9/jupiterReference",0).toInt(),0,2);
    }
}
void storeGocharRules(GocharRules& rules,bool save) {
    QSettings settings;
    auto field=[&](const char* name,double& value,double low,double high) {
        const QString key="vedic/gochar/"+QString::fromLatin1(name);
        if(save) settings.setValue(key,value);
        else {const double saved=settings.value(key,value).toDouble();if(std::isfinite(saved)) value=std::clamp(saved,low,high);}
    };
    field("tierPoints",rules.tierPoints,1,50);
    field("nakshatraSuppression",rules.nakshatraSuppression,0,1);
    field("aspectShare",rules.aspectShare,0,1);
    field("dignityFloor",rules.dignityFloor,0,1);
    field("retrogradeShift",rules.retrogradeShift,0,1);
    field("combustionFactor",rules.combustionFactor,0,1);
    field("combustionOrb",rules.combustionOrb,0,30);
    field("pakshaFactor",rules.pakshaFactor,1,2);
    for(int i=0;i<5;++i) {
        const QString key="vedic/gochar/avastha"+QString::number(i);
        if(save) settings.setValue(key,rules.avasthaFactors[i]);
        else {const double value=settings.value(key,rules.avasthaFactors[i]).toDouble();
            if(std::isfinite(value)) rules.avasthaFactors[i]=std::clamp(value,0.0,1.0);}
    }
    field("samagamamOrb",rules.samagamamOrb,0,10);
    field("samagamamShare",rules.samagamamShare,0,1);
}
void storeRules(VedicBenchmarkRules& rules, bool save) {
    QSettings settings;
    auto values = [&](const QString& group, auto& array, double low, double high) {
        for (int i=0; i<int(array.size()); ++i) {
            const QString key = "vedic/benchmark/" + group + QString::number(i);
            if (save) settings.setValue(key, array[i]);
            else { double v = settings.value(key, array[i]).toDouble(); if (std::isfinite(v)) array[i] = std::clamp(v, low, high); }
        }
    };
    values("weight", rules.weights, 0, 10); values("benefic", rules.benefic, -1, 1);
    values("malefic", rules.malefic, -1, 1); values("tara", rules.tara, -1, 1);
    values("dashaWeight", rules.dashaWeights, 0, 10);
    values("planetWeight", rules.planetWeights, 0, 100);
    if(save) settings.setValue("vedic/benchmark/usePlanetWeights",rules.usePlanetWeights);
    else rules.usePlanetWeights=settings.value("vedic/benchmark/usePlanetWeights",false).toBool();
    if(save) settings.setValue("vedic/benchmark/weightByDasha",rules.weightByDasha);
    else rules.weightByDasha=settings.value("vedic/benchmark/weightByDasha",false).toBool();
    for (int i=0; i<9; ++i) {
        const QString key = "vedic/benchmark/type" + QString::number(i);
        if (save) settings.setValue(key, rules.useMalefic[i]);
        else rules.useMalefic[i] = settings.value(key, rules.useMalefic[i]).toBool();
    }
}
}

class VedicBenchmarkPlot final : public QWidget {
public:
    explicit VedicBenchmarkPlot(QWidget* parent) : QWidget(parent) {
        setObjectName("vedicBenchmarkPlot"); setMouseTracking(true); setMinimumSize(340, 220);
        setAccessibleDescription("Shift + drag selects a time range and shows sample totals inside the graph. Drag either selection edge to resize. Ctrl + wheel zooms; ordinary drag pans; click inspects. Right-click to clear selection or fit the graph.");
    }
    const QVector<VedicBenchmarkSample>* samples = nullptr;
    std::function<double(int)> score;
    std::function<QString(int)> tooltip;
    std::function<void(int)> clicked;
    std::function<bool(int)> incomplete;
    TimeGraphRange range;
    QTimeZone zone;
    QString title;
    QString unavailableText = "No score available for this line. Zero-weight planets are excluded; in active-dasha mode a planet must also be active. Moon has no Moorthi; nodes have no BAV.";
    int selected = -1, hovered = -1;
    bool pressed=false, dragging=false;
    QPointF pressPosition;
    double panStart=0;
    bool hasSelection=false, selectingRange=false;
    double selectionStart=0, selectionEnd=0, selectionAnchor=0;
    QRectF selectionOverlayRect;
    QRectF area() const { return QRectF(52, 32, width()-74, height()-72); }
    void clearSelection() {hasSelection=selectingRange=false; selectionOverlayRect={}; update();}
    double selectionX(double time) const {
        return area().left()+(time-range.start())/(range.end()-range.start())*area().width();
    }
    int selectionEdge(QPointF position) const {
        if(!hasSelection || !area().contains(position)) return 0;
        const double left=std::abs(position.x()-selectionX(selectionStart));
        const double right=std::abs(position.x()-selectionX(selectionEnd));
        if(std::min(left,right)>7) return 0;
        return left<=right?1:2;
    }
    double selectionTime(double x) const {
        const double time=range.start()+std::clamp((x-area().left())/area().width(),0.0,1.0)*(range.end()-range.start());
        // Snap to calculated sample times, including unscored samples, so a
        // selected endpoint is included exactly. Keep snapping inside the view.
        auto next=std::lower_bound(samples->cbegin(),samples->cend(),time,[](const auto& s,double t){return s.jd<t;});
        double snapped=time, distance=std::numeric_limits<double>::infinity();
        auto consider=[&](double candidate) {
            if(candidate>=range.start() && candidate<=range.end() && std::abs(candidate-time)<distance) {
                snapped=candidate; distance=std::abs(candidate-time);
            }
        };
        if(next!=samples->cend()) consider(next->jd);
        if(next!=samples->cbegin()) consider((next-1)->jd);
        return snapped;
    }
    void extendSelection(double x) {
        const double time=selectionTime(x);
        selectionStart=std::min(selectionAnchor,time); selectionEnd=std::max(selectionAnchor,time);
        hovered=-1; QToolTip::hideText(); update();
    }
    void paintSelectionSummary(QPainter& p) {
        if(!hasSelection) return;
        double positive=0, negative=0; int total=0, scored=0, partial=0;
        const auto begin=std::lower_bound(samples->cbegin(),samples->cend(),selectionStart,[](const auto& s,double t){return s.jd<t;});
        const auto end=std::upper_bound(begin,samples->cend(),selectionEnd,[](double t,const auto& s){return t<s.jd;});
        for(auto it=begin;it!=end;++it) {
            const int index=int(it-samples->cbegin()); const double value=score(index); ++total;
            if(incomplete && incomplete(index)) ++partial;
            if(!std::isfinite(value)) continue;
            ++scored; if(value>0) positive+=value; else if(value<0) negative+=value;
        }
        auto signedNumber=[](double value) {return (value>0?QString("+"):QString())+number(value);};
        const QString dates=localTime(selectionStart,zone).toString("d MMM yyyy HH:mm:ss")+" → "+localTime(selectionEnd,zone).toString("d MMM yyyy HH:mm:ss");
        QString note=QString("Sample totals · %1 of %2 scored").arg(scored).arg(total);
        if(total>scored) note+=QString(" · %1 unscored").arg(total-scored);
        if(partial) note+=QString(" · %1 incomplete D9 (partial assessment)").arg(partial);
        const QStringList labels{"Positive "+(scored?signedNumber(positive):"N/A"),
            "Negative "+(scored?signedNumber(negative):"N/A"),"Net "+(scored?signedNumber(positive+negative):"N/A")};
        const auto fm=fontMetrics(); const auto r=area();
        const double boxWidth=std::min(r.width()-12, double(std::max({fm.horizontalAdvance(dates),fm.horizontalAdvance(note),
            fm.horizontalAdvance(labels.join("     "))})+20));
        const double textWidth=boxWidth-20;
        const double dateHeight=fm.boundingRect(QRect(0,0,int(textWidth),1000),Qt::TextWordWrap,dates).height();
        const double noteHeight=fm.boundingRect(QRect(0,0,int(textWidth),1000),Qt::TextWordWrap,note).height();
        double x=0,y=0;
        QVector<QPointF> positions;
        for(const auto& label:labels) {
            const double w=fm.horizontalAdvance(label);
            if(x>0 && x+w>textWidth) {x=0; y+=fm.height()+3;}
            positions.push_back({x,y}); x+=w+18;
        }
        const QRectF box(r.right()-boxWidth-6,r.top()+12,boxWidth,dateHeight+noteHeight+y+fm.height()+24);
        selectionOverlayRect=box;
        p.save(); p.setPen(QPen(palette().highlight().color(),1)); p.setBrush(palette().base()); p.drawRoundedRect(box,4,4);
        const QPointF origin=box.topLeft()+QPointF(10,6);
        p.setPen(palette().text().color());
        p.drawText(QRectF(origin,QSizeF(textWidth,dateHeight)),Qt::TextWordWrap,dates);
        for(int i=0;i<labels.size();++i) {
            p.setPen(!scored?palette().text().color():i==0?QColor("#2E8B57"):i==1?QColor("#C4473A"):
                positive+negative>0?QColor("#2E8B57"):positive+negative<0?QColor("#C4473A"):palette().text().color());
            p.drawText(origin+QPointF(positions[i].x(),dateHeight+6+positions[i].y()+fm.ascent()),labels[i]);
        }
        p.setPen(partial?QColor("#B7791F"):palette().text().color());
        p.drawText(QRectF(origin+QPointF(0,dateHeight+y+fm.height()+12),QSizeF(textWidth,noteHeight)),Qt::TextWordWrap,note);
        p.restore();
    }
    QPointF point(int i) const {
        const auto r = area();
        return {r.left() + ((*samples)[i].jd-range.start())/(range.end()-range.start())*r.width(), r.center().y()-score(i)/200*r.height()};
    }
    std::pair<double,double> extremes() const {
        double low=std::numeric_limits<double>::infinity(), high=-low;
        if (samples) for (int i=0;i<samples->size();++i) {
            const double value=score(i);
            if (std::isfinite(value)) {low=std::min(low,value); high=std::max(high,value);}
        }
        return {low,high};
    }
    static bool sameScore(double a,double b) {return std::abs(a-b)<1e-8;}
protected:
    void wheelEvent(QWheelEvent* event) override {
        if (pressed || !(event->modifiers() & Qt::ControlModifier) || !area().contains(event->position()) || !samples || samples->isEmpty()) {
            event->ignore(); return;
        }
        const double steps = event->angleDelta().y() ? event->angleDelta().y()/120.0 : event->pixelDelta().y()/40.0;
        range.zoom((event->position().x()-area().left())/area().width(), steps);
        hovered=-1; setCursor(range.zoomed()?Qt::OpenHandCursor:Qt::ArrowCursor); QToolTip::hideText(); update(); event->accept();
    }
    void contextMenuEvent(QContextMenuEvent* event) override {
        pressed=dragging=selectingRange=false; unsetCursor();
        QMenu menu(this);
        auto* fit=menu.addAction("Fit graph / Reset zoom"); fit->setEnabled(range.zoomed());
        auto* clear=menu.addAction("Clear selection"); clear->setEnabled(hasSelection);
        const auto* action=menu.exec(event->globalPos());
        if(action==fit) {range.reset(); hovered=-1; QToolTip::hideText(); update();}
        else if(action==clear) clearSelection();
        event->accept();
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing); p.fillRect(rect(), palette().base());
        p.setPen(palette().text().color()); p.drawText(QRectF(6,2,width()-12,26), Qt::AlignVCenter, title);
        const double start=range.start(), end=range.end();
        if (!samples || samples->isEmpty() || end<=start) {
            p.drawText(rect(), Qt::AlignCenter, "Choose a range and planets, then Calculate."); return;
        }
        const auto [low,high]=extremes();
        const bool available=std::isfinite(low), flat=available && sameScore(low,high);
        if (available) {
            int highs=0,lows=0;
            for (int i=0;i<samples->size();++i) {const double value=score(i); highs+=sameScore(value,high); lows+=sameScore(value,low);}
            const QString summary=flat?QString("Full range: ◇ High = Low %1 (%2 points)").arg(number(high)).arg(highs):
                QString("Full range: ▲ High %1 (%2) · ▼ Low %3 (%4)").arg(number(high)).arg(highs).arg(number(low)).arg(lows);
            const int summaryWidth=std::min(width()-12,fontMetrics().horizontalAdvance(summary)+12);
            p.fillRect(QRectF(0,0,width(),30),palette().base());
            p.drawText(QRectF(6,2,std::max(0,width()-summaryWidth-24),26),Qt::AlignVCenter,
                       fontMetrics().elidedText(title,Qt::ElideRight,std::max(0,width()-summaryWidth-24)));
            p.drawText(QRectF(width()-summaryWidth-6,2,summaryWidth,26),Qt::AlignRight|Qt::AlignVCenter,
                       fontMetrics().elidedText(summary,Qt::ElideRight,summaryWidth));
        }
        const auto r = area(); QColor grid = palette().text().color(); grid.setAlpha(35);
        const bool yearAxis = end-start > 730;
        double lastYearLabel = -100;
        const int firstYear = localTime(start,zone).date().year();
        const int finalYear = localTime(end,zone).date().year();
        auto xFor = [&](double date) { return r.left()+(date-start)/(end-start)*r.width(); };
        for (int year=firstYear; year<=finalYear; ++year) {
            const auto begin = QDate(year,1,1).startOfDay(zone);
            const auto finish = QDate(year+1,1,1).startOfDay(zone);
            if (!begin.isValid()) continue;
            const double boundary = jd(begin);
            const double left = xFor(std::max(start,boundary));
            const double right = xFor(std::min(end,finish.isValid()?jd(finish):end));
            if (right<=left) continue;
            if (year%2==0) p.fillRect(QRectF(left,r.top(),right-left,r.height()),QColor(75,115,165,10));
            if (boundary>=start) {
                p.setPen(QPen(QColor(75,115,165,90),1,Qt::DashLine));
                p.drawLine(QPointF(left,r.top()),QPointF(left,r.bottom()));
                p.setPen(QPen(QColor(75,115,165,170),2));
                p.drawLine(QPointF(left,r.bottom()),QPointF(left,r.bottom()+4));
            }
            if (left-lastYearLabel>=60 && left<r.right()-30 && (yearAxis || boundary>=start)) {
                p.setPen(QColor(65,100,145));
                p.drawText(QRectF(left+3,yearAxis?r.bottom()+7:r.top()+3,48,20),Qt::AlignLeft|Qt::AlignVCenter,QString::number(year));
                lastYearLabel=left;
            }
        }
        if(hasSelection) {
            const double left=std::max(r.left(),xFor(selectionStart)), right=std::min(r.right(),xFor(selectionEnd));
            if(right>=left) {QColor fill=palette().highlight().color(); fill.setAlpha(28); p.fillRect(QRectF(left,r.top(),std::max(1.0,right-left),r.height()),fill);}
        }
        for (int value : {-100,-50,0,50,100}) {
            const double y = r.center().y()-value/200.0*r.height();
            p.setPen(QPen(value==0 ? palette().mid().color() : grid, value==0 ? 1.5 : 1));
            p.drawLine(QPointF(r.left(),y),QPointF(r.right(),y)); p.setPen(palette().text().color());
            p.drawText(QRectF(0,y-10,44,20),Qt::AlignRight|Qt::AlignVCenter,QString::number(value));
        }
        const int ticks = std::clamp(int(r.width()/125),1,12);
        const QString format = (end-start)/ticks < 1 ? "d MMM HH:mm" : end-start>370 ? "MMM yyyy" : "d MMM";
        for (int i=0;!yearAxis && i<=ticks;++i) {
            const double x = r.left()+i*r.width()/ticks;
            p.setPen(grid); p.drawLine(QPointF(x,r.top()),QPointF(x,r.bottom()));
            p.setPen(palette().text().color());
            p.drawText(QRectF(std::clamp(x-57,0.0,double(width()-114)),r.bottom()+7,114,24),Qt::AlignCenter,
                       localTime(start+(end-start)*i/ticks,zone).toString(format));
        }
        auto color = [&](double value) { return value>0 ? QColor("#2E8B57") : value<0 ? QColor("#C4473A") : palette().text().color(); };
        p.save(); p.setClipRect(r.adjusted(-6,-6,6,6));
        const int first=int(std::lower_bound(samples->cbegin(),samples->cend(),start,[](const auto& s,double time){return s.jd<time;})-samples->cbegin());
        const int last=int(std::upper_bound(samples->cbegin(),samples->cend(),end,[](double time,const auto& s){return time<s.jd;})-samples->cbegin());
        for (int i=std::max(0,first-1);i<std::min(int(samples->size()),last+1);++i) {
            const double value = score(i); if (!std::isfinite(value)) continue;
            const QPointF b = point(i);
            if (i>0 && std::isfinite(score(i-1))) {
                const double previous = score(i-1); const QPointF a = point(i-1);
                if (previous*value<0) {
                    const QPointF cross(a.x()+(b.x()-a.x())*previous/(previous-value),r.center().y());
                    p.setPen(QPen(color(previous),1.8)); p.drawLine(a,cross);
                    p.setPen(QPen(color(value),1.8)); p.drawLine(cross,b);
                } else { p.setPen(QPen(color(value==0 ? previous : value),1.8)); p.drawLine(a,b); }
            }
            p.setPen(Qt::NoPen); p.setBrush(color(value)); const double radius=last-first>r.width()/5 ? 1.5 : 3;
            p.drawEllipse(b,radius,radius);
        }
        // Amber ticks are coverage warnings, not score values. They also allow
        // inspection when a conditional D9 contact has no numeric score at all.
        if(incomplete) for(int i=first;i<last;++i) if(incomplete(i)) {
            const double x=r.left()+((*samples)[i].jd-start)/(end-start)*r.width();
            p.setPen(QPen(QColor("#B7791F"),2));
            p.drawLine(QPointF(x,r.top()+1),QPointF(x,r.top()+7));
        }
        // Mark every tied extreme of the full calculated line, including while zoomed.
        for (int i=first;i<last;++i) {
            const double value=score(i);
            const bool isHigh=sameScore(value,high), isLow=sameScore(value,low);
            if (!isHigh && !isLow) continue;
            const auto pos=point(i); const double x=pos.x(), y=pos.y();
            p.setPen(QPen(flat?palette().highlight().color():isHigh?QColor("#276BB0"):QColor("#8D4E92"),1.8));
            p.setBrush(palette().base());
            if (flat) p.drawPolygon(QPolygonF{QPointF(x,y-5),QPointF(x+5,y),QPointF(x,y+5),QPointF(x-5,y)});
            else {const double direction=isHigh?-1:1;
                p.drawPolygon(QPolygonF{QPointF(x,y+direction*5),QPointF(x-5,y-direction*4),QPointF(x+5,y-direction*4)});}
        }
        auto guide = [&](int index, bool hover) {
            if (index<0 || index>=samples->size() || !std::isfinite(score(index))) return;
            const auto pos=point(index);
            if (pos.x()<r.left() || pos.x()>r.right()) return;
            QColor ink=palette().highlight().color(); ink.setAlpha(hover?160:85);
            p.setPen(QPen(ink,1,Qt::DashLine));
            p.drawLine(QPointF(pos.x(),r.top()),QPointF(pos.x(),r.bottom()));
            p.drawLine(QPointF(r.left(),pos.y()),QPointF(r.right(),pos.y()));
            p.setPen(QPen(palette().highlight().color(),hover?2.5:1.8));
            p.setBrush(palette().base()); p.drawEllipse(pos,hover?6:4.5,hover?6:4.5);
        };
        guide(selected,false); guide(hovered,true);
        if(hasSelection) for(double time:{selectionStart,selectionEnd}) {
            const double x=xFor(time); if(x<r.left() || x>r.right()) continue;
            p.setPen(QPen(palette().highlight().color(),1.5)); p.setBrush(palette().base());
            p.drawLine(QPointF(x,r.top()),QPointF(x,r.bottom()));
            p.drawRoundedRect(QRectF(x-4,r.center().y()-11,8,22),2,2);
        }
        p.restore();
        if (hovered>=0 && hovered<samples->size() && std::isfinite(score(hovered))) {
            const auto pos=point(hovered);
            auto badge = [&](QRectF box,const QString& text) {
                p.setPen(QPen(palette().highlight().color(),1)); p.setBrush(palette().base());
                p.drawRoundedRect(box,3,3); p.setPen(palette().text().color()); p.drawText(box,Qt::AlignCenter,text);
            };
            badge(QRectF(1,std::clamp(pos.y()-11,0.0,double(height()-24)),48,22),number(score(hovered)));
            const QString date=localTime((*samples)[hovered].jd,zone).toString("dd MMM yyyy HH:mm");
            const int w=fontMetrics().horizontalAdvance(date)+14;
            badge(QRectF(std::clamp(pos.x()-w/2.0,0.0,double(width()-w)),r.bottom()+5,w,24),date);
        }
        if (!available) {
            p.setPen(palette().text().color());
            p.drawText(r.adjusted(12,12,-12,-12), Qt::AlignCenter|Qt::TextWordWrap,
                unavailableText);
        }
        paintSelectionSummary(p);
    }
    int nearest(QPointF position) const {
        if (!samples || range.end()<=range.start() || !area().contains(position)) return -1;
        double best=std::numeric_limits<double>::infinity(), bestY=best; int nearest=-1;
        for(int i=0;i<samples->size();++i) {
            if ((*samples)[i].jd<range.start() || (*samples)[i].jd>range.end()) continue;
            const bool hasScore=std::isfinite(score(i));
            if(!hasScore && !(incomplete && incomplete(i))) continue;
            const auto marker=hasScore?point(i):QPointF(area().left()+((*samples)[i].jd-range.start())/(range.end()-range.start())*area().width(),area().top()+4);
            const auto delta=marker-position; const double distance=std::abs(delta.x());
            if(distance<best-1e-6 || (std::abs(distance-best)<=1e-6 && std::abs(delta.y())<bestY)) {
                best=distance; bestY=std::abs(delta.y()); nearest=i;
            }
        }
        return nearest;
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (pressed && (event->buttons() & Qt::LeftButton)) {
            if(selectingRange) {
                extendSelection(event->position().x()); setCursor(Qt::SizeHorCursor); event->accept(); return;
            }
            const double dx=event->position().x()-pressPosition.x();
            if (range.zoomed() && std::abs(dx)>=QApplication::startDragDistance()) dragging=true;
            if (dragging) {
                range.panTo(panStart-dx/area().width()*(range.end()-range.start()));
                hovered=-1; setCursor(Qt::ClosedHandCursor); QToolTip::hideText(); update();
            }
            event->accept(); return;
        }
        pressed=dragging=selectingRange=false;
        if(area().contains(event->position()) && ((event->modifiers() & Qt::ShiftModifier) || selectionEdge(event->position()))) {
            hovered=-1; QToolTip::hideText();
            setCursor(event->modifiers() & Qt::ShiftModifier?Qt::CrossCursor:Qt::SizeHorCursor);
            update(); event->accept(); return;
        }
        if(hasSelection && selectionOverlayRect.contains(event->position())) {
            hovered=-1; QToolTip::hideText(); update();
            setCursor(range.zoomed()?Qt::OpenHandCursor:Qt::ArrowCursor); event->accept(); return;
        }
        const int i=nearest(event->position());
        setCursor(range.zoomed() && area().contains(event->position())?Qt::OpenHandCursor:i<0?Qt::ArrowCursor:Qt::PointingHandCursor);
        if (hovered!=i) { hovered=i; update(); }
        if(i>=0) {
            QString text=tooltip(i); const auto [low,high]=extremes();
            if (sameScore(score(i),high)) text+="\n▲ Highest score in the full calculated range";
            if (sameScore(score(i),low)) text+="\n▼ Lowest score in the full calculated range";
            QToolTip::showText(event->globalPosition().toPoint(),text,this);
        } else QToolTip::hideText();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button()==Qt::LeftButton && area().contains(event->position()) && samples && !samples->isEmpty()) {
            pressed=true; dragging=false; pressPosition=event->position(); panStart=range.start();
            const int edge=selectionEdge(event->position());
            if(event->modifiers() & Qt::ShiftModifier) {
                hasSelection=selectingRange=true; selectionAnchor=selectionTime(event->position().x());
                selectionStart=selectionEnd=selectionAnchor;
            } else if(edge) {
                selectingRange=true; selectionAnchor=edge==1?selectionEnd:selectionStart;
            }
            if(selectingRange) {hovered=-1; setCursor(Qt::SizeHorCursor); update();}
            QToolTip::hideText(); event->accept();
        } else event->ignore();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button()!=Qt::LeftButton || !pressed) {event->ignore(); return;}
        if(selectingRange) {
            extendSelection(event->position().x()); pressed=dragging=selectingRange=false;
            setCursor(selectionEdge(event->position())?Qt::SizeHorCursor:Qt::ArrowCursor);
            event->accept(); return;
        }
        const bool wasDrag=dragging; pressed=dragging=false;
        if (!wasDrag) {const int i=nearest(event->position()); if(i>=0) clicked(i);}
        setCursor(range.zoomed() && area().contains(event->position())?Qt::OpenHandCursor:Qt::ArrowCursor);
        event->accept();
    }
    void leaveEvent(QEvent*) override { hovered=-1; update(); QToolTip::hideText(); if(!pressed) unsetCursor(); }
};

VedicBenchmarkPanel::VedicBenchmarkPanel(SwissEph* swe, QWidget* parent) : QWidget(parent), swe_(swe) {
    setObjectName("vedicBenchmarkPanel"); storeRules(rules_,false); storeGocharRules(gocharRules_,false); storeNavamsaRules(d9Rules_,false);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(4);
    auto* controls = new CompactControls;
    sampling_=new QComboBox(this); sampling_->setObjectName("vedicBenchmarkSampling");
    sampling_->addItems({"Daily snapshot", "Planet sign entries", "Planet Kaksha entries", "Natal crossings"});
    sampling_->setToolTip("Daily: one snapshot at the chosen local time, not a daily average.\nSign entries: range start plus sign changes.\nKaksha entries: also sample every 3°45' section change of the chosen planet(s), including retrograde re-entry.\nNatal crossings: exact conjunction times of selected transit planets to natal planets.\nD9 / D1+D9 event modes always include every selected planet's sign entry; the anchor only limits additional Kaksha/natal crossings.\nActive-dasha mode also samples every change in the enabled dasha levels.");
    time_=new QTimeEdit(QTime(12,0),this); time_->setObjectName("vedicBenchmarkTime"); time_->setDisplayFormat("HH:mm");
    anchor_=new QComboBox(this); anchor_->setObjectName("vedicBenchmarkAnchor"); anchor_->hide();
    display_=new QComboBox(this); display_->setObjectName("vedicBenchmarkDisplay"); display_->addItem("Overall selected planets");
    scoreMode_=new QComboBox(this); scoreMode_->setObjectName("vedicGocharView"); scoreMode_->addItems(gocharViewNames());
    scoreMode_->setToolTip("Custom composite retains your Moorthi/Tara/BAV/Kaksha scores. Other views show successive Gochar Phaladeepika research layers. Samagamam is an event-only view.");
    chartScope_=new QComboBox(this); chartScope_->setObjectName("vedicBenchmarkChartScope");
    chartScope_->addItems({"D1", "D9 only", "D1+D9"});
    chartScope_->setToolTip("D1: existing score unchanged. D9: ordinary transits against natal Navamsa signs, using specific book contacts. D1+D9: blend the two layer means; edit the share in Scoring rules → Natal D9. Unsupported/ambiguous D9 contacts are not invented scores.");
    weights_=new QPushButton("Scoring rules…",this); weights_->setObjectName("vedicBenchmarkRules");
    copy_=new QPushButton("Copy results",this); copy_->setObjectName("vedicBenchmarkCopy"); copy_->setEnabled(false);
    controls->addWidget(new QLabel("Sample",this)); controls->addWidget(sampling_); controls->addWidget(time_); controls->addWidget(anchor_);
    controls->addWidget(new QLabel("Score",this)); controls->addWidget(scoreMode_);
    controls->addWidget(new QLabel("Chart",this)); controls->addWidget(chartScope_);
    controls->addWidget(new QLabel("Line",this)); controls->addWidget(display_); controls->addWidget(weights_); controls->addWidget(copy_);
    layout->addLayout(controls);
    auto* dashaControls=controls;
    activeDashas_=new QCheckBox("Active dasha lords only",this); activeDashas_->setObjectName("benchmarkActiveDashas");
    activeDashas_->setToolTip("Follow the active lords at each sample time, independently of manual planet selection. Repeated lords count once.");
    dashaControls->addWidget(activeDashas_);
    for (int level=0;level<5;++level) {
        auto* check=new QCheckBox(dashaLevelAbbreviation(level),this); levels_[level]=check;
        check->setObjectName("benchmarkDashaLevel"+QString::number(level)); check->setToolTip(dashaLevelName(level));
        check->setChecked(level<3); check->setEnabled(false); dashaControls->addWidget(check);
    }
    auto* splitter=new QSplitter(Qt::Vertical,this); splitter->setChildrenCollapsible(false);
    plot_=new VedicBenchmarkPlot(splitter); plot_->samples=&samples_;
    plot_->score=[this](int i){return displayedScore(i);}; plot_->tooltip=[this](int i){return tooltip(i);};
    plot_->incomplete=[this](int i) {
        if(!chartScope_->currentIndex()) return false;
        const auto& sample=samples_[i];
        if(display_->currentIndex()==0) return !sample.d9Unresolved.isEmpty();
        for(const auto& r:sample.readings) if(r.planet==display_->currentText()) return r.planetWeight>0 && r.d9.incomplete;
        return false;
    };
    plot_->clicked=[this](int i){select(i); if(onMomentSelected) onMomentSelected(qRound64((samples_[i].jd-epoch)*dayMs));};
    auto* detailPane=new QWidget(splitter); auto* detailLayout=new QVBoxLayout(detailPane); detailLayout->setContentsMargins(0,0,0,0);
    detailLabel_=new QLabel("Select a graph point for the component breakdown.",detailPane); detailLabel_->setWordWrap(true); detailLayout->addWidget(detailLabel_);
    details_=new QTableWidget(0,29,detailPane); details_->setObjectName("vedicBenchmarkDetails");
    details_->setHorizontalHeaderLabels({"Planet","Sign","Nakshatra","Moorthi","Since (local)","Tara","BAV / 8","SAV","Kaksha","Bindu","M pts","T pts","AV pts","K pts","Net","Roles","Weight",
        "Moon H","House","Sign Vedha","Star Vedha","Paryaya / pts","Strength","Aspects","Samagamam","Gochar details","D1 score","D9 score","D9 contacts / source"});
    details_->setEditTriggers(QAbstractItemView::NoEditTriggers); details_->setSelectionBehavior(QAbstractItemView::SelectRows);
    details_->verticalHeader()->hide(); details_->verticalHeader()->setDefaultSectionSize(23);
    details_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    for (int column : {0,2,5}) details_->horizontalHeader()->setSectionResizeMode(column,QHeaderView::Stretch);
    details_->horizontalHeader()->setSectionResizeMode(25,QHeaderView::Stretch);
    details_->horizontalHeader()->moveSection(15,1); details_->setColumnHidden(15,true);
    details_->horizontalHeader()->moveSection(16,2); details_->setColumnHidden(16,true);
    for(int col=17;col<29;++col) details_->setColumnHidden(col,true);
    details_->horizontalHeader()->setSectionResizeMode(28,QHeaderView::Stretch);
    details_->setAlternatingRowColors(true);
    details_->setMinimumHeight(125); detailLayout->addWidget(details_); splitter->setSizes({450,235}); layout->addWidget(splitter,1);
    status_=new QLabel(this); status_->setObjectName("vedicBenchmarkStatus"); status_->setWordWrap(true); layout->addWidget(status_);
    timer_=new QTimer(this); timer_->setObjectName("vedicBenchmarkTimer"); timer_->setInterval(0);
    connect(timer_,&QTimer::timeout,this,[this]{step();});
    connect(sampling_,&QComboBox::currentIndexChanged,this,[this]{time_->setVisible(sampling_->currentIndex()==0); anchor_->setVisible(sampling_->currentIndex()!=0); clearSamples();});
    connect(time_,&QTimeEdit::timeChanged,this,[this]{clearSamples();});
    connect(anchor_,&QComboBox::currentIndexChanged,this,[this]{clearSamples();});
    connect(display_,&QComboBox::currentIndexChanged,this,[this]{rescore();});
    connect(scoreMode_,&QComboBox::currentIndexChanged,this,[this](int mode){
        if(mode==7 && sampling_->currentIndex()!=3) sampling_->setCurrentIndex(3);
        // Custom samples did not fetch the other planets needed for Vedha.
        const bool cached=std::any_of(samples_.cbegin(),samples_.cend(),[](const auto& sample){return std::isfinite(sample.gochar.transit[0]);});
        if(mode && !samples_.isEmpty() && !cached) {clearSamples();rescore();}
        else rescore();
    });
    connect(chartScope_,&QComboBox::currentIndexChanged,this,[this](int scope){
        scoreMode_->setEnabled(scope!=1);
        // Reuse cached positions for daily snapshots. Event mode must include
        // every selected planet's ingress, not only an old sampling anchor.
        if(sampling_->currentIndex()!=0) clearSamples();
        const bool cached=std::any_of(samples_.cbegin(),samples_.cend(),[](const auto& sample){return std::isfinite(sample.gochar.transit[0]);});
        if(scope!=1 && scoreMode_->currentIndex()!=0 && !samples_.isEmpty() && !cached) clearSamples();
        rescore();
    });
    connect(weights_,&QPushButton::clicked,this,[this]{editRules();});
    connect(copy_,&QPushButton::clicked,this,[this]{copySummary();});
    // The complete per-sample export stays one right-click away.
    copy_->setToolTip("Copy a summary, per-year overview and merged periods.\nRight-click for the full per-sample export.");
    copy_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(copy_,&QWidget::customContextMenuRequested,this,[this](const QPoint& position){
        if(samples_.isEmpty()) return;
        QMenu menu(this);
        QAction* summary=menu.addAction("Copy summary and periods");
        QAction* full=menu.addAction("Copy full detail (every sample)");
        QAction* chosen=menu.exec(copy_->mapToGlobal(position));
        if(chosen==summary) copySummary(); else if(chosen==full) copyFullDetail();
    });
    connect(activeDashas_,&QCheckBox::toggled,this,[this] {
        for (auto* check:levels_) check->setEnabled(activeDashasOnly());
        details_->setColumnHidden(15,!activeDashasOnly());
        details_->setColumnHidden(16,!rules_.usePlanetWeights && !(activeDashasOnly() && rules_.weightByDasha));
        display_->setItemText(0,activeDashasOnly()?"Overall active dasha lords":"Overall selected planets");
        display_->setCurrentIndex(0);
        clear(); if(onPlanetScopeChanged) onPlanetScopeChanged();
    });
    for (auto* check:levels_) connect(check,&QCheckBox::toggled,this,[this,check] {
        if (std::none_of(levels_.begin(),levels_.end(),[](auto* c){return c->isChecked();})) {
            const QSignalBlocker block(check); check->setChecked(true); return;
        }
        clearSamples();
    });
    setDashaYearDays(QSettings().value("vedic/dashaYearDays",365.25).toDouble());
    clearSamples();
}

void VedicBenchmarkPanel::setContext(const NatalInput& input,const NatalChart& chart) {
    clear(); input_=input; input_.zodiacSystem=chart.zodiacSystem;
    natalChart_=chart; ayanamsa_=chart.siderealAyanamsa; natalStar_=-1;
    validBirth_=false; natalLongitudes_.fill(std::numeric_limits<double>::quiet_NaN());
    const auto primaryNode=effectivePrimaryNodeType(input.lunarNodePolicy);
    for(const auto& body:chart.bodies) {
        int planet=planets.indexOf(body.name);
        if(isLunarNodeName(body.name)) {
            if(lunarNodeTypeForName(body.name,primaryNode)!=primaryNode) continue;
            planet=isNorthLunarNodeName(body.name)?7:8;
        }
        if(planet>=0 && std::isfinite(body.longitude)) natalLongitudes_[planet]=normalizeDegrees(body.longitude);
    }
    for(const auto& body:chart.bodies) if(body.name=="Moon") {
        natalStar_=classifyVedicNakshatra(body.longitude).index; birthMoon_=body.longitude;
        validBirth_=chart.utcDateTime.isValid() && std::isfinite(birthMoon_);
    }
    birthMs_=chart.utcDateTime.toMSecsSinceEpoch(); dashas_=Vimshottari();
    if(validBirth_) dashas_.initialize(birthMs_,birthMoon_,yearDays_);
    haveBav_=computeAshtakavarga(chart,&natalScores_,&natalError_);
}
bool VedicBenchmarkPanel::activeDashasOnly() const { return activeDashas_->isChecked(); }
void VedicBenchmarkPanel::setDashaYearDays(double days) {
    if ((days!=365.25 && days!=360) || days==yearDays_) return;
    yearDays_=days;
    if(validBirth_) dashas_.initialize(birthMs_,birthMoon_,yearDays_);
    if(activeDashasOnly()) clearSamples();
}
void VedicBenchmarkPanel::setControlsBusy(bool busy) {
    sampling_->setEnabled(!busy); anchor_->setEnabled(!busy); time_->setEnabled(!busy); weights_->setEnabled(!busy); scoreMode_->setEnabled(!busy && chartScope_->currentIndex()!=1); chartScope_->setEnabled(!busy);
    activeDashas_->setEnabled(!busy);
    for(auto* check:levels_) check->setEnabled(!busy && activeDashasOnly());
}
QString VedicBenchmarkPanel::dashaDescription() const {
    const QString importance=rules_.usePlanetWeights?"Manual planet weights":"Equal planet weights";
    if(!activeDashasOnly()) return "Manual planet selection · "+importance;
    QStringList levels;
    for(int level=0;level<5;++level) if(levels_[level]->isChecked()) levels<<dashaLevelAbbreviation(level);
    QString text=QString("Active dashas: %1 · %2-day year (Dashas tab)").arg(levels.join(" / ")).arg(yearDays_);
    if(!rules_.weightByDasha) return text+" · "+importance;
    QStringList weights;
    for(int level=0;level<5;++level) if(levels_[level]->isChecked())
        weights<<dashaLevelAbbreviation(level)+" "+QString::number(rules_.dashaWeights[level]);
    return text+" · Dasha weights: "+weights.join(" / ")+" · Highest active role wins"+(rules_.usePlanetWeights?" × manual planet weights":"");
}
QString VedicBenchmarkPanel::scoreDescription() const {
    if(chartScope_->currentIndex()==1) return "D9 only · Natal Navamsa contacts";
    if(chartScope_->currentIndex()==2) return QString("D1+D9 · %1 · D9 share %2%")
        .arg(scoreMode_->currentText()).arg(d9Rules_.blendShare*100,0,'f',0);
    return scoreMode_->currentText();
}
QString VedicBenchmarkPanel::d9Summary(const VedicBenchmarkSample& sample) const {
    if(!chartScope_->currentIndex()) return {};
    const bool incomplete=!sample.d9Unresolved.isEmpty();
    QString text=incomplete ? "D9 INCOMPLETE · unresolved: "+sample.d9Unresolved.join(", ")
                            : std::isfinite(sample.d9Overall)?"D9: assessed under implemented rules":"D9: no covered score";
    if(!sample.d9NotCovered.isEmpty()) text+=" · not covered: "+sample.d9NotCovered.join(", ");
    if(chartScope_->currentIndex()==2) {
        text+=QString("\nD1 %1 × %2% = %3 · D9 %4 × %5% = %6 · Total %7")
            .arg(std::isfinite(sample.d1Overall)?number(sample.d1Overall):"N/A")
            .arg(sample.effectiveD1Share*100,0,'f',1).arg(number(sample.d1Contribution))
            .arg(std::isfinite(sample.d9Overall)?number(sample.d9Overall):"N/A")
            .arg(sample.effectiveD9Share*100,0,'f',1).arg(number(sample.d9Contribution))
            .arg(sample.scoredPlanets?number(sample.overall):"N/A");
        if(incomplete) text+=" · partial assessment";
        if(!std::isfinite(sample.d9Overall)) text+=" · D9 omitted; no available D9 score";
    } else if(incomplete) text+=" · shown scores cover resolved rules only";
    return text;
}
QString VedicBenchmarkPanel::d9RangeSummary() const {
    if(!chartScope_->currentIndex()) return {};
    int incomplete=0;
    for(const auto& sample:samples_) incomplete+=!sample.d9Unresolved.isEmpty();
    return QString(" · D9: Saturn/Jupiter rules only · %1 incomplete samples · amber top ticks = unresolved D9").arg(incomplete);
}
void VedicBenchmarkPanel::setSeries(const QVector<MoorthiGraphSeries>& series,double start,double end,const QTimeZone& zone) {
    clear(); series_=series; start_=start; end_=end; zone_=zone;
    plot_->range.setRange(start,end); plot_->zone=zone;
    const QSignalBlocker a(anchor_),d(display_);
    const QString selectedAnchor=anchor_->currentText(), selectedDisplay=display_->currentText();
    anchor_->clear(); anchor_->addItem(activeDashasOnly()?"Any planet":"Any selected planet");
    display_->clear(); display_->addItem(activeDashasOnly()?"Overall active dasha lords":"Overall selected planets");
    for(const auto& s:series_) {anchor_->addItem(s.planet); display_->addItem(s.planet);}
    anchor_->setCurrentIndex(std::max(0,anchor_->findText(selectedAnchor))); display_->setCurrentIndex(std::max(0,display_->findText(selectedDisplay)));
    rescore();
}
bool VedicBenchmarkPanel::isRunning() const {return timer_->isActive();}
void VedicBenchmarkPanel::clearSamples() {
    if(isRunning()) complete("Cancelled: settings changed");
    samples_.clear(); eventTimes_.clear(); scanningKaksha_=scanningNatal_=false; selected_=-1; partial_=false; plot_->selected=-1; plot_->hovered=-1;
    plot_->clearSelection();
    plot_->range.reset();
    plot_->pressed=plot_->dragging=false; plot_->unsetCursor();
    details_->setRowCount(0); copy_->setEnabled(false); QToolTip::hideText();
    detailLabel_->setText("Select a graph point for the component breakdown.");
    status_->setText(scoreDescription()+" · "+dashaDescription()+" · Choose sampling, then Calculate.");
    plot_->title=display_->currentText()+((rules_.usePlanetWeights || (activeDashasOnly() && rules_.weightByDasha))?" · Weighted":"")+" · "+scoreDescription()+" · −100 to +100"; plot_->update();
}
void VedicBenchmarkPanel::clear() {clearSamples(); series_.clear();}
void VedicBenchmarkPanel::calculate() {
    if(isRunning()) return;
    clearSamples();
    if(series_.isEmpty() || natalStar_<0 || !swe_ || !swe_->isLoaded()) {status_->setText("Calculate the selected planets' Moorthi entries first."); return;}
    if(activeDashasOnly() && !dashas_.isValid()) {status_->setText("Birth Moon or UTC birth time unavailable for dashas."); return;}
    if(chartScope_->currentIndex()!=1 && scoreMode_->currentIndex()==0 && !haveBav_ && (rules_.weights[2]>0 || rules_.weights[3]>0) && std::any_of(series_.begin(),series_.end(),[](const auto& s){return s.colorIndex<7;})) {
        status_->setText("Natal BAV unavailable: "+natalError_); return;
    }
    if(chartScope_->currentIndex()!=1 && scoreMode_->currentIndex()==0 && std::all_of(rules_.weights.begin(),rules_.weights.end(),[](double weight){return weight<=0;})) {status_->setText("Enable at least one component in Scoring rules."); return;}
    for(const auto& s:series_) if(s.entries.isEmpty() || s.calculatedThrough<end_) {status_->setText("Moorthi search incomplete. Calculate the full range first."); return;}
    if(sampling_->currentIndex()!=0) {
        eventTimes_.push_back(start_);
        for(int i=0;i<series_.size();++i) {
            if(chartScope_->currentIndex()==0 && anchor_->currentIndex()>0 && i!=anchor_->currentIndex()-1) continue;
            for(const auto& entry:series_[i].entries) if(entry.jd>start_ && entry.jd<end_) eventTimes_.push_back(entry.jd);
        }
        scanningKaksha_=false;
        if(sampling_->currentIndex()==2) {scanningKaksha_=true; kakshaSeries_=0; kakshaCursor_=start_;}
        else if(sampling_->currentIndex()==3) {scanningNatal_=true;natalSeries_=0;natalCursor_=start_;}
        else {
            std::sort(eventTimes_.begin(),eventTimes_.end());
            eventTimes_.erase(std::unique(eventTimes_.begin(),eventTimes_.end(),[](double a,double b){return std::abs(a-b)<.02/86400;}),eventTimes_.end());
        }
    }
    nextDate_=localTime(start_,zone_).date(); nextEvent_=0; entryIndexes_.fill(0,series_.size());
    nextDashaEvent_=activeDashasOnly()?start_:std::numeric_limits<double>::infinity();
    setControlsBusy(true);
    timer_->start(); if(onBusyChanged) onBusyChanged(true);
    status_->setText(scanningKaksha_?"Finding Kaksha crossings…":scanningNatal_?"Finding natal crossings…":"Calculating benchmark…");
}

void VedicBenchmarkPanel::step() {
    QElapsedTimer batch; batch.start(); QString failure; bool done=false;
    const int zodiacFlags=input_.zodiacSystem==ZodiacSystem::Sidereal?SEFLG_SIDEREAL:0;
    swe_->setSidMode(siderealAyanamsaSwissMode(ayanamsa_));
    if(scanningKaksha_) {
        while(batch.elapsed()<12 && kakshaSeries_<series_.size()) {
            if(anchor_->currentIndex()>0 && kakshaSeries_!=anchor_->currentIndex()-1) {++kakshaSeries_; kakshaCursor_=start_; continue;}
            const auto& series=series_[kakshaSeries_];
            const double next=std::min(end_,kakshaCursor_+.25);
            if(!findKakshaCrossings(*swe_,series.body,series.offset,kakshaCursor_,next,&eventTimes_,&failure,zodiacFlags)) break;
            kakshaCursor_=next;
            if(kakshaCursor_>=end_) {++kakshaSeries_; kakshaCursor_=start_;}
        }
        swe_->setSidMode(siderealAyanamsaSwissMode(input_.siderealAyanamsa));
        if(!failure.isEmpty()) {partial_=true; complete("Kaksha search failed: "+failure); return;}
        if(kakshaSeries_==series_.size()) {
            std::sort(eventTimes_.begin(),eventTimes_.end());
            eventTimes_.erase(std::unique(eventTimes_.begin(),eventTimes_.end(),[](double a,double b){return std::abs(a-b)<.02/86400;}),eventTimes_.end());
            scanningKaksha_=false;
            status_->setText(QString("Scoring %1 sign and Kaksha events…").arg(eventTimes_.size()));
        } else status_->setText(QString("Finding Kaksha crossings · planet %1 of %2").arg(kakshaSeries_+1).arg(series_.size()));
        return;
    }
    if(scanningNatal_) {
        while(batch.elapsed()<12 && natalSeries_<series_.size()) {
            if(anchor_->currentIndex()>0 && natalSeries_!=anchor_->currentIndex()-1) {++natalSeries_;natalCursor_=start_;continue;}
            const auto& series=series_[natalSeries_];
            const double next=std::min(end_,natalCursor_+.25);
            if(!findNatalCrossings(*swe_,series.body,series.offset,natalCursor_,next,natalLongitudes_,&eventTimes_,&failure,zodiacFlags)) break;
            natalCursor_=next;
            if(natalCursor_>=end_) {++natalSeries_;natalCursor_=start_;}
        }
        swe_->setSidMode(siderealAyanamsaSwissMode(input_.siderealAyanamsa));
        if(!failure.isEmpty()) {partial_=true;complete("Natal-crossing search failed: "+failure);return;}
        if(natalSeries_==series_.size()) {
            std::sort(eventTimes_.begin(),eventTimes_.end());
            eventTimes_.erase(std::unique(eventTimes_.begin(),eventTimes_.end(),[](double a,double b){return std::abs(a-b)<.02/86400;}),eventTimes_.end());
            scanningNatal_=false;status_->setText(QString("Scoring %1 natal crossings and sign entries…").arg(eventTimes_.size()));
        } else status_->setText(QString("Finding natal crossings · planet %1 of %2").arg(natalSeries_+1).arg(series_.size()));
        return;
    }
    while(batch.elapsed()<12) {
        VedicBenchmarkSample sample;
        if(sampling_->currentIndex()==0) {
            if(!nextDate_.isValid() || nextDate_>localTime(end_-1/dayMs,zone_).date()) {done=true; break;}
            const auto moment=QDateTime(nextDate_,time_->time(),zone_,QDateTime::TransitionResolution::Reject);
            sample.jd=moment.isValid() ? jd(moment) : start_+localTime(start_,zone_).date().daysTo(nextDate_);
            if(!moment.isValid()) sample.error="Skipped/repeated local time: "+nextDate_.toString(Qt::ISODate)+" "+time_->time().toString();
            nextDate_=nextDate_.addDays(1);
        } else {
            const double entryTime=nextEvent_<eventTimes_.size()?eventTimes_[nextEvent_]:std::numeric_limits<double>::infinity();
            sample.jd=std::min(entryTime,nextDashaEvent_);
            if(sample.jd>=end_) {done=true; break;}
            while(nextEvent_<eventTimes_.size() && eventTimes_[nextEvent_]<=sample.jd) ++nextEvent_;
        }
        if(!sample.error.isEmpty()) {samples_.push_back(sample); continue;}
        if(chartScope_->currentIndex()!=1 && scoreMode_->currentIndex()!=0) {
            sample.gochar.jd=sample.jd;
            sample.gochar.birthJd=validBirth_?epoch+birthMs_/dayMs:std::numeric_limits<double>::quiet_NaN();
            sample.gochar.natal=natalLongitudes_;
            const int node=swissBodyIdForLunarNode(effectivePrimaryNodeType(input_.lunarNodePolicy));
            const std::array<int,9> bodies{SE_SUN,SE_MOON,SE_MARS,SE_MERCURY,SE_JUPITER,SE_VENUS,SE_SATURN,node,node};
            for(int p=0;p<9;++p) {
                double values[6]{};
                if(!swe_->calcUtFull(sample.jd,bodies[p],zodiacFlags|SEFLG_SPEED,values,&failure)) break;
                sample.gochar.transit[p]=normalizeDegrees(values[0]+(p==8?180:0));
                sample.gochar.retrograde[p]=values[3]<0;
            }
            if(!failure.isEmpty()) break;
        }
        std::array<QString,9> roles;
        std::array<unsigned,9> roleMasks{};
        if(activeDashasOnly()) {
            const qint64 moment=qRound64((sample.jd-epoch)*dayMs);
            const auto path=dashas_.activeAt(moment);
            if(path.size()!=5) {failure="Dasha path unavailable at "+localTime(sample.jd,zone_).toString(Qt::ISODate); break;}
            qint64 nextChange=std::numeric_limits<qint64>::max(); QStringList labels;
            for(const auto& period:path) if(levels_[period.level]->isChecked()) {
                const QString lord=dashaLordName(period.lord), level=dashaLevelAbbreviation(period.level);
                const int planet=planets.indexOf(lord);
                if(!roles[planet].isEmpty()) roles[planet]+=" / ";
                roles[planet]+=level; labels<<level+" "+lord;
                roleMasks[planet]|=1u<<period.level;
                nextChange=std::min(nextChange,period.endMs);
            }
            sample.dashaPath=labels.join(" · ");
            nextDashaEvent_=epoch+nextChange/dayMs;
        }
        for(int i=0;i<series_.size();++i) {
            const auto& s=series_[i]; int& index=entryIndexes_[i];
            if(activeDashasOnly() && roles[s.colorIndex].isEmpty()) continue;
            while(index+1<s.entries.size() && s.entries[index+1].jd<=sample.jd+.01/86400) ++index;
            VedicBenchmarkReading reading; reading.planet=s.planet; reading.planetIndex=s.colorIndex; reading.entry=s.entries[index];
            reading.roles=roles[s.colorIndex]; reading.roleMask=roleMasks[s.colorIndex];
            double longitude=0;
            if(!swe_->calcUt(sample.jd,s.body,zodiacFlags,&longitude,&failure) || !std::isfinite(longitude)) {
                failure=s.planet+": "+(failure.isEmpty()?QString("Invalid longitude"):failure); break;
            }
            reading.longitude=normalizeDegrees(longitude+s.offset);
            // Ingress roots have a finite bracket. At an event sample, assign the
            // destination side consistently for BAV and star classification.
            if(std::abs(sample.jd-reading.entry.jd)<.02/86400)
                reading.longitude=normalizeDegrees(reading.entry.retrograde ? (reading.entry.newSign+1)*30-1e-7 : reading.entry.newSign*30+1e-7);
            const auto star=classifyVedicNakshatra(reading.longitude); reading.nakshatra=star.index;
            reading.tara=classifyVedicTara(natalStar_,star.index).number;
            const int sign=signIndex(reading.longitude);
            if(haveBav_) {
                reading.sav=natalScores_.sav[sign];
                if(s.colorIndex<7) {
                    reading.bav=natalScores_.bav[s.colorIndex][sign];
                    kakshaBinduAt(natalScores_,s.colorIndex,reading.longitude,&reading.kaksha);
                }
            }
            if(chartScope_->currentIndex()!=1 && scoreMode_->currentIndex()!=0 && (reading.planetIndex<7 ||
                s.body==swissBodyIdForLunarNode(effectivePrimaryNodeType(input_.lunarNodePolicy))))
                sample.gochar.transit[reading.planetIndex]=reading.longitude;
            sample.readings.push_back(reading);
        }
        if(!failure.isEmpty()) break;
        scoreSample(sample,rules_,gocharRules_,scoreMode_->currentIndex(),chartScope_->currentIndex(),d9Rules_,natalLongitudes_,natalChart_.angles.asc); samples_.push_back(std::move(sample));
    }
    swe_->setSidMode(siderealAyanamsaSwissMode(input_.siderealAyanamsa));
    if(!failure.isEmpty()) {partial_=true; complete("Calculation failed · partial: "+failure);}
    else if(done) complete("Complete");
    else {status_->setText(QString("Calculating · %1 samples").arg(samples_.size())); plot_->update();}
}
void VedicBenchmarkPanel::stop() {if(isRunning()) {partial_=true; complete("Stopped · partial results");}}
void VedicBenchmarkPanel::complete(const QString& message) {
    timer_->stop(); setControlsBusy(false);
    int available=0; for(const auto& s:samples_) available+=s.scoredPlanets>0;
    status_->setText(QString("%1 · %2 of %3 samples scored · %4 · Green >0 / red <0 · %5")
                     .arg(message).arg(available).arg(samples_.size()).arg(QString::fromUtf8(zone_.id()),scoreDescription()));
    if(activeDashasOnly()) status_->setText(status_->text()+" · "+dashaDescription());
    status_->setText(status_->text()+d9RangeSummary());
    copy_->setEnabled(!samples_.isEmpty());
    if(!samples_.isEmpty()) select(0);
    plot_->update();
    if(onBusyChanged) onBusyChanged(false);
}
double VedicBenchmarkPanel::displayedScore(int index) const {
    const auto& sample=samples_[index];
    if(display_->currentIndex()==0 && sample.scoredPlanets) return sample.overall;
    for(const auto& r:sample.readings) if(r.planet==display_->currentText() && r.score.valid && r.planetWeight>0) return r.score.net;
    return std::numeric_limits<double>::quiet_NaN();
}
QString VedicBenchmarkPanel::tooltip(int index) const {
    const auto& sample=samples_[index];
    QStringList lines{localTime(sample.jd,zone_).toString("dd MMM yyyy HH:mm:ss.zzz ttt"),
        display_->currentText()+": "+(std::isfinite(displayedScore(index))?number(displayedScore(index)):"N/A"),"Overall: "+(sample.scoredPlanets?number(sample.overall):"N/A")};
    if(!sample.dashaPath.isEmpty()) lines<<sample.dashaPath;
    if(chartScope_->currentIndex()) lines<<d9Summary(sample);
    if(selected_>=0 && selected_!=index && std::isfinite(displayedScore(index)) && std::isfinite(displayedScore(selected_)))
        lines<<QString("Pinned %1: %2 · Difference %3")
            .arg(localTime(samples_[selected_].jd,zone_).toString("dd MMM yyyy HH:mm"),number(displayedScore(selected_)),
                 number(displayedScore(index)-displayedScore(selected_)));
    for(const auto& r:sample.readings) {
        if(chartScope_->currentIndex()) {
            lines << QString("%1 · transit %2 · D9 %3 · Net %4")
                .arg(r.planet,signName(signIndex(r.longitude)),r.d9.valid?number(r.d9.score):"N/A",
                     r.score.valid?number(r.score.net):"N/A");
            if(chartScope_->currentIndex()==2) lines << "  D1: "+(r.d1Score.valid?number(r.d1Score.net):QString("N/A"));
            lines << "  "+r.d9.details;
            if(chartScope_->currentIndex()==1) continue;
        }
        if(scoreMode_->currentIndex()) {
            const auto& g=r.gochar;
            lines << QString("%1 · H%2 · House %3 · Sign Vedha %4 · Star Vedha %5 · Paryaya %6 · Strength %7 · Aspects %8 · Samagamam %9 · Score %10")
                .arg(r.planet).arg(g.house).arg(number(g.base),number(g.afterSignVedha),number(g.afterNakshatraVedha))
                .arg(number(g.afterParyaya),number(g.afterStrength),number(g.afterAspects),number(g.samagamamScore),r.score.valid?number(r.score.net):"N/A");
            if(!g.signVedha.isEmpty()) lines<<"  "+g.signVedha;
            if(!g.paryayaVedha.isEmpty() && g.paryayaVedha!=g.signVedha) lines<<"  Paryaya: "+g.paryayaVedha;
            if(!g.nakshatraVedha.isEmpty()) lines<<"  "+g.nakshatraVedha;
            if(!g.samagamam.isEmpty()) lines<<"  "+g.samagamam;
        } else {
            lines << QString("%1 · %2 · %3 · Tara %4 · BAV %5 · Kaksha %6 · Net %7 · Weight %8")
                .arg(r.planet+(r.roles.isEmpty()?QString():" ["+r.roles+"]"),signName(signIndex(r.longitude)),moorthiName(r.entry.moorthi)).arg(r.tara)
                .arg(r.bav<0 ? "N/A" : QString::number(r.bav))
                .arg(r.kaksha.bindu<0?"N/A":QString("%1/8 %2 %3").arg(r.kaksha.section+1).arg(kakshaDonorName(r.kaksha.donor)).arg(r.kaksha.bindu))
                .arg(r.score.valid ? number(r.score.net) : "N/A").arg(r.planetWeight);
        }
    }
    return lines.join('\n');
}
void VedicBenchmarkPanel::rescore() {
    plot_->unavailableText=chartScope_->currentIndex()==1
        ? "No scored D9 result for this line. This layer has specific Saturn/Jupiter rules only. Other planets and unresolved book conditions are unscored; select a point's details or Scoring rules → Natal D9. Zero-weight/inactive planets remain excluded."
        : "No score available for this line. Zero-weight planets are excluded; in active-dasha mode a planet must also be active. Unavailable components or D9 contacts are omitted.";
    details_->setColumnHidden(16,!rules_.usePlanetWeights && !(activeDashasOnly() && rules_.weightByDasha));
    const bool d9Only=chartScope_->currentIndex()==1;
    const bool gochar=scoreMode_->currentIndex()!=0 && !d9Only;
    details_->setColumnHidden(2,d9Only);
    for(int col=3;col<=13;++col) details_->setColumnHidden(col,gochar || d9Only);
    for(int col=17;col<26;++col) details_->setColumnHidden(col,!gochar);
    details_->setColumnHidden(26,chartScope_->currentIndex()!=2);
    details_->setColumnHidden(27,chartScope_->currentIndex()==0);
    details_->setColumnHidden(28,chartScope_->currentIndex()==0);
    for(auto& sample:samples_) scoreSample(sample,rules_,gocharRules_,scoreMode_->currentIndex(),chartScope_->currentIndex(),d9Rules_,natalLongitudes_,natalChart_.angles.asc);
    plot_->title=display_->currentText()+((rules_.usePlanetWeights || (activeDashasOnly() && rules_.weightByDasha))?" · Weighted":"")+" · "+scoreDescription()+" · −100 to +100";
    if(selected_>=0) select(selected_);
    if(!samples_.isEmpty()) {
        int available=0;for(const auto& sample:samples_) available+=sample.scoredPlanets>0;
        status_->setText(QString("%1 · %2 of %3 samples scored · %4")
            .arg(scoreDescription()).arg(available).arg(samples_.size()).arg(partial_?"Partial":"Complete"));
        status_->setText(status_->text()+d9RangeSummary());
    }
    plot_->update();
}
void VedicBenchmarkPanel::select(int index) {
    if(index<0 || index>=samples_.size()) return;
    selected_=index; plot_->selected=index; plot_->update(); const auto& sample=samples_[index];
    detailLabel_->setText(localTime(sample.jd,zone_).toString("dd MMM yyyy HH:mm:ss ttt")+
        QString(" · %1 · Overall %2 · %3 planets scored%4").arg(scoreDescription()).arg(sample.scoredPlanets ? number(sample.overall) : "N/A").arg(sample.scoredPlanets)
            .arg(sample.error.isEmpty()?QString():" · "+sample.error));
    if(!sample.dashaPath.isEmpty()) detailLabel_->setText(detailLabel_->text()+"\n"+sample.dashaPath);
    if(chartScope_->currentIndex()) detailLabel_->setText(detailLabel_->text()+"\n"+d9Summary(sample));
    details_->setRowCount(sample.readings.size());
    for(int row=0;row<sample.readings.size();++row) {
        const auto& r=sample.readings[row];
        const auto tara=classifyVedicTara(natalStar_,r.nakshatra);
        const bool hasMoorthi=r.planetIndex!=1;
        const QStringList cells{r.planet,signName(signIndex(r.longitude)),vedicNakshatraNames().value(r.nakshatra),moorthiName(r.entry.moorthi),
            hasMoorthi?localTime(r.entry.jd,zone_).toString("dd MMM yyyy HH:mm"):"—",QString::number(r.tara)+" · "+tara.name,
            r.bav<0?"—":QString::number(r.bav),r.sav<0?"—":QString::number(r.sav),
            r.kaksha.bindu<0?"—":QString("%1 · %2").arg(r.kaksha.section+1).arg(kakshaDonorName(r.kaksha.donor)),
            r.kaksha.bindu<0?"—":QString::number(r.kaksha.bindu),
            hasMoorthi&&r.d1Score.valid?number(r.d1Score.points[0]):"—",r.d1Score.valid?number(r.d1Score.points[1]):"—",
            r.bav<0||!r.d1Score.valid?"—":number(r.d1Score.points[2]),r.kaksha.bindu<0||!r.d1Score.valid?"—":number(r.d1Score.points[3]),
            r.score.valid?number(r.score.net):"—",r.roles,QString::number(r.planetWeight),
            r.gochar.valid?QString::number(r.gochar.house):"—",
            r.gochar.valid?number(r.gochar.base):"—",r.gochar.valid?number(r.gochar.afterSignVedha):"—",
            r.gochar.valid?number(r.gochar.afterNakshatraVedha):"—",
            r.gochar.valid?QString("%1 / %2").arg(r.gochar.paryaya?QString::number(r.gochar.paryaya):"—",number(r.gochar.afterParyaya)):"—",
            r.gochar.valid?number(r.gochar.afterStrength):"—",r.gochar.valid?number(r.gochar.afterAspects):"—",
            r.gochar.samagamam.isEmpty()?"—":number(r.gochar.samagamamScore),
            QStringList{r.gochar.signVedha,r.gochar.paryayaVedha,r.gochar.nakshatraVedha,r.gochar.strengthNotes,r.gochar.aspects,r.gochar.samagamam}.filter(QRegularExpression(".+")).join("; "),
            r.d1Score.valid?number(r.d1Score.net):"—",
            !r.d9.supported?"Not covered":r.d9.valid?number(r.d9.score)+(r.d9.incomplete?" (partial)":""):"Unresolved",r.d9.details};
        for(int col=0;col<cells.size();++col) {
            auto* item=new QTableWidgetItem(cells[col]); if((col>=6 && col<=14 && col!=8) || col==16 || (col>=17 && col<=24 && col!=21)) item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter); details_->setItem(row,col,item);
            if(col>=10 && col<=14 && (col==14?r.score.valid:r.d1Score.valid)) {const double value=col==14?r.score.net:r.d1Score.points[col-10]; if(value!=0) item->setForeground(QColor(value>0?"#2E8B57":"#C4473A"));}
            if(col==26 || col==27) {
                item->setTextAlignment(Qt::AlignRight|Qt::AlignVCenter);
                const double value=col==26?r.d1Score.net:r.d9.score;
                if((col==26?r.d1Score.valid:r.d9.valid) && value!=0) item->setForeground(QColor(value>0?"#2E8B57":"#C4473A"));
            }
        }
        details_->item(row,3)->setToolTip(!hasMoorthi?"Moon has no Moorthi. Its score uses only available Tara/BAV weights.":QString("%1 rule · Latest entry: %2\nMoon at entry: %3 · Count %4")
            .arg(rules_.useMalefic[r.planetIndex]?"Malefic":"Benefic",localTime(r.entry.jd,zone_).toString("dd MMM yyyy HH:mm:ss.zzz ttt"),signName(r.entry.moonSign)).arg(r.entry.count));
        details_->item(row,6)->setToolTip(r.bav<0?"No node BAV in this method. Its unavailable component is omitted from that planet's weight denominator.":
            QString("Own natal BAV %1 · SAV %2 (context only, not scored)\nBAV normalization: (BAV − 4) / 4").arg(r.bav).arg(r.sav));
        details_->item(row,7)->setToolTip("Natal Sarvashtakavarga in the transit sign · Context only; not added to the score.");
        details_->item(row,8)->setToolTip(r.kaksha.bindu<0?"No natal Prastara Kaksha for nodes.":
            QString("Kaksha %1/8 · %2° to %3° of %4 · Ruler %5\nOwn natal Prastara contribution: %6 bindu")
                .arg(r.kaksha.section+1).arg(r.kaksha.section*3.75,0,'f',2).arg((r.kaksha.section+1)*3.75,0,'f',2)
                .arg(signName(r.kaksha.sign),kakshaDonorName(r.kaksha.donor)).arg(r.kaksha.bindu));
        details_->item(row,9)->setToolTip("The section ruler's 0/1 bindu in this transiting planet's natal Prastara for the occupied sign.");
        details_->item(row,13)->setToolTip("Optional research encoding: 1 bindu = +1, 0 bindu = −1 before component weighting. Zero Kaksha weight preserves earlier scores.");
        details_->item(row,16)->setToolTip("Overall weight = enabled manual planet weight × enabled dasha weight. Highest active dasha role wins; zero excludes this planet. Individual net score is unchanged.");
        if(r.gochar.valid) for(int col=17;col<26;++col) details_->item(row,col)->setToolTip(cells[25]);
        details_->item(row,27)->setToolTip(r.d9.details);
        details_->item(row,28)->setToolTip(r.d9.details);
        if(chartScope_->currentIndex()==2) details_->item(row,14)->setToolTip("Per-planet D1/D9 blend. Overall blends the independent layer means; unsupported or unresolved D9 scores are omitted, not zero. Component columns show the unblended D1 contributions.");
    }
}

void VedicBenchmarkPanel::editRules() {
    QDialog dialog(this); dialog.setWindowTitle("Vedic benchmark scoring rules"); dialog.resize(1040,720); dialog.setMinimumSize(900,620);
    auto* outer=new QVBoxLayout(&dialog); outer->setContentsMargins(10,10,10,8); outer->setSpacing(8);
    auto* pages=new QTabWidget(&dialog); outer->addWidget(pages);
    auto* scoringPage=new QWidget(pages); pages->addTab(scoringPage,"Scoring rules"); auto* layout=new QVBoxLayout(scoringPage);
    layout->setContentsMargins(8,8,8,8); layout->setSpacing(7);
    auto spin=[&](double value,double low,double high) {auto* s=new QDoubleSpinBox(&dialog);s->setRange(low,high);s->setDecimals(2);s->setSingleStep(.25);s->setValue(value);return s;};

    std::array<QDoubleSpinBox*,4> weights;
    auto* componentBox=new QGroupBox("Component weights",scoringPage); auto* componentGrid=new QGridLayout(componentBox);
    const QStringList labels{"Moorthi","Tara","BAV","Kaksha"};
    for(int i=0;i<4;++i) {
        componentGrid->addWidget(new QLabel(labels[i],componentBox),0,i*2);
        weights[i]=spin(rules_.weights[i],0,10); weights[i]->setObjectName("benchmarkWeight"+QString::number(i));
        weights[i]->setMinimumWidth(100); componentGrid->addWidget(weights[i],0,i*2+1);
    }
    weights[3]->setToolTip("Own natal Prastara bindu at the planet's current 3°45' Kaksha: 1 = +1, 0 = −1. This numerical encoding is an editable research choice.");
    layout->addWidget(componentBox);

    auto* weightingRow=new QHBoxLayout; weightingRow->setSpacing(7); layout->addLayout(weightingRow);
    auto* dashaBox=new QGroupBox("Active dasha weighting",scoringPage); auto* dashaLayout=new QVBoxLayout(dashaBox);
    auto* useDasha=new QCheckBox("Apply dasha weights to active lords",dashaBox); useDasha->setObjectName("benchmarkUseDashaWeights");
    useDasha->setChecked(rules_.weightByDasha); dashaLayout->addWidget(useDasha);
    auto* dashaValues=new CompactControls;
    std::array<QDoubleSpinBox*,5> dashaWeights;
    for(int level=0;level<5;++level) {
        dashaValues->addWidget(new QLabel(dashaLevelAbbreviation(level),dashaBox));
        dashaWeights[level]=spin(rules_.dashaWeights[level],0,10);
        dashaWeights[level]->setObjectName("benchmarkDashaWeight"+QString::number(level));
        dashaWeights[level]->setMinimumWidth(68); dashaWeights[level]->setEnabled(useDasha->isChecked()); dashaValues->addWidget(dashaWeights[level]);
        connect(useDasha,&QCheckBox::toggled,dashaWeights[level],&QWidget::setEnabled);
    }
    dashaLayout->addLayout(dashaValues);
    auto* dashaHint=new QLabel("Only used when Active dasha lords only is enabled. Highest enabled role wins; repeated lords count once.",dashaBox);
    dashaHint->setWordWrap(true); dashaLayout->addWidget(dashaHint); weightingRow->addWidget(dashaBox,3);

    auto* planetWeightBox=new QGroupBox("Planet weighting",scoringPage); auto* planetWeightLayout=new QVBoxLayout(planetWeightBox);
    auto* usePlanets=new QCheckBox("Apply manual planet weights",planetWeightBox); usePlanets->setObjectName("benchmarkUsePlanetWeights");
    usePlanets->setChecked(rules_.usePlanetWeights); planetWeightLayout->addWidget(usePlanets);
    auto* planetWeightHint=new QLabel("Set each planet's value on the Planets tab: 1 = normal, 2 = double, 0 = exclude. Multiplies dasha weights when enabled.",planetWeightBox);
    planetWeightHint->setWordWrap(true); planetWeightLayout->addWidget(planetWeightHint); weightingRow->addWidget(planetWeightBox,2);

    auto* ruleTabs=new QTabWidget(scoringPage); ruleTabs->setObjectName("benchmarkRuleTabs"); layout->addWidget(ruleTabs,1);
    auto* metalsTable=new QTableWidget(4,3,&dialog); metalsTable->setHorizontalHeaderLabels({"Moorthi","Benefic","Malefic"});
    auto* taraTable=new QTableWidget(9,2,&dialog); taraTable->setHorizontalHeaderLabels({"Tara","Score"});
    auto* planetTable=new QTableWidget(9,3,&dialog); planetTable->setHorizontalHeaderLabels({"Planet","Moorthi rule","Weight"});
    for(auto* table:{metalsTable,taraTable,planetTable}) {
        table->verticalHeader()->hide(); table->verticalHeader()->setDefaultSectionSize(27);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->horizontalHeader()->setMinimumSectionSize(90); table->setMinimumHeight(270);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setAlternatingRowColors(true);
    }
    auto* metalsPage=new QWidget(ruleTabs); auto* metalsLayout=new QVBoxLayout(metalsPage); metalsLayout->setContentsMargins(6,6,6,6);
    metalsLayout->addWidget(new QLabel("Choose how each Moorthi metal contributes for benefic and malefic planets.",metalsPage)); metalsLayout->addWidget(metalsTable,1);
    ruleTabs->addTab(metalsPage,"Moorthi metals");
    auto* taraPage=new QWidget(ruleTabs); auto* taraLayout=new QVBoxLayout(taraPage); taraLayout->setContentsMargins(6,6,6,6);
    taraLayout->addWidget(new QLabel("Edit the numeric value assigned to each Tara category.",taraPage)); taraLayout->addWidget(taraTable,1);
    ruleTabs->addTab(taraPage,"Tara values");
    auto* planetPage=new QWidget(ruleTabs); auto* planetLayout=new QVBoxLayout(planetPage); planetLayout->setContentsMargins(6,6,6,6);
    planetLayout->addWidget(new QLabel("Set each planet's Moorthi classification and its optional overall weight.",planetPage)); planetLayout->addWidget(planetTable,1);
    ruleTabs->addTab(planetPage,"Planet rules");
    for(int i=0;i<4;++i) {metalsTable->setItem(i,0,new QTableWidgetItem(metals[i]));metalsTable->setCellWidget(i,1,spin(rules_.benefic[i],-1,1));metalsTable->setCellWidget(i,2,spin(rules_.malefic[i],-1,1));}
    for(int i=0;i<9;++i) {
        taraTable->setItem(i,0,new QTableWidgetItem(QString::number(i+1)+" "+classifyVedicTara(0,i).name));taraTable->setCellWidget(i,1,spin(rules_.tara[i],-1,1));
        planetTable->setItem(i,0,new QTableWidgetItem(planets[i]));auto* type=new QComboBox(&dialog);type->addItems({"Benefic","Malefic"});type->setCurrentIndex(rules_.useMalefic[i]?1:0);planetTable->setCellWidget(i,1,type);
        if(i==1) {type->addItem("Not applicable");type->setCurrentIndex(2);type->setEnabled(false);type->setToolTip("Moon has no Moorthi; Tara/BAV and planet/dasha weights still apply.");}
        auto* planetWeight=spin(rules_.planetWeights[i],0,100); planetWeight->setObjectName("benchmarkPlanetWeight"+QString::number(i));
        planetWeight->setEnabled(usePlanets->isChecked()); planetTable->setCellWidget(i,2,planetWeight);
        connect(usePlanets,&QCheckBox::toggled,planetWeight,&QWidget::setEnabled);
    }
    auto* message=new QLabel("Custom research score: 100 × weighted mean of available components. Kaksha order and bindu follow Gochar Phaladeepika (PDF pp. 234–235); its numeric score is your editable research choice.",scoringPage);
    message->setObjectName("benchmarkScoringNote"); message->setWordWrap(true); layout->addWidget(message);
    auto* gocharPage=new QWidget(pages); pages->addTab(gocharPage,"Gochar research");
    auto* gocharLayout=new QVBoxLayout(gocharPage);
    auto* gocharNote=new QLabel("Book: Ch. 20 Moon-house results; Ch. 21 aspects; Ch. 22 sign Vedha; Ch. 23 star Vedha; Ch. 26 Paryaya; Ch. 2 strength; Ch. 29 natal crossings. The book does not supply one composite score. These values are editable research choices. Custom composite is unaffected.",gocharPage);
    gocharNote->setWordWrap(true);gocharLayout->addWidget(gocharNote);
    auto* gocharGrid=new QGridLayout;gocharGrid->setColumnStretch(2,1);gocharLayout->addLayout(gocharGrid);
    const QStringList gocharLabels{"House tier points","Star Vedha suppression","Aspect share","Dignity floor",
        "Retrograde shift","Combustion factor","Combustion orb (degrees)","Waxing Moon factor","Samagamam orb (degrees)","Samagamam share"};
    const std::array<double,10> gocharValues{gocharRules_.tierPoints,gocharRules_.nakshatraSuppression,
        gocharRules_.aspectShare,gocharRules_.dignityFloor,gocharRules_.retrogradeShift,
        gocharRules_.combustionFactor,gocharRules_.combustionOrb,gocharRules_.pakshaFactor,
        gocharRules_.samagamamOrb,gocharRules_.samagamamShare};
    const std::array<double,10> gocharLow{1,0,0,0,0,0,0,1,0,0};
    const std::array<double,10> gocharHigh{50,1,1,1,1,1,30,2,10,1};
    std::array<QDoubleSpinBox*,10> gocharSpins{};
    for(int i=0;i<10;++i) {
        gocharGrid->addWidget(new QLabel(gocharLabels[i],gocharPage),i,0);
        gocharSpins[i]=spin(gocharValues[i],gocharLow[i],gocharHigh[i]);
        gocharSpins[i]->setObjectName("gocharRule"+QString::number(i));
        gocharSpins[i]->setDecimals(i==0?1:2);
        gocharGrid->addWidget(gocharSpins[i],i,1);
    }
    gocharSpins[0]->setToolTip("Book results are ordinal. Each tier step maps to this many graph points; two steps reach ±100 at the default 50.");
    gocharSpins[1]->setToolTip("Fraction of positive results suspended by a Ch. 23 Nakshatra Vedha. The book gives no numerical fraction.");
    gocharSpins[2]->setToolTip("Share of the Ch. 21 aspected-house mean when blended with the occupied-house result. The book gives no fixed ratio.");
    gocharSpins[3]->setToolTip("Maps the book's dignity fractions into a less compressed graph scale: floor + (1 − floor) × fraction.");
    gocharSpins[9]->setToolTip("Zero keeps Samagamam as a separate event line and detail marker. A positive share blends its event score into the continuous combined line.");
    gocharGrid->addWidget(new QLabel("Avastha potency · odd signs from 0°; even signs reversed",gocharPage),10,0,1,2);
    const QStringList avasthaNames{"Balya","Kumara","Yuva","Vriddha","Mrita"};
    std::array<QDoubleSpinBox*,5> avasthaSpins{};
    for(int i=0;i<5;++i) {
        gocharGrid->addWidget(new QLabel(avasthaNames[i],gocharPage),11+i,0);
        avasthaSpins[i]=spin(gocharRules_.avasthaFactors[i],0,1);
        avasthaSpins[i]->setObjectName("gocharAvastha"+QString::number(i));
        gocharGrid->addWidget(avasthaSpins[i],11+i,1);
    }
    gocharLayout->addStretch(1);
    auto* d9Page=new QWidget(pages); pages->addTab(d9Page,"Natal D9");
    auto* d9Layout=new QVBoxLayout(d9Page);
    auto* d9Note=new QLabel("Ordinary transit signs are compared with natal D9 signs. No transit-D9 conversion, D9 Moorthi/Tara/BAV/Kaksha substitution, or additional dasha rules. Source: Gochar Phaladeepika, PDF pp. 227–229 (printed pp. 234–236).",d9Page);
    d9Note->setWordWrap(true); d9Layout->addWidget(d9Note);
    auto* d9Grid=new QGridLayout; d9Layout->addLayout(d9Grid);
    auto* d9Points=spin(d9Rules_.contactPoints,0,100); d9Points->setObjectName("navamsaContactPoints");
    auto* d9Share=spin(d9Rules_.blendShare*100,0,100); d9Share->setSuffix(" %"); d9Share->setObjectName("navamsaBlendShare");
    d9Grid->addWidget(new QLabel("Contact magnitude (research points)",d9Page),0,0); d9Grid->addWidget(d9Points,0,1);
    d9Grid->addWidget(new QLabel("D9 share in D1+D9",d9Page),1,0); d9Grid->addWidget(d9Share,1,1);
    auto* jupiterReference=new QComboBox(d9Page); jupiterReference->setObjectName("navamsaJupiterReference");
    jupiterReference->addItems({"Unspecified — conditional contact unscored", "Natal Moon — research convention", "Natal Lagna — research convention"});
    jupiterReference->setCurrentIndex(d9Rules_.jupiterDusthanaReference);
    d9Grid->addWidget(new QLabel("Jupiter: Dusthana reference",d9Page),2,0); d9Grid->addWidget(jupiterReference,2,1);
    auto* d9RulesNote=new QLabel(
        "Saturn → natal Sun / Jupiter D9 sign: adverse.\n"
        "Saturn → natal D1 Lagna lord / third lord D9 sign: adverse under the natal-D9-target reading of the listed Rasi/Navamsa positions. No simultaneous natal Rasi match is required.\n"
        "Jupiter → natal D1 Lagna lord D9 sign: favorable, 'except Dusthanas'. The paragraph does not name the reference; by default this contact is shown without a score. Selecting an explicit Moon/Lagna convention excludes the benefit in houses 6, 8 and 12; it does not invent an adverse result.\n\n"
        "One planet's simultaneous contacts do not stack. No matching contact = 0; no supported rule or unresolved condition = unavailable. D1 and D9 each average their scored planets using your existing planet weights; D1+D9 blends those layer means. An unavailable layer is omitted. Unresolved supported rules mark the sample INCOMPLETE with amber graph ticks; remaining scores are partial assessments, not complete verdicts. Unsupported planets are labeled Not covered. Numerical magnitude and blend are research choices, not book formulas.",d9Page);
    d9RulesNote->setWordWrap(true); d9Layout->addWidget(d9RulesNote); d9Layout->addStretch(1);
    auto* naturePage=new QWidget(pages); pages->addTab(naturePage,"Natal classifications");
    auto* natureLayout=new QVBoxLayout(naturePage);
    const QString natureContext=input_.name+" · "+input_.date.toString(Qt::ISODate)+" "+input_.time.toString("HH:mm:ss")+" · "+input_.timezone
        +" · "+zodiacDescription(input_.zodiacSystem,ayanamsa_)+" · "+lunarNodePolicySummary(input_.lunarNodePolicy);
    auto* natureContextLabel=new QLabel(natureContext,naturePage); natureContextLabel->setWordWrap(true); natureLayout->addWidget(natureContextLabel);
    const QString convention="Natal reference only; these classifications do not change the Moorthi scoring choices. Mercury: same-sign majority convention, ties mixed. Functional roles: Rao Table 30.";
    auto* natureNote=new QLabel(convention,naturePage); natureNote->setWordWrap(true); natureLayout->addWidget(natureNote);
    auto* natureTable=new QTableWidget(9,6,naturePage); natureTable->setObjectName("benchmarkNatalClassifications");
    const QStringList natureHeaders{"Planet","Natural","Natural basis","Houses ruled","Functional","Functional basis"};
    natureTable->setHorizontalHeaderLabels(natureHeaders); natureTable->verticalHeader()->hide();
    natureTable->setEditTriggers(QAbstractItemView::NoEditTriggers); natureTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    natureTable->setAlternatingRowColors(true); natureTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    for(int col:{2,5}) natureTable->horizontalHeader()->setSectionResizeMode(col,QHeaderView::Stretch);
    natureLayout->addWidget(natureTable,1);
    QStringList natureReport{natureContext,convention,natureHeaders.join('\t')};
    const auto natures=classifyVedicPlanetNatures(natalChart_,input_.lunarNodePolicy);
    for(int row=0;row<natures.size();++row) {
        const auto& n=natures[row]; const QStringList cells{n.planet,n.natural,n.naturalReason,n.houses,n.functional,n.functionalReason};
        natureReport<<cells.join('\t');
        for(int col=0;col<cells.size();++col) {auto* cell=new QTableWidgetItem(cells[col]);cell->setToolTip(cells[col]);natureTable->setItem(row,col,cell);}
    }
    const QString source="https://vedicastrologer.org/articles/vedic_astro_textbook.pdf#page=178";
    natureReport<<"Source: P.V.R. Narasimha Rao, Vedic Astrology: An Integrated Approach, §§3.2.2 / 13.2, Table 30 (printed pp.166–167). "+source;
    auto* sourceLabel=new QLabel("<a href=\""+source+"\">Source: Rao · §13.2 / Table 30 · pp.166–167</a>",naturePage);
    sourceLabel->setOpenExternalLinks(true); natureLayout->addWidget(sourceLabel);
    auto* copyNature=new QPushButton("Copy classifications",naturePage); natureLayout->addWidget(copyNature,0,Qt::AlignRight);
    connect(copyNature,&QPushButton::clicked,&dialog,[natureReport,copyNature]{QApplication::clipboard()->setText(natureReport.join('\n'));copyNature->setText("Copied");});
    connect(pages,&QTabWidget::currentChanged,&dialog,[natureTable]{
        QTimer::singleShot(0,natureTable,[natureTable]{natureTable->resizeRowsToContents();});
    });
    pages->setCurrentIndex(chartScope_->currentIndex()?2:scoreMode_->currentIndex()?1:0);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel|QDialogButtonBox::RestoreDefaults,&dialog);outer->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{
        if(chartScope_->currentIndex()!=1 && scoreMode_->currentIndex()==0 && std::none_of(weights.begin(),weights.end(),[](auto* weight){return weight->value()>0;})){pages->setCurrentIndex(0);message->setText("At least one component weight must be greater than zero.");return;}
        if(useDasha->isChecked() && std::none_of(dashaWeights.begin(),dashaWeights.end(),[](auto* s){return s->value()>0;})) {
            pages->setCurrentIndex(0);message->setText("At least one dasha weight must be greater than zero.");return;
        }
        bool anyPlanet=false;
        for(int i=0;i<9;++i) anyPlanet|=static_cast<QDoubleSpinBox*>(planetTable->cellWidget(i,2))->value()>0;
        if(usePlanets->isChecked() && !anyPlanet) {
            pages->setCurrentIndex(0);message->setText("At least one planet weight must be greater than zero.");return;
        }
        dialog.accept();
    });
    connect(buttons->button(QDialogButtonBox::RestoreDefaults),&QPushButton::clicked,&dialog,[&]{
        if(pages->currentWidget()==d9Page) {
            const NavamsaTransitRules defaults;
            d9Points->setValue(defaults.contactPoints); d9Share->setValue(defaults.blendShare*100);
            jupiterReference->setCurrentIndex(defaults.jupiterDusthanaReference);
            return; // Resetting the D9 page must not reset the user's D1 preset.
        }
        const VedicBenchmarkRules defaults;
        const GocharRules gocharDefaults;
        const NavamsaTransitRules d9Defaults;
        d9Points->setValue(d9Defaults.contactPoints); d9Share->setValue(d9Defaults.blendShare*100);
        jupiterReference->setCurrentIndex(d9Defaults.jupiterDusthanaReference);
        const std::array<double,10> resetValues{gocharDefaults.tierPoints,gocharDefaults.nakshatraSuppression,
            gocharDefaults.aspectShare,gocharDefaults.dignityFloor,gocharDefaults.retrogradeShift,
            gocharDefaults.combustionFactor,gocharDefaults.combustionOrb,gocharDefaults.pakshaFactor,
            gocharDefaults.samagamamOrb,gocharDefaults.samagamamShare};
        for(int i=0;i<10;++i) gocharSpins[i]->setValue(resetValues[i]);
        for(int i=0;i<5;++i) avasthaSpins[i]->setValue(gocharDefaults.avasthaFactors[i]);
        useDasha->setChecked(defaults.weightByDasha);
        usePlanets->setChecked(defaults.usePlanetWeights);
        for(int i=0;i<9;++i) static_cast<QDoubleSpinBox*>(planetTable->cellWidget(i,2))->setValue(defaults.planetWeights[i]);
        for(int level=0;level<5;++level) dashaWeights[level]->setValue(defaults.dashaWeights[level]);
        for(int i=0;i<4;++i) weights[i]->setValue(defaults.weights[i]);
        for(int i=0;i<4;++i) {static_cast<QDoubleSpinBox*>(metalsTable->cellWidget(i,1))->setValue(defaults.benefic[i]);static_cast<QDoubleSpinBox*>(metalsTable->cellWidget(i,2))->setValue(defaults.malefic[i]);}
        for(int i=0;i<9;++i) {static_cast<QDoubleSpinBox*>(taraTable->cellWidget(i,1))->setValue(defaults.tara[i]);static_cast<QComboBox*>(planetTable->cellWidget(i,1))->setCurrentIndex(i==1?2:defaults.useMalefic[i]?1:0);}
    });
    if(dialog.exec()!=QDialog::Accepted) return;
    d9Rules_.contactPoints=d9Points->value(); d9Rules_.blendShare=d9Share->value()/100;
    d9Rules_.jupiterDusthanaReference=jupiterReference->currentIndex(); storeNavamsaRules(d9Rules_,true);
    for(int i=0;i<4;++i) rules_.weights[i]=weights[i]->value();
    rules_.weightByDasha=useDasha->isChecked();
    rules_.usePlanetWeights=usePlanets->isChecked();
    for(int i=0;i<9;++i) rules_.planetWeights[i]=static_cast<QDoubleSpinBox*>(planetTable->cellWidget(i,2))->value();
    for(int level=0;level<5;++level) rules_.dashaWeights[level]=dashaWeights[level]->value();
    for(int i=0;i<4;++i) {rules_.benefic[i]=static_cast<QDoubleSpinBox*>(metalsTable->cellWidget(i,1))->value();rules_.malefic[i]=static_cast<QDoubleSpinBox*>(metalsTable->cellWidget(i,2))->value();}
    for(int i=0;i<9;++i) {rules_.tara[i]=static_cast<QDoubleSpinBox*>(taraTable->cellWidget(i,1))->value();if(i!=1)rules_.useMalefic[i]=static_cast<QComboBox*>(planetTable->cellWidget(i,1))->currentIndex()==1;}
    gocharRules_.tierPoints=gocharSpins[0]->value();gocharRules_.nakshatraSuppression=gocharSpins[1]->value();
    gocharRules_.aspectShare=gocharSpins[2]->value();gocharRules_.dignityFloor=gocharSpins[3]->value();
    gocharRules_.retrogradeShift=gocharSpins[4]->value();gocharRules_.combustionFactor=gocharSpins[5]->value();
    gocharRules_.combustionOrb=gocharSpins[6]->value();gocharRules_.pakshaFactor=gocharSpins[7]->value();
    gocharRules_.samagamamOrb=gocharSpins[8]->value();gocharRules_.samagamamShare=gocharSpins[9]->value();
    for(int i=0;i<5;++i) gocharRules_.avasthaFactors[i]=avasthaSpins[i]->value();
    storeRules(rules_,true);storeGocharRules(gocharRules_,true);
    if(chartScope_->currentIndex()!=1 && scoreMode_->currentIndex()==0 && !haveBav_ && (rules_.weights[2]>0 || rules_.weights[3]>0) && std::any_of(series_.begin(),series_.end(),[](const auto& s){return s.colorIndex<7;})) {
        clearSamples(); status_->setText("Natal BAV unavailable: "+natalError_); return;
    }
    rescore();
    if(!samples_.isEmpty()) status_->setText(QString(partial_?"Partial samples rescored · ":"Scores updated from cached positions · ")+ruleDescription().section('\n',0,0)+" · "+dashaDescription()+d9RangeSummary());
}
QString VedicBenchmarkPanel::ruleDescription() const {
    QString text="Graph score view: "+scoreDescription()+"\n";
    if(chartScope_->currentIndex()) {
        text+=QString("D9: ordinary transit signs -> natal D9 targets. Book: Gochar Phaladeepika PDF pp.227–229 / printed pp.234–236. Research contact magnitude ±%1; D9 blend share %2%.\n")
            .arg(d9Rules_.contactPoints).arg(d9Rules_.blendShare*100);
        text+="Saturn: Sun/Jupiter D9 contacts adverse; D1 Lagna/third lord D9 contacts use the natal-D9-target reading of the listed Rasi/Navamsa positions, without requiring a simultaneous Rasi match. No trine extension. No dasha-lord target.\n";
        text+="Jupiter: Lagna lord D9 contact favorable except Dusthanas. Reference: "+QString(d9Rules_.jupiterDusthanaReference==0?"unspecified; conditional contact unscored":d9Rules_.jupiterDusthanaReference==1?"natal Moon (research convention)":"natal Lagna (research convention)")+". Houses 6/8/12 remove benefit, not add adversity.\n";
        text+="Repeated contacts per planet do not stack. No match = 0 research baseline; unsupported/unresolved = unavailable. D1 and D9 separately average scored planets with existing planet weights. D1+D9 blends those layer means by the stated share; unavailable layers are omitted and remaining weight normalized. Incomplete supported rules are flagged at each sample and in the export; the remaining D9 mean is partial. Unsupported planets are Not covered. Per-planet Net uses the same blend on that planet's available scores. Daily sampling is a snapshot; event sampling includes every selected planet's sign changes.\n";
        if(chartScope_->currentIndex()==1) return text+dashaDescription()+"\n";
    }
    if(scoreMode_->currentIndex()) {
        text+=QString("Gochar research encoding: house tier ±%1 points; star Vedha suppresses %2 of positive result; aspect share %3; dignity floor %4; retrograde shift %5; combustion factor %6 within %7°; waxing Moon factor %8; Samagamam orb %9°; event share %10.\n")
            .arg(gocharRules_.tierPoints).arg(gocharRules_.nakshatraSuppression).arg(gocharRules_.aspectShare)
            .arg(gocharRules_.dignityFloor).arg(gocharRules_.retrogradeShift).arg(gocharRules_.combustionFactor)
            .arg(gocharRules_.combustionOrb).arg(gocharRules_.pakshaFactor).arg(gocharRules_.samagamamOrb).arg(gocharRules_.samagamamShare);
        text+="Book: Gochar Phaladeepika Ch. 2, 20–23, 26, 29. House/aspect/Paryaya/star rules follow its qualitative descriptions; numeric encodings, suppression, orb, and blend are editable research choices. Unknown Paryaya cells retain the ordinary house tier. Samagamam is an event score, unavailable outside the chosen orb; mixed/conditional book descriptions get zero when an event occurs.\n";
        text+=QString("Avastha potency (Balya/Kumara/Yuva/Vriddha/Mrita): %1 / %2 / %3 / %4 / %5. Retrograde and Paksha classification uses the planet choices on the Planet rules tab; Moon follows its waxing/waning phase. Friend/enemy and Moolatrikona dignity are not inferred without a separate sign-relationship convention.\n")
            .arg(gocharRules_.avasthaFactors[0]).arg(gocharRules_.avasthaFactors[1]).arg(gocharRules_.avasthaFactors[2])
            .arg(gocharRules_.avasthaFactors[3]).arg(gocharRules_.avasthaFactors[4]);
        text+="The custom Moorthi/Tara/BAV/Kaksha composite is separate. Gochar does not add Kaksha as a second BAV vote.\n";
        text+=dashaDescription()+"\n";
        text+="Overall = weighted mean of scored planets. Repeated True/Mean node variants share one planet weight.\n";
        text+=QString("Manual planet weighting: %1; dasha weighting: %2 (active-dasha mode only).\n")
            .arg(rules_.usePlanetWeights?"on":"off",rules_.weightByDasha?"on":"off");
        return text;
    }
    text+=QString("Custom-composite weights: Moorthi %1 · Tara %2 · BAV %3 · Kaksha %4\n").arg(rules_.weights[0]).arg(rules_.weights[1]).arg(rules_.weights[2]).arg(rules_.weights[3]);
    text+=dashaDescription()+"\n";
    if(activeDashasOnly()) text+="Lords evaluated independently at every sample. Sign-entry mode includes enabled dasha boundaries; daily mode is a snapshot, not an average over intervening changes.\n";
    text+="Formula: planet = 100 * sum(component weight * component) / sum(available component weights).\n";
    text+="Moon: no Moorthi; its Moorthi weight is omitted, not scored as zero. Nodes: no BAV/Kaksha. A planet with no weighted available component is excluded.\n";
    text+=(rules_.usePlanetWeights || (activeDashasOnly() && rules_.weightByDasha))?
        "Overall = sum(planet net * effective weight) / sum(effective weights). Effective weight = enabled manual planet weight * enabled dasha weight; disabled factors = 1. Zero-weight planets are excluded.\n":
        "Overall = equal mean of scored planets.\n";
    text+="Mean/True variants share one planet weight. Planet/dasha weights affect the overall mean, not individual planet net scores.\n";
    text+=QString("Manual planet weights: %1\n").arg(rules_.usePlanetWeights?"on":"off");
    for(int i=0;i<9;++i) text+=planets[i]+" weight: "+QString::number(rules_.planetWeights[i])+"\n";
    text+=QString("Dasha weighting preference: %1 (applies only in active-dasha mode)\n").arg(rules_.weightByDasha?"on":"off");
    for(int level=0;level<5;++level) text+=dashaLevelAbbreviation(level)+" weight: "+QString::number(rules_.dashaWeights[level])+"\n";
    text+="BAV component = (BAV - 4) / 4. Node BAV omitted; SAV is context only.\n";
    text+="Kaksha: eight 3°45' sections per sign, ruled Saturn/Jupiter/Mars/Sun/Venus/Mercury/Moon/Lagna. Look up the ruler's 0/1 bindu in the transiting planet's own natal Prastara. Optional component: 1 → +1, 0 → −1. Numeric mapping is a research choice. Nodes omitted. For every crossing use Planet Kaksha entries; Daily snapshot is one moment per day.\n";
    for(int i=0;i<4;++i) text+=QString("%1: benefic %2, malefic %3\n").arg(metals[i]).arg(rules_.benefic[i]).arg(rules_.malefic[i]);
    for(int i=0;i<9;++i) text+=QString("Tara %1 %2: %3\n").arg(i+1).arg(classifyVedicTara(0,i).name).arg(rules_.tara[i]);
    for(int i=0;i<9;++i) text+=planets[i]+": "+(i==1?"not applicable":rules_.useMalefic[i]?"malefic":"benefic")+" Moorthi rule\n";
    return text;
}
// Compact export for pasting into notes or a chat. Scores are only re-read from
// the finished samples: nothing here recalculates or changes a value. Daily
// snapshots repeat identical values between events, so consecutive samples whose
// shown values match are merged into one period row without losing information.
void VedicBenchmarkPanel::copySummary() {
    if(samples_.isEmpty()) return;
    const bool daily=sampling_->currentIndex()==0;
    auto signedNumber=[](double value){ const QString text=number(value); return value>.00001?"+"+text:text; };
    const QChar dot(0x00B7);
    // Each sample holds until the next one (or the range end); summaries are
    // weighted by that span so event sampling is not biased by event density.
    QVector<double> spans(samples_.size());
    for(int i=0;i<samples_.size();++i)
        spans[i]=std::max(0.0,(i+1<samples_.size()?samples_[i+1].jd:end_)-samples_[i].jd);
    auto dateText=[&](double value){ return localTime(value,zone_).toString(daily?"yyyy-MM-dd":"yyyy-MM-dd HH:mm"); };
    auto daysText=[&](double days){ return daily?QString::number(qRound(days)):QString::number(days,'f',1); };

    QStringList lines;
    const QString name=input_.name.trimmed().isEmpty()?QString("Untitled"):input_.name.trimmed();
    lines<<QString("Vedic transit score %1 %2 %1 %3 to %4 %1 %5").arg(dot).arg(name,
        localTime(start_,zone_).date().toString(Qt::ISODate), localTime(end_-1/dayMs,zone_).date().toString(Qt::ISODate),
        partial_?"PARTIAL (stopped or failed early)":"Complete");
    lines<<QString("%2 %1 Sampling: %3 %1 %4 %1 %5 %1 %6%7").arg(dot).arg(scoreDescription(),
        sampling_->currentText()+" "+(daily?time_->time().toString("HH:mm"):anchor_->currentText()),
        zodiacDescription(input_.zodiacSystem,ayanamsa_), lunarNodePolicySummary(input_.lunarNodePolicy),
        QString::fromUtf8(zone_.id()), d9RangeSummary());
    lines<<"Planets: "+dashaDescription();
    if(chartScope_->currentIndex()!=1 && scoreMode_->currentIndex()==0)
        lines<<QString("Weights: Moorthi %2 %1 Tara %3 %1 BAV %4 %1 Kaksha %5").arg(dot)
            .arg(rules_.weights[0]).arg(rules_.weights[1]).arg(rules_.weights[2]).arg(rules_.weights[3]);
    lines<<QString("Birth %1 %2 %3").arg(input_.date.toString(Qt::ISODate),input_.time.toString("HH:mm:ss"),input_.timezone);
    lines<<QString("Scores run -100 to +100. Rows below merge consecutive %1 whose values are identical. "
                   "Right-click Copy results for every sample and the full scoring rules.").arg(daily?"days":"samples");

    // --- Summary of the line shown on the graph ---
    double total=0, weighted=0, positive=0, negative=0, high=-1e9, low=1e9;
    int highCount=0, lowCount=0; double highFirst=0, lowFirst=0;
    for(int i=0;i<samples_.size();++i) {
        const double value=displayedScore(i);
        if(!std::isfinite(value)) continue;
        total+=spans[i]; weighted+=value*spans[i];
        if(value>.00001) positive+=spans[i]; else if(value<-.00001) negative+=spans[i];
        if(value>high+1e-9) {high=value; highCount=0; highFirst=samples_[i].jd;}
        if(std::abs(value-high)<=1e-9) ++highCount;
        if(value<low-1e-9) {low=value; lowCount=0; lowFirst=samples_[i].jd;}
        if(std::abs(value-low)<=1e-9) ++lowCount;
    }
    // Longest unbroken positive and negative stretches.
    struct Run { double days=0, from=0, to=0; };
    Run bestPositive, bestNegative, current; int currentSign=0;
    for(int i=0;i<=samples_.size();++i) {
        const double value=i<samples_.size()?displayedScore(i):std::numeric_limits<double>::quiet_NaN();
        const int sign=!std::isfinite(value)?0:value>.00001?1:value<-.00001?-1:0;
        if(sign!=currentSign || i==samples_.size()) {
            if(currentSign>0 && current.days>bestPositive.days) bestPositive=current;
            if(currentSign<0 && current.days>bestNegative.days) bestNegative=current;
            current={0, i<samples_.size()?samples_[i].jd:0, 0}; currentSign=sign;
        }
        if(i<samples_.size()) {current.days+=spans[i]; current.to=samples_[i].jd;}
    }
    const QString unit=daily?"days":"samples";
    lines<<""<<"SUMMARY "+QString(dot)+" "+display_->currentText();
    if(total>0) {
        lines<<QString("Mean %2 %1 %3% of the time positive %1 %4% negative").arg(dot)
            .arg(signedNumber(weighted/total)).arg(100*positive/total,0,'f',0).arg(100*negative/total,0,'f',0);
        lines<<QString("High %1 on %2 %3 (first %4)").arg(signedNumber(high)).arg(highCount).arg(unit,dateText(highFirst));
        lines<<QString("Low %1 on %2 %3 (first %4)").arg(signedNumber(low)).arg(lowCount).arg(unit,dateText(lowFirst));
        if(bestPositive.days>0) lines<<QString("Longest positive stretch: %1 days, %2 to %3")
            .arg(daysText(bestPositive.days),dateText(bestPositive.from),dateText(bestPositive.to));
        if(bestNegative.days>0) lines<<QString("Longest negative stretch: %1 days, %2 to %3")
            .arg(daysText(bestNegative.days),dateText(bestNegative.from),dateText(bestNegative.to));
    } else lines<<"No scored samples for this line.";

    // --- One row per calendar year (local time) ---
    lines<<""<<"YEAR\tMean\tMin\tMax\t% positive\tDays";
    for(int i=0;i<samples_.size();) {
        const int year=localTime(samples_[i].jd,zone_).date().year();
        double yearTotal=0, yearWeighted=0, yearPositive=0, yearHigh=-1e9, yearLow=1e9, yearDays=0;
        for(;i<samples_.size() && localTime(samples_[i].jd,zone_).date().year()==year;++i) {
            yearDays+=spans[i];
            const double value=displayedScore(i);
            if(!std::isfinite(value)) continue;
            yearTotal+=spans[i]; yearWeighted+=value*spans[i];
            if(value>.00001) yearPositive+=spans[i];
            yearHigh=std::max(yearHigh,value); yearLow=std::min(yearLow,value);
        }
        lines<<(yearTotal>0
            ? QString("%1\t%2\t%3\t%4\t%5\t%6").arg(year).arg(signedNumber(yearWeighted/yearTotal),signedNumber(yearLow),
                  signedNumber(yearHigh)).arg(100*yearPositive/yearTotal,0,'f',0).arg(daysText(yearDays))
            : QString("%1\tN/A\tN/A\tN/A\tN/A\t%2").arg(year).arg(daysText(yearDays)));
    }

    // --- Merged periods ---
    QStringList planetColumns;
    for(const auto& s:series_) planetColumns<<s.planet;
    const bool showDasha=activeDashasOnly();
    auto cellFor=[&](const VedicBenchmarkReading& r) {
        if(!r.score.valid) return QString("N/A");
        QString cell=signedNumber(r.score.net)+" "+signName(signIndex(r.longitude)).left(3)
            +QString(" T%1 %2").arg(r.tara).arg(classifyVedicTara(natalStar_,r.nakshatra).name);
        if(r.planetIndex!=1) cell+=" "+moorthiName(r.entry.moorthi);  // Moon has no Moorthi
        if(!r.roles.isEmpty()) cell+=" ["+r.roles+"]";
        return cell;
    };
    auto rowCells=[&](const VedicBenchmarkSample& sample) {
        if(!sample.error.isEmpty()) return QStringList{"ERROR: "+sample.error};
        QStringList cells{sample.scoredPlanets?signedNumber(sample.overall):"N/A"};
        if(showDasha) cells<<sample.dashaPath;
        for(const QString& planet:planetColumns) {
            QString cell=QString::fromUtf8("—");
            for(const auto& r:sample.readings) if(r.planet==planet) {cell=cellFor(r); break;}
            cells<<cell;
        }
        return cells;
    };
    QStringList periodRows;
    for(int i=0;i<samples_.size();) {
        const QStringList cells=rowCells(samples_[i]);
        int j=i; double days=0;
        while(j<samples_.size() && rowCells(samples_[j])==cells) {days+=spans[j]; ++j;}
        const QString to=daily?dateText(samples_[j-1].jd):dateText(j<samples_.size()?samples_[j].jd:end_);
        periodRows<<QStringList{dateText(samples_[i].jd),to,daysText(days)}.join('\t')+'\t'+cells.join('\t');
        i=j;
    }
    lines<<""<<QString("PERIODS %1 %2 rows from %3 %4 %1 cell = Net, sign, Tara, Moorthi%5")
        .arg(dot).arg(periodRows.size()).arg(samples_.size()).arg(unit, showDasha?", [dasha roles]":"");
    lines<<QStringList{"From",daily?"To (incl.)":"To (excl.)","Days","Overall"}.join('\t')
        +(showDasha?"\tActive dashas":"")+'\t'+planetColumns.join('\t');
    lines<<periodRows;
    QApplication::clipboard()->setText(lines.join('\n'));
    status_->setText(QString("Summary copied %1 %2 periods from %3 %4. Right-click Copy results for full detail.")
        .arg(dot).arg(periodRows.size()).arg(samples_.size()).arg(unit));
}

void VedicBenchmarkPanel::copyFullDetail() {
    if(samples_.isEmpty()) return;
    QStringList lines{scoreDescription()+" · "+QString(partial_?"PARTIAL":"Complete"),
        input_.name+" · Birth "+input_.date.toString(Qt::ISODate)+" "+input_.time.toString("HH:mm:ss.zzz")+" · "+input_.timezone,
        QString("Location %1, %2 · %3 · %4").arg(input_.latitude,0,'g',12).arg(input_.longitude,0,'g',12).arg(zodiacDescription(input_.zodiacSystem,ayanamsa_),lunarNodePolicySummary(input_.lunarNodePolicy)),
        localTime(start_,zone_).toString(Qt::ISODate)+" to "+localTime(end_,zone_).toString(Qt::ISODate)+" (end excluded)",
        "Sampling: "+sampling_->currentText()+" · "+(sampling_->currentIndex()==0?time_->time().toString("HH:mm"):anchor_->currentText()),
        ruleDescription(),"Local\tUTC\tOverall\tPlanet\tRoles\tWeight\tSign\tNakshatra\tMoorthi\tEntry UTC\tMoon at entry\tCount\tTara\tBAV\tSAV\tKaksha\tKaksha ruler\tKaksha bindu\tMoorthi pts\tTara pts\tBAV pts\tKaksha pts\tNet\tActive dashas\tMoon H\tGochar house\tSign Vedha\tStar Vedha\tParyaya\tParyaya score\tStrength\tAspects\tSamagamam score\tGochar notes\tD1 score\tD9 score\tD9 contacts / source\tD1 layer mean\tD9 layer mean\tD9 assessment\tD9 unresolved planets\tD9 not covered\tEffective D1 share\tEffective D9 share\tD1 contribution\tD9 contribution"};
    for(const auto& sample:samples_) {
        const auto time=localTime(sample.jd,zone_); const QString prefix=time.toString(Qt::ISODateWithMs)+'\t'+time.toUTC().toString(Qt::ISODateWithMs)+'\t'+(sample.scoredPlanets?number(sample.overall):"N/A");
        if(!sample.error.isEmpty()){lines<<prefix+'\t'+sample.error;continue;}
        for(const auto& r:sample.readings) lines<<QStringList{prefix,r.planet,r.roles,QString::number(r.planetWeight),signName(signIndex(r.longitude)),vedicNakshatraNames().value(r.nakshatra),moorthiName(r.entry.moorthi),
            r.planetIndex==1?"N/A":localTime(r.entry.jd,QTimeZone::UTC).toString(Qt::ISODateWithMs),r.planetIndex==1?"N/A":signName(r.entry.moonSign),r.planetIndex==1?"N/A":QString::number(r.entry.count),
            QString::number(r.tara)+" "+classifyVedicTara(natalStar_,r.nakshatra).name,r.bav<0?"N/A":QString::number(r.bav),r.sav<0?"N/A":QString::number(r.sav),
            r.kaksha.bindu<0?"N/A":QString::number(r.kaksha.section+1),r.kaksha.bindu<0?"N/A":kakshaDonorName(r.kaksha.donor),r.kaksha.bindu<0?"N/A":QString::number(r.kaksha.bindu),
            r.d1Score.valid&&r.planetIndex!=1&&!scoreMode_->currentIndex()?number(r.d1Score.points[0]):"N/A",r.d1Score.valid&&!scoreMode_->currentIndex()?number(r.d1Score.points[1]):"N/A",r.d1Score.valid&&r.bav>=0&&!scoreMode_->currentIndex()?number(r.d1Score.points[2]):"N/A",
            r.d1Score.valid&&r.kaksha.bindu>=0&&!scoreMode_->currentIndex()?number(r.d1Score.points[3]):"N/A",r.score.valid?number(r.score.net):"N/A",sample.dashaPath,
            r.gochar.valid?QString::number(r.gochar.house):"N/A",r.gochar.valid?number(r.gochar.base):"N/A",
            r.gochar.valid?number(r.gochar.afterSignVedha):"N/A",r.gochar.valid?number(r.gochar.afterNakshatraVedha):"N/A",
            r.gochar.paryaya?QString::number(r.gochar.paryaya):"N/A",r.gochar.valid?number(r.gochar.afterParyaya):"N/A",r.gochar.valid?number(r.gochar.afterStrength):"N/A",
            r.gochar.valid?number(r.gochar.afterAspects):"N/A",r.gochar.samagamam.isEmpty()?"N/A":number(r.gochar.samagamamScore),
            QStringList{r.gochar.signVedha,r.gochar.paryayaVedha,r.gochar.nakshatraVedha,r.gochar.strengthNotes,r.gochar.aspects,r.gochar.samagamam}.filter(QRegularExpression(".+")).join("; "),
            r.d1Score.valid?number(r.d1Score.net):"N/A",r.d9.valid?number(r.d9.score):"N/A",r.d9.details,
            std::isfinite(sample.d1Overall)?number(sample.d1Overall):"N/A",
            std::isfinite(sample.d9Overall)?number(sample.d9Overall):"N/A",
            !chartScope_->currentIndex()?"Not used":!sample.d9Unresolved.isEmpty()?"INCOMPLETE":std::isfinite(sample.d9Overall)?"Assessed under implemented rules":"No covered score",
            sample.d9Unresolved.join(", "),sample.d9NotCovered.join(", "),
            chartScope_->currentIndex()==2?QString::number(sample.effectiveD1Share,'g',12):"N/A",
            chartScope_->currentIndex()==2?QString::number(sample.effectiveD9Share,'g',12):"N/A",
            chartScope_->currentIndex()==2?QString::number(sample.d1Contribution,'g',12):"N/A",
            chartScope_->currentIndex()==2?QString::number(sample.d9Contribution,'g',12):"N/A"}.join('\t');
    }
    QApplication::clipboard()->setText(lines.join('\n'));status_->setText("Benchmark results and scoring rules copied.");
}
} // namespace dracoved
