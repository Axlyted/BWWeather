@echo off
rem Double-click to remove BWWeather and put the game's original LHMultiplayerR.dll back.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-BWWeather.ps1" -Uninstall %*
pause
