# Lunar Return views

Calculate a Lunar Return using the existing date, timezone and location controls. The two scope tabs above the aspect matrix also select the main chart view:

- **Lunar Return**: the return chart alone and its internal aspect matrix.
- **Lunar–Natal**: natal inside, Lunar Return outside, with cross-chart aspects. Natal houses and Ascendant orient the wheel; the return's Ascendant, Midheaven, Descendant and IC are marked in the outer lane. The ring labels and chart legend identify both charts.

The placement tables continue to describe the Lunar Return. The natal-house placement table describes where those return bodies fall in the natal chart. A custom return location changes the return chart without relocating the natal reference.

In Lunar–Natal, click an aspect cell or a line in the wheel to emphasize that contact and ring both endpoints. Identical planet names in the two charts remain distinct. Hovering other lines still works. Press Escape, click an empty cell or empty chart space, or click a planet to clear the selected aspect. Clicking the selected wheel line again also clears it. Existing body focus remains available.

Changing the return, switching chart modes or leaving for another chart clears the aspect selection. Returning to Lunar Return restores the selected single/two-ring mode. Refreshing the matrix clears its selection. Existing wheel visibility and display-orb controls still apply; hidden aspects or bodies are not forced visible by selecting a cell. Ordinary transit scope preferences are unchanged.

## Standalone validation

From PowerShell at the repository root:

```powershell
& .\dracoved_app\src\gui\lunar_return_view_check.ps1
```

This compiles the changed main-window and chart-wheel code plus a test harness into `build/lunar_return_view_checks/lunar_return_view_checks.exe`. It reuses other object files from a prior normal application build; it does not build/package or launch `dracoved_app.exe`. Settings are isolated. `-Incremental` reuses unchanged test objects for a retry.

Checks cover real natal and lunar charts at different locations, ring identity, natal house orientation, return angles, selecting a grid contact between the two Moons, selecting a drawn line, Escape and empty-cell clearing, aspect-filter refresh/copy, previous/next return navigation, restoration of single-chart mode and other tabs, and transit preference isolation. The rendered wheel is saved as `build/lunar_return_view_checks/lunar_natal.png`.

After the normal build/package, choose Lunar–Natal, click an aspect, switch back to Lunar Return, and navigate to the next return to check the same interactions at your usual window size and theme.
