@echo off
rem ===========================================================================
rem build_gw.bat - build / flash / clean helper for GatewayV2 (STM32H563).
rem
rem Usage:   build_gw.bat [command]
rem
rem   build      Configure (if needed) and build            [default]
rem   flash      Flash build\GatewayV2.bin at the app slot address
rem   all        build, then flash
rem   clean      Remove build outputs, keep the CMake configuration
rem   distclean  Delete the whole build\ directory
rem   rebuild    distclean, then build
rem   config     Force a fresh CMake configure
rem   size       Print section sizes of build\GatewayV2.elf
rem   help       Show this text
rem
rem Optional environment variables:
rem   BUILD_TYPE             Release (default) or Debug
rem   JOBS                   Parallel jobs (default: number of processors)
rem   STM32_PROGRAMMER_CLI   Full path of STM32_Programmer_CLI.exe
rem
rem Requirements in PATH: cmake, ninja, arm-none-eabi-gcc.
rem The programmer is searched in: STM32_PROGRAMMER_CLI, PATH, the default
rem STM32CubeProgrammer folder and the STM32CubeCLT folder that contains the
rem compiler found in PATH.
rem ===========================================================================
setlocal EnableExtensions

set "ROOT=%~dp0"
cd /d "%ROOT%"

set "BUILD_DIR=%ROOT%build"
set "ELF_FILE=%BUILD_DIR%\GatewayV2.elf"
set "BIN_FILE=%BUILD_DIR%\GatewayV2.bin"

rem Application slot (same values as flash.bat): 656 KB at 0x08010000.
set "APP_ADDR=0x08010000"
set "APP_MAX_SIZE=671744"

if not defined BUILD_TYPE set "BUILD_TYPE=Release"
if not defined JOBS set "JOBS=%NUMBER_OF_PROCESSORS%"

rem Same project options as build.bat.
set "PROJECT_OPTS=-DST67_SPI_LOOPBACK=OFF -DST67W611_ARCH=T02 -DGW_USB_NETIF_ENABLE=OFF -DGW_MDNS_ENABLE=ON"

set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=build"

if /i "%ACTION%"=="build"     goto :cmd_build
if /i "%ACTION%"=="flash"     goto :cmd_flash
if /i "%ACTION%"=="all"       goto :cmd_all
if /i "%ACTION%"=="clean"     goto :cmd_clean
if /i "%ACTION%"=="distclean" goto :cmd_distclean
if /i "%ACTION%"=="rebuild"   goto :cmd_rebuild
if /i "%ACTION%"=="config"    goto :cmd_config
if /i "%ACTION%"=="size"      goto :cmd_size
if /i "%ACTION%"=="help"      goto :cmd_help
if /i "%ACTION%"=="-h"        goto :cmd_help
if /i "%ACTION%"=="/?"        goto :cmd_help

echo [ERROR] Unknown command: %ACTION%
echo.
goto :cmd_help_fail

rem ---------------------------------------------------------------------------
:cmd_help
call :print_help
endlocal & exit /b 0

:cmd_help_fail
call :print_help
endlocal & exit /b 1

:cmd_build
call :build_project
set "RESULT=%ERRORLEVEL%"
endlocal & exit /b %RESULT%

:cmd_flash
call :flash_project
set "RESULT=%ERRORLEVEL%"
endlocal & exit /b %RESULT%

:cmd_all
call :build_project
if errorlevel 1 (
    endlocal & exit /b 1
)
call :flash_project
set "RESULT=%ERRORLEVEL%"
endlocal & exit /b %RESULT%

:cmd_clean
call :clean_project
set "RESULT=%ERRORLEVEL%"
endlocal & exit /b %RESULT%

:cmd_distclean
call :distclean_project
set "RESULT=%ERRORLEVEL%"
endlocal & exit /b %RESULT%

:cmd_rebuild
call :distclean_project
if errorlevel 1 (
    endlocal & exit /b 1
)
call :build_project
set "RESULT=%ERRORLEVEL%"
endlocal & exit /b %RESULT%

:cmd_config
call :check_tools
if errorlevel 1 (
    endlocal & exit /b 1
)
call :configure_project
set "RESULT=%ERRORLEVEL%"
endlocal & exit /b %RESULT%

:cmd_size
if not exist "%ELF_FILE%" (
    echo [ERROR] Missing %ELF_FILE%. Run "build_gw.bat build" first.
    endlocal & exit /b 1
)
arm-none-eabi-size "%ELF_FILE%"
set "RESULT=%ERRORLEVEL%"
endlocal & exit /b %RESULT%

rem ---------------------------------------------------------------------------
:print_help
echo Usage: build_gw.bat [command]
echo.
echo   build      Configure (if needed) and build            [default]
echo   flash      Flash build\GatewayV2.bin at %APP_ADDR%
echo   all        build, then flash
echo   clean      Remove build outputs, keep the CMake configuration
echo   distclean  Delete the whole build\ directory
echo   rebuild    distclean, then build
echo   config     Force a fresh CMake configure
echo   size       Print section sizes of build\GatewayV2.elf
echo   help       Show this text
echo.
echo Environment: BUILD_TYPE (Release^|Debug), JOBS, STM32_PROGRAMMER_CLI
exit /b 0

rem ---------------------------------------------------------------------------
:check_tools
where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cmake not found in PATH.
    exit /b 1
)
where ninja >nul 2>&1
if errorlevel 1 (
    echo [ERROR] ninja not found in PATH.
    exit /b 1
)
where arm-none-eabi-gcc >nul 2>&1
if errorlevel 1 (
    echo [ERROR] arm-none-eabi-gcc not found in PATH.
    exit /b 1
)
exit /b 0

rem ---------------------------------------------------------------------------
:configure_project
echo [INFO] Configuring CMake (%BUILD_TYPE%)...
cmake -S . -B "%BUILD_DIR%" -G Ninja ^
    -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake ^
    %PROJECT_OPTS% ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
if errorlevel 1 (
    echo [ERROR] CMake configuration failed!
    exit /b 1
)
exit /b 0

rem ---------------------------------------------------------------------------
:build_project
call :check_tools
if errorlevel 1 exit /b 1

rem Reconfigure only when there is no usable configuration yet.
if not exist "%BUILD_DIR%\build.ninja" (
    call :configure_project
    if errorlevel 1 exit /b 1
)

echo [INFO] Building GatewayV2 with %JOBS% jobs...
cmake --build "%BUILD_DIR%" -j %JOBS%
if errorlevel 1 (
    echo [ERROR] Build failed!
    exit /b 1
)

echo [SUCCESS] Build completed: %BIN_FILE%
exit /b 0

rem ---------------------------------------------------------------------------
:clean_project
if not exist "%BUILD_DIR%\build.ninja" (
    echo [INFO] Nothing to clean: no CMake configuration in build\
    exit /b 0
)
echo [INFO] Cleaning build outputs...
cmake --build "%BUILD_DIR%" --target clean
if errorlevel 1 (
    echo [ERROR] Clean failed!
    exit /b 1
)
echo [SUCCESS] Clean completed.
exit /b 0

rem ---------------------------------------------------------------------------
:distclean_project
if not exist "%BUILD_DIR%" (
    echo [INFO] Nothing to delete: build\ does not exist.
    exit /b 0
)
echo [INFO] Deleting %BUILD_DIR% ...
rmdir /s /q "%BUILD_DIR%"
if exist "%BUILD_DIR%" (
    echo [ERROR] Could not delete build\ - close any program using its files.
    exit /b 1
)
echo [SUCCESS] build\ deleted.
exit /b 0

rem ---------------------------------------------------------------------------
:find_programmer
set "CLI="
if defined STM32_PROGRAMMER_CLI (
    if exist "%STM32_PROGRAMMER_CLI%" set "CLI=%STM32_PROGRAMMER_CLI%"
)
if defined CLI exit /b 0

for /f "delims=" %%P in ('where STM32_Programmer_CLI 2^>nul') do (
    if not defined CLI set "CLI=%%P"
)
if defined CLI exit /b 0

set "DEFAULT_CLI=C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
if exist "%DEFAULT_CLI%" set "CLI=%DEFAULT_CLI%"
if defined CLI exit /b 0

rem STM32CubeCLT layout: <CLT>\GNU-tools-for-STM32\bin (compiler) and
rem <CLT>\STM32CubeProgrammer\bin (programmer).
for /f "delims=" %%G in ('where arm-none-eabi-gcc 2^>nul') do (
    if not defined CLI call :try_cubeclt "%%~dpG"
)
if defined CLI exit /b 0
exit /b 1

:try_cubeclt
set "CLT_CANDIDATE=%~1..\..\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
if exist "%CLT_CANDIDATE%" (
    for %%X in ("%CLT_CANDIDATE%") do set "CLI=%%~fX"
)
exit /b 0

rem ---------------------------------------------------------------------------
:flash_project
if not exist "%BIN_FILE%" (
    echo [ERROR] Missing firmware image: %BIN_FILE%
    echo [ERROR] Run "build_gw.bat build" first.
    exit /b 1
)

call :find_programmer
if errorlevel 1 (
    echo [ERROR] STM32_Programmer_CLI.exe not found.
    echo [ERROR] Install STM32CubeProgrammer or set STM32_PROGRAMMER_CLI to its full path.
    exit /b 1
)

set "BIN_SIZE=0"
for %%F in ("%BIN_FILE%") do set "BIN_SIZE=%%~zF"
if %BIN_SIZE% GTR %APP_MAX_SIZE% (
    echo [ERROR] Application image is %BIN_SIZE% bytes, larger than the 656 KB slot ^(%APP_MAX_SIZE%^).
    exit /b 1
)

echo [INFO] Using programmer: "%CLI%"
echo [INFO] Flashing GatewayV2.bin ^(%BIN_SIZE% bytes^) at %APP_ADDR%...
"%CLI%" -c port=SWD mode=UR reset=SWrst -w "%BIN_FILE%" %APP_ADDR% -v -rst
if errorlevel 1 (
    echo [ERROR] Flash failed!
    exit /b 1
)

echo [SUCCESS] Flash completed successfully!
exit /b 0