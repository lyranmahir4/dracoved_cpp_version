@echo off
set "LOGFILE=C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\build_step.log"
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.10.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%"

echo === STEP 1: Building EXE === >> "%LOGFILE%"
cmake --build C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\build >> "%LOGFILE%" 2>&1
if errorlevel 1 (
    echo BUILD FAILED >> "%LOGFILE%"
    exit /b 1
)
echo Build OK >> "%LOGFILE%"

echo === STEP 2: Copy EXE to dist === >> "%LOGFILE%"
copy /Y C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\build\dracoved_app.exe C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\dist\dracoved_app.exe >> "%LOGFILE%" 2>&1
if errorlevel 1 (
    echo COPY FAILED >> "%LOGFILE%"
    exit /b 1
)
echo Copy OK >> "%LOGFILE%"

echo === STEP 3: windeployqt === >> "%LOGFILE%"
set "QT_BIN=C:\Qt\6.10.1\mingw_64\bin"
"%QT_BIN%\windeployqt.exe" --compiler-runtime --no-translations C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\dist\dracoved_app.exe >> "%LOGFILE%" 2>&1
if errorlevel 1 (
    echo WINDEPLOYQT FAILED >> "%LOGFILE%"
    exit /b 1
)
echo windeployqt OK >> "%LOGFILE%"

echo === STEP 4: Copy swedll64.dll === >> "%LOGFILE%"
copy /Y C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\swedll64.dll C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\dist\swedll64.dll >> "%LOGFILE%" 2>&1
if errorlevel 1 (
    echo COPY DLL FAILED >> "%LOGFILE%"
    exit /b 1
)
echo DLL copy OK >> "%LOGFILE%"

echo === STEP 5: xcopy ephe === >> "%LOGFILE%"
xcopy /E /I /Y C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\ephe C:\Users\Mahir\Downloads\DracoVed\DracoVed\DracoVed_cpp_version\dracoved_app\dist\ephe >> "%LOGFILE%" 2>&1
if errorlevel 1 (
    echo XCOPY EPHE FAILED >> "%LOGFILE%"
    exit /b 1
)
echo ephe copy OK >> "%LOGFILE%"

echo === ALL STEPS COMPLETE === >> "%LOGFILE%"
