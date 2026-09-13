# Focused classifier test; this does not build or package the application.
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$qt = 'C:\Qt\6.10.1\mingw_64'
$gxx = 'C:\Qt\Tools\mingw1310_64\bin\g++.exe'
$out = Join-Path $env:TEMP 'dracoved_vedic_nakshatra_test.exe'
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;$qt\bin;$env:PATH"
& $gxx -std=c++20 -O0 -Wall -Wextra `
    "-I$qt\include" "-I$qt\include\QtCore" "-I$root\dracoved_app\src\core" `
    "$PSScriptRoot\vedic_nakshatra.cpp" "$PSScriptRoot\vedic_nakshatra_test.cpp" `
    "-L$qt\lib" -lQt6Core -o $out
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $out
exit $LASTEXITCODE

# Independent fixture source:
# sweph/bin/swetest64.exe -b4.1.1999 -ut10:01:00 -p0123456mt -sid1 -fPl -head -eswe -edirephe -house90.389,23.764,W
