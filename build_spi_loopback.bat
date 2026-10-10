@echo off
setlocal
set "CMAKE_CMD=cmake"
where cmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    if exist "C:\Program Files\CMake\bin\cmake.exe" (
        set "CMAKE_CMD=C:\Program Files\CMake\bin\cmake.exe"
    ) else if exist "C:\Users\DELL\AppData\Local\stm32cube\bundles\cmake\4.0.1+st.3\bin\cmake.exe" (
        set "CMAKE_CMD=C:\Users\DELL\AppData\Local\stm32cube\bundles\cmake\4.0.1+st.3\bin\cmake.exe"
    ) else (
        echo [ERROR] CMake was not found.
        exit /b 1
    )
)
echo [INFO] Configuring standalone STM32 SPI loopback build...
"%CMAKE_CMD%" -S . -B build -G "Unix Makefiles" ^
  -DCMAKE_MAKE_PROGRAM="C:\MinGW\bin\make.exe" ^
  -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake ^
  -DST67_SPI_LOOPBACK=ON -DST67_SPI_DIAG_POLLING=OFF
if errorlevel 1 exit /b 1
echo [INFO] Building GatewayV2 loopback image...
"%CMAKE_CMD%" --build build --target GatewayV2 -j 4
if errorlevel 1 exit /b 1
echo [SUCCESS] Loopback image: build\GatewayV2.bin
echo [INFO] This image disables the ST67 startup task and requires ST67_EN LOW.
