@echo off
setlocal EnableExtensions

rem Flash ST67 through the Gateway STM32 AUTO loader.
rem The STM32 must run GatewayV2_NCP_Loader_Auto.bin first.

set "ROOT=%~dp0.."
set "ST=%ROOT%\libs\x-cube-st67w61\Projects\ST67W6X_Scripts\Binaries"
set "QC_EXE=%ST%\QConn_Flash\QConn_Flash_Cmd.exe"
set "NCP_DIR=%ST%\NCP_Binaries"
set "TEMPLATE=%NCP_DIR%\mission_t02_flash_prog_cfg.ini"
set "AUTO_CONFIG=%NCP_DIR%\mission_t02_flash_prog_cfg_gateway_auto.ini"
set "COMPORT=%~1"
set "VERSION=%~2"
set "BAUDRATE=%~3"

if not defined COMPORT set /p "COMPORT=Enter Gateway debug COM port (for example COM26): "
if not defined VERSION set "VERSION=2.0.106"
if not defined BAUDRATE set "BAUDRATE=2000000"
set "FW=st67w611m_mission_t02_v%VERSION%.bin"

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
    echo [ERROR] efusedata.bin not found: "%NCP_DIR%\efusedata.bin"
    exit /b 1
)

echo [INFO] Using Gateway AUTO loader on %COMPORT%
echo [INFO] Firmware: %FW%
echo [INFO] Baudrate: %BAUDRATE%
echo [INFO] Do not open another terminal on %COMPORT%.

powershell -NoProfile -ExecutionPolicy Bypass -Command "$template='%TEMPLATE%'; $output='%AUTO_CONFIG%'; $fw='%FW%'; $lines=Get-Content -LiteralPath $template | ForEach-Object { if ($_ -match '^filedir\s*=\s*\./st67w611m_mission_t02_v[^\s]*\.bin\s*$') { 'filedir = ./' + $fw } else { $_ } }; Set-Content -LiteralPath $output -Value $lines -Encoding ascii"
if errorlevel 1 goto :error

mode %COMPORT% baud=%BAUDRATE% parity=N data=8 stop=1 >nul
if errorlevel 1 (
    echo [ERROR] Cannot configure %COMPORT%.
    goto :error
)
timeout /t 1 /nobreak >nul
mode %COMPORT% baud=%BAUDRATE% parity=N data=8 stop=1 >nul

set "LOG=%TEMP%\st67_qconn_auto.log"
set /a RETRY=0
:run_qconn
echo [INFO] Starting QConn_Flash (attempt %RETRY%)...
"%QC_EXE%" --interface uart --port "%COMPORT%" --baudrate "%BAUDRATE%" --config "%AUTO_CONFIG%" --efuse="%NCP_DIR%\efusedata.bin" >"%LOG%" 2>&1
set "RC=%ERRORLEVEL%"
type "%LOG%"
if "%RC%"=="0" goto :success
if %RETRY% GEQ 1 goto :error
set /a RETRY+=1
timeout /t 1 /nobreak >nul
goto :run_qconn

:success
echo [SUCCESS] ST67W611 firmware flashed successfully.
del /q "%AUTO_CONFIG%" >nul 2>&1
exit /b 0

:error
echo [ERROR] ST67W611 AUTO flashing failed.
del /q "%AUTO_CONFIG%" >nul 2>&1
exit /b 1
