@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "APP_DIR=%ROOT%dracoved_app"
set "BUILD_DIR=%APP_DIR%\build"
set "DIST_DIR=%APP_DIR%\dist"
set "BUILD_EXE=%BUILD_DIR%\dracoved_app.exe"
set "DIST_EXE=%DIST_DIR%\dracoved_app.exe"
set "LOGFILE=%ROOT%build_step.log"

set "QT_ROOT=C:\Qt\6.10.1\mingw_64"
set "QT_BIN=%QT_ROOT%\bin"
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%QT_BIN%;C:\Qt\Tools\mingw1310_64\bin;%PATH%"
set "QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"

cd /d "%ROOT%" || goto :root_failed

> "%LOGFILE%" echo DracoVed build started: %DATE% %TIME%
echo.
echo ============================================================
echo DracoVed build and package
echo Root: %ROOT%
echo ============================================================

if not exist "%APP_DIR%\CMakeLists.txt" (
    call :fail "CMakeLists.txt was not found at %APP_DIR%."
    goto :failed
)
if not exist "%QT_ROOT%" (
    call :fail "Qt was not found at %QT_ROOT%."
    goto :failed
)
if not exist "%ROOT%swedll64.dll" (
    call :fail "swedll64.dll was not found in the project root."
    goto :failed
)
if not exist "%ROOT%ephe" (
    call :fail "The ephe folder was not found in the project root."
    goto :failed
)

where cmake.exe >nul 2>&1 || (
    call :fail "cmake.exe is not available on PATH."
    goto :failed
)
where ninja.exe >nul 2>&1 || (
    call :fail "ninja.exe is not available on PATH."
    goto :failed
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"

echo.
echo [1/7] Configuring CMake...
call :run cmake -S "%APP_DIR%" -B "%BUILD_DIR%" -G Ninja -DCMAKE_PREFIX_PATH="%QT_ROOT%"
if errorlevel 1 goto :failed

echo.
echo [2/7] Building executable...
call :run cmake --build "%BUILD_DIR%" --parallel 2
if errorlevel 1 (
    echo.
    echo First build attempt failed. Retrying once after refreshing Qt AutoGen state...
    >> "%LOGFILE%" echo First build attempt failed; retrying after AutoGen cleanup.
    timeout /t 2 /nobreak >nul
    call :clear_autogen_state
    call :run cmake --build "%BUILD_DIR%" --parallel 2
    if errorlevel 1 goto :failed
)

if not exist "%BUILD_EXE%" (
    call :fail "Build reported success, but %BUILD_EXE% does not exist."
    goto :failed
)

echo.
echo [3/7] Copying fresh executable to dist...
call :run copy /Y "%BUILD_EXE%" "%DIST_EXE%"
if errorlevel 1 goto :failed

echo.
echo [4/7] Deploying Qt runtime files...
call :run "%QT_BIN%\windeployqt.exe" --compiler-runtime --no-translations "%DIST_EXE%"
if errorlevel 1 goto :failed

echo.
echo [5/7] Copying Swiss Ephemeris DLL...
call :run copy /Y "%ROOT%swedll64.dll" "%DIST_DIR%\swedll64.dll"
if errorlevel 1 goto :failed

echo.
echo [6/7] Copying ephemeris data...
call :run xcopy /E /I /Y "%ROOT%ephe" "%DIST_DIR%\ephe"
if errorlevel 1 goto :failed

echo.
echo [7/7] Verifying packaged executable...
for %%F in ("%BUILD_EXE%") do (
    set "BUILD_SIZE=%%~zF"
    set "BUILD_TIME=%%~tF"
)
for %%F in ("%DIST_EXE%") do (
    set "DIST_SIZE=%%~zF"
    set "DIST_TIME=%%~tF"
)
if not "%BUILD_SIZE%"=="%DIST_SIZE%" (
    call :fail "The build and dist executable sizes do not match."
    goto :failed
)

>> "%LOGFILE%" echo Build EXE: %BUILD_EXE%
>> "%LOGFILE%" echo Build timestamp: %BUILD_TIME%
>> "%LOGFILE%" echo Dist EXE: %DIST_EXE%
>> "%LOGFILE%" echo Dist timestamp: %DIST_TIME%
>> "%LOGFILE%" echo DracoVed build completed successfully: %DATE% %TIME%

echo.
echo ============================================================
echo BUILD AND PACKAGING COMPLETED SUCCESSFULLY
echo Build EXE: %BUILD_EXE%
echo Dist EXE:  %DIST_EXE%
echo Timestamp: %DIST_TIME%
echo Log:       %LOGFILE%
echo ============================================================
goto :finish

:clear_autogen_state
if exist "%BUILD_DIR%\dracoved_app_autogen\deps" (
    attrib -R "%BUILD_DIR%\dracoved_app_autogen\deps" >nul 2>&1
    del /F /Q "%BUILD_DIR%\dracoved_app_autogen\deps" >nul 2>&1
)
if exist "%BUILD_DIR%\dracoved_app_autogen\timestamp" (
    attrib -R "%BUILD_DIR%\dracoved_app_autogen\timestamp" >nul 2>&1
    del /F /Q "%BUILD_DIR%\dracoved_app_autogen\timestamp" >nul 2>&1
)
exit /b 0

:run
>> "%LOGFILE%" echo.
>> "%LOGFILE%" echo Command: %*
%*
set "COMMAND_ERROR=%ERRORLEVEL%"
>> "%LOGFILE%" echo Exit code: %COMMAND_ERROR%
exit /b %COMMAND_ERROR%

:fail
echo ERROR: %~1
>> "%LOGFILE%" echo ERROR: %~1
exit /b 1

:root_failed
echo ERROR: Could not enter project root: %~dp0
goto :failed

:failed
echo.
echo ============================================================
echo BUILD FAILED - the old dist executable was not replaced.
echo Read the error above or open:
echo %LOGFILE%
echo ============================================================
if /I not "%~1"=="--no-pause" pause
exit /b 1

:finish
if /I not "%~1"=="--no-pause" pause
exit /b 0
