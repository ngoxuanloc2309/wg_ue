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
    exit /b 1
)

if not exist "%ROOT%build\GatewayV2_factory.bin" (
    echo [ERROR] Missing factory image: build\GatewayV2_factory.bin
    echo [ERROR] Copy a verified factory .bin to that path before flashing.
    exit /b 1
)
for %%F in ("%ROOT%build\GatewayV2_factory.bin") do if %%~zF GTR 671744 (
    echo [ERROR] Factory image exceeds 656KB.
    exit /b 1
)

echo [INFO] Flashing factory image at 0x08158000...
"%CLI%" -c port=SWD mode=UR reset=SWrst -w "%ROOT%build\GatewayV2_factory.bin" 0x08158000 -v -rst
if %ERRORLEVEL% EQU 0 (echo [SUCCESS] Factory image flashed successfully!) else (echo [ERROR] Factory flash failed!)
endlocal
