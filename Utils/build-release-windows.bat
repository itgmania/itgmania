@echo off
setlocal
cd /d "%~dp0.."

where makensis >nul 2>&1
if errorlevel 1 (
    echo "Error: NSIS not found in PATH. Ensure it is installed and part of the system PATH."
    exit /b 1
)

echo "NSIS detected."

cmake -B Build\release -T v143 -DWITH_FULL_RELEASE=On -DWITH_CLUB_FANTASTIC=On || exit /b
cmake --build Build\release --config Release -j %NUMBER_OF_PROCESSORS% || exit /b
cmake --build Build\release --config Release --target package || exit /b
cmake -B Build\release -T v143 -DWITH_FULL_RELEASE=On -DWITH_CLUB_FANTASTIC=Off || exit /b
cmake --build Build\release --config Release --target package || exit /b
