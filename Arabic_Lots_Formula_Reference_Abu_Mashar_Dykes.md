# Arabic Lots Formula Reference

Source base: local OCR files in `C:\Users\Mahir\Downloads\Abu Mashar`.

Primary formula source:

- `ocr_output/Introductions_to_Traditional_Astrology/Introductions to Traditional Astrology (Benjamin N. Dykes)_ocr.md`
- Main section: Book VI, Lots, approximately lines `11192-13350`.

PN IV revolution-use source:

- `ocr_output/Persian_Nativities_IV/pages/page_0154.txt`
- `ocr_output/Persian_Nativities_IV/pages/page_0155.txt`
- `ocr_output/Persian_Nativities_IV/pages/page_0444.txt`
- `ocr_output/Persian_Nativities_IV/pages/page_0445.txt`
- `ocr_output/Persian_Nativities_IV/pages/page_0446.txt`
- `ocr_output/Persian_Nativities_IV/pages/page_0447.txt`

This file is a working reference for LLMs. It paraphrases the formula material and source notes; it is not a verbatim copy of the books.

## Calculation Convention

Formula notation:

```text
Lot = Projector + To - From
```

Usually the projector is the Ascendant. If the source says "from A to B, projected from the Ascendant", calculate:

```text
Lot = ASC + B - A
```

Normalize the result into `0-360` degrees.

Example:

```text
from Sun to Moon, projected from ASC = ASC + Moon - Sun
```

If the source says "conversely by night" or "reversed by night", reverse `From` and `To` in nocturnal charts.

If the source says "day and night", do not reverse.

If the source says "probably not reversed", treat the formula as unreversed unless a later audit chooses otherwise.

## Important Terms

- **Lot of the Absent / Non-Appearance / Hidden** = Lot of Spirit.
- **House degree** may mean the house cusp degree. In whole-sign use, this is often the first degree of the sign of that house. In quadrant use, use the actual cusp.
- **Domicile of a house** means the sign of that house.
- **Lord of a house** means the traditional sign ruler.
- **Conjunctional nativity** means birth after a New Moon and before the next Full Moon.
- **Preventional nativity** means birth after a Full Moon and before the next New Moon.

## How PN IV Uses Lots In Revolutions

PN IV does not use Lots only as natal static points. In revolutions:

- Lots can be profected.
- Lots can be directed or turned.
- The Lord of the Year activates Lots it occupies, aspects, or rules.
- Solar Return planets contacting natal Lots can activate the Lot topic.
- A Solar Return Lot can be computed and judged for that year.
- A Monthly Revolution Lot can be computed and judged for that month.
- The Lot of Fortune is especially important for assets, livelihood, trade, social prominence, praise, and opportunities.

PN IV Book VI.2 also says that planets, houses, and Lots are "turned" in revolutions: one sign per year, and degrees can also be directed. When a turned/directed Lot reaches a fortune, infortune, sign, or planet, it produces the indication according to the nature of what it reaches.

## Seven Planetary Lots

| Lot | Day Formula | Night Formula | Projector | Main Source Location | Main Topic |
|---|---|---|---|---|---|
| Fortune / Moon | ASC + Moon - Sun | ASC + Sun - Moon | ASC | ITA Book VI.1.1, around lines `11202-11247` | Body, fortune, assets, success, visible circumstances |
| Spirit / Sun / Absent | ASC + Sun - Moon | ASC + Moon - Sun | ASC | ITA Book VI.1.2, around lines `11298-11344` | Soul, mind, faith, intention, hidden matters |
| Eros / Venus / Concord | ASC + Spirit - Fortune | ASC + Fortune - Spirit | ASC | ITA Book VI.1.3, around lines `11345-11372` | Love, concord, desire, pleasure, unions |
| Basis / Stability | ASC + Spirit - Fortune | ASC + Fortune - Spirit | ASC | ITA Book VI.1.4, around lines `11372-11416` | Same formula as Eros/Venus in this presentation; body-form, stability, appearance, durability |
| Necessity / Mercury | ASC + Fortune - Spirit | ASC + Spirit - Fortune | ASC | ITA Book VI.1.5, around lines `11416-11450` | Poverty, worry, business, trade, calculation, writing, conflict |
| Courage / Mars | ASC + Fortune - Mars | ASC + Mars - Fortune | ASC | ITA Book VI.1.6, around lines `11452-11470` | Courage, boldness, harshness, robbery, dispute |
| Victory / Jupiter | ASC + Jupiter - Spirit | ASC + Spirit - Jupiter | ASC | ITA Book VI.1.7, around lines `11478-11503` | Victory, assistance, blessedness, justice, faith |
| Nemesis / Saturn | ASC + Fortune - Saturn | ASC + Saturn - Fortune | ASC | ITA Book VI.1.8, around lines `11505-11539` | Bonds, imprisonment, loss, stolen things, dead things, anxiety |

Note: the source uses different titles across Abbreviation, Great Introduction, and al-Qabisi excerpts. The formula table above gives the practical calculation.

## House And Topic Lots

### Life, Body, Origin

| Lot | Day Formula | Night Formula | Projector | Source Location | Notes |
|---|---|---|---|---|---|
| Life | ASC + Saturn - Jupiter | ASC + Jupiter - Saturn | ASC | ITA VI.2.1, around `11548-11565` | Natural life, body, sustenance |
| Releaser / Hyleg Lot | Conjunctional: ASC + natal Moon - prenatal New Moon; Preventional: ASC + natal Moon - prenatal Full Moon | No day/night reversal stated | ASC | ITA VI.2.2, around `11569-11621` | Source notes this differs from Valens; use as Abu Mashar/Great Introduction formula, not as a universal Hellenistic rule |
| Origins / Condition / Character | Mercury + Mars - Saturn | Mercury + Saturn - Mars | Mercury / beginning of Mercury's sign | ITA VI.2.3, around `11630-11645` | Same calculation as the Oppressive/Weighty Place: day Saturn to Mars, night reverse, with Mercury's degree-in-sign projected from the beginning of Mercury's sign |

### Assets And Family

| Lot | Formula | Reversed? | Projector | Source Location | Notes |
|---|---|---|---|---|---|
| Assets / Resources | ASC + 2nd house cusp - Lord of 2nd | Day and night | ASC | ITA VI.2.4, around `11650-11685`; PN IV page_0444 | Money, resources, livelihood |
| Brothers | ASC + Jupiter - Saturn | Main ITA Hermes/al-Qabisi version is not reversed; PN IV/Sahl footnote and Firmicus tradition reverse by night | ASC | ITA VI.2.5, around `11687-11709`; PN IV page_0445 note | Variant: Mercury to Jupiter; if using the reversed-night tradition, night formula is ASC + Saturn - Jupiter |
| Death of Brothers | ASC + MC - Sun | Reverse by night | ASC | ITA VI.2.6, around `11721-11735` | Cause/danger to siblings |
| Father | ASC + Saturn - Sun | ASC + Sun - Saturn | ASC | ITA VI.2.7, around `11740-11791`; PN IV page_0445 | If Saturn is under the rays, Hermes substitution is day ASC + Jupiter - Sun, night ASC + Sun - Jupiter; another rejected variant uses Mars to Jupiter |
| Death of Father / Parents | ASC + Jupiter - Saturn | ASC + Saturn - Jupiter | ASC | ITA VI.2.8, around `11791-11820` | Danger/death of father/parents |
| Grandfathers | ASC + Saturn - Lord of Sun's sign | ASC + Lord of Sun's sign - Saturn | ASC | ITA VI.2.9, around `11822-11860` | If Sun is in Leo, use 0 Leo to Saturn by day and reverse by night; if Sun is in Saturn's domicile, use Sun to Saturn by day and reverse by night |
| Real Estate / Possessions | ASC + Moon - Saturn | Day and night | ASC | ITA VI.2.10, around `11864-11888` | Land, fields, buildings, possessions |
| Cultivation | ASC + Saturn - Venus | Day and night | ASC | ITA VI.2.11, around `11895-11908` | Agriculture, sowing, planting |
| End of Matters | ASC + Lord of prenatal syzygy sign - Saturn | Day and night | ASC | ITA VI.2.12, around `11910-11940` | Endings, outcomes, suspected death/illness variant |

### Children

| Lot | Formula | Reversed? | Projector | Source Location | Notes |
|---|---|---|---|---|---|
| Children | ASC + Saturn - Jupiter | ASC + Jupiter - Saturn | ASC | ITA VI.2.13, around `11944-11984`; PN IV page_0446 | General condition of children |
| Time / Number / Sex of Children | ASC + Jupiter - Mars | Day and night | ASC | ITA VI.2.14, around `11988-12018` | Jupiter reaching it can confirm child timing if natal promise supports |
| Male Children | ASC + Jupiter - Moon | Day and night in Hermes version | ASC | ITA VI.2.15, around `12028-12060` | Variant: Moon to Saturn, sometimes reversed |
| Female Children | ASC + Venus - Moon | Day and night in Hermes version | ASC | ITA VI.2.16, around `12065-12105` | Some Theophilus variant reverses |
| Child's Sex | ASC + Moon - Lord of Moon's sign | ASC + Lord of Moon's sign - Moon | ASC | ITA VI.2.17, around `12111-12128` | Masculine/feminine sign judgment |

### Pleasure, Illness, Servants

| Lot | Formula | Reversed? | Projector | Source Location | Notes |
|---|---|---|---|---|---|
| Delight | ASC + Saturn - Venus | Day and night | ASC | ITA VI.2.18, around `12130-12134` | Same as cultivation/women's marriage Hermes |
| Chronic Illness | ASC + Mars - Saturn | ASC + Saturn - Mars | ASC | ITA VI.2.19, around `12135-12176`; PN IV page_0446 | Illness, chronic disease, defects |
| Slaves / Servants | ASC + Moon - Mercury | Day and night in Hermes version; al-Qabisi also notes reversal | ASC | ITA VI.2.20, around `12178-12208`; PN IV page_0446 | Servants, attendants, subordinates |
| Slaves Variant | ASC + Fortune - Mercury | ASC + Mercury - Fortune | ASC | ITA VI.2.20, around `12204-12208` | al-Andarzaghar variant; source suggests using both |

### Marriage And Sexual/Union Topics

| Lot | Formula | Reversed? | Projector | Source Location | Notes |
|---|---|---|---|---|---|
| Men's Marriage, Hermes | ASC + Venus - Saturn | Usually treated day/night; note says should reverse at night in Dorotheus | ASC | ITA VI.2.21, around `12211-12240`; PN IV page_0446 | Main men's marriage Lot in PN IV/Sahl note |
| Men's Marriage, Valens | ASC + Venus - Sun | Day and night | ASC | ITA VI.2.22, around `12247-12260` | Betrothal/rumors connection |
| Women's Marriage, Hermes | ASC + Saturn - Venus | Day and night in text; note says should reverse at night | ASC | ITA VI.2.23, around `12262-12297` | Matches cultivation |
| Women's Marriage, Valens | ASC + Mars - Moon | Day and night in text; some Persians reverse by night | ASC | ITA VI.2.24, around `12300-12314` | Valens variant |
| Time of Marriage | ASC + Moon - Sun | Day and night | ASC | ITA VI.2.25, around `12316-12347` | Jupiter reaching/aspecting can time marriage if natal promise supports |
| Delight and Pleasure | ASC + 7th cusp - Venus | Day and night in Abu Mashar/al-Qabisi; Dykes notes a Dorothean night-reversal issue | ASC | ITA VI.2.26, around `12350-12362` | Marriage pleasure/disgrace indicators |

### Death, Danger, Suffering

| Lot | Formula | Reversed? | Projector | Source Location | Notes |
|---|---|---|---|---|---|
| Death | Beginning of Saturn's sign + Saturn's degree-within-sign + 8th cusp - Moon | Day and night | Beginning of Saturn's sign, not ASC | ITA VI.2.27, around `12365-12401` | Special nonstandard formula: measure Moon to 8th cusp, add Saturn's degree within its own sign, project from the beginning of Saturn's sign |
| Killing Planet / Anareta | ASC + Moon - Lord of ASC | ASC + Lord of ASC - Moon | ASC | ITA VI.2.28, around `12404-12434` | Killing/cutting danger technique |
| Suspected Year / Year Death Is Feared | ASC + Lord of prenatal syzygy sign - Saturn | Day and night | ASC | ITA VI.2.29, around `12436-12474`; al-Qabisi V.11c | Death/illness/poverty/impediment/destruction year |
| Oppressive / Weighty Place | Mercury + Mars - Saturn | Mercury + Saturn - Mars | Mercury / beginning of Mercury's sign | ITA VI.2.30, around `12476-12506` | Serious illness, suffering in limb, obstruction |

### Travel And Mind

| Lot | Formula | Reversed? | Projector | Source Location | Notes |
|---|---|---|---|---|---|
| Travel / Foreign Travel | ASC + 9th cusp - Lord of 9th | Day and night | ASC | ITA VI.2.31, around `12511-12523`; PN IV page_0446 | Foreign travel and condition in it |
| Navigation / Water Travel | ASC + 15 Cancer - Saturn | ASC + Saturn - 15 Cancer | ASC | ITA VI.2.32, around `12525-12555` | Sea/water travel; if Saturn itself is at 15 Cancer, the Lot is the Ascendant and Saturn/ASC become significators |
| Intellect / Profound Thought / Reason | ASC + Moon - Saturn | ASC + Saturn - Moon | ASC | ITA VI.2.33, around `12558-12575` | Reason, counsel, obscure sciences |
| Wisdom | Mercury + Jupiter - Saturn | Mercury + Saturn - Jupiter | Mercury | ITA VI.2.34, around `12577-12605` | Wisdom, judgment, patience; source notes projection correction |
| Truth/Falsity of Rumors | ASC + Moon - Mercury | Main Abu Mashar/Abbr. text says day and night; al-Qabisi/Masha'allah reverse by night as ASC + Mercury - Moon | ASC | ITA VI.2.35, around `12609-12645` | Truth or falsehood of rumors/messages |
| Religion / Piety | ASC + Mercury - Moon | ASC + Moon - Mercury | ASC | ITA VI.2.36, around `12647-12658` | Piety, virtue, religion |

### Nobility, Authority, Work

| Lot | Formula | Reversed? | Projector | Source Location | Notes |
|---|---|---|---|---|---|
| Nobility / Exaltation | Day: ASC + 19 Aries - Sun; Night: ASC + 3 Taurus - Moon | Luminary-based | ASC | ITA VI.2.37, around `12661-12710` | Uses the fulfillment of the Sun's exaltation degree (19 Aries) or Moon's exaltation degree (3 Taurus); if the luminary is already there, the Lot is the Ascendant |
| Kingdom and Authority | ASC + Moon - Mars | ASC + Mars - Moon | ASC | ITA VI.2.38, around `12716-12732` | Kingship, leadership, authority |
| Power / Supremacy | ASC + Saturn - Sun | ASC + Sun - Saturn | ASC | ITA VI.2.39, around `12741-12754` | Same as father Lot if Saturn not under rays |
| Authority and What Native Does | ASC + Moon - Saturn | Day and night in ITA; PN IV/Sahl footnote treats it as reversed by night | ASC | ITA VI.2.40, around `12756-12805`; PN IV page_0446 | Work, mastery, office, power, craft; night-reversed variant would be ASC + Saturn - Moon |
| Action / Work, Sahl Variant | ASC + Mars - Mercury | ASC + Mercury - Mars | ASC | PN IV page_0446 note citing Sahl, Nativities Ch. 10.1.1 | Additional work/action Lot mentioned in PN IV footnote, distinct from Abu Mashar's Saturn-to-Moon authority/work Lot |
| Mother | ASC + Moon - Venus | ASC + Venus - Moon | ASC | ITA VI.2.41, around `12807-12821`; PN IV page_0445 | Mother, maternal condition |
| Job and Authority, Valens | ASC + MC - Sun | Day and night | ASC | ITA VI.2.42, around `12829-12834` | Positions, work, kingdom |
| Cause of Kingdom | Jupiter + MC - Sun | Day and night | Jupiter | ITA VI.2.43, around `12836-12840` | Whether there is a cause of authority |

### Hope, Friends, Enemies

| Lot | Formula | Reversed? | Projector | Source Location | Notes |
|---|---|---|---|---|---|
| Hope | ASC + Venus - Saturn | ASC + Saturn - Venus | ASC | ITA VI.2.44, around `12842-12858` | Hope and trust; source criticizes non-reversal confusion |
| Friends | ASC + Mercury - Moon | Day and night in main text; al-Andarzaghar reverses by night | ASC | ITA VI.2.45, around `12860-12888` | Friends, comrades |
| Making Friends and Enemies | ASC + Spirit - Fortune | ASC + Fortune - Spirit | ASC | ITA VI.2.46, around `12890-12900` | Same formula as Eros/Venus Lot |
| Enemies, certain ancients | ASC + Mars - Saturn | Day and night | ASC | ITA VI.2.47, around `12910-12922` | Enemies |
| Enemies, Hermes | ASC + 12th cusp - Lord of 12th | Day and night | ASC | ITA VI.2.48, around `12924-12945`; PN IV page_0447 | Enemies and hostile conditions |
| Enemies, Sahl Variant 1 | ASC + Fortune - Mercury | ASC + Mercury - Fortune | ASC | PN IV page_0447 note citing Sahl, Nativities Ch. 12.1 | Variant enemy Lot mentioned in PN IV footnote |
| Enemies, Sahl Variant 2 | ASC + Moon - Mercury | ASC + Mercury - Moon | ASC | PN IV page_0447 note citing Sahl, Nativities Ch. 12.1 | Variant enemy Lot mentioned in PN IV footnote |

## Miscellaneous Lots From al-Qabisi

| Lot | Formula | Reversed? | Projector | Source Location | Notes |
|---|---|---|---|---|---|
| Knowledge | ASC + Jupiter - Moon | ASC + Moon - Jupiter | ASC | ITA VI.3.1, around `12955-12958` | Knowledge |
| War / Fighting / Heroism | ASC + Moon - Saturn | Day and night in al-Qabisi; Gr. Intr. reverses by night | ASC | ITA VI.3.2, around `12960-12978` | War, fighting, bravery |
| Peace among Soldiers / Armies | ASC + Mercury - Moon | Day and night | ASC | ITA VI.3.3, around `12985-12990` | Concord of armies |
| Revolution of the Year | Sun + Venus - Moon | Sun + Moon - Venus | Sun | ITA VI.3.4, around `12992-12998` | Revolution-of-year Lot |

## Mundane Lots

These are for mundane/world astrology, kingdoms, dynasties, and conjunction/revolution charts, not ordinary natal work unless specifically adapted.

| Lot | Formula | Projector | Source Location | Notes |
|---|---|---|---|---|
| Kingdom and Command #1 | Project from Ascendant of the relevant Saturn-Jupiter conjunction: Mars to Moon by day, Moon to Mars by night | Ascendant of conjunction | ITA VI.4.1, around `13005-13015` | Same planets as natal kingdom/authority Lot; BRD note says it is reversed at night |
| Kingdom and Command #2 | From Ascendant of conjunction to conjunction degree, projected from revolution Ascendant | Revolution Ascendant | ITA VI.4.2, around `13016-13024` | Mundane revolution technique |
| Kingdom and Command #3 | From Sun to MC of revolution, projected from Jupiter | Jupiter | ITA VI.4.3, around `13030-13036` | Mundane authority |
| Duration of Kingdom #1 | Day: Moon + 15 Leo - Sun; Night: Sun + 15 Cancer - Moon | Moon/Sun | ITA VI.4.4, around `13038-13050` | Accession/king activity; source notes this is really two Lots |
| Duration of Kingdom #2 | Day: Revolution ASC + Saturn - Jupiter; Night: Revolution ASC + Jupiter - Saturn, with special exceptions | Revolution Ascendant | ITA VI.4.5, around `13052-13076` | Dynasty/king lifespan or duration; source gives exceptions for cadency, opposition, and Jupiter in exaltation |
| Greatest Lot #1 | Complex: profected degree from triplicity-shift conjunction; measure from eastern Saturn/Jupiter to saved degree; project from revolution Ascendant | Revolution Ascendant | ITA VI.4.6, around `13085-13125` | Royal/dynastic timing |
| Greatest Lot #2 | Complex: profected degree from smaller Saturn-Jupiter conjunction; measure from western Saturn/Jupiter to saved degree; project from revolution Ascendant | Revolution Ascendant | ITA VI.4.7, around `13128-13150` | Royal/dynastic timing |

## Commodity Lots

Source: ITA VI.5, around `13170-13270`. These are for mundane/revolution charts and market prices. The text says these are projected from the Ascendant of the revolution.

| Commodity Lot | Formula |
|---|---|
| Food / Wheat | ASC + Mars - Sun |
| Water | ASC + Venus - Moon |
| Barley | ASC + Jupiter - Moon |
| Chickpeas | ASC + Sun - Venus |
| Lentils | ASC + Saturn - Mars |
| Egyptian beans | ASC + Mars - Saturn |
| Indian peas | ASC + Mars - Saturn |
| Dates | ASC + Venus - Sun |
| Honey | ASC + Sun - Moon |
| Rice | ASC + Saturn - Jupiter |
| Olives | ASC + Moon - Mercury |
| Grapes | ASC + Venus - Saturn |
| Cotton | ASC + Venus - Mercury |
| Sesame | ASC + Jupiter - Saturn, or ASC + Venus - Saturn |
| Watermelons | ASC + Saturn - Mercury |
| Acidic foods | ASC + Mars - Saturn |
| Sweet foods | ASC + Venus - Sun |
| Pungent/spicy foods | ASC + Saturn - Mars |
| Bitter foods | ASC + Saturn - Mercury |
| Purgative/sweet medicines | ASC + Moon - Sun |
| Purgative/acidic medicines | ASC + Jupiter - Saturn |
| Purgative/salty medicines | ASC + Moon - Mars |
| Poisons | ASC + Saturn - Node |

## PN IV Topic-Lot Use In Annual Revolutions

Source: PN IV pages `page_0444.txt` to `page_0447.txt`.

PN IV says to turn/direct the relevant rooted indicator for each topic:

| Topic | Rooted indicators to turn/direct in revolutions |
|---|---|
| Body | Ascendant and Moon |
| Assets | Lot of Fortune, Lot of Assets, 2nd house, triplicity lords of sect light |
| Siblings | 3rd house, Lot of Siblings, triplicity lords of Mars sign |
| Father | Sun or Saturn by sect, Lot of Father, 4th house |
| Mother | Venus or Moon by sect, Lot of Mother, 10th house |
| Children | 5th house, Lot of Children |
| Slaves/Servants | 6th house, Lot of Slaves |
| Illnesses | 6th house, Lot of Illnesses |
| Women/Marriage | 7th house, Lot of Marriage |
| Death/Catastrophes | 8th house, Lot of Death |
| Travel | 9th house, Lot of Travel |
| Authority/Rank | 10th house, Lot of Authority/Rank |
| Hope/Friends | 11th house, Lot of Hope/Friends |
| Enemies/Riding Animals | 12th house, Lot of Enemies |

PN IV also says that if another planet, house, or Lot is an indicator of the topic being investigated, that indicator is also turned and directed individually.

## Practical Implementation Checklist For LLMs

1. Determine sect: day or night.
2. Use tropical longitudes unless a specific sidereal framework is requested.
3. Convert every point to absolute `0-360` longitude.
4. For each Lot, identify:
   - `From`
   - `To`
   - `Projector`
   - whether to reverse by night
   - whether a special condition applies
5. Compute `Lot = Projector + To - From`.
6. Normalize into `0-360`.
7. Convert to sign-degree-minute-second.
8. Assign whole-sign house from the natal Ascendant unless a quadrant reference is specifically requested.
9. Identify domicile lord, exaltation lord if needed, triplicity lord, bound lord, and decan lord if the analysis requires dignity.
10. In revolutions, check:
    - natal Lot condition
    - Lot lord natal condition
    - profected Lot
    - SR Lot when relevant
    - SR planet contacts to natal Lot
    - monthly Lot of Fortune and monthly indicators
    - whether the Lord of Year rules, occupies, or aspects the Lot

## Caution Notes

- Some formulas differ between Abu Ma'shar, al-Qabisi, Hermes, Valens, Dorotheus, Sahl, and later commentators.
- Some Lots are explicitly not reversed by night, some are reversed, and some sources disagree.
- When the text says "probably not reversed", mark that as a methodological assumption.
- For death/danger Lots, do not blend formulas without labeling the source. There are multiple variants.
- Do not use a Lot formula if the source location is unclear; mark it as uncertain instead.
- PN IV is a revolution text: for annual/monthly prediction, use Lots with profections, SR contacts, monthly revolution indicators, and transits, not as isolated natal symbols.
