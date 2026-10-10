@echo off
echo ===================================================
echo   GatewayV2 Project Setup
echo ===================================================
echo.

echo [INFO] Initializing and updating Git submodules...
git submodule update --init --recursive
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Failed to initialize git submodules! Make sure you have Git installed and are connected to the internet.
    pause
    exit /b %ERRORLEVEL%
)
echo [SUCCESS] Git submodules initialized successfully!
echo.

echo [INFO] You can now build the project using build.bat.
pause
