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
        echo [ERROR] CMake is not installed or not added to your system PATH!
        exit /b 1
    )
)

echo [INFO] Building Bootloader...
pushd "%~dp0"

rem STM32Cube installs Ninja separately from CMake. Add it when available so
rem the Release preset can configure on a clean machine.
if exist "C:\Users\DELL\AppData\Local\stm32cube\bundles\ninja\1.13.1+st.1\bin" (
    set "PATH=C:\Users\DELL\AppData\Local\stm32cube\bundles\ninja\1.13.1+st.1\bin;%PATH%"
)

rem Configure the bootloader in its own build tree. Do not reuse the
rem application build cache: it has a different target and linker layout.
"%CMAKE_CMD%" --preset Release
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Bootloader CMake configure failed!
    popd
    exit /b 1
)

"%CMAKE_CMD%" --build "build\Release" --target Bootloader -j 4

if %ERRORLEVEL% EQU 0 (
    if not exist "..\build" mkdir "..\build"
    copy /Y "build\Release\Bootloader.bin" "..\build\Bootloader.bin" >nul
    if %ERRORLEVEL% NEQ 0 (
        echo [ERROR] Could not copy Bootloader.bin to the flash script output path!
        popd
        exit /b 1
    )
    echo [SUCCESS] Bootloader build completed successfully!
    echo [INFO] Output: %~dp0build\Release\Bootloader.bin
    echo [INFO] Flash input: %~dp0..\build\Bootloader.bin
) else (
    echo [ERROR] Bootloader build failed!
)

popd
endlocal
