@echo off
setlocal
set "ROOT=%~dp0"
cd /d "%ROOT%"
set "CMAKE_CMD=cmake"
where cmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    if exist "C:\Program Files\CMake\bin\cmake.exe" (
        set "CMAKE_CMD=C:\Program Files\CMake\bin\cmake.exe"
    ) else if exist "C:\Users\DELL\AppData\Local\stm32cube\bundles\cmake\4.0.1+st.3\bin\cmake.exe" (
        set "CMAKE_CMD=C:\Users\DELL\AppData\Local\stm32cube\bundles\cmake\4.0.1+st.3\bin\cmake.exe"
    ) else (
        echo [ERROR] CMake is not installed or not added to your system PATH!
        echo [HINT] Please install CMake from https://cmake.org/download/ 
        echo [HINT] Make sure to check the box "Add CMake to the system PATH for all users" during installation.
        exit /b 1
    )
)

set "NINJA_DIR=C:\Users\DELL\AppData\Local\stm32cube\bundles\ninja\1.13.1+st.1\bin"
if exist "%NINJA_DIR%\ninja.exe" set "PATH=%NINJA_DIR%;%PATH%"

echo [INFO] Configuring CMake...
if not exist build mkdir build
"%CMAKE_CMD%" -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA_DIR%\ninja.exe" -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DST67_SPI_LOOPBACK=OFF -DST67W611_ARCH=T02 -DGW_USB_NETIF_ENABLE=OFF -DGW_MDNS_ENABLE=ON -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake configuration failed!
    exit /b %ERRORLEVEL%
)

echo [INFO] Building GatewayV2...
"%CMAKE_CMD%" --build build -j 4
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed!
    exit /b %ERRORLEVEL%
)

echo [SUCCESS] Build completed successfully!
echo [SUCCESS] Output: %ROOT%build\GatewayV2.bin
endlocal
