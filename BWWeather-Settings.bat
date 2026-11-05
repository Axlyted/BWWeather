@echo off
rem Opens the BWWeather settings window: pick your town and check that it works.
start "" powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0BWWeather-Settings.ps1"
