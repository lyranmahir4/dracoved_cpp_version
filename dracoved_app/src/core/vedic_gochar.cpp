#include "vedic_gochar.h"
#include "formatting.h"
#include "vedic_nakshatra.h"
#include "tajaka.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace dracoved {
namespace {
const QStringList names{"Sun","Moon","Mars","Mercury","Jupiter","Venus","Saturn","Rahu","Ketu"};
// Ch. 20, Janma Rasi results. The five ordinal tiers are a research encoding
// of the book's prose; the book does not prescribe these numbers.
constexpr int houses[9][12] = {
    {-1,-1, 2,-1,-2, 2,-1,-2,-1, 1, 2,-1},
    { 1,-1, 1,-1,-1, 1, 1,-2,-1, 1, 2,-1},
    {-1,-1, 2,-1,-1, 2,-1,-2,-1,-1, 2,-1},
    {-1, 1,-1, 1,-1, 1,-1, 1,-1, 1, 2,-1},
    {-1, 2,-1,-1, 2,-1, 2,-2, 2,-1, 2,-1},
    { 1, 1, 1, 1, 1,-1,-1, 1, 1,-1, 1, 1},
    {-1,-1, 2,-2,-1, 1,-1,-2,-1,-2, 2,-1},
    {-1,-1, 1,-1,-1, 1,-1,-2,-1,-1, 1,-1},
    {-1,-1, 1,-1,-1, 1,-1,-2,-1,-1, 1,-1}
};
// Ch. 22: supported benefit house -> obstruction house pairs. The OCR omits
// Rahu/Ketu's fourth counterpart, so their tenth-house pair is not inferred.
constexpr int vedha[9][12] = {
    {0,0,9,0,0,12,0,0,0,4,5,0},
    {5,0,9,0,0,12,2,0,0,4,8,0},
    {0,0,12,0,0,9,0,0,0,0,5,0},
    {0,5,0,3,0,9,0,1,0,8,12,0},
    {0,12,0,0,4,0,3,0,10,0,8,0},
    {8,7,1,10,9,0,0,5,11,0,3,6},
    {0,0,12,0,0,9,0,0,0,0,5,0},
    {0,0,12,0,0,9,0,0,0,0,5,0},
    {0,0,12,0,0,9,0,0,0,0,5,0}
};
// Ch. 21 Table 13: quality of a planet's aspect on the indicated Moon-house.
constexpr int aspected[9][12] = {
    {-1,-1,1,-1,-1,1,-1,-1,-1,1,1,-1},
    { 1,-1,1,-1,-1,1, 1,-1, 1,1,1,-1},
    {-1,-1,1,-1,-1,1,-1,-1,-1,1,1,-1},
    {-1, 1,-1,1,-1,1,-1,1,-1,1,1,-1},
    { 1, 1,1,1, 1,-1,1,1,1,-1,1,-1},
    { 1, 1,1,1,-1,-1,1,-1,1,1,1,1},
    {-1,-1,1,-1,-1,1,-1,-1,-1,-1,1,-1},
    {-1,-1,1,-1,-1,1,-1,-1,-1,1,1,-1},
    { 1, 1,1,-1,-1,1,-1,-1,-1,1,1,-1}
};
struct StarVedha { int natal, count, transit; };
constexpr StarVedha starVedha[] = {
    {0,9,7},{0,9,8},{0,15,8},{1,7,2},{1,12,0},
    {2,4,3},{2,12,1},{3,5,4},{3,17,6},{4,6,5},{4,12,7},
    {5,8,6},{5,18,3},{6,9,0},{6,12,4},
    {7,9,1},{7,13,2},{8,9,1},{8,13,2}
};
int moonHouse(int moonSign, double longitude) {return (signIndex(longitude)-moonSign+12)%12+1;}
double clampScore(double value) {return std::clamp(value,-100.0,100.0);}
double separation(double a,double b) {return std::abs(std::remainder(a-b,360.0));}
bool usable(double value) {return std::isfinite(value) && value>=0 && value<360;}

int paryayaNumber(const GocharContext& c,int planet) {
    if(!std::isfinite(c.birthJd) || c.jd<c.birthJd || !usable(c.natal[planet]) || !usable(c.transit[planet])) return 0;
    const double period=planet==4?4332.59:10759.22;
    const double meanTurns=(c.jd-c.birthJd)/period;
    const double residual=std::remainder(c.transit[planet]-c.natal[planet],360.0)/360.0;
    return std::max(1,int(std::llround(meanTurns-residual))+1);
}
// Ch. 26 gives selected house/cycle outcomes rather than a full Jupiter table.
// 99 means the book does not supply a standalone replacement; zero is mixed.
int paryayaTier(int planet,int cycle,int house,int sign,double ageYears) {
    if(planet==6) {
        static constexpr int saturn[3][12] = {
            {-1,-1,-1,0,-2,-1,-1,-1,1,1,1,-1},
            {1,1,1,1,0,-1,0,1,1,0,-1,0},
            {0,1,-1,-1,-1,1,-1,-1,-1,99,99,99}
        };
        return cycle>=1 && cycle<=3?saturn[cycle-1][house-1]:99;
    }
    if(planet==4) {
        if(cycle==1 && house==8) return -2;
        if(cycle==2 && ageYears<24 && (sign==0 || sign==1)) return 0;
        if(cycle==3 && (house==1 || house==4 || house==5 || house==7 || house==11 || house==12)) return 1;
        if(cycle==4) {
            static constexpr int fourth[12]={-1,1,99,99,1,99,99,99,1,99,1,1};
            return fourth[house-1];
        }
        if(cycle==5) {if(house==1) return -1; if(house==2 || house==4) return 1;}
        if(cycle==6) {
            if(house==2 || house==5 || house==7 || house==9 || house==11) return 1;
            // The book conditions the 8th-house danger on other factors; retain
            // ordinary Ch. 20 result, rather than assert that condition.
        }
    }
    return 99;
}
bool excludedVedhaPair(int subject,int blocker) {
    return (subject==0 && blocker==6) || (subject==6 && blocker==0) ||
           (subject==1 && blocker==3) || (subject==3 && blocker==1) ||
           (subject==5 && blocker==0);
}
double applySignVedha(int planet,int house,double score,const GocharContext& c,int moonSign,QString* explanation) {
    if(score==0) return score;
    int counterpart=0;
    if(score>0) counterpart=vedha[planet][house-1];
    else for(int good=1;good<=12;++good) if(vedha[planet][good-1]==house) {counterpart=good;break;}
    if(!counterpart) return score;
    for(int other=0;other<9;++other) {
        if(other==planet || !usable(c.transit[other]) || excludedVedhaPair(planet,other)) continue;
        if(moonHouse(moonSign,c.transit[other])==counterpart) {
            *explanation=QString("%1 in H%2 cancels %3 H%4 result (%5)")
                .arg(names[other]).arg(counterpart).arg(names[planet]).arg(house)
                .arg(score>0?"Gochar Vedha":"Vipareetha Vedha");
            return 0;
        }
    }
    return score;
}
// Transiting planetary conjunctions: Ch. 29 is qualitative and frequently
// mixed. Only the unambiguous outcomes below have a polarity; zero denotes
// mixed/not assigned, and is still shown as an event in the detail text.
constexpr int samagamamQuality[9][9] = {
    {1,1,0,1,1,1,-1,0,-1},
    {0,1,0,1,1,1,-1,1,-1},
    {-1,0,0,0,1,-1,-1,1,-1},
    {1,0,0,1,1,1,-1,1,-1},
    {1,1,1,1,1,1,0,1,-1},
    {1,1,1,1,1,1,1,1,-1},
    {-1,-1,0,-1,0,0,1,-1,-1},
    {0,1,1,1,1,1,0,0,0},
    {-1,-1,-1,-1,-1,-1,-1,0,0}
};
}

QStringList gocharViewNames() {
    return {"Custom composite","Gochar · Moon-house","+ Sign Vedha","+ Nakshatra Vedha",
            "+ Paryaya","+ Transit strength","+ Vedic aspects","Samagamam events","Combined Gochar research"};
}
double gocharViewScore(const GocharReading& r,int mode) {
    if(!r.valid) return std::numeric_limits<double>::quiet_NaN();
    switch(mode) {
        case 1:return r.base; case 2:return r.afterSignVedha; case 3:return r.afterNakshatraVedha;
        case 4:return r.afterParyaya; case 5:return r.afterStrength; case 6:return r.afterAspects;
        case 7:return r.samagamam.isEmpty()?std::numeric_limits<double>::quiet_NaN():r.samagamamScore;
        case 8:return r.combined;
        default:return std::numeric_limits<double>::quiet_NaN();
    }
}

std::array<GocharReading,9> calculateGochar(const GocharContext& c,const GocharRules& rules) {
    std::array<GocharReading,9> out;
    if(!usable(c.natal[1])) return out;
    const int moonSign=signIndex(c.natal[1]);
    std::array<QStringList,9> triggeredBy;
    std::array<bool,9> isTrigger{};
    for(const auto& rule:starVedha) {
        if(!usable(c.natal[rule.natal]) || !usable(c.transit[rule.transit])) continue;
        const int natalStar=classifyVedicNakshatra(c.natal[rule.natal]).index;
        const int transitStar=classifyVedicNakshatra(c.transit[rule.transit]).index;
        if((transitStar-natalStar+27)%27+1==rule.count) {
            isTrigger[rule.transit]=true;
            const QString text=QString("%1 in star %2 from natal %3").arg(names[rule.transit]).arg(rule.count).arg(names[rule.natal]);
            for(auto& target:triggeredBy) target<<text;
        }
    }
    for(int p=0;p<9;++p) {
        auto& r=out[p]; if(!usable(c.transit[p])) continue;
        r.valid=true; r.house=moonHouse(moonSign,c.transit[p]);r.tier=houses[p][r.house-1];
        r.base=clampScore(r.tier*rules.tierPoints);
        r.afterSignVedha=applySignVedha(p,r.house,r.base,c,moonSign,&r.signVedha);
        r.afterNakshatraVedha=r.afterSignVedha;
        if(!isTrigger[p] && !triggeredBy[p].isEmpty()) r.nakshatraVedha=triggeredBy[p].join("; ");
        if(r.afterNakshatraVedha>0 && !r.nakshatraVedha.isEmpty()) r.afterNakshatraVedha*=1-rules.nakshatraSuppression;
        r.paryaya=(p==4 || p==6)?paryayaNumber(c,p):0;
        const double ageYears=(c.jd-c.birthJd)/365.25;
        const int replacement=paryayaTier(p,r.paryaya,r.house,signIndex(c.transit[p]),ageYears);
        double paryayaBase=replacement!=99 ? clampScore(replacement*rules.tierPoints):r.base;
        r.afterParyaya=applySignVedha(p,r.house,paryayaBase,c,moonSign,&r.paryayaVedha);
        if(r.afterParyaya>0 && !r.nakshatraVedha.isEmpty()) r.afterParyaya*=1-rules.nakshatraSuppression;
        r.afterStrength=r.afterParyaya;
        if(p<7) {
            // Ch. 2 dignity fractions; friend/enemy and Moolatrikona require
            // extra positional tables, so unclassified signs remain neutral.
            QString dignity=dignityLabel(names[p],signName(signIndex(c.transit[p])));
            if(p==3 && signIndex(c.transit[p])==5) dignity="Exalt"; // Mercury in Virgo
            double fraction=dignity=="Exalt"?1:dignity=="Ruler"?.5:dignity=="Fall"?1.0/16:1.0/8;
            const double floor=std::clamp(rules.dignityFloor,0.0,1.0);
            double factor=floor+(1-floor)*fraction;
            const int sign=signIndex(c.transit[p]);
            int section=std::min(4,int(std::fmod(c.transit[p],30.0)/6));
            if(sign%2==1) section=4-section;
            factor*=rules.avasthaFactors[section];
            r.strengthNotes=QString("%1 (%2 fraction); avastha %3/5")
                .arg(dignity=="-"?"Unclassified sign":dignity).arg(fraction).arg(section+1);
            if(p!=0 && p!=1 && usable(c.transit[0]) && separation(c.transit[p],c.transit[0])<=rules.combustionOrb) {
                factor*=rules.combustionFactor; r.strengthNotes+="; near Sun (custom orb)";
            }
            r.afterStrength*=factor;
            if(c.retrograde[p] && p!=0 && p!=1) {
                const bool benefic=!rules.useMalefic[p];
                r.afterStrength+=(benefic?1:-1)*rules.retrogradeShift*std::abs(r.afterParyaya);
                r.strengthNotes+=benefic?"; retrograde benefic":"; retrograde malefic";
            }
            if(r.afterStrength<0) {
                const double severity=p==0?.5:p==2?.75:p==6?1:1;
                r.afterStrength*=severity;
                if(p==0 || p==2) r.strengthNotes+=QString("; malefic potency %1").arg(severity);
            }
        }
        if(usable(c.transit[0]) && usable(c.transit[1])) {
            const double phase=normalizeDegrees(c.transit[1]-c.transit[0]);
            const bool waxing=phase>0 && phase<180;
            const bool darkMoon=phase>=270 || phase<=60;
            const bool malefic=p==1?darkMoon:rules.useMalefic[p];
            if(r.afterStrength>0) {
                double beneficQuantum=1;
                if(p==5) beneficQuantum=.75;
                if(p==3 && !malefic) beneficQuantum=.25;
                if(p==1 && darkMoon) beneficQuantum=.25;
                if(p==3 && darkMoon && separation(c.transit[3],c.transit[1])<=rules.combustionOrb)
                    beneficQuantum=.125;
                if(beneficQuantum<1) {
                    r.afterStrength*=beneficQuantum;
                    r.strengthNotes+=QString("; Ch. 2 benefic quantum %1").arg(beneficQuantum);
                }
            }
            if((waxing && !malefic && r.afterStrength>0) || (!waxing && malefic && r.afterStrength<0)) {
                r.afterStrength*=rules.pakshaFactor;
                r.strengthNotes+=waxing?"; waxing Paksha":"; waning Paksha";
            }
            if(p==4 && usable(c.transit[7]) && separation(c.transit[4],c.transit[7])<=rules.combustionOrb)
                r.strengthNotes+="; Jupiter near Rahu (book malefic condition; no numeric override)";
        }
        r.afterStrength=clampScore(r.afterStrength);
        // Ch. 21: combine occupied Moon-house and aspected Moon-house.
        static constexpr int aspects[9][4] = {{7,0,0,0},{7,0,0,0},{4,7,8,0},{7,0,0,0},
                                               {5,7,9,0},{7,0,0,0},{3,7,10,0},{7,0,0,0},{7,0,0,0}};
        double aspectMean=0;int aspectCount=0;QStringList aspectLabels;
        for(int distance:aspects[p]) if(distance) {
            const int house=(r.house+distance-2)%12+1;
            const int quality=aspected[p][house-1];
            aspectMean+=quality*rules.tierPoints;++aspectCount;
            aspectLabels<<QString("H%1 %2").arg(house).arg(quality>0?"good":"adverse");
        }
        r.aspects=aspectLabels.join(", ");
        if(!r.nakshatraVedha.isEmpty() && aspectMean>0) aspectMean*=1-rules.nakshatraSuppression;
        r.afterAspects=clampScore((1-rules.aspectShare)*r.afterStrength + rules.aspectShare*(aspectMean/aspectCount));
        if(!r.paryayaVedha.isEmpty()) r.afterAspects=0; // Ch. 22 cancels the planet's result.
        // Ch. 29 crossings are events. The ordinal +/- tierPoints is a custom
        // encoding of clearly favorable/adverse descriptions, not a book score.
        double eventSum=0;int events=0;QStringList eventLabels;
        for(int natal=0;natal<9;++natal) if(usable(c.natal[natal])) {
            const double orb=separation(c.transit[p],c.natal[natal]);
            if(orb>rules.samagamamOrb) continue;
            const int quality=samagamamQuality[p][natal];
            eventSum+=quality*rules.tierPoints;++events;
            eventLabels<<QString("natal %1 (%2°, %3)").arg(names[natal]).arg(orb,0,'f',2)
                .arg(quality>0?"favorable":quality<0?"adverse":"mixed/unclassified");
        }
        r.samagamam=eventLabels.join("; ");
        r.samagamamScore=events?clampScore(eventSum/events):0;
        r.combined=events ? clampScore((1-rules.samagamamShare)*r.afterAspects+rules.samagamamShare*r.samagamamScore)
                          : r.afterAspects;
    }
    return out;
}
NavamsaTransitReading calculateNavamsaTransit(int planet, double longitude,
    const std::array<double, 9>& natal, double ascendant, const NavamsaTransitRules& rules) {
    NavamsaTransitReading out;
    if (planet != 4 && planet != 6) {
        out.details = "No explicit natal-D9 contact rule in this layer for this transiting planet (not scored).";
        return out;
    }
    out.supported = true;
    if (!usable(longitude) || !usable(ascendant)) {
        out.incomplete = true;
        out.details = "Natal Ascendant or transit position unavailable.";
        return out;
    }
    // Classical D1 sign rulers, in the same planet order as GocharContext.
    constexpr int rulers[12]{2,5,3,1,0,3,5,2,4,6,6,4};
    const int lagna = signIndex(ascendant), sign = signIndex(longitude);
    const int lagnaLord = rulers[lagna], thirdLord = rulers[(lagna+2)%12];
    QStringList notes;
    bool matched = false, unresolved = false;
    std::array<bool, 9> counted{};
    auto contact = [&](int target, const QString& role, int page, bool listedAlongsideRasi) {
        if (!usable(natal[target])) { unresolved = true; notes << role+": natal position unavailable"; return; }
        if (sign != tajaka::divisionSign(9, natal[target])) return;
        const QString label = QString("%1 (%2), natal D9 %3 · PDF p. %4")
            .arg(role, names[target], signName(sign)).arg(page);
        // The passage lists Rasi/Navamsa targets. It does not require their
        // signs to coincide (which would incorrectly restrict this to vargottama).
        if (!counted[target]) { counted[target] = true; matched = true; }
        notes << label+" · adverse"+(listedAlongsideRasi
            ? " · interpreted as a natal D9 target from the listed Rasi/Navamsa positions" : "");
    };
    if (planet == 6) {
        contact(0, "Sun", 227, false);
        contact(lagnaLord, "D1 Lagna lord", 227, true);
        contact(thirdLord, "D1 third-house lord", 228, true);
        contact(4, "Jupiter", 228, false);
        // Several descriptions of one sign contact do not multiply its score.
        out.valid = matched || !unresolved;
        out.incomplete = unresolved;
        out.score = matched ? -std::clamp(rules.contactPoints, 0.0, 100.0) : 0;
    } else if (!usable(natal[lagnaLord])) {
        out.incomplete = true;
        notes << "Natal Lagna lord position unavailable.";
    } else if (sign == tajaka::divisionSign(9, natal[lagnaLord])) {
        const QString label = QString("D1 Lagna lord (%1), natal D9 %2 · PDF p. 229")
            .arg(names[lagnaLord], signName(sign));
        const int reference = rules.jupiterDusthanaReference;
        if (reference == 0 || (reference == 1 && !usable(natal[1]))) {
            out.incomplete = true;
            notes << label+(reference == 0
                ? " · favorable but unresolved: 'except Dusthanas' has no specified reference in this passage"
                : " · favorable but unresolved: natal Moon position unavailable for the selected convention");
        } else {
            const int baseSign = reference == 1 ? signIndex(natal[1]) : lagna;
            const int house = (sign-baseSign+12)%12+1;
            const bool excluded = house == 6 || house == 8 || house == 12;
            out.valid = true;
            out.score = excluded ? 0 : std::clamp(rules.contactPoints, 0.0, 100.0);
            notes << label+QString(" · %1 · H%2 from natal %3 (explicit research convention)")
                .arg(excluded ? "benefit excluded; no adverse score inferred" : "favorable")
                .arg(house).arg(reference == 1 ? "Moon" : "Lagna");
        }
    } else out.valid = true; // supported planet, no matching contact: neutral baseline
    if (notes.isEmpty()) notes << "No matching natal-D9 contact · 0 is a research baseline, not a favorable/adverse prediction.";
    out.details = notes.join("; ");
    return out;
}
} // namespace dracoved
