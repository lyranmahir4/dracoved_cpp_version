#include "vedic_planet_nature.h"
#include "formatting.h"
#include <array>
#include <cmath>
#include <limits>

namespace dracoved {
QVector<VedicPlanetNature> classifyVedicPlanetNatures(const NatalChart& chart, const LunarNodePolicy& nodes) {
    const QStringList names{"Sun","Moon","Mars","Mercury","Jupiter","Venus","Saturn","Rahu","Ketu"};
    // P.V.R. Narasimha Rao, Vedic Astrology: An Integrated Approach,
    // section 13.2, Table 30, printed pp.166–167. C = conditional Moon.
    // Columns: Sun, Moon, Mars, Mercury, Jupiter, Venus, Saturn.
    static constexpr char function[12][8] = {
        "BCBMBMM", "BMNBMMY", "MNMNMBN", "NBYMBMN",
        "BNYMBMM", "NMMBMBN", "MCMBMBY", "NBNMBMM",
        "BNBNNMM", "NCMBMYB", "NMMNMYB", "MBBMNMM"
    };
    static constexpr int rulers[12]={2,5,3,1,0,3,5,2,4,6,6,4};
    std::array<double,9> longitude;
    longitude.fill(std::numeric_limits<double>::quiet_NaN());
    for(const auto& body:chart.bodies) {
        int index=names.indexOf(body.name);
        if(isLunarNodeName(body.name)) {
            if(lunarNodeTypeForName(body.name,effectivePrimaryNodeType(nodes))!=effectivePrimaryNodeType(nodes)) continue;
            index=isNorthLunarNodeName(body.name)?7:8;
        }
        if(index>=0 && std::isfinite(body.longitude)) longitude[index]=normalizeDegrees(body.longitude);
    }
    const int lagna=std::isfinite(chart.angles.asc)?signIndex(chart.angles.asc):-1;
    QVector<VedicPlanetNature> result(9);
    for(int i=0;i<9;++i) {
        auto& row=result[i]; row.planet=names[i]; row.houses="—";
        row.natural="Unavailable"; row.naturalReason="Natal position unavailable.";
        row.functional="Unavailable"; row.functionalReason="Natal Ascendant unavailable.";
        if(i!=1 && i!=3) {
            row.natural=(i==4 || i==5)?"Benefic":"Malefic";
            row.naturalReason="Natural classification · Rao §13.2.";
        }
        if(lagna<0) continue;
        if(i>=7) {
            row.functional="Not assigned";
            row.functionalReason="Rahu/Ketu have no entry in Table 30; no sign ownership assumed.";
            continue;
        }
        QStringList houses;
        for(int house=1;house<=12;++house) if(rulers[(lagna+house-1)%12]==i) houses<<QString::number(house);
        row.houses=houses.join(" / ");
        const char nature=function[lagna][i];
        row.functional=nature=='B'?"Benefic":nature=='M'?"Malefic":nature=='Y'?"Yogakaraka":nature=='C'?"Conditional":"Neutral";
        row.functionalReason=QString("%1 Lagna · rules H%2 · Rao Table 30").arg(signName(lagna),houses.join(" / H"));
        if(nature=='Y') row.functionalReason+=" · quadrant + trine lordship";
    }
    if(std::isfinite(longitude[0]) && std::isfinite(longitude[1])) {
        const double phase=normalizeDegrees(longitude[1]-longitude[0]);
        const bool boundary=phase<1e-8 || 360-phase<1e-8 || std::abs(phase-180)<1e-8;
        result[1].natural=boundary?"Boundary":phase<180?"Benefic":"Malefic";
        result[1].naturalReason=QString("Natal Moon %1 · Moon − Sun %2° · waxing/waning rule")
            .arg(boundary?"at new/full Moon":phase<180?"waxing":"waning").arg(phase,0,'f',3);
        if(lagna>=0 && function[lagna][1]=='C') {
            result[1].functional=boundary?"Conditional":phase<180?"Malefic":"Neutral";
            result[1].functionalReason+=" · quadrant lord: waxing → malefic; waning → neutral";
        }
    }
    bool complete=true;
    for(double lon:longitude) complete &= std::isfinite(lon);
    if(complete) {
        QStringList benefics,malefics; bool uncertain=false;
        for(int i=0;i<9;++i) if(i!=3 && signIndex(longitude[i])==signIndex(longitude[3])) {
            if(result[i].natural=="Benefic") benefics<<names[i];
            else if(result[i].natural=="Malefic") malefics<<names[i];
            else uncertain=true;
        }
        const int good=benefics.size(), bad=malefics.size();
        result[3].natural=uncertain?"Conditional":good==0 && bad==0?"Benefic":good>bad?"Benefic":bad>good?"Malefic":"Mixed";
        result[3].naturalReason=QString("Same-sign association convention · benefics: %1; malefics: %2. Majority rule; ties mixed. %3 nodes.")
            .arg(benefics.isEmpty()?"none":benefics.join(", "),malefics.isEmpty()?"none":malefics.join(", "),lunarNodeTypeToString(effectivePrimaryNodeType(nodes)));
        if(uncertain) result[3].naturalReason+=" Moon classification is at a boundary.";
    } else result[3].naturalReason="Complete natal planet/node positions needed for the same-sign association convention.";
    return result;
}
} // namespace dracoved
