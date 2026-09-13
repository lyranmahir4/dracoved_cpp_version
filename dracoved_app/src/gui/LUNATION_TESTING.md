# Lunations filter UI validation

After building and packaging with `do_build.bat`, check the standalone Lunations tab and the Transits Lunations subtab (they share controls).

- Next/Previous: enter a reference in the displayed timezone. Confirm events fall after/before that reference. Check Now and Use chart time (selected transit time). Change the date manually and confirm Search uses it.
- Year Range: show year fields and hide After/Before. Switch back and retain the edited reference.
- Eclipse rule: show only when an eclipse event is selected; tropical mode keeps astronomical classification. Sidereal mode allows the existing two rules.
- Degree bounds: hidden until enabled; apply to the Moon's degree within sign. Test 27–29 and the supported wrapped range 29–2.
- Planet conjunction: enabling it selects Year Range and reveals planet, targets, and orb. Next/Previous remain disabled until the requirement is removed. Multiple checked targets mean any target may match.
- Placement filters: compare the unfiltered event list with Only selected / Exclude selected signs and natal houses. Verify both house systems supported by the natal input, and no-chart disabling. These filters refine found events, not the Next/Previous worker's search horizon.
- Advanced: List Events hides strictness, match key, and target degree; Repeated Degrees shows strictness and match key; Target Degree also shows degree/minute/second. Orb input appears only for Within orb. Advanced modes retain their Year Range requirement.
- Select a filtered result, check chart/details and copied placements refer to that result. Smoke-test ordinary New/Full Moon and eclipse searches with optional filters off.

The changes do not modify the lunar phase or eclipse worker algorithms. Syntax validation alone does not replace these packaged-app checks.

## Repeated degree research checks

Run the standalone grouping regression executable from the project root in CMD:

```bat
set "PATH=C:\Qt\Tools\mingw1310_64\bin;%PATH%"
g++ -std=c++20 dracoved_app\src\gui\lunation_degree_tests.cpp -o dracoved_app\build\lunation_degree_tests.exe
dracoved_app\build\lunation_degree_tests.exe
```

- Maximum spread is total highest-minus-lowest distance, not plus/minus the average. Groups are non-overlapping and anchored to the lowest unused degree; nearby events can lie in different groups at a boundary. There is no 29/0 circular merging. The standalone tests cover boundaries, no chain linking, duplicates, no wrap, and arcsecond rounding.
- Compare exact grouping with same rounded arcsecond, then 0.25°, 0.5°, and 1° maximum spread. Check average, min/max, member deviations, and counts against Copy Group's numeric values.
- Set minimum occurrences to 2, 5, and a value above all counts. Test all sorting modes and separate event types, including combinations with sign/house match keys.
- Change sorting with a group selected: retain it if its membership still exists. Click and keyboard-select member events; the chart should change while the member list remains available.
- Check Copy Group and Copy All Groups: tab-separated member rows, source search context, group rule, placement filters, group statistics, exact UTC/local timestamps, timezone, and signed deviations.
- Counts represent records, including separately requested eclipse records. Use separate event types or search only New/Full Moon when comparing phase occurrences.
- Switch to List Events, Target Degree, and other tabs: no hidden/reordered research columns should leak into their tables. Test control changes in both Lunations entry points.
