@echo off
setlocal
cd /d "%~dp0"
if exist "%VS100COMNTOOLS%..\..\VC\vcvarsall.bat" (
  call "%VS100COMNTOOLS%..\..\VC\vcvarsall.bat" x86
) else (
  call "%ProgramFiles(x86)%\Microsoft Visual Studio 10.0\VC\vcvarsall.bat" x86
)
if errorlevel 1 exit /b 1
set "buildConfig=%~1"
if "%buildConfig%"=="" set "buildConfig=Release"
"%WINDIR%\Microsoft.NET\Framework\v4.0.30319\MSBuild.exe" FtpUploaderTool.sln /m /t:Build /p:Configuration=%buildConfig% /p:Platform=Win32 /v:minimal /nologo
exit /b %errorlevel%
