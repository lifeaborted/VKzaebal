@echo off
setlocal enabledelayedexpansion

set ARCH=%1
if "%ARCH%"=="" set ARCH=x86_64
if "%ARCH%"=="arm64" set ARCH=arm64_v8a

set CONFIG=%2
if "%CONFIG%"=="" set CONFIG=debug

if /I "%CONFIG%"=="release" (
    set CMAKE_CONFIG=Release
) else (
    set CMAKE_CONFIG=Debug
)

set PATH=C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;C:\Users\danya\AppData\Local\Android\Sdk\platform-tools;%PATH%
if exist "C:\Program Files\Android\Android Studio\jbr" (
    set "JAVA_HOME=C:\Program Files\Android\Android Studio\jbr"
) else if exist "C:\Program Files\Java\jdk-20" (
    set "JAVA_HOME=C:\Program Files\Java\jdk-20"
)

set QT_DIR=C:\Qt\6.11.1\android_%ARCH%
set ANDROID_SDK=C:\Users\danya\AppData\Local\Android\Sdk
set ANDROID_NDK=C:\Users\danya\AppData\Local\Android\Sdk\ndk\30.0.16248370
set BUILD_DIR=cmake-build-android-%ARCH%

echo ========================================================
echo Building VKAudioPlayer for Android [%ARCH%] [%CMAKE_CONFIG%]
echo Qt: %QT_DIR%
echo NDK: %ANDROID_NDK%
echo Output: %BUILD_DIR%
echo ========================================================

if not exist "%BUILD_DIR%" (
    mkdir "%BUILD_DIR%"
)

if not exist "3rdparty\android_openssl" (
    echo [INFO] Cloning Android OpenSSL prebuilts...
    git clone --depth 1 https://github.com/KDAB/android_openssl.git 3rdparty\android_openssl
)

call "%QT_DIR%\bin\qt-cmake.bat" -B "%BUILD_DIR%" -S . ^
    -G Ninja ^
    -DCMAKE_BUILD_TYPE=%CMAKE_CONFIG% ^
    -DANDROID_SDK_ROOT="%ANDROID_SDK%" ^
    -DANDROID_NDK_ROOT="%ANDROID_NDK%" ^
    -DENABLE_FTXUI=OFF

if errorlevel 1 (
    echo [ERROR] CMake configuration failed!
    exit /b 1
)

if "%3"=="--config-only" (
    echo [INFO] Config-only requested. Exiting successfully.
    exit /b 0
)

"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build "%BUILD_DIR%" --target VKAudioPlayer_make_apk --parallel

if errorlevel 1 (
    echo [ERROR] Build failed!
    exit /b 1
)

echo ========================================================
echo [SUCCESS] Android APK built successfully in %BUILD_DIR%!
echo ========================================================
