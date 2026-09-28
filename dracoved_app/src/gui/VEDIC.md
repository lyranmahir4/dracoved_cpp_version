# Vedic D1 workspace

The Vedic tab is an independent zodiac presentation of the active natal chart. It
shows Ascendant, the seven classical planets, and Rahu/Ketu in a read-only
table with sign, degree in sign, absolute longitude, whole-sign house, sign
lord, signed daily motion, nakshatra, pada, progress within the star, star
lord, D9 sign, existing app dignity, and distance from the Sun.
D9 in the placements table is a sign column; an asterisk marks vargottama. Dignity uses the
existing app convention (Ruler, Exalt, Fall, Detriment); nodes and Ascendant
are unclassified. Solar distance is the shortest ecliptic arc for Mercury
through Saturn; no combustion thresholds are applied.

The upper table and lower calculators use an adjustable vertical splitter.
Moorthi and Tara are visible side by side in a horizontal splitter. Sizes
persist in `vedic/mainSplitter` and `vedic/transitSplitter`. Controls wrap as
panes narrow. Numeric columns are compact and right aligned; position
tooltips retain precise values. The shared facts strip shows the actual
ayanamsa at birth, local/UTC context, node model, Lagna and natal star.

The tab copies the active natal birth input and defaults to a sidereal calculation;
it does not mutate the Natal workspace or the global zodiac toolbar. Its
Zodiac / Ayanamsa selector includes Tropical for research, using unshifted tropical
longitudes throughout D1, dashas, Moorthi, Tara, Ashtakavarga/Kaksha and every graph
score profile. Nakshatras then start at 0° tropical Aries; the tropical birth Moon
determines the dasha balance. No ayanamsa is subtracted. Switching zodiac clears
cached results. Context labels and copies identify the zodiac used.
The zodiac persists at `vedic/zodiacSystem`; the last sidereal ayanamsa is retained
independently at `vedic/ayanamsa`. Mean, true, and Both
lunar-node policies follow the active chart policy. Both mode labels the two
node models explicitly.

Rows are initially Ascendant-first. Table sorting uses canonical zodiac order
for Sign and canonical 27-nakshatra order for Nakshatra, numeric ordering for
Degree in sign and Pada, and alphabetical ordering for Body and Nakshatra lord.
A refresh preserves the selected body and active sort. Natural order restores
Ascendant-first order without clearing calculator results.
Copy exports the displayed row order together with birth, UTC, ayanamsa, and
node-policy context. Calculator copies also include these birth facts.

## Graph chart reference: D1 / D9 only / D1+D9

The Combined score graph's Chart selector starts at D1, preserving the existing
profile and component settings. D9 compares ordinary transit signs with natal D9
signs (`tajaka::divisionSign(9, natal longitude)`); it does not transform transit
positions or recalculate Moorthi/Tara/BAV/Kaksha on D9. Natal house lords are D1
sign lords. No new dasha-target or trine rules are introduced.

Source: Gochar Phaladeepika OCR PDF pp.227–229 (printed pp.234–236).
Saturn crossing natal Sun/Jupiter D9 signs is adverse. Lagna/third-lord passages
also name Rasi: the implemented interpretation treats the listed Navamsa as a
separate natal target. A simultaneous natal Rasi match is not required (that
restriction would incorrectly limit these rules to vargottama targets). Jupiter crossing the natal
Lagna lord's D9 sign is favorable subject to “except Dusthanas”. Since the passage
does not specify that reference, matching contacts are unscored by default.
Scoring rules → Natal D9 permits an explicitly labeled Moon or Lagna research
convention: 6/8/12 exclude the benefit without inventing a negative result.

Contact magnitude defaults to 50 points, editable; simultaneous descriptions do
not stack. No contact = zero research baseline. Unsupported planets / unresolved
conditions are unavailable, not zeros. D1 and D9 independently average scored
planets with existing planet weights. D1+D9 blends those layer means (default
50% D9); unavailable layers are omitted and remaining weight normalized. These
numbers and blend are research encodings, not book formulas. Individual planet
lines blend that planet's available scores; the overall line blends layer means.
The detail table, hover and export identify raw layer scores and source conditions.
Unresolved supported rules mark the sample D9 INCOMPLETE, with amber top-edge
ticks in the graph (inspectable even without a numeric score). Any remaining D9
mean and combined total are labeled partial assessments. Unsupported planets are
Not covered, never neutral. Combined details/export show effective layer shares,
weighted contributions and the total, including any unavailable-layer fallback.
Event sampling includes all selected planets' sign entries, including retrograde
re-entries; daily sampling stays a snapshot. Settings persist under `vedic/d9/`.

## Moorthi Nirnaya

Choose From and Through dates, a planet (or All planets), then Find entries.
Dates include both selected days, in the birth chart's timezone shown beside
the controls. Searches have no duration cap and can be stopped; stopped or
failed searches are labeled partial in copied output. A metal filter applies
to the completed results without another calculation. Copy results preserves
the visible sort order, filter, range, timezone, natal Moon and ayanamsa.
Calendar controls accept years 1–9999; actual position availability is checked
by Swiss Ephemeris during calculation. A missing position stops the search with
the affected planet/date and an explicit error, retaining partial results.
Unloaded ephemeris and reversed dates receive inline feedback. Result status
distinguishes an empty successful search from hidden rows and incomplete work.

Entries are sign crossings under the selected Vedic zodiac. Each direct or
retrograde re-entry is listed separately and evaluated at its own entry moment.
Mean/true Rahu and Ketu follow the chart's node policy. Moon is excluded from
Moorthi calculations and the table selector. Its sign crossings are retained
internally as graph sampling anchors, with no metal or Moorthi count. These
anchors use the destination sign at the numerical root. Other planets use the
Moon evaluated at their entry root.

Source: *Gochar Phaladeepika*, chapter 25, OCR PDF page 212. Counting signs
inclusively from the natal Moon: 1/6/11 = Swarna (gold), 2/5/9 = Rajata (silver),
3/7/10 = Tamra (copper), 4/8/12 = Loha (iron). The book's benefic/malefic
interpretive weighting and combined scores are not applied; the table reports
the metal classification and the positions used to obtain it.

The search samples six-hour intervals, splits reversals at stations, then
bisects crossings to a 0.01-second time bracket. Displayed timestamps are local
seconds, with UTC milliseconds in the timestamp tooltip. Small timer batches
keep the UI responsive and restore the shared ephemeris ayanamsa before yielding.
Changing natal data, Vedic ayanamsa or node policy invalidates saved results.

Run the standalone calculation/UI checks:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\dracoved_app\src\gui\moorthi_check.ps1
```

These cover the 12 book mappings, annual Sun crossings, Mercury retrograde
entries, Moon and true-node crossings, zodiac wraparound, boundary bracketing,
entry-Moon classification, search/filter/copy, and stale-result cancellation.

## Focused validation

From the repository root, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\dracoved_app\src\gui\vedic_panel_integration_check.ps1
```

The check uses `QT_QPA_PLATFORM=offscreen`, a temporary QSettings scope under
the check executable's directory, and the root `swedll64.dll`/`ephe` assets.
It validates the real `VedicPanel` through `MainWindow`, node modes, reference
placements, sorting, copy order, ayanamsa persistence, input refresh, tab and
dock routing, save/reopen, and reset-layout behavior. It writes the screenshot
`dracoved_app/build/vedic_panel_integration_checks/vedic_d1.png` for visual
review. It never invokes `do_build.bat` or changes the production executable.

## Transit Tara

The Transit Tara pane beside Moorthi Nirnaya calculates one selected moment.
Date/time are interpreted in the birth chart's timezone; Now fills that zone's
current date/time. Calculate uses the Vedic ayanamsa and the active mean/true
node policy. Changing the moment or natal context clears previous results.
Skipped or ambiguous daylight-saving times are rejected with inline feedback.

Columns show planet, sign in the selected zodiac, nakshatra, pada, inclusive count from the
natal Moon's nakshatra (1–27), and Tara number/name (1–9). The book's sequence
is Janma, Sampat, Vipat, Kshema, Pratyak, Daivanukula, Naidhana, Mitra, Parama
Maitra; it repeats three times. Source: Gochar Phaladeepika, OCR PDF page 65
(cycle definition) and page 347 (transit table). Categories are reported
without assigning interpretive scores. Sorting uses numeric counts and
zodiac/nakshatra order. Copy includes only filtered rows in displayed order,
with natal context, settings, and local/UTC calculation timestamps.

Run the focused calculation/UI check from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\dracoved_app\src\gui\tara_check.ps1
```

This covers all 729 birth/transit star pairs, invalid indices, both node pairs,
birth-time Moon self-reference, timezone conversion, filter/sort/copy, shared
ayanamsa restoration, stale results, and daylight-saving gaps/overlaps.

## Vimshottari Dashas

The Vedic workspace has four inner views: Placements & transits, Dashas,
Vedic transit graph, and Ashtakavarga. They share the independent Vedic ayanamsa and birth facts. The slim
active-lord strip reports the **inspection instant**, not necessarily today.
Its Active lords only checkbox filters existing Moorthi/Tara results when
**All planets** is selected, without rerunning their searches. Selecting a
specific planet takes priority over that checkbox, including when the dasha
inspection time lies outside the Moorthi search range. The metal filter still
applies. The dasha filter uses that one inspection instant, not the active
lords at every historical entry. Status text reports shown versus found counts
and explains hidden rows; copied reports describe the effective filters.
Repeated roles share one planet. Active planet names
are bold, with the inspection chain in their tooltip. Copies of filtered results
include that chain and the dasha year convention.

`core/vimshottari` is a pure UTC-millisecond calculator with five levels:
Mahadasha, Antardasha, Pratyantar, Sookshma and Prana. The order is Ketu (7),
Venus (20), Sun (6), Moon (10), Mars (7), Rahu (18), Jupiter (16), Saturn (19),
Mercury (17), totaling 120 years. The natal Moon's nakshatra in the selected zodiac determines
the first lord; the elapsed fraction of its 13°20′ span determines how much of
that Mahadasha has already elapsed at birth. All lower levels are subdivisions
of the **full parent**, including the portion before birth. Each child starts
with its parent's lord and takes its lord's years / 120 of the parent duration.
Cumulative boundaries are rounded to milliseconds, with the last child ending
exactly at its parent's end. Starts are inclusive; ends belong to the next period.

The visible year convention defaults to fixed 365.25-day years; fixed 360-day
years are an explicit alternative, persisted at `vedic/dashaYearDays`. Years
are durations, not additions of calendar years. Leap days and DST therefore
cannot accumulate drift in the underlying schedule. Display times use the birth
chart's timezone; timestamp tooltips and copied reports retain milliseconds and
UTC offsets. Millisecond storage is numerical resolution, not a claim about
birth-time or ephemeris accuracy. Fine sublevels inherit those uncertainties.

Source for the sequence, proportional periods and Moon balance:
Sanjay Rath, [Vimsottari Dasa](https://srath.com/jyoti%E1%B9%A3a/dasa/vimsottari-dasa/).
The supplied *Gochar Phaladeepika* also uses a 365.25-day year in its example
on OCR PDF page 338. Its separate Varsha Dasa passage is not used as a
Vimshottari formula.

The schedule displays the full 120-year cycle containing the inspection time.
Expanding rows generates nine children on demand, down to Prana. Show active
path opens the five active levels. Double-click or Inspect selected start moves
the inspection time to that period's exact beginning. Previous change visits the
nearest earlier boundary at the chosen level: the current period's start when
inside it, or the preceding period's start when already on a boundary. Next change visits the current
period's exclusive end. Periods may start before birth, explicitly labeled in
the age column. Inspection controls accept calendar years 1–9999. Transit
availability comes from the ephemeris; mathematical periods remain available
even if a transit position cannot be calculated. Period boundaries may extend
outside the calendar input range.

The active table always shows all five levels, their start/end, remaining time
and elapsed fraction. Lord colors identify planets and repeated roles; they do
not express favorable/unfavorable scores. The transit snapshot deduplicates
lords, preserving all role abbreviations. Both-node mode creates separate Mean
and True transit rows while retaining one Rahu and one Ketu in the dasha sequence.

Each snapshot uses the selected instant for sign, house from natal Moon,
nakshatra and Tara. **Current Moorthi** uses the latest actual sign entry in the selected zodiac
at or before that instant, and the Moon evaluated at that entry. The existing
six-hour crossing finder is scanned backward in short timer batches. Retrograde
re-entries supersede earlier direct entries. There is no fixed ten-year lookback;
searching ends at the latest entry, the calendar boundary, or an ephemeris error.
Unresolved/stopped/failed results
remain unavailable with the reason in a tooltip and copied output. No metal is
inferred from the current Moon. An active Moon has Moorthi marked Not applicable;
its Tara and latest sign-entry time remain available. Every batch restores the shared Swiss ayanamsa.
An invalidated or replaced context cancels the old lookup immediately.

Selecting an active level highlights its planet in the snapshot. Selecting a
snapshot row exposes its entry time, direction, entry Moon and count. Open Tara
switches to the existing pane, calculates the same exact instant, and selects
that planet. Conversely, calculating Tara updates the shared inspection chain.
Manual skipped/repeated local times are rejected; UTC-based navigation retains
the exact occurrence of a repeated hour. Hidden Dashas views defer transit
lookups until opened. Splitter proportions persist at `vedic/dashaSplitter`.

Copy active exports all five periods and the current transit snapshot, including
unresolved lookup reasons and entry provenance. Copy schedule exports expanded
rows in chronological tree order. Both include birth facts, convention and exact
local/UTC inspection times. These reports never silently substitute today.

Run the focused dasha checks (production executables remain untouched):

```powershell
.\dracoved_app\src\gui\dasha_check.ps1 -Incremental
```

They verify every parent/child partition through all five levels for both year
conventions, an independently hand-worked half-Ashwini birth balance, all 27
nakshatra boundaries, cycle wraparound, a real Mercury retrograde re-entry,
node-model separation, exact navigation, copy parity, cancellation, timezone
ambiguity and ephemeris-state restoration. Wide, compact and dark-palette
previews are written to `build/dasha_checks`. The full Vedic integration check
also covers shared time, active filters, Open Tara, and existing workspace flows.
The Moorthi checks additionally reproduce the explicit Rahu 2026–2029 query
with unrelated 2034 active lords, Jupiter 2024–2030, a complete 100-year Sun
search, inclusive dates, empty-filter recovery, long-search cancellation, and
unavailable ephemeris. Tara tests cover explicit planet priority and both node
models; dasha tests check Previous change from the middle of a period.

## Vedic transit graph

The Vedic transit graph subtab contains From/Through dates, a dropdown with planet
checkboxes (including All planets), Calculate and Stop. Time is horizontal;
Gold/Swarna, Silver/Rajata, Copper/Tamra and Iron/Loha are the four vertical
categories. Each selected planet has a distinct colored trace and legend entry.
These are metal categories, not a combined benefic/malefic score. Moon has no
metal trace; selecting Moon alone explains that Tara/BAV are in Combined score.

In both graph modes, Ctrl + mouse wheel zooms the time axis around the cursor.
When zoomed, hold the left mouse button and drag horizontally to pan within the
calculated range. Small clicks still inspect points; dragging preserves selection.
Right-click anywhere in the graph and choose Fit graph / Reset zoom to restore
the full range. The vertical scale stays fixed. Zoom changes only the visible
time window: calculations, copied data and the selected point are preserved.
New calculations reset the view; the zoomed window remains within the input range.

Combined score marks every tied full-range high (up triangle) and low (down
triangle) for the selected line, with values/counts in the graph header and
identification on hover. Zoom does not redefine the extremes. A flat line uses
diamonds for its shared high/low; unavailable scores are omitted.

At the user's request, entry points are joined by straight segments rather than
steps. Intermediate line heights do not denote additional calculated metals.
Hovering a dot reports its entry timestamp, planet, metal, sign, motion, Moon
at entry and inclusive count. Overlapping dots report all matching entries.
A hollow point at the start shows the last entry before the selected range;
its tooltip retains the actual earlier timestamp. A planet without new entries
still shows that metal throughout the range. The final segment stays flat after
the last event, through the inclusive Through day.

The graph reuses findMoorthiEntries and its retrograde/node handling. It searches
backward for the initial metal, then forward in six-hour chunks, yielding to Qt
every 12 ms and restoring the shared Swiss ayanamsa before each yield. There is
no duration cap. Calendar/ephemeris failures are explicit; stopped or failed
calculations retain only the portions already scanned. Chart, ayanamsa, node,
date and planet changes cancel pending calculations and clear stale results.
The graph's planet selection is independent of the inspection-time Active lords
only filter; that filter and the active chain strip are hidden in this subtab.
Both node models, when enabled, get separate traces and stroke patterns. Ketu
uses a broken stroke so coincident Rahu/Ketu lines remain visible.

Run the focused check with `./dracoved_app/src/gui/moorthi_graph_check.ps1`.
It verifies agreement with the existing table, the initial metal, a single day
with no new entry, 2016–2032 multi-planet results, all/individual selections,
both node models, the reported Rahu 2026–2029 case, cancellation, invalid inputs
and ephemeris failures. It saves wide, compact and dark screenshots under
`build/moorthi_graph_checks` without modifying production executables. The Vedic
integration check also exercises the real subtab and preservation of other views.

### Combined score mode

Shift-drag inside the graph selects a time interval; drag either boundary to
resize it. A compact in-plot overlay shows the selected dates, positive sum,
negative sum, net and scored/total sample counts for the displayed line. Bounds
snap to calculated samples and include both endpoints. These are sample sums,
not time-weighted integrals; unavailable scores are excluded and incomplete D9
assessments are counted explicitly. Zoom/pan and rescoring preserve the selection;
new calculations clear it. Right-click → Clear selection removes it. No scoring
rules or calculation inputs change when selecting a range.

The View selector retains the metal graph and adds Combined score.
The existing ingress search supplies each planet's actual latest Moorthi,
including the entry before the range and retrograde re-entries. After a complete
ingress search, Daily snapshot samples once per local date at the chosen time
(default noon). Planet sign entries samples the range start and subsequent
entries of any selected planet, or one selected anchor such as the Moon.
This is a snapshot benchmark, not a daily average or an event-exhaustive score.
Tara changes between event samples may therefore be absent from that view.

The book's Moorthi fractions on OCR PDF page 212 have separate benefic and
malefic rankings. Defaults use those fractions transformed by `2*f - 1`:
Gold/Silver/Copper/Iron = `1, .5, 0, -.5` for benefics and `-.5, 1, .5, 0`
for malefics. Zero at 50% is a research normalization. Sun, Mars, Saturn,
Rahu and Ketu initially use the malefic rule; Mercury, Jupiter and Venus use
the benefic rule. These choices and values are editable; Moon's Moorthi choice
is disabled as Not applicable. The natal classification reference does not
automatically change the selected Moorthi rules.

Tara defaults follow the categories on OCR PDF page 205: Janma and Parama
Maitra are `0` (medium/moderate); Sampat is `1`; Kshema, Daivanukula and
Mitra are `.5`; Vipat, Pratyak and Naidhana are `-1`. These numeric encodings
are research choices. Ashtakavarga uses each planet's own unreduced natal
BAV in its transit sign, normalized as `(BAV - 4) / 4`. SAV is shown as context
in the detail table/export and is not added again.

Kaksha follows *Gochar Phaladeepika* PDF pp. 234–235: every sign is divided
into eight 3°45' sections ruled Saturn, Jupiter, Mars, Sun, Venus, Mercury,
Moon, Lagna. For each transiting planet, its own natal Prastara contribution
from that section's ruler gives 0 or 1 bindu. The Ashtakavarga transit table
shows the current Kaksha and bindu. The combined graph shows both in its point
detail/tooltip and copy. Optional Kaksha weight defaults to zero, preserving
earlier scores. When enabled, 1 maps to +1 and 0 to −1 before weighting; that
numerical mapping is a custom research choice. Nodes have no Kaksha component.
Planet Kaksha entries sampling adds section changes (including retrograde
re-entry) to the sign and dasha events; Daily remains one local-time snapshot.

Each planetary score is 100 times the weighted mean of available components;
the overall line defaults to the equal mean of scored planets. Mean/True variants of
one node share one planet's weight. Nodes have no BAV; that component is
omitted from their denominator. With BAV alone enabled they are unavailable,
not zero, and are excluded from the overall with the scored count shown.
Moon has no Moorthi component: its weight is omitted from the denominator,
while Tara and BAV remain available. With Moorthi alone enabled, Moon is
unavailable and excluded, never a neutral zero. Moon entry anchors, active-dasha
roles, and optional planet/dasha weighting still work. Details and exports
mark its Moorthi and related provenance as not applicable. For weights 2.5/1/.5,
Moon's score is `100 * (Tara + .5 * normalized BAV) / 1.5`.
The combined formula is a custom benchmark, not a formula asserted by the book.

Green/red segments indicate scores above/below zero. The Line selector switches
between the overall and individual planets. Clicking a point shows all raw
categories and weighted contributions and updates the shared inspection time.
Scoring rules persist under `vedic/benchmark/` and rescore cached readings;
sampling changes reuse the ingress cache. Copy includes all sample rows,
timestamps, natal context, settings, entry provenance and partial status.
Invalid local DST times create gaps. Stopping sampling retains only completed
samples; an incomplete ingress search must finish before scoring can begin.

Optional active-dasha mode replaces manual planet selection with the distinct lords
of enabled levels at each sample (MD/AD/PD initially; SD/PrD optional). It uses the
Dashas tab's year length, counts repeated lords once, and adds enabled dasha
boundaries to sign-entry sampling. Daily mode remains one snapshot per day.
Roles and the active path appear in point details and copies. Hover adds score/date
crosshairs and comparison with the pinned point; January markers distinguish years.

Scoring rules optionally weight active lords by MD/AD/PD/SD/PrD (custom defaults
3/2/1/0.5/0.25). The highest enabled role takes precedence, even if its numerical
weight is lower; duplicate lords count once and zero weights exclude a planet.
Weights rescore cached samples and are included in details/copies. Individual
planet net scores retain their original component formula.
Optional manual planet weights (all 1 by default) multiply enabled dasha weights
and apply in either selection mode. Zero excludes a planet from scoring; node
variants still share one logical planet weight. Settings, effective weights, and
the weighted-mean formula are included in copied results; edits rescore the cache.
The informational Natal classifications tab uses Rao §13.2 / Table 30 for D1
functional roles, natal lunar phase, and an explicitly labeled same-sign majority
convention for Mercury (ties mixed; primary node model only). Nodes receive no
functional label from that table. These classifications do not alter Moorthi rules.

Run `./dracoved_app/src/gui/vedic_benchmark_check.ps1` for the focused formula,
daily/event sampling, ingress provenance, node weighting, graph selection,
copy, cached-rescore and cancellation check. The preview is saved under
`build/vedic_benchmark_checks`; production executables are not replaced.

## Ashtakavarga

The Ashtakavarga subtab calculates unreduced natal D1 BAV (0–8 benefic points)
for Sun through Saturn, and SAV as the sum of these seven rows (337 overall).
Each planetary BAV has eight contributors: the seven planets and Lagna.
Rahu/Ketu and a separate Lagna BAV are not included. No Trikona/Ekadhipatya
reductions or combined Moorthi/Tara scores are applied.

The rule table follows Parashara as presented by P.V.R. Narasimha Rao,
[Vedic Astrology: An Integrated Approach](https://vedicastrologer.org/articles/vedic_astro_textbook.pdf),
chapter 12, tables 19–25. This specifically uses Moon-from-Moon house 9,
Moon-from-Jupiter house 2, and Venus-from-Mars house 4; the alternative
Varahamihira rules are not mixed in. “Points” consistently means benefic points.

Columns switch between Aries-first and Lagna-first order, with whole-sign
house numbers. Selecting a planetary row changes the Prastara contribution
table. BAV text is green above 4 and red below 4; SAV has no color classification.
Transit scores look up each classical planet's own natal BAV and natal SAV in
its transit sign in the selected zodiac at the selected moment. The dasha inspection time is
shared and active lords are bold, without filtering out other planets. Date
edits clear transit results; chart/ayanamsa changes refresh natal scores and
clear old transits. Copy all preserves displayed column order and timestamps.

Run `./dracoved_app/src/gui/ashtakavarga_check.ps1` for the focused check.
It compares every BAV/SAV value with the book's Chart 6 and answers to
Exercises 19/20, plus contribution totals, wraparound, transit lookup,
selection/copy, stale state, DST rejection and ephemeris-mode restoration.
It compiles the Vedic integration changes and saves a UI preview under
`build/ashtakavarga_checks`, without replacing production executables.
