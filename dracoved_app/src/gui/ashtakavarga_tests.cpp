#include "ashtakavarga_panel.h"
#include "../core/swiss_eph.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateEdit>
#include <QDir>
#include <QFontDatabase>
#include <QPushButton>
#include <QTableWidget>
#include <QTimeEdit>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>

using namespace dracoved;
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
    app.setFont(QFont("Segoe UI", 9));
    try {
        // Independent published fixture: Rao, Chart 6 (p.107), answers to
        // Exercises 19/20 (p.163). Signs are Aries=0; no ephemeris rounding involved.
        const int signs[] = {2,11,2,2,4,0,4};
        const std::array<std::array<int,12>,7> expected = {{
            {5,3,5,3,4,4,2,3,5,4,5,5}, {3,2,5,3,6,3,4,5,5,5,3,5},
            {4,3,4,3,4,3,2,5,1,3,3,4}, {7,4,7,4,4,3,4,4,4,3,6,4},
            {4,3,5,6,3,7,4,3,5,6,5,5}, {8,7,4,3,3,2,4,6,4,4,4,3},
            {3,3,4,3,2,3,2,3,4,5,3,4}
        }};
        const std::array<int,12> sav = {34,25,34,25,26,25,22,29,28,30,29,30};
        NatalChart chart; chart.zodiacSystem = ZodiacSystem::Sidereal; chart.angles.asc = 5*30+24;
        for (int p=0; p<7; ++p) { BodyPosition body; body.name=ashtakavargaPlanets()[p]; body.longitude=signs[p]*30+10; chart.bodies.push_back(body); }
        AshtakavargaResult scores; QString error;
        require(computeAshtakavarga(chart,&scores,&error),qPrintable(error));
        require(scores.bav==expected && scores.sav==sav,"Published BAV/SAV fixture mismatch");
        require(std::accumulate(scores.sav.begin(),scores.sav.end(),0)==337,"SAV total");
        const int kakshaDonors[] = {6,4,2,0,5,3,1,7};
        for (int section=0; section<8; ++section) {
            KakshaBindu point;
            require(kakshaBinduAt(scores,6,section*3.75+0.001,&point)
                && point.sign==0 && point.section==section && point.donor==kakshaDonors[section]
                && point.bindu==scores.contributions[6][kakshaDonors[section]][0],
                "Book Kaksha order and own Prastara bindu");
        }
        KakshaBindu wrapped;
        require(kakshaBinduAt(scores,0,360,&wrapped) && wrapped.sign==0 && wrapped.section==0,
                "Kaksha zodiac wrap");
        require(!kakshaBinduAt(scores,7,0,&wrapped) && wrapped.bindu==-1,"No node Kaksha");
        for (int p=0;p<7;++p) for (int s=0;s<12;++s) {
            int sum=0; for (int d=0;d<8;++d) sum+=scores.contributions[p][d][s];
            require(sum==scores.bav[p][s],"Prastara sum mismatch");
        }
        auto shifted=chart; shifted.angles.asc+=30;
        for (auto& body:shifted.bodies) body.longitude+=30;
        AshtakavargaResult rotated; require(computeAshtakavarga(shifted,&rotated,&error),"Rotated chart");
        for (int s=0;s<12;++s) for(int p=0;p<7;++p)
            require(rotated.bav[p][(s+1)%12]==scores.bav[p][s],"Zodiac wraparound");
        shifted.bodies[0].longitude=std::numeric_limits<double>::quiet_NaN();
        require(!computeAshtakavarga(shifted,&rotated,&error) && rotated.sav[0]==0,"Invalid position must clear scores");
        shifted=chart; shifted.bodies[0].longitude=360;
        require(computeAshtakavarga(shifted,&rotated,&error) && rotated.referenceSigns[0]==0,"360 boundary");
        shifted.bodies[0].longitude=-0.001;
        require(computeAshtakavarga(shifted,&rotated,&error) && rotated.referenceSigns[0]==11,"Negative longitude wrap");

        SwissEph swe; require(swe.load({QDir::currentPath()+"/swedll64.dll"},&error),qPrintable(error));
        swe.setEphePath(QDir::currentPath()+"/ephe");
        NatalInput input; input.name="Published reference"; input.timezone="Asia/Dhaka";
        input.siderealAyanamsa=SiderealAyanamsa::Raman;
        AshtakavargaPanel panel(&swe); panel.resize(1600,700);
        panel.setContext(input,chart,"Reference chart · Sidereal Lahiri · Lagna Virgo");
        auto* matrix=panel.findChild<QTableWidget*>("ashtakavargaMatrix");
        auto* transits=panel.findChild<QTableWidget*>("ashtakavargaTransits");
        auto* order=panel.findChild<QComboBox*>("ashtakavargaOrder");
        auto* planet=panel.findChild<QComboBox*>("ashtakavargaPlanet");
        require(matrix->item(7,13)->text()=="337","Matrix total");
        order->setCurrentIndex(1); require(matrix->horizontalHeaderItem(1)->text()=="Vir\nH1","Lagna order");
        require(matrix->item(7,1)->text()=="25","Reordered scores"); order->setCurrentIndex(0);
        const qint64 instant=QDateTime(QDate(2026,9,21),QTime(12,0),QTimeZone::UTC).toMSecsSinceEpoch();
        panel.setInspectionTime(instant); panel.setActiveLords({"Saturn"});
        qint64 notified=0; panel.onMomentCalculated=[&](qint64 ms) {notified=ms; panel.setInspectionTime(ms);};
        auto* run=panel.findChild<QPushButton*>("ashtakavargaCalculate"); run->click();
        require(transits->rowCount()==7 && notified==instant,"Transit calculation / shared time");
        require(transits->item(6,0)->font().bold(),"Active-lord indication");
        for(int p=0;p<7;++p) {
            int s=transits->item(p,1)->data(Qt::UserRole).toInt();
            require(transits->item(p,3)->text().toInt()==expected[p][s] && transits->item(p,4)->text().toInt()==sav[s],"Transit scores must use natal matrices");
        }
        transits->setCurrentCell(6,0); require(planet->currentIndex()==6 && matrix->currentRow()==6,"Transit selection linking");
        panel.findChild<QPushButton*>("ashtakavargaCopy")->click();
        require(app.clipboard()->text().contains("Prastara: Saturn") && app.clipboard()->text().contains("2026-09-21T12:00:00.000Z"),"Copy parity");
        double restored=0, reference=0; swe.getAyanamsaUt(2461305.0,&restored,&error);
        swe.setSidMode(siderealAyanamsaSwissMode(input.siderealAyanamsa)); swe.getAyanamsaUt(2461305.0,&reference,&error);
        require(std::abs(restored-reference)<1e-10,"Shared ayanamsa restoration");
        panel.show(); app.processEvents(); panel.grab().save(QCoreApplication::applicationDirPath()+"/ashtakavarga.png");
        panel.findChild<QDateEdit*>("ashtakavargaDate")->setDate(QDate(2026,9,22));
        require(transits->rowCount()==0 && matrix->rowCount()==8,"Time change must clear only transit results");
        input.timezone="America/New_York"; panel.setContext(input,chart,"DST check");
        panel.findChild<QDateEdit*>("ashtakavargaDate")->setDate(QDate(2026,3,8));
        panel.findChild<QTimeEdit*>("ashtakavargaTime")->setTime(QTime(2,30)); run->click();
        require(transits->rowCount()==0,"DST gap must be rejected");
        panel.setContext(input,{},{}); require(matrix->rowCount()==0 && !run->isEnabled(),"Invalid natal context must clear scores");
        std::cout<<"PASS: published seven-planet BAV/SAV fixture; contributions; wraparound; transit lookup; linked selection; time/copy/state handling.\n";
    } catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1;}
}
