#include "vedic_benchmark_panel.h"
#include "moorthi_panel.h"
#include "dasha_panel.h"
#include "time_graph_range.h"
#include "../core/vedic_nakshatra.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDateEdit>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QLabel>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QTimer>
#include <QWheelEvent>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace dracoved;
void require(bool ok,const char* text) {if(!ok) throw std::runtime_error(text);}
bool near(double a,double b) {return std::abs(a-b)<1e-8;}
int main(int argc,char** argv) {
    QApplication app(argc,argv); QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf"); app.setFont(QFont("Segoe UI",9));
    QCoreApplication::setOrganizationName("DracoVedBenchmarkCheck"); QCoreApplication::setApplicationName("Isolated");
    QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,QCoreApplication::applicationDirPath()+"/settings");
    QSettings().clear();
    try {
        TimeGraphRange window;window.setRange(0,100);window.zoom(.5,1);window.panTo(1000);
        require(near(window.start(),20)&&near(window.end(),100),"Pan clamps to range end without changing zoom");
        window.panTo(-1000);require(near(window.start(),0)&&near(window.end(),80),"Pan clamps to range start");
        VedicBenchmarkRules rules;
        require(near(scoreVedicBenchmark(4,0,2,8,rules).net,100),"All maximum components");
        require(near(scoreVedicBenchmark(6,0,2,8,rules).net,50),"Book malefic Gold ranking");
        require(near(scoreVedicBenchmark(4,2,1,4,rules).net,0),"Neutral Copper/Janma/BAV4");
        require(near(scoreVedicBenchmark(7,0,1,-1,rules).net,-25),"Node BAV omitted from denominator");
        VedicBenchmarkRules kakshaRules; kakshaRules.weights={0,0,0,1};
        require(near(scoreVedicBenchmark(4,0,1,5,kakshaRules,1).net,100)
            && near(scoreVedicBenchmark(4,0,1,5,kakshaRules,0).net,-100)
            && scoreVedicBenchmark(1,-1,1,5,kakshaRules,1).valid
            && !scoreVedicBenchmark(7,0,1,-1,kakshaRules,1).valid,
            "Optional Kaksha component follows bindu and excludes nodes");
        rules.weights={0,0,1}; require(!scoreVedicBenchmark(7,0,1,-1,rules).valid,"BAV-only node is unavailable, not zero");
        rules.weights={2,1,1}; const auto weighted=scoreVedicBenchmark(4,0,3,0,rules);
        require(near(weighted.net,0) && near(weighted.points[0],50) && near(weighted.points[1],-25),"Weighted contribution arithmetic");
        rules.weights={2.5,1,.5};
        const auto moonScore=scoreVedicBenchmark(1,-1,3,8,rules);
        require(moonScore.valid && near(moonScore.net,-100.0/3) && moonScore.points[0]==0,"Moon omits Moorthi weight rather than treating it as zero");
        rules.useMalefic[1]=true;
        require(near(scoreVedicBenchmark(1,0,3,8,rules).net,moonScore.net),"Stale Moon metal or classification cannot alter its score");
        rules.usePlanetWeights=true;rules.planetWeights[1]=2;rules.weightByDasha=true;
        require(near(vedicPlanetWeight(1,3,rules),6),"Moon keeps manual and highest-role dasha weights");
        rules.weights={1,0,0};require(!scoreVedicBenchmark(1,-1,3,8,rules).valid,"Moorthi-only Moon is unavailable, not neutral");
        require(scoreVedicBenchmark(4,0,3,8,rules).valid && !scoreVedicBenchmark(4,-1,3,8,rules).valid,"Non-Moon metal requirements preserved");

        SwissEph swe; QString error; require(swe.load({"swedll64.dll"},&error),qPrintable(error)); swe.setEphePath("ephe");
        NatalInput input; input.name="Reference chart"; input.date=QDate(1921,6,28); input.time=QTime(12,49); input.timezone="Asia/Dhaka";
        input.siderealAyanamsa=SiderealAyanamsa::Raman; input.lunarNodePolicy.mode=LunarNodeMode::Both;
        NatalChart chart; chart.zodiacSystem=ZodiacSystem::Sidereal; chart.siderealAyanamsa=SiderealAyanamsa::Lahiri; chart.angles.asc=174;
        const int signs[]={2,11,2,2,4,0,4};
        for(int p=0;p<7;++p) {BodyPosition b;b.name=ashtakavargaPlanets()[p];b.longitude=signs[p]*30+10;chart.bodies<<b;}
        AshtakavargaResult natal; require(computeAshtakavarga(chart,&natal,&error),"Natal fixture");
        MoorthiGraphPanel panel(&swe);panel.resize(1600,850);panel.setContext(input,chart);panel.show();
        auto* view=panel.findChild<QComboBox*>("moorthiGraphView");
        auto* from=panel.findChild<QDateEdit*>("moorthiGraphFrom");auto* through=panel.findChild<QDateEdit*>("moorthiGraphThrough");
        auto* run=panel.findChild<QPushButton*>("moorthiGraphCalculate");auto* stop=panel.findChild<QPushButton*>("moorthiGraphStop");
        auto* ingressTimer=panel.findChild<QTimer*>("moorthiGraphTimer");
        auto* benchmark=static_cast<VedicBenchmarkPanel*>(panel.findChild<QWidget*>("vedicBenchmarkPanel"));
        auto wait=[&] {QElapsedTimer elapsed;elapsed.start();while((ingressTimer->isActive()||benchmark->isRunning())&&elapsed.elapsed()<60000)app.processEvents();require(!ingressTimer->isActive()&&!benchmark->isRunning(),"Complete cancellable graph pipeline");};
        for(auto* check:panel.findChildren<QCheckBox*>("moorthiGraphChoice"))check->setChecked(check->text()=="Moon"||check->text()=="Mercury"||check->text().startsWith("Rahu"));
        from->setDate(QDate(2026,6,1));through->setDate(QDate(2026,7,15));view->setCurrentIndex(1);run->click();wait();
        require(benchmark->samples().size()==45,"One daily snapshot per inclusive date");
        for(const auto& sample:benchmark->samples()) {
            require(sample.scoredPlanets==3 && sample.readings.size()==4,"Equal logical planets with both node models");
            double sum=0,rahus=0;
            for(const auto& reading:sample.readings) {
                require(reading.entry.jd<=sample.jd+.02/86400,"No future Moorthi entry used");
                const auto source=std::find_if(panel.series().begin(),panel.series().end(),[&](const auto& s){return s.planet==reading.planet;});
                int latest=0;while(latest+1<source->entries.size()&&source->entries[latest+1].jd<=sample.jd+.01/86400)++latest;
                require(near(reading.entry.jd,source->entries[latest].jd),"Use latest actual entry, including retrograde re-entry");
                const int sign=int(reading.longitude/30);
                require(reading.bav==(reading.planetIndex<7?natal.bav[reading.planetIndex][sign]:-1),"Own natal BAV lookup");
                if(reading.planetIndex<7) {
                    KakshaBindu expected;
                    require(kakshaBinduAt(natal,reading.planetIndex,reading.longitude,&expected)
                        && reading.kaksha.section==expected.section && reading.kaksha.bindu==expected.bindu,
                        "Graph uses own natal Prastara at transit degree");
                } else require(reading.kaksha.bindu==-1,"Node Kaksha unavailable");
                if(reading.planetIndex==1) require(reading.entry.moorthi==Moorthi::NotApplicable && reading.entry.count==0 && reading.score.points[0]==0
                    && near(reading.score.net,50*(VedicBenchmarkRules{}.tara[reading.tara-1]+(reading.bav-4)/4.0)),"Real Moon samples use Tara/BAV only");
                if(reading.planetIndex==7)rahus+=reading.score.net;else sum+=reading.score.net;
            }
            require(near(sample.overall,(sum+rahus/2)/3),"Both node models share one planet weight");
        }
        auto* plot=panel.findChild<QWidget*>("vedicBenchmarkPlot");app.processEvents();
        const auto& sample=benchmark->samples()[10];
        const QTimeZone zone("Asia/Dhaka");const double first=2440587.5+from->date().startOfDay(zone).toMSecsSinceEpoch()/86400000.0;
        const double last=2440587.5+through->date().addDays(1).startOfDay(zone).toMSecsSinceEpoch()/86400000.0;
        const QPointF pos(52+(sample.jd-first)/(last-first)*(plot->width()-74),32+(plot->height()-72)/2.0-sample.overall/200*(plot->height()-72));
        qint64 inspected=0;panel.onMomentSelected=[&](qint64 ms){inspected=ms;};
        auto mouse=[&](QEvent::Type type,QPointF at,Qt::MouseButton button,Qt::MouseButtons buttons) {
            QMouseEvent event(type,at,plot->mapToGlobal(at.toPoint()),button,buttons,Qt::NoModifier);QApplication::sendEvent(plot,&event);
        };
        auto clickAt=[&](QPointF at) {mouse(QEvent::MouseButtonPress,at,Qt::LeftButton,Qt::LeftButton);mouse(QEvent::MouseButtonRelease,at,Qt::LeftButton,Qt::NoButton);};
        clickAt(pos);
        require(std::abs(inspected-qRound64((sample.jd-2440587.5)*86400000))<2,"Graph click updates shared inspection time");
        QWheelEvent zoom(pos,plot->mapToGlobal(pos.toPoint()),QPoint(),QPoint(0,120),Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(plot,&zoom);app.processEvents();
        const auto& later=benchmark->samples()[15];
        const QPointF zoomPos(pos.x()+1.25*(later.jd-sample.jd)/(last-first)*(plot->width()-74),pos.y());
        clickAt(zoomPos);
        require(std::abs(inspected-qRound64((later.jd-2440587.5)*86400000))<2 && benchmark->samples().size()==45 && !ingressTimer->isActive(),"Cursor-anchored zoom preserves click mapping without recalculation");
        const qint64 pinned=inspected;
        mouse(QEvent::MouseButtonPress,zoomPos,Qt::LeftButton,Qt::LeftButton);
        mouse(QEvent::MouseMove,zoomPos-QPointF(100,0),Qt::NoButton,Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease,zoomPos-QPointF(100,0),Qt::LeftButton,Qt::NoButton);
        require(inspected==pinned,"Dragging does not select a different point");
        clickAt(zoomPos-QPointF(100,0));require(inspected==pinned,"Point selection follows the panned time axis");
        mouse(QEvent::MouseButtonPress,zoomPos-QPointF(100,0),Qt::LeftButton,Qt::LeftButton);
        mouse(QEvent::MouseMove,zoomPos,Qt::NoButton,Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease,zoomPos,Qt::LeftButton,Qt::NoButton);
        clickAt(zoomPos);require(inspected==pinned,"Panning back restores point position");
        QWheelEvent zoomOut(pos,plot->mapToGlobal(pos.toPoint()),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);
        QApplication::sendEvent(plot,&zoomOut);clickAt(pos);
        require(std::abs(inspected-qRound64((sample.jd-2440587.5)*86400000))<2,"Ctrl-wheel down zooms back out");
        QApplication::sendEvent(plot,&zoom);
        QTimer::singleShot(0,[&]{auto* menu=qobject_cast<QMenu*>(app.activePopupWidget());require(menu && menu->actions().first()->isEnabled(),"Graph has enabled Fit action when zoomed");
            menu->setActiveAction(menu->actions().first());QKeyEvent enter(QEvent::KeyPress,Qt::Key_Return,Qt::NoModifier);QApplication::sendEvent(menu,&enter);});
        QContextMenuEvent context(QContextMenuEvent::Mouse,QPoint(5,5),plot->mapToGlobal(QPoint(5,5)));
        QApplication::sendEvent(plot,&context);clickAt(pos);
        require(std::abs(inspected-qRound64((sample.jd-2440587.5)*86400000))<2,"Fit from graph margin restores full-range point mapping");
        require(panel.findChild<QTableWidget*>("vedicBenchmarkDetails")->rowCount()==4,"Graph point breakdown");
        auto* details=panel.findChild<QTableWidget*>("vedicBenchmarkDetails");
        for(int row=0;row<details->rowCount();++row) if(details->item(row,0)->text()=="Moon")
            require(details->item(row,3)->text()=="Not applicable" && details->item(row,4)->text()=="—" && details->item(row,10)->text()=="—","Moon detail cells never imply a metal or zero contribution");
        benchmark->findChild<QPushButton*>("vedicBenchmarkCopy")->click();
        require(app.clipboard()->text().contains("Weights: Moorthi")&&app.clipboard()->text().contains("Entry UTC"),"Copy records scoring rules and source entries");
        require(app.clipboard()->text().contains("Not applicable\tN/A\tN/A\tN/A"),"Copied Moon has no fabricated metal provenance");
        app.processEvents(); app.processEvents();
        panel.grab().save(QCoreApplication::applicationDirPath()+"/vedic_benchmark.png");
        // Editing weights must reuse raw cached readings and preserve sample times.
        const double firstJd=benchmark->samples()[0].jd;
        QTimer::singleShot(0,[&]{auto* dialog=qobject_cast<QDialog*>(app.activeModalWidget());
            bool found=false;for(auto* type:dialog->findChildren<QComboBox*>()) if(type->currentText()=="Not applicable")found=!type->isEnabled();
            require(found,"Moon Moorthi rule is disabled and explicitly N/A");
            for(int i=0;i<3;++i)dialog->findChild<QDoubleSpinBox*>("benchmarkWeight"+QString::number(i))->setValue(i==2?1:0);dialog->accept();});
        benchmark->findChild<QPushButton*>("vedicBenchmarkRules")->click();
        require(benchmark->samples().size()==45 && near(benchmark->samples()[0].jd,firstJd)&&!ingressTimer->isActive(),"Weights rescore cached samples");
        require(benchmark->samples()[0].scoredPlanets==2,"Unavailable BAV-only nodes excluded explicitly");
        auto* sampling=benchmark->findChild<QComboBox*>("vedicBenchmarkSampling");
        auto* anchor=benchmark->findChild<QComboBox*>("vedicBenchmarkAnchor");sampling->setCurrentIndex(1);anchor->setCurrentIndex(anchor->findText("Moon"));
        run->click();require(!ingressTimer->isActive(),"Sampling change reuses cached Moorthi entries");wait();
        const auto moon=std::find_if(panel.series().begin(),panel.series().end(),[](const auto& s){return s.planet=="Moon";});
        require(benchmark->samples().size()==moon->entries.size(),"Range start plus Moon ingress samples");
        for(const auto& s:benchmark->samples())for(const auto& r:s.readings)if(r.planet=="Moon")require(int(r.longitude/30)==r.entry.newSign,"Ingress sample uses destination side");
        sampling->setCurrentIndex(2);run->click();wait();
        require(benchmark->samples().size()>moon->entries.size(),"Kaksha sampling includes within-sign crossings");
        view->setCurrentIndex(0);require(!plot->isVisible(),"Original metals view preserved");view->setCurrentIndex(1);
        QTimer::singleShot(0,[&]{auto* dialog=qobject_cast<QDialog*>(app.activeModalWidget());
            for(int i=0;i<3;++i)dialog->findChild<QDoubleSpinBox*>("benchmarkWeight"+QString::number(i))->setValue(i==0?1:0);dialog->accept();});
        benchmark->findChild<QPushButton*>("vedicBenchmarkRules")->click();
        for(const auto& s:benchmark->samples()) {require(s.scoredPlanets==2,"Moorthi-only overall excludes Moon");
            for(const auto& r:s.readings)if(r.planetIndex==1)require(!r.score.valid,"Moon-only Moorthi has no plottable value");}
        run->click();stop->click();require(!benchmark->isRunning(),"Score sampling stops");
        chart.bodies[0].longitude+=30;panel.setContext(input,chart);
        require(benchmark->samples().isEmpty()&&!benchmark->hasSource(),"Non-Moon natal change invalidates BAV benchmark");
        MoorthiPanel entries(&swe);entries.setContext(input,chart);
        require(entries.findChild<QComboBox*>("moorthiPlanet")->findText("Moon")==-1,"Standalone Moorthi excludes Moon");
        chart.utcDateTime=QDateTime(QDate(2026,6,1),QTime(6,0),QTimeZone::UTC);chart.bodies[1].longitude=40;
        DashaPanel dashas(&swe);dashas.setContext(input,chart,"Moon fixture");dashas.setInspectionTime(chart.utcDateTime.toMSecsSinceEpoch());dashas.show();app.processEvents();
        auto* lookup=dashas.findChild<QTimer*>();QElapsedTimer elapsed;elapsed.start();
        while(lookup->isActive()&&elapsed.elapsed()<10000)app.processEvents();require(!lookup->isActive(),"Moon dasha snapshot resolves");
        auto* transits=dashas.findChild<QTableWidget*>("dashaTransits");
        require(transits->rowCount()==1 && transits->item(0,0)->text()=="Moon" && transits->item(0,6)->text()=="Not applicable"
            && transits->item(0,6)->icon().isNull() && transits->item(0,7)->text()!="—","Active Moon keeps Tara/sign-entry, never a metal icon");
        dashas.findChild<QPushButton*>("dashaCopy")->click();require(app.clipboard()->text().contains("Not applicable")&&!app.clipboard()->text().contains("Count 0"),"Dasha copy matches Moon applicability");
        std::cout<<"PASS: book defaults; weighted arithmetic; daily/event sampling; actual Moorthi carry-in/re-entries; node handling; linked click/copy; cached rescore; cancellation/invalidation.\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
