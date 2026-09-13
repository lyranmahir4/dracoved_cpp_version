# Vedic D1 workspace

The Vedic tab is a focused sidereal presentation of the active natal chart. It
shows Ascendant, the seven classical planets, and Rahu/Ketu in a read-only
table with Body, Sign, Degree in sign, Nakshatra, Pada, and Nakshatra lord.
The workspace shares table width across the placement columns and keeps Pada
compact. The natal table sits above the Moorthi sign-entry calculator, with
scrolling when vertical space is limited.

The tab always copies the active natal birth input into a sidereal calculation;
it does not mutate the Natal workspace or the global zodiac toolbar. Its
ayanamsa selector is persisted independently at `vedic/ayanamsa`, so changing
the Vedic view does not change the natal chart's ayanamsa. Mean, true, and Both
lunar-node policies follow the active chart policy. Both mode labels the two
node models explicitly.

Rows are initially Ascendant-first. Table sorting uses canonical zodiac order
for Sign and canonical 27-nakshatra order for Nakshatra, numeric ordering for
Degree in sign and Pada, and alphabetical ordering for Body and Nakshatra lord.
A refresh preserves the selected body and active sort.
Copy exports the displayed row order together with birth, UTC, ayanamsa, and
node-policy context.

## Moorthi Nirnaya

Choose From and Through dates, a planet (or All planets), then Find entries.
Dates include both selected days, in the birth chart's timezone shown beside
the controls. Searches support up to ten years and can be stopped; stopped or
failed searches are labeled partial in copied output. A metal filter applies
to the completed results without another calculation. Copy results preserves
the visible sort order, filter, range, timezone, natal Moon and ayanamsa.

Entries are sidereal sign crossings under the Vedic ayanamsa. Each direct or
retrograde re-entry is listed separately and evaluated at its own entry moment.
Mean/true Rahu and Ketu follow the chart's node policy. When the Moon itself
enters a sign, its destination sign is used (rather than floating-point noise
on the numerical root). Other planets use the Moon evaluated at the root.

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

The Transit Tara subtab beside Moorthi Nirnaya calculates one selected moment.
Date/time are interpreted in the birth chart's timezone; Now fills that zone's
current date/time. Calculate uses the Vedic ayanamsa and the active mean/true
node policy. Changing the moment or natal context clears previous results.
Skipped or ambiguous daylight-saving times are rejected with inline feedback.

Columns show planet, sidereal sign, nakshatra, pada, inclusive count from the
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
