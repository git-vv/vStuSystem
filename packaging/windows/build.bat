@echo off
REM ==================================================================
REM vStuSystem Windows Build Script
REM Steps: CMake configure -> CMake build -> NSIS package
REM ==================================================================

setlocal
set VERSION=0.0.2
REM %~dp0 ends with backslash; ..\.. goes up two levels (windows -> packaging -> root)
set ROOT=%~dp0..\..
pushd "%ROOT%"

echo ==========================================================
echo  vStuSystem Windows Build (version %VERSION%)
echo ==========================================================
echo.

REM Locate cmake: try PATH first, fallback to VS 2022 bundled path
set "CMAKE_EXE="
where cmake >nul 2>nul
if not errorlevel 1 (
    for /f "delims=" %%I in ('where cmake') do set "CMAKE_EXE=%%I"
)
if not defined CMAKE_EXE if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "C:\Program Files\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"
if not defined CMAKE_EXE (
    echo [ERROR] cmake not found in PATH or VS 2022 install.
    echo         Please install CMake 3.7+ from https://cmake.org/download/
    goto :end_with_pause
)
echo Using cmake: %CMAKE_EXE%

REM Locate makensis: try PATH first, fallback to common install paths
set "MAKENSIS_EXE="
where makensis >nul 2>nul
if not errorlevel 1 (
    for /f "delims=" %%I in ('where makensis') do set "MAKENSIS_EXE=%%I"
)
if not defined MAKENSIS_EXE if exist "C:\Program Files (x86)\NSIS\makensis.exe" set "MAKENSIS_EXE=C:\Program Files (x86)\NSIS\makensis.exe"
if not defined MAKENSIS_EXE if exist "C:\Program Files\NSIS\makensis.exe" set "MAKENSIS_EXE=C:\Program Files\NSIS\makensis.exe"
if not defined MAKENSIS_EXE (
    echo [ERROR] makensis not found in PATH or common install locations.
    echo         Please install NSIS 3.8+ from https://nsis.sourceforge.io/
    goto :end_with_pause
)
echo Using makensis: %MAKENSIS_EXE%
echo.

echo [1/3] CMake configure...
"%CMAKE_EXE%" -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    goto :end_with_pause
)
echo.

echo [2/3] CMake build (Release)...
"%CMAKE_EXE%" --build build --config Release
if errorlevel 1 (
    echo [ERROR] CMake build failed.
    goto :end_with_pause
)
echo.

REM Check vStuSystem.exe exists (VS multi-config generator puts it under Release/)
set "EXE_PATH=bin\vStuSystem.exe"
if not exist "%EXE_PATH%" set "EXE_PATH=bin\Release\vStuSystem.exe"
if not exist "%EXE_PATH%" (
    echo [ERROR] vStuSystem.exe not found after build.
    goto :end_with_pause
)

echo [3/3] NSIS package...
if not exist "packaging\output" mkdir "packaging\output"
"%MAKENSIS_EXE%" /DVERSION=%VERSION% packaging\windows\register_student.nsi
if errorlevel 1 (
    echo [ERROR] NSIS packaging failed.
    goto :end_with_pause
)

echo.
echo ==========================================================
echo  BUILD SUCCESS
echo  Package: vStuSystem-%VERSION%-windows-x64-setup.exe
echo  Location: %ROOT%\packaging\output\vStuSystem-%VERSION%-windows-x64-setup.exe
echo ==========================================================

:end_with_pause
popd
endlocal
echo.
echo ----------------------------------------------------------
echo  Press any key to close this window...
echo ----------------------------------------------------------
pause >nul
exit /b 0
