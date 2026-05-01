@echo off
setlocal

set "JAVA_HOME=C:\Program Files\Microsoft\jdk-21.0.10.7-hotspot"
set "ANDROID_HOME=C:\Users\jongh\AppData\Local\Android\Sdk"
set "ANDROID_SDK_ROOT=%ANDROID_HOME%"
set "NDKROOT=%ANDROID_HOME%\ndk\27.2.12479018"
set "NDK_ROOT=%NDKROOT%"
set "PATH=%JAVA_HOME%\bin;%ANDROID_HOME%\platform-tools;%PATH%"

set "UE_ROOT=C:\Program Files\Epic Games\UE_5.7"
set "PROJECT=C:\projects\UnrealEngine\VirtualWorld_2026-1\HandTrackingDemo.uproject"
set "DEVICE=2G0YC1ZF9K0PX5"

echo [BUILD] JAVA_HOME=%JAVA_HOME%
echo [BUILD] NDKROOT=%NDKROOT%
echo [BUILD] DEVICE=%DEVICE%
echo [BUILD] starting RunUAT BuildCookRun...

call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" ^
  BuildCookRun ^
  -project="%PROJECT%" ^
  -platform=Android ^
  -clientconfig=Development ^
  -targetplatform=Android ^
  -cookflavor=ASTC ^
  -build -cook -stage -package -pak -deploy ^
  -device=%DEVICE% ^
  -utf8output

set EXITCODE=%ERRORLEVEL%
echo [BUILD] RunUAT exit code: %EXITCODE%
endlocal & exit /b %EXITCODE%
