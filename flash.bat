@echo off
setlocal
set "ROOT=%~dp0"
cd /d "%ROOT%"

set "CLI="
if exist "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" (
    set "CLI=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
) else if exist "C:\ST\STM32CubeIDE_1.18.1\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.100.202412061334\tools\bin\STM32_Programmer_CLI.exe" (
    set "CLI=C:\ST\STM32CubeIDE_1.18.1\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.100.202412061334\tools\bin\STM32_Programmer_CLI.exe"
)

if not defined CLI (
    echo [ERROR] STM32_Programmer_CLI.exe not found.
    echo [ERROR] Please install STM32CubeProgrammer or update this script with the correct path.
    exit /b 1
)

if not exist "%ROOT%build\GatewayV2.bin" (
    echo [ERROR] Missing firmware image: build\GatewayV2.bin
    echo [ERROR] Please run build.bat first to compile the project.
    exit /b 1
)

echo [INFO] Using programmer: "%CLI%"
for %%F in ("%ROOT%build\GatewayV2.bin") do if %%~zF GTR 671744 (
    echo [ERROR] Application image exceeds 656KB primary partition.
    exit /b 1
)
echo [INFO] Flashing GatewayV2.bin to STM32H5 at 0x08010000...
"%CLI%" -c port=SWD mode=UR reset=SWrst -w "%ROOT%build\GatewayV2.bin" 0x08010000 -v -rst

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] Flash completed successfully!
) else (
    echo [ERROR] Flash failed!
)

endlocal
