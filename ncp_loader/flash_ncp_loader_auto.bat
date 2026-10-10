@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "IMAGE=%ROOT%\build\ncp_loader_auto\GatewayV2_NCP_Loader_Auto.bin"
set "CLI="

where STM32_Programmer_CLI.exe >nul 2>&1
if not errorlevel 1 set "CLI=STM32_Programmer_CLI.exe"
if not defined CLI if exist "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" set "CLI=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"

if not defined CLI (
    echo [ERROR] STM32_Programmer_CLI.exe not found.
    exit /b 1
)
if not exist "%IMAGE%" (
    echo [ERROR] Auto loader image not found: "%IMAGE%"
    echo [INFO] Run build_ncp_loader_auto.bat first.
    exit /b 1
)

echo [INFO] Flashing NCP Loader AUTO at 0x08000000...
"%CLI%" -c port=SWD mode=UR reset=HWrst -w "%IMAGE%" 0x08000000 -v -rst
if errorlevel 1 (
    echo [ERROR] Auto loader flash failed.
    exit /b 1
)

echo [SUCCESS] Auto loader flashed.
echo [INFO] Connect the Gateway debug UART and run QConn_Flash at 2000000 baud.
exit /b 0
