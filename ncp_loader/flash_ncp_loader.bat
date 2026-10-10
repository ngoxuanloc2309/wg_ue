@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "IMAGE=%ROOT%\build\ncp_loader\GatewayV2_NCP_Loader.bin"
set "CLI="

where STM32_Programmer_CLI.exe >nul 2>&1
if not errorlevel 1 set "CLI=STM32_Programmer_CLI.exe"
if not defined CLI if exist "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" set "CLI=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
if not defined CLI if exist "C:\ST\STM32CubeIDE_1.18.1\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.100.202412061334\tools\bin\STM32_Programmer_CLI.exe" set "CLI=C:\ST\STM32CubeIDE_1.18.1\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.100.202412061334\tools\bin\STM32_Programmer_CLI.exe"

if not defined CLI (
    echo [ERROR] STM32_Programmer_CLI.exe not found.
    exit /b 1
)
if not exist "%IMAGE%" (
    echo [ERROR] Loader image not found: "%IMAGE%"
    echo [INFO] Run build_ncp_loader.bat first.
    exit /b 1
)

echo [INFO] Flashing NCP Loader at 0x08000000...
"%CLI%" -c port=SWD mode=UR reset=HWrst -w "%IMAGE%" 0x08000000 -v -rst
if errorlevel 1 (
    echo [ERROR] NCP Loader flash failed.
    exit /b 1
)

echo [SUCCESS] NCP Loader flashed.
echo [INFO] Connect Gateway debug UART and run QConn_Flash at 2000000 baud.
exit /b 0
