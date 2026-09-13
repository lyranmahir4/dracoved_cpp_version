# Transits to Return

Calculate the desired Solar Return, then choose the main **Transits to Return** tab (or **Open Transits to Return** beneath the Solar Return controls). Its results pane uses the full available height alongside the main chart. Drag the dock boundary to resize it, or use **Expand results / Restore layout**. The previous dock width is restored when leaving this workspace. **Show summary** reveals the longer result summary, collapsed by default.

Open **Filters…** to select moving bodies, fixed return-chart targets, an aspect (or all five major aspects), and a proximity orb. Optional house crossings use all twelve fixed return houses. Click **Done**, then **Generate return year**. Filters open in a separate non-modal window. The aspect matrix is hidden by default in this workspace; **Grid** in the chart header enables it independently of the ordinary Transits preference. When a contact is selected, this optional matrix compares Transit to Solar Return.

**Generate aspects** in Filters defaults to **All major aspects**. The Activity **All generated aspects** choice only includes aspects in the saved calculation; it does not expand or rerun that search. Activity lists the generated aspects above the table. If a prior search contains only conjunctions, change **Generate aspects** to **All major aspects** and click **Generate return year** to fill the other columns.

The reference is a copy of the calculated return chart, its zodiac/ayanamsa, node policy, house system, location, and exact moment. Changing filters does not relabel existing results. Changing the loaded return invalidates the old year; pending return settings block a new generation until recalculated. Standard and Tajaka timing use their existing return definitions. Tajaka contacts are measured in the zodiac of the loaded return chart.

## What the dates mean

- **Exact contact**: moving longitude reaches the selected aspect to the fixed return target. Repeated contacts are numbered within that body/target/aspect combination for this year. Pass numbers do not imply that every sequence is a three-pass retrograde cycle.
- **Exact at year start**: already exact at the return moment, including a planet's contact to its own return placement.
- **Orb entry / exit**: refined boundaries of the chosen proximity orb. The timeline shows the full intervening interval.
- **Already within orb / Within orb all year**: an active period intersects the beginning of the year. It can remain active through the next return without another exact contact.
- **Near miss at station**: a non-exact local minimum of orb at a planetary station. Another exact contact can exist elsewhere in the year; this particular approach turns away without reaching exactness.
- **Approaching at year end**: the orb is still decreasing at the next-return boundary. This boundary measurement is not a completed near miss or an in-year exact hit.
- **Station within orb**: a direction change occurring during a qualifying contact period.
- **House crossing**: chronological departure from one fixed return house and entry into the next, including retrograde movement. It does not use moving transit house cusps.

The interval starts at the loaded return moment and excludes the next return. The next-return boundary is refined beyond the shared return solver's whole-second stopping precision to prevent a duplicate annual solar contact. Boundary-proximity measurements at year end are explicitly marked. Events outside the chosen orb do not receive artificial important-date labels.

**Dates** supports event-type filtering and precise orb values. **Year timeline** shows separate lanes, active bars, and selectable date markers. Selecting either displays the return chart inside and the selected transit moment outside on the **main chart**, at the return location. Its existing zoom, visibility, and aspect-orb controls apply; the search proximity orb remains independent of the display orb. The selected transit body is highlighted and the note identifies the return target and event. The embedded chart tab has been removed. Leaving this workspace restores the normal view; coming back restores the selected contact if its return source is still valid. Pending return settings suppress the overlay and a different return clears it. The ordinary Transits workflow's selected time and calculation state are not changed. Copy actions export the saved query's dates, settings, and orb periods as tab-separated text.

## Event checklist and activity ranking

Right-click Dates or open **Event types** to combine exact contacts, entries, exits, period events, near misses, year-end approaches, stations and house crossings. **Exact only**, **Show all** and **Reset** are shortcuts. The button reports how many dates remain visible. These controls only filter saved rows; they do not rerun the ephemeris calculation. Hiding the selected date clears its overlay request. Selecting a timeline marker enables its category so that its selected date is visible again. **Copy visible dates** follows this checklist; **Copy results** continues to export the complete saved search.

**Activity** ranks days, Monday-based weeks, calendar months or return months by a chosen major aspect or all generated major aspects. Choose **Most contacts first**, **Fewest contacts first** or **Chronological**, and optionally constrain the inclusive local-date range. Click **Total** or any aspect heading to sort its counts; click again to reverse direction. Click **Period** for date order. Zero-contact periods remain included, so the lowest counts reveal quiet days/months. The return year's exact start and exclusive end still bound every period. Return months use the original return date and local time as their anchor, including end-of-month adjustment; local days and weeks respect daylight-saving transitions. Partial first/last periods are clipped rather than treated as full periods.

**Dates** and the Activity contact list also support heading sorting. Dates and periods sort chronologically, orbs numerically, aspects by angle, and names alphabetically. The Dates **Local time** column sorts by time of day; **Date** sorts by the complete event moment. Sorting retains the selected contact and chart; event filtering retains the chosen order. **Copy visible dates** and **Copy activity** follow the displayed order.

- **Exact contacts** counts each exact hit once. Distinct retrograde passes count separately. Entry/exit/station rows and house crossings are not extra exact contacts.
- **Contacts active within orb** counts each transit-body/return-target/aspect combination once per period if any saved orb interval overlaps it. Several intervals for the same combination count once in that period. A sustained contact can count in multiple periods, so totals across periods are not a unique annual total.

Select a ranked period to list its contacts, then select a contact to update the main chart. Active-mode rows show a representative overlapping interval, not an invented exact hit; **Copy selected** also supports these selections. **Copy activity** includes local period boundaries, counting mode and aspect totals. Date checklist visibility is independent of activity counts. Only generated bodies/targets/aspects can be analyzed; an em dash means that aspect was not generated, and selecting an ungenerated aspect gives an explanatory message.

## Implementation boundaries

Calculation/UI files are local to this feature. Existing Search, Lunations, Return Finder, Solar Return calculation, and Technique algorithms are not modified. The new background worker loads a temporary isolated Swiss Ephemeris module so other calculations cannot change its zodiac state. Cancellation discards partial work. Unavailable moving-body data is reported as a warning; it is not silently replaced with zero longitude.

Ephemeris samples are spaced by six hours and crossings are refined individually, splitting relative-motion reversals. Stations and local orb minima are refined separately. The analytic near-miss classification is for a moving planet against a fixed return target, not two moving targets.

## Validation

From PowerShell in the project root:

```powershell
& .\dracoved_app\src\gui\solar_transit_check.ps1 -Mode all
```

The script builds standalone test executables only. Ephemeris/UI checks reuse core object files from a prior normal build and require the repository's Swiss DLL and `ephe` folder. It does not run `do_build.bat` or modify the build/dist application executables.

- Synthetic checks: wraparound, exact and tangent contacts, narrow orb periods, station near misses, full-year slow motion, no-contact cases, end exclusion, three passes, opposition, cancellation.
- Real ephemeris checks: Tropical, Sidereal, and Tajaka return-year bounds; exact contacts; orb-window interiors; Whole Sign and Placidus cusp crossings; private-library zodiac isolation; cancellation.
- Offscreen UI checks: asynchronous generation, result selection/overlay, timeline, copy output, pending-source guard, Stop preserving the last complete result, and changed-source invalidation. Screenshots are written to `dracoved_app/build/solar_transit_ui_*.png`.
- Activity checks: duplicate windows, repeated exact passes, entry/exit exclusion, aspect filters, clipped boundaries, return-month anchors and DST. Run separately with `-Mode activity`.
- UI checks also cover independent event checkboxes, exact-only/visible export, numeric/date sorting in both directions, quiet days, sorted selection and copy parity, active-contact selection, invalid ranges, and Expand/Restore callbacks. Run these checks alone with `-Mode ui`.
- Add `-AllMajorAspects` to the UI check to generate all five major aspects with every planet except the Moon, verify every aspect has numeric counts and hits, and sort every count column in both directions.

For the actual main-window integration check, run `& .\dracoved_app\src\gui\solar_transit_workspace_check.ps1`. It recompiles the MainWindow translation units into a separate test executable with isolated settings and synthetic chart data. It checks workspace sizing, matrix preference isolation, chart selection, tab restoration and return invalidation. Its outputs are under `dracoved_app/build/solar_workspace_checks`; the application EXEs and dist package are not changed.

After the user's full build/package, check the main Transits to Return tab at normal window sizes, switch return years/settings, test Stop, choose a timeline marker, and compare a selected contact with its chart. Existing Solar Return, Technique, and other tabs should retain their previous controls and behavior.
