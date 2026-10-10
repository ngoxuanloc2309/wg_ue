@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Flash ST67W611 Mission T01 directly through a 3.3 V USB-TTL adapter.
rem BOOT and CHIP_EN must be controlled manually; STM32 is not used.

set "ROOT=%~dp0.."
set "ST=%ROOT%\libs\x-cube-st67w61\Projects\ST67W6X_Scripts\Binaries"
set "QC_EXE=%ST%\QConn_Flash\QConn_Flash_Cmd.exe"
set "NCP_DIR=%ST%\NCP_Binaries"
set "TEMPLATE=%NCP_DIR%\mission_t01_flash_prog_cfg.ini"
set "AUTO_CONFIG=%NCP_DIR%\mission_t01_flash_prog_cfg_gateway_loader.ini"
set "COMPORT=%~1"
set "VERSION=%~2"
set "BAUDRATE=%~3"

if not defined COMPORT set /p "COMPORT=Enter ST67 UART COM port (for example COM5): "
if not defined COMPORT (
    echo [ERROR] No COM port specified.
    exit /b 1
)
if not defined VERSION set "VERSION=2.0.106"
if not defined BAUDRATE set "BAUDRATE=2000000"
set "FW=st67w611m_mission_t01_v%VERSION%.bin"

if not exist "%QC_EXE%" (
    echo [ERROR] QConn_Flash not found: "%QC_EXE%"
    exit /b 1
)
if not exist "%TEMPLATE%" (
    echo [ERROR] QConn_Flash template not found: "%TEMPLATE%"
    exit /b 1
)
if not exist "%NCP_DIR%\%FW%" (
    echo [ERROR] ST67 firmware not found: "%NCP_DIR%\%FW%"
    exit /b 1
)
if not exist "%NCP_DIR%\efusedata.bin" (
    echo [ERROR] efusedata.bin not found in "%NCP_DIR%"
    exit /b 1
)

echo [INFO] Using direct USB-TTL COM port: %COMPORT%
echo [INFO] Firmware: %FW%
echo [INFO] Profile: Mission T01
echo [INFO] STM32/Gateway NCP_Loader is not used.
echo [INFO] USB-TTL must be 3.3 V with common GND.
echo [INFO] Sequence: BOOT high, pulse CHIP_EN low/high, then release BOOT.
echo [INFO] Do not open another terminal on %COMPORT% during flashing.

powershell -NoProfile -ExecutionPolicy Bypass -Command "$template='%TEMPLATE%'; $output='%AUTO_CONFIG%'; $fw='%FW%'; $lines=Get-Content -LiteralPath $template | ForEach-Object { if ($_ -match '^filedir\s*=\s*\./st67w611m_mission_t01_v[^\s]*\.bin\s*$') { 'filedir = ./' + $fw } else { $_ } }; Set-Content -LiteralPath $output -Value $lines -Encoding ascii"
if errorlevel 1 goto :error
findstr /l /c:"%FW%" "%AUTO_CONFIG%" >nul
if errorlevel 1 (
    echo [ERROR] Generated QConn_Flash config does not reference %FW%.
    goto :error
)

echo [INFO] Starting QConn_Flash at %BAUDRATE% baud...
set "QCONN_LOG=%TEMP%\st67_qconn_flash_t01.log"
"%QC_EXE%" --port %COMPORT% --baudrate %BAUDRATE% --config "%AUTO_CONFIG%" --efuse="%NCP_DIR%\efusedata.bin" >"%QCONN_LOG%" 2>&1
type "%QCONN_LOG%"
findstr /i /c:"Error:" /c:"ErrorCode" /c:"failed" "%QCONN_LOG%" >nul
if not errorlevel 1 goto :error

echo [SUCCESS] ST67W611 Mission T01 firmware flashed successfully.
del /q "%AUTO_CONFIG%" >nul 2>&1
exit /b 0

:error
echo [ERROR] ST67W611 Mission T01 flashing failed.
del /q "%AUTO_CONFIG%" >nul 2>&1
exit /b 1
