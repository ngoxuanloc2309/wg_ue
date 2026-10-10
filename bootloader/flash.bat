@echo off
setlocal

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

if not exist "..\build\Bootloader.bin" (
    echo [ERROR] Missing firmware image: ..\build\Bootloader.bin
    echo [ERROR] Please run build.bat first to compile the project.
    exit /b 1
)

echo [INFO] Using programmer: "%CLI%"
for %%F in ("..\build\Bootloader.bin") do echo [INFO] Bootloader.bin size: %%~zF bytes
for /f "tokens=*" %%H in ('certutil -hashfile "..\build\Bootloader.bin" SHA256 ^| findstr /R /V "hash CertUtil"') do echo [INFO] Bootloader.bin SHA256: %%H
echo [INFO] Flashing current Bootloader.bin to STM32H5 at 0x08000000...
rem Use hardware reset with Under-Reset attach. Do not combine UR with SWrst:
rem the latter can leave CubeProgrammer unable to attach to a target that is
rem continuously resetting or immediately jumping to the application.
"%CLI%" -c port=SWD mode=UR freq=1000 reset=HWrst -w "..\build\Bootloader.bin" 0x08000000 -v -rst

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] Flash completed successfully!
) else (
    echo [ERROR] Flash failed!
)

endlocal
