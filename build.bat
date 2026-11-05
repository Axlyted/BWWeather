@echo off
rem Builds bin\LHMultiplayerR.dll (32-bit). Needs Visual Studio Build Tools with the C++ workload.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS="
if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS set "VS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools"
call "%VS%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || (echo Visual Studio C++ tools not found & exit /b 1)
cd /d "%~dp0"
if not exist bin mkdir bin
if not exist obj mkdir obj
cl /nologo /O2 /MT /W3 /LD src\bwweather.c /Fo:obj\ /Fe:bin\LHMultiplayerR.dll /link /DEF:src\LHMultiplayerR.def /IMPLIB:obj\LHMultiplayerR.lib winhttp.lib user32.lib advapi32.lib kernel32.lib || exit /b 1
echo "Built %~dp0bin\LHMultiplayerR.dll"
