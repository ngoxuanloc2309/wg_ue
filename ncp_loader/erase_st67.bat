@echo off
setlocal EnableExtensions

rem Explicitly erase the complete ST67W611 flash before programming.
rem Usage: erase_st67.bat COM26 [baudrate]

set "ROOT=%~dp0.."
set "ST=%ROOT%\libs\x-cube-st67w61\Projects\ST67W6X_Scripts\Binaries"
set "QC_EXE=%ST%\QConn_Flash\QConn_Flash_Cmd.exe"
set "NCP_DIR=%ST%\NCP_Binaries"
set "COMPORT=%~1"
set "BAUDRATE=%~2"

if not defined COMPORT set /p "COMPORT=Enter direct USB-TTL COM port (for example COM26): "
if not defined BAUDRATE set "BAUDRATE=2000000"

if not defined COMPORT (
    echo [ERROR] No COM port specified.
    exit /b 1
)

if not exist "%QC_EXE%" (
    echo [ERROR] QConn_Flash not found: "%QC_EXE%"
    exit /b 1
)
echo.
echo ================================================================
echo WARNING: This will erase the complete ST67W611 flash.
echo Port: %COMPORT%   Baud: %BAUDRATE%
echo ================================================================
choice /c YN /n /m "Press Y to erase or N to abort: "
if errorlevel 2 exit /b 1

echo [INFO] Erasing ST67W611 whole chip...
set "LOG=%TEMP%\st67_qconn_erase.log"
rem Match the ST Community workaround: no firmware/config/efuse.
rem Explicitly select UART because this is a direct USB-TTL connection.
"%QC_EXE%" --interface uart --port "%COMPORT%" --baudrate "%BAUDRATE%" --erase --whole_chip >"%LOG%" 2>&1
set "QCONN_RC=%ERRORLEVEL%"
type "%LOG%"

if not "%QCONN_RC%"=="0" (
    echo [ERROR] QConn_Flash exited with code %QCONN_RC%.
    goto :error
)
findstr /i /c:"Error:" /c:"ErrorCode" /c:"failed" "%LOG%" >nul
if not errorlevel 1 (
    echo [ERROR] ST67W611 whole-chip erase failed.
    goto :error
)

echo [SUCCESS] ST67W611 whole-chip erase completed.
exit /b 0

:error
exit /b 1
