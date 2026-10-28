@echo off
setlocal
cd /d "%~dp0"
set "buildConfig=%~1"
if "%buildConfig%"=="" set "buildConfig=Release"
set "buildPlatform=%~2"
if "%buildPlatform%"=="" set "buildPlatform=Win32"
set "buildArch=x86"
if /i "%buildPlatform%"=="x64" (
  set "buildPlatform=x64"
  set "buildArch=x86_amd64"
) else if /i "%buildPlatform%"=="Win32" (
  set "buildPlatform=Win32"
) else (
  echo Usage: build.cmd [Debug^|Release] [Win32^|x64]
  exit /b 1
)
if exist "%VS100COMNTOOLS%..\..\VC\vcvarsall.bat" (
  call "%VS100COMNTOOLS%..\..\VC\vcvarsall.bat" %buildArch%
) else (
  call "%ProgramFiles(x86)%\Microsoft Visual Studio 10.0\VC\vcvarsall.bat" %buildArch%
)
if errorlevel 1 exit /b 1
"%WINDIR%\Microsoft.NET\Framework\v4.0.30319\MSBuild.exe" FtpUploaderTool.sln /m /t:Build /p:Configuration=%buildConfig% /p:Platform=%buildPlatform% /v:minimal /nologo
exit /b %errorlevel%
