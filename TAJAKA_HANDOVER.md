# Tajaka Feature Handover — DracoVed

Status date: 2026-08-16
Scope: everything implemented for the Tajaka (Varshaphala) feature set. No future work is described here.

---

## 1. Reference material (read these first)

Two PDFs by **P.V.R. Narasimha Rao** define the methodology. Both were read in full during development:

1. `C:\Users\Mahir\Downloads\pp_tajaka.pdf` — "Re-defining Tajaka Varshaphal Charts (Annual Solar Return Charts)" (version 1, 2014-06-15).
   - Core re-definition: the varshaphal return moment is when the Sun reaches its **natal TROPICAL longitude**, always — even though the chart itself is judged **sidereally**. Time is tropical, space is sidereal.
   - Charts are judged like natal charts (rasi + divisional charts). He uses his own Pushya-paksha ayanamsa.
2. `C:\Users\Mahir\Downloads\Tajaka Analysis Chapter.pdf` — "Part 4: Tajaka Analysis" from *Vedic Astrology: An Integrated Approach* (pp. 366–395).
   - The classical toolbox: chart casting rules (birthplace coordinates always), muntha, Tajaka aspects with deeptamsa orbs, Harsha Bala, Pancha Vargeeya Bala, Dwadasha Vargeeya Bala, Lord of the Year (Varsheswara) with the Triraasi table, the 36 sahams (Table 74, with the hadda lord table as Table 72), and the 16 Tajaka yogas (Ch. 29).

A third paper by the same author was also consulted for the ayanamsa:
- `http://www.vedicastrologer.org/articles/pp_ayanamsa.pdf` — "Introducing Pushya-paksha Ayanamsa" (2013-12-31). Definition: fix the sidereal longitude of Delta Cancri (Pushya yogatara) at 16° Cancer 0'; ayanamsa(t) = tropical longitude of Delta Cancri at t − 106°, computed exactly (no linear approximation). Reference values included (e.g., 2000-01-01 6am IST: 22°43′19.12″; 2014-01-01: 22°55′3.87″).

---

## 2. Architecture map — where the Tajaka code lives

| File | Role |
|---|---|
| `dracoved_app/src/core/tajaka.h/.cpp` | NEW module, namespace `dracoved::tajaka`. Muntha, Tajaka aspect engine (whole-sign kinds + deeptamsa + ithasala/eesarpha/poorna), strength calculators (Harsha / Pancha Vargeeya / Dwadasha Vargeeya), Lord of the Year, embedded tables (hadda lords, Triraasi day/night lords, deep exaltation points, natural friendships, division mappings D-2..D-12), and `tajakaSelfCheck()`. |
| `dracoved_app/src/core/chart_types.h` | `SiderealAyanamsa::PushyaPaksha` added to the enum; all four conversion helpers updated; Swiss sidereal mode **29 (True Pushya)** — native in the vendored Swiss Ephemeris 2.10.03, exactly matches Rao's definition. |
| `dracoved_app/src/gui/return_calculation_service.h/.cpp` | Root-finder refactored to `solarReturnTimeUtcImpl(..., bool tropicalBasis)`. Public wrappers: `solarReturnTimeUtc()` (unchanged behavior) and **`tajakaSolarReturnTimeUtc()`** (always tropical basis; target must be the natal Sun's tropical longitude). |
| `dracoved_app/src/gui/main_window.h/.cpp` | Solar Return tab: Method dropdown (`SolarChartMethod::Standard/Tajaka`, member `solarMethodCombo_`), basis routing via `solarChartMethod()` / `solarReturnTargetSunLongitude()`, birthplace lock in `resolveSolarReturnContext()` + `updateSolarLocationAvailability()`, ayanamsa combo enabled under Tajaka, `applyTajakaDefaultAyanamsa()`, `computeTajakaDataForChart()` / `clearTajakaData()` (muntha + aspects + strengths + LoY, against a sidereal natal reference recomputed on demand when the app chart is tropical), Tajaka dock rows and wheel note, `populateTajakaAspectsTable()`, Tajaka branch in `refreshSolarReturnView()` AND in `refreshAspectsForHeaderMode()`, scope-tab handling in `updateAspectScopeTabs()` / `handleTransitAspectViewChanged()`, persistence key `solar/method`, startup `tajakaSelfCheck()` call after `loadUiState()`. |
| `dracoved_app/src/gui/main_window_solar_report.cpp` | Solar Return Markdown report: "Tajaka Varshaphala" section (method/muntha), "Lord of the Year" section (candidacy + pancha table + selection + benefic list), "Tajaka Strengths" section (all components per planet), "Tajaka Aspects" section. |
| `dracoved_app/src/gui/main_window_return_finder.cpp` | Open-Return-Chart handoff: recomputes sidereally for Tajaka queries, names it "Tajaka Varshaphala YYYY", switches the Solar tab Method combo to Tajaka, computes the full Tajaka data set. |
| `dracoved_app/src/gui/return_finder_types.h` | New: `ReturnFinderTargetKind::{Muntha, MunthaLord, LordOfYear}`, `ReturnFinderConditionType::{MunthaPlacement, TajakaAspect, LordOfYearPlacement}`, `ReturnFinderTajakaMotion {Any, Ithasala, Eesarpha}`, `ReturnFinderCondition::tajakaMotion`, `ReturnFinderQuery::tajakaMode`. |
| `dracoved_app/src/gui/return_finder_worker.cpp` | Tajaka scan branch: `tajakaScan = tajakaMode && Solar`; sidereal natal recompute when needed; tropical Sun target from natalInput; `tajakaSolarReturnTimeUtc` in the year loop; `buildReturnChart` forces sidereal zodiac; evaluators `evaluateMunthaPlacement`, `evaluateTajakaAspect`, `evaluateLordOfYearPlacement`; target resolution for Muntha / MunthaLord / LordOfYear in `resolveTarget`. |
| `dracoved_app/src/gui/return_finder_controller.h/.cpp` (compile chain via `_compile.cpp` → `_all.cpp` → `_impl.cpp` → `controller.cpp`) | "Return Method" combo (`returnMethodCombo_`: Standard/Tajaka), `tajakaMethodActive()`, signal `tajakaMethodChanged(bool)`; target editor kinds Muntha/Muntha Lord/Lord of the Year with hint pages; condition pages for the three new types; buildQuery sets `tajakaMode`, pins natal location, validates TajakaAspect (two distinct classical planets); copy-report row; JSON preset round-trip (`tajaka_motion` added; schema stays v1). |
| `dracoved_app/CMakeLists.txt` | Added `src/core/tajaka.h/.cpp`. |

---

## 3. What was implemented, in order

### Phase 1 — Tajaka chart calculation (WORKING, user-verified)
- **Method dropdown** on the Solar Return tab. Tajaka = return moment from the Sun's **natal tropical longitude** (in tropical app mode this equals the standard return; in sidereal mode it deliberately differs — Rao's correction), chart **always computed sidereal** with the selected ayanamsa, **always Whole Sign houses**. Defect history: the first whole-sign forcing was inserted BEFORE an unconditional `input.houseSystem = houseSystem;` later in `computeSolarReturnChartPure` — dead code, so Tajaka charts silently kept the app-level (Placidus) houses while Muntha/LoY were computed whole-sign internally, producing contradictory house data that shifted year to year (caught by the user while browsing with Previous/Next). The forcing is now applied AFTER all generic assignments in `computeSolarReturnChartPure`, and `buildReturnChart` in the Return Finder worker forces both Sidereal and WholeSign directly.
- Initial bug (fixed early): the chart first followed the app zodiac, so in tropical mode it produced an ordinary tropical SR. User caught it; corrected to force sidereal always.
- **Muntha**: natal Ascendant progressed one sign per completed year, degree-preserving (natal ASC 15° Sc → age 1 → 15° Sg). Always computed against the **sidereal natal Ascendant** (recomputed on demand if the app chart is tropical). Shown in the wheel note ("Tajaka · Muntha: … · H# (meaning) · Profection: …") and in right-dock rows with sign/lord/house/house-meaning.
- **Tajaka aspects engine** (core): whole-sign kinds among the 7 classical planets — conjunction (same sign, strong malefic), semi-sextile 2/12 (neutral), sextile 3/11 (weak benefic), square 4/10 (weak malefic), trine 5/9 (strong benefic), opposition (strong malefic); **6th/8th sign distance has no aspect**; orbs = per-planet deeptamsa (Sun 15, Moon 12, Mars 8, Mercury 7, Jupiter 9, Venus 7, Saturn 9); only **mutual** (both within each other's deeptamsa = vartamaana) aspects are kept; **Ithasala/Eesarpha** from advancement-in-sign comparison with retrogression refinements (faster-retrograde-and-behind → eesarpha; faster-retrograde-and-ahead → ithasala; slower retrograde noted); **Poorna** flag when advancements within 1°. The "about to station retrograde" refinement is NOT implemented (needs ephemeris sampling).

### Phase 2 — Return Finder integration (WORKING, user-verified)
- **Return Method combo** in Research Settings (Solar only; disables for Lunar). Tajaka scans: tropical basis, every scanned chart sidereal, **house mode forced to Whole Sign** (in `buildQuery`, mirrored in the worker's `run()`; the House System combo is locked and auto-set to Whole Sign while Tajaka is active), natal location pinned, sidereal natal reference recomputed in the worker when the app chart is tropical (also applied to the natal-Placidus recompute path).
- **Muntha Placement** condition (house under the Whole-Sign/Placidus/Both settings, or sign).
- **Tajaka Aspect** condition: two distinct classical planets, aspect selector incl. Any-Major, **Motion selector** (Ithasala / Eesarpha / Any), no user orb (deeptamsa-based). Non-matches get explanatory descriptions.
- **Muntha / Muntha Lord** as subject/target kinds in the regular Aspect condition editor (Return/Natal scope for the lord's position).
- **Lord of the Year Placement** condition + **Lord of the Year (Tajaka)** target kind (added with Tier 1).
- Copy report gains a "Return Method" row.
- Standard scans verified unregressed: every Tajaka behavior is gated behind `tajakaMode`/`tajakaScan`; the standard root-finder call, target longitude, and chart zodiac are the original code paths.

### Tier 1 — Pushya-paksha, strengths, Lord of the Year, Tajaka aspect view (COMPILED; verification partial — see §5)
- **Pushya-paksha ayanamsa**: enum entry + Swiss mode 29 ("True Pushya": Delta Cancri fixed at 16° Cancer 0′ — exactly Rao's definition, natively supported by the vendored swisseph 2.10.03). Added to the toolbar combo (with tooltip) and to string/from-string/names conversions (profile round-trip safe).
- **Auto-default**: switching the Solar tab Method OR the Return Finder Return Method to Tajaka auto-selects Pushya-paksha in the zodiac toolbar (`applyTajakaDefaultAyanamsa()`, wired via `tajakaMethodChanged` signal from the controller). The switch is not signal-blocked — the natal chart intentionally recomputes. The user can still pick another ayanamsa afterwards.
- **Strengths** (`computeTajakaStrengths`): Harsha Bala (4 sources × 5 units: favored house per planet; exaltation/own; feminine planets in 1/2/3/7/8/9 vs masculine in 4/5/6/10/11/12; day→masculine, night→feminine using `chart.isDayChart`), Pancha Vargeeya (Kshetra 30/15/7.5, Uchcha = 20 × distance-from-debilitation/180, Hadda 15/7.5/3.75, Drekkana 10/5/2.5, Navamsa 5/2.5/1.25, sum ÷ 4, rating scale), Dwadasha Vargeeya (strong−weak across D-1..D-12).
- **Lord of the Year** (`computeTajakaLordOfYear`): five candidates (day/night Sun-or-Moon sign lord in the annual chart; natal lagna lord; muntha lord; annual lagna lord; Triraasi lord from the day/night table). Benefic-on-lagna shortlist = candidate's Tajaka trine/sextile to the annual ASC within its deeptamsa (degree-based); rank by pancha vargeeya with 0.5-unit "similar" tie broken by candidacy count; fallback chain: malefic-aspecting candidates → very strong (≥15) → first candidate. `selectionReason` (QString) documents the rule applied.
- **Display**: right dock gains LoY, LoY pancha bala + rating, LoY placement, LoY selection reason; report gains the LoY and Tajaka Strengths sections.
- **Tajaka aspects view**: `populateTajakaAspectsTable()` — flat table (Point A / Aspect / Point B / Orb / Nature / Motion) replacing the Ptolemaic matrix when Method = Tajaka; scope tab shows a single "Tajaka Aspects" tab; `populateAspectMatrix` restores the matrix delegate + clears spans when the standard matrix runs.

### Self-checks
`tajakaSelfCheck()` runs at MainWindow startup (after `loadUiState()`; failure shows in `solarStatusLabel_`). Validates against the books' worked examples: Muntha Sc→Ge at 31 years; deeptamsa table; Moon 14°Le/Venus 19°Li ithasala vs 23°Le eesarpha; Venus 13°Li trine window 6–20° Ge (accept/reject); Uchcha bala Jupiter 8°Vi30 = 12.94; hadda lord Venus at 8°Vi30; and the complete Harsha example (Example 119's annual chart reproduced synthetically; expected Moon 15, Mercury/Venus 10, Jupiter/Saturn 5, Sun/Mars 0).

---

## 4. Documented interpretation choices (book is silent/ambiguous)

- **Neutral relations grouped with friend** in all strength tables (book values only own/friend/enemy).
- **D-5 (panchamsa)** uses the BPHS odd/even tables (odd: Ar/Aq/Sg/Ge/Li; even: Ta/Vi/Pi/Cp/Sc); **D-11** progresses from the sign itself. Book 2 gives no division formulas.
- **Muntha is degree-preserving** (standard computation); the book's example only works at sign level.
- **Tajaka aspects keep only mutual-orb (vartamaana) pairs** — a deliberate strictness for the list/report.
- Two known source contradictions (NOT yet implemented anywhere — relevant only if yogas are ever built): Duhphali-Kutta's definition contradicts its own worked example, and Nakta/Yamaya's "aspect" wording conflicts with their examples.

---

## 5. Current state — what works, what doesn't, what's unverified

**Working (user-verified with builds):**
- Tajaka chart calculation: tropical return moment, sidereal chart, birthplace lock, tropical/sidereal method difference behaving as designed.
- Muntha (wheel note + dock), standard-solar regression behavior.
- Return Finder: Tajaka scan mode, Muntha Placement, Tajaka Aspect (motion filter), Muntha/Muntha Lord targets, open-result handoff. User confirmed with a working screenshot.
- The last full build (08/16/2026 07:51 PM) compiled everything including Pushya-paksha, strengths, LoY, and the aspects-view code.

**PARTIALLY WORKING — the Tajaka aspects panel (rendering fixed, layout still poor):**
Symptom history: panel first rendered empty on the Solar tab in Tajaka mode. Three successive defects were found and fixed in sequence:
1. The table was only populated from `refreshSolarReturnView()`; the central dispatcher `refreshAspectsForHeaderMode()` (fired by grid/visibility/header toggles) **overwrote it** with the standard matrix. Fixed: dispatcher now routes to `populateTajakaAspectsTable()` when Tajaka is active.
2. Defensive: `populateAspectMatrix` now clears spans (the Tajaka empty-case uses a span).
3. **Root cause of the blank panel: `populateTajakaAspectsTable()` called `aspectsTable_->setItemDelegate(nullptr)` — a null item delegate in Qt paints NO cells.** Fixed: a dedicated `QStyledItemDelegate` (`flatAspectDelegate_`, lazily created, member of MainWindow) is installed instead.
**Verified in a build after the fix: rows now render.** Remaining defect is visual layout only — see §8.

**Verified-compiled but user-verification incomplete:**
- Pushya-paksha values (compare against the paper's reference values, e.g., 2014-01-01 → 22°55′3.87″).
- Strengths and Lord of the Year outputs (cross-check against JHora's Varsheswara/panchavargeeya if available; the startup self-check validates the book examples, but per-chart outputs were not yet eyeballed by the user).
- Return Finder Lord-of-Year condition/target (implemented, not yet user-tested).

**Honest defect log from this work (all fixed, listed so the patterns are known):**
- `selectionReason` was declared `QStringList` but assigned/consumed as `QString` — caused two build-error rounds until the field was changed to `QString`.
- A function-splice edit accidentally replaced `applySolarReturnYear`'s signature with a duplicate `applyTajakaDefaultAyanamsa` — caught in self-review before building.
- One edit mangled a newline in `return_finder_controller_impl.cpp` — caught immediately.
- The aspects-panel issues described above.

---

## 6. Build & environment notes

- Build via `do_build.bat` from CMD (user runs it; agent does not).
- **Known pre-existing script bug (NOT fixed — user's script):** on a failed build, the retry path does not propagate the errorlevel correctly, and the script prints "BUILD AND PACKAGING COMPLETED SUCCESSFULLY" with an empty timestamp while both EXEs are stale. Always check that steps `[3/7]`–`[7/7]` actually ran and a real timestamp printed.
- Stack: C++20, Qt 6.10.1 (Widgets/Network/Svg), MinGW, Ninja, Swiss Ephemeris 2.10.03 via `swedll64.dll`; ephemeris files in `ephe/` (1800–2399).
- Qt pitfall encountered (important for future edits here): the right-dock rows use `QVector<QPair<QString,QString>>` and report tables use `QVector<QStringList>` — brace-init with mismatched types (e.g., a `QStringList` where a `QString` cell is expected) fails with confusing errors at the outer initializer. And never `setItemDelegate(nullptr)` on a view that must render.

---

## 7. Quick verification checklist (for whoever picks this up)

1. Launch, load a natal chart → Solar Return tab → Method = "Tajaka Varshaphala (P.V.R. Rao)" → toolbar ayanamsa auto-flips to "Pushya-paksha" → Calculate.
2. Chart renders sidereal; wheel note shows Tajaka/Muntha; right dock shows Method, Muntha rows, Lord of the Year rows (planet, pancha, placement, selection reason), Tajaka aspect count.
3. Aspects panel (bottom-left): flat Tajaka Aspects table with readable layout — **verified working by the user across multiple years**. Toggling chart settings/grid options must not replace it with the matrix; switching Method back to Standard + recalculate must restore the matrix.
4. Copy Solar Return Report → Tajaka Varshaphala / Lord of the Year / Tajaka Strengths / Tajaka Aspects sections present.
5. Return Finder → Return Method = Tajaka → conditions: Muntha Placement, Tajaka Aspect (motion filter), Lord of the Year Placement; LoY as Aspect target kind. Run over a year range; open a result → lands on the Solar tab in Tajaka mode.
6. Regression: Standard solar return (tropical and sidereal), Natal matrix, Transits overlay grid, Lunar Return matrix unchanged.

---

## 7a. MULTI-AGENT AUDIT (2026-08-16) — findings and fixes applied

A five-way audit (core math, main-window integration, Return Finder, regression vs git baseline, tables) produced the following; all are FIXED in code unless marked otherwise:

**Critical (fixed):**
- **Uchcha Bala was inverted** (`tajaka.cpp`): the deep EXALTATION longitude was used as the debilitation reference (missing +180°), so exalted planets scored 0 and debilitated ones 20 — corrupting Pancha Vargeeya and LoY rankings. Fixed: `debilitation = normalize360(exaltation + 180)`.
- **Self-checks were broken**: the Muntha check used 240° for Scorpio (correct: 210°) and the trine test asserted the wrong name order — so the startup check failed at the first assertion and masked the Uchcha bug. Both fixed; the startup check now passes its own math (the Harsha book example was already correct).
- **Whole-sign forcing was dead code** (found earlier by the user): forcing placed before an unconditional `input.houseSystem = houseSystem;`. Fixed by reordering; worker + open-handler also force directly.

**Integration (fixed):**
- The Solar-tab and Return-Finder method combos fought over one remembered ayanamsa slot (each had independent apply/restore). Fixed with a single arbiter `syncTajakaAyanamsaDefault()`: Pushya-paksha applies while EITHER combo is Tajaka, restore happens only when NEITHER is. The RF return-type combo now also emits `tajakaMethodChanged` (previously switching Solar→Lunar with Tajaka on desynced silently).
- Opening a Standard Return-Finder result while the Solar tab was Tajaka left the method combo on Tajaka → mislabeled wheel/dock/report and the next calculation ran Tajaka. Fixed: the combo now syncs BOTH ways and the ayanamsa arbiter runs after.
- Mixed-zodiac comparisons under tropical-app + Tajaka: SR (sidereal) was compared against the tropical natal in the right-bottom dock overlay, the technique view's natal hits, and the report's Solar–Natal aspects/overlays/natal sections. Fixed: `computeTajakaDataForChart` stores the sidereal natal reference (`currentTajakaNatalChart_`), and all those paths use it under Tajaka.
- Return Finder `buildQuery`: Tajaka WholeSign forcing was overwritten one line later by the combo read (stale combo after preset load or Lunar detour leaked a wrong `lastQuery_.houseMode` into reports). Forcing now applied after the combo read.
- Solar Placement Finder under Tajaka: Placidus/Both modes scanned charts whose cusps were never computed → every year "failed". Now degrades to Whole Sign with a status note.
- Tajaka "Any Major Aspect" condition wrongly matched semi-sextiles. Fixed (excluded).
- Aspect scope tab bar wasn't refreshed on method switch/calculate (stale "Tajaka Aspects" or dead Solar tabs). `updateAspectScopeTabs()` now called from the method handler.
- Tajaka-family RF conditions (Muntha/LoY/Tajaka Aspect, and Muntha/MunthaLord/LoY targets) under Standard method with a TROPICAL natal computed references across zodiacs. Now a buildQuery validation error: they require the Tajaka method or a sidereal natal chart.
- Same-scope Muntha-vs-Muntha / MunthaLord / LoY aspect pairs now rejected at validation (previously silent no-match).
- RF custom-location fields were overwritten with natal data when Tajaka activated (data loss). Fields now left untouched and disabled.
- Muntha placement description missing a space ("is inHouse"); RF details dock missing the three new condition-type labels; Profection Lord editor hint wrongly said "lord of the year"; Muntha age now year-based (consistent with the profection display, timezone-proof).

**Audited clean (no action):** all embedded tables verified entry-by-entry (hadda ×60, triraasi, friendships, exaltation/own-sign, deeptamsa, speed ranks, division formulas incl. boundary clamping); standard (non-Tajaka) flows byte-equivalent to the pre-Tajaka git baseline across solar/lunar/natal/transits/synastry; worker threading clean; startup sequence safe. Known intentional: the shared-report aspect dedup (X-conj-Descendant mirror rows dropped) affects the standard Solar report too — that was the approved natal-report behavior change, not a Tajaka regression.

## 8. Tajaka aspects table layout — FIXED AND VERIFIED

**History:** the six columns initially rendered as equal-width slivers with truncated text and stale matrix row heights. Root cause: `populateTajakaAspectsTable()` set no column resize modes, and the matrix view leaves the vertical header in `Fixed` square-cell mode which the flat list inherited.

**Fix (in `populateTajakaAspectsTable()`, `main_window.cpp`):** an `applyFlatLayout()` lambda runs after both `setupTable` calls (populated and empty-message cases):
- Columns 0–4 (Point A / Aspect / Point B / Orb / Nature): `QHeaderView::ResizeToContents`
- Column 5 (Motion — longest text): `QHeaderView::Stretch`, with `setStretchLastSection(false)`
- Vertical header reset to `Interactive` with `setDefaultSectionSize(24)`
- `setAlternatingRowColors(true)`

**Verified by the user across multiple Tajaka years:** rows render with readable per-column sizing, Ithasala/Eesarpha motion labels present, orbs and natures populated. Safety note: `populateAspectMatrix` re-asserts its own `Fixed` sizing and delegate on every run, so the flat-list modes cannot leak into the standard matrix view.

**Visual overhaul (applied after verification, pending build):** the table now colorizes and glyphs everything, reusing the app's existing conventions:
- Aspect kind column: bold text in the wheel's aspect-line colors (Conjunction amber #C99A2E, Sextile cyan #3FA7D6, Square red #E0533D, Trine green #3FA66A, Opposition violet #9B59B6, Semi-sextile gray) — from `chart_wheel_widget.cpp`'s `aspectTypeColor` palette.
- Nature column: colored by benefic/malefic intensity (strong benefic deep green, weak benefic green, weak malefic orange, strong malefic red, neutral gray).
- Motion column: Ithasala green, Poorna Ithasala deep green + bold, Eesarpha red.
- Point A / Point B: the app's SVG planet glyphs (`bodySvgResourcePath` + `tintedSvgIcon`, 18px) tinted theme-aware (`#3A2E22` ink on Light/Creme, `#E8E0D4` on Dark).
- Every cell in a row carries the full aspect summary as a tooltip.
- **Row background quality tint (heuristic, documented as such):** each row gets a light green wash (favorable) or red wash (challenging), intensity scaling with the score. **Important implementation note:** `QTableWidgetItem::setBackground()` does NOT work on this table — the app-wide stylesheet sets `QTableWidget { background-color: ... }` for all three themes, which makes Qt's style engine ignore per-item BackgroundRole brushes. The tint is therefore painted by a dedicated delegate (`TajakaAspectRowDelegate`, defined near `tintedSvgIcon` in `main_window.cpp`): rows store a QColor in `Qt::UserRole + 1`, and the delegate `fillRect`s it before normal cell painting. The score is a synthesis of the book's judging rules, NOT a single classical formula: aspect nature (+2 strong benefic / +1 weak benefic / −1 weak malefic / −2 strong malefic) + motion (+1 ithasala, +2 poorna, −2 eesarpha) + the pair's average pancha vargeeya (−1 if < 5, radda-like weak participants; +1 if ≥ 15). The full breakdown and score are shown in each row's tooltip, and the formula is commented in `populateTajakaAspectsTable()`.
