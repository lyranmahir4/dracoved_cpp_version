# Search validation

Run the standalone calculation checks from the project root in CMD:

```bat
set "PATH=C:\Qt\Tools\mingw1310_64\bin;%PATH%"
g++ -std=c++20 -O2 dracoved_app\src\gui\search_event_tests.cpp -o dracoved_app\build\search_event_tests.exe
dracoved_app\build\search_event_tests.exe
```

These checks use `swedll64.dll` and `ephe` from the project root. They cover zodiac wraparound, opposition branch cuts, narrow orb entry/exit, contacts around a relative-motion reversal, near-miss exclusions, cancellation, Mercury shadow station pairing, short date windows, and moving aspects in tropical and sidereal coordinates. They do not rebuild or launch the application.

After building and packaging with `do_build.bat`, manually check:

- Search: switch between every event type; only applicable filter rows should appear. Change Between dates / Next after / Previous before and check the date controls.
- Transit-to-transit Aspect: Moon to Sun conjunctions over 40 days; test Exact and Within orb, and select a result to update the chart and detail pane. Copy the selected result.
- Retrograde Shadow: Mercury over calendar 2026 should return three pre-shadow entries and three post-shadow exits. Test each phase filter and a short interval containing a boundary.
- Closest Approach to Natal: load a natal chart and choose a retrograding planet, natal target, aspect, and maximum orb. Results should be local orb minima at stations, excluding exact contacts.
- Test Next and Previous, cancellation, empty results, and invalid selections for all three additions.
- House filters: select a natal body whose house differs between Whole Sign and Placidus, switch systems, and confirm the numeric House matches the body's updated label. Manually choose another House and confirm the shortcut resets to Any while the chosen House remains.
- House direction: find an ingress/egress in a date range, then use Next just before it and Previous just after it. Both should return the same event and house under each house system, including a retrograding body.
- Result summary: run a house search, change the system and filters without running again, and confirm the summary retains the submitted settings. Run again and confirm it updates. Switch away from Search and confirm the summary is hidden.
- Smoke-test existing Search types, Calendar, Conjunction, Scan, Lunation, natal chart generation, and solar return. These application workflows require a fresh packaged executable.
