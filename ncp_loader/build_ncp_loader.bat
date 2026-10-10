@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "BUILD=%ROOT%\build\ncp_loader"
set "TOOLCHAIN=%ROOT%\ncp_loader\cmake\gcc-arm-none-eabi.cmake"
set "CMAKE="
set "NINJA="
set "OBJCOPY="

where cmake >nul 2>&1
if not errorlevel 1 set "CMAKE=cmake"
if not defined CMAKE if exist "C:\Users\DELL\AppData\Local\stm32cube\bundles\cmake\4.0.1+st.3\bin\cmake.exe" set "CMAKE=C:\Users\DELL\AppData\Local\stm32cube\bundles\cmake\4.0.1+st.3\bin\cmake.exe"
if not defined CMAKE if exist "C:\Program Files\CMake\bin\cmake.exe" set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
if not defined CMAKE (
    echo [ERROR] CMake not found.
    exit /b 1
)

where ninja >nul 2>&1
if not errorlevel 1 set "NINJA=ninja"
if not defined NINJA if exist "C:\Users\DELL\AppData\Local\stm32cube\bundles\ninja\1.13.1+st.1\bin\ninja.exe" set "NINJA=C:\Users\DELL\AppData\Local\stm32cube\bundles\ninja\1.13.1+st.1\bin\ninja.exe"
if not defined NINJA (
    echo [ERROR] Ninja not found.
    exit /b 1
)

where arm-none-eabi-objcopy >nul 2>&1
if not errorlevel 1 set "OBJCOPY=arm-none-eabi-objcopy"
if not defined OBJCOPY if exist "C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin\arm-none-eabi-objcopy.exe" set "OBJCOPY=C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin\arm-none-eabi-objcopy.exe"
if not defined OBJCOPY (
    echo [ERROR] arm-none-eabi-objcopy not found.
    exit /b 1
)

if not exist "%TOOLCHAIN%" (
    echo [ERROR] Toolchain file not found: "%TOOLCHAIN%"
    exit /b 1
)

echo [INFO] Configuring GatewayV2 NCP Loader...
"%CMAKE%" -S "%ROOT%\ncp_loader" -B "%BUILD%" -G Ninja ^
  "-DCMAKE_BUILD_TYPE=Release" ^
  "-DCMAKE_C_FLAGS_RELEASE=-Ofast -g0" ^
  "-DCMAKE_TOOLCHAIN_FILE=%TOOLCHAIN%" ^
  "-DCMAKE_MAKE_PROGRAM=%NINJA%"
if errorlevel 1 goto :error

echo [INFO] Building NCP_Loader...
"%CMAKE%" --build "%BUILD%" --parallel 4
if errorlevel 1 goto :error

echo [INFO] Creating binary image...
"%OBJCOPY%" -O binary "%BUILD%\NCP_Loader.elf" "%BUILD%\GatewayV2_NCP_Loader.bin"
if errorlevel 1 goto :error

echo [SUCCESS] Image: "%BUILD%\GatewayV2_NCP_Loader.bin"
exit /b 0

:error
echo [ERROR] NCP Loader build failed.
exit /b 1
