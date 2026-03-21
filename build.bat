@echo off
setlocal

cd /d "%~dp0tasbot"

echo === Configuring (CMake) ===
cmake --preset default
if errorlevel 1 (
    echo.
    echo Configure FAILED.
    pause
    exit /b 1
)

echo.
echo === Building (Release) ===
cmake --build --preset release
if errorlevel 1 (
    echo.
    echo Build FAILED.
    pause
    exit /b 1
)

echo.
echo === Build OK! Executables in tasbot\build\Release ===
pause
