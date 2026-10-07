@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if "%1"=="release" (
    "C:\Qt\Tools\CMake_64\bin\cmake.exe" --build "c:\others\Codes\audio player\cmake-build-release" --target VKAudioPlayer --parallel
) else (
    "C:\Qt\Tools\CMake_64\bin\cmake.exe" --build "c:\others\Codes\audio player\cmake-build-debug" --target VKAudioPlayer --parallel
)
