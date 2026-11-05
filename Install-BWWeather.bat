@echo off
rem Double-click to install BWWeather (live weather for Black & White).
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-BWWeather.ps1" %*
pause
