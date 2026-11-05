<#
.SYNOPSIS
    Installs (or removes) BWWeather: live weather for Black & White (2001).

.DESCRIPTION
    Renames the game's LHMultiplayerR.dll to LHMultiplayerR_orig.dll and puts
    BWWeather's LHMultiplayerR.dll in its place. BWWeather forwards everything
    to the original and only answers the game's requests to its old weather
    server, with the current weather of the town in bwweather.ini (from
    Open-Meteo). Also sets UseInternetWeatherSystem = 0 in the game's
    settings, which is what turns the game's weather feature on.

.PARAMETER GameDir
    The Black & White folder. Default: the folder above this script, then the
    GameDir value in the registry.

.PARAMETER City
    Your town, written to bwweather.ini (optional, you can edit the file later).

.PARAMETER Uninstall
    Puts the original LHMultiplayerR.dll back.

.PARAMETER Force
    Install even if LHMultiplayerR.dll is not the known v1.42 file.
#>
[CmdletBinding()]
param(
    [string]$GameDir,
    [string]$City,
    [switch]$Uninstall,
    [switch]$Force
)
$ErrorActionPreference = 'Stop'

# SHA-256 of LHMultiplayerR.dll from Black & White v1.42 (the unofficial patch keeps it)
$KnownOriginal = '7B6E099AFA2B9F29B4D2A93D2B5810E4B88397D1545783E7A98F43BE409EA1D2'
$RegGame  = 'HKCU:\Software\Lionhead Studios Ltd\Black & White'
$RegSetup = "$RegGame\BWSetup"

function Step($m) { Write-Host "==> $m" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "    $m" -ForegroundColor Green }
function Warn($m) { Write-Host "    $m" -ForegroundColor Yellow }

if (-not $GameDir) {
    $parent = Split-Path -Parent $PSScriptRoot
    if (Test-Path (Join-Path $parent 'runblack.exe')) { $GameDir = $parent }
    elseif (Test-Path $RegGame) { $GameDir = (Get-ItemProperty $RegGame -ErrorAction SilentlyContinue).GameDir }
}
if (-not $GameDir -or -not (Test-Path (Join-Path $GameDir 'runblack.exe'))) {
    throw "Could not find runblack.exe. Run again with -GameDir `"<your Black & White folder>`"."
}
$GameDir = (Resolve-Path $GameDir).Path
Step "Game folder: $GameDir"
if (Get-Process runblack -ErrorAction SilentlyContinue) { throw 'Black & White is running. Close it first.' }

# administrator rights if the game folder is protected
$probe = Join-Path $GameDir ('.bww_' + [guid]::NewGuid().ToString('N'))
try { [IO.File]::WriteAllText($probe, ''); Remove-Item $probe -Force }
catch {
    $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if ($isAdmin) { throw "No write access to $GameDir even as administrator." }
    Step 'The game folder needs administrator rights - asking Windows to elevate...'
    $a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"", '-GameDir', "`"$GameDir`"")
    if ($City)      { $a += @('-City', "`"$City`"") }
    if ($Uninstall) { $a += '-Uninstall' }
    if ($Force)     { $a += '-Force' }
    Start-Process powershell.exe -Verb RunAs -ArgumentList $a -Wait
    return
}

$dll     = Join-Path $GameDir 'LHMultiplayerR.dll'
$origDll = Join-Path $GameDir 'LHMultiplayerR_orig.dll'
$ini     = Join-Path $GameDir 'bwweather.ini'

function Test-IsBWWeather([string]$Path) {
    # BWWeather's DLL exports BWWeatherVersion; the original does not
    $bytes = [IO.File]::ReadAllBytes($Path)
    return ([Text.Encoding]::ASCII.GetString($bytes).Contains('BWWeatherVersion'))
}

if ($Uninstall) {
    Step 'Removing BWWeather'
    if ((Test-Path $origDll) -and (Test-Path $dll) -and (Test-IsBWWeather $dll)) {
        Remove-Item $dll -Force
        Rename-Item $origDll 'LHMultiplayerR.dll'
        Ok 'original LHMultiplayerR.dll is back'
    } elseif ((Test-Path $dll) -and -not (Test-IsBWWeather $dll)) {
        Ok 'BWWeather is not installed - nothing to do'
    } else { throw "Unexpected state: check LHMultiplayerR.dll and LHMultiplayerR_orig.dll in $GameDir." }
    foreach ($f in 'bwweather.log') { $p = Join-Path $GameDir $f; if (Test-Path $p) { Remove-Item $p -Force } }
    Ok 'Your bwweather.ini is left in the game folder; delete it if you like.'
    return
}

$new = Join-Path $PSScriptRoot 'bin\LHMultiplayerR.dll'
if (-not (Test-Path $new)) { throw "Missing $new - run build.bat first." }

Step 'Installing BWWeather'
if (Test-IsBWWeather $dll) {
    if (-not (Test-Path $origDll)) { throw "LHMultiplayerR.dll is BWWeather but LHMultiplayerR_orig.dll is missing. Restore the original DLL from your game CD or patch." }
    Ok 'already installed - updating'
} else {
    $hash = (Get-FileHash $dll -Algorithm SHA256).Hash
    if ($hash -ne $KnownOriginal) {
        if (-not $Force) { throw "LHMultiplayerR.dll is not the known v1.42 file (SHA-256 $hash). BWWeather was made for v1.42. Use -Force to try anyway." }
        Warn 'LHMultiplayerR.dll is not the known v1.42 file - installing anyway (-Force)'
    }
    if (Test-Path $origDll) { Remove-Item $origDll -Force }
    Rename-Item $dll 'LHMultiplayerR_orig.dll'
    Ok 'original renamed to LHMultiplayerR_orig.dll'
}
Copy-Item $new $dll -Force
Ok 'copied LHMultiplayerR.dll (BWWeather)'

if (-not (Test-Path $ini)) { Copy-Item (Join-Path $PSScriptRoot 'bwweather.ini') $ini; Ok 'copied bwweather.ini' }
else { Ok 'kept your existing bwweather.ini' }
if ($City) {
    $c = Get-Content $ini
    $c = $c -replace '^City=.*$', "City=$City"
    Set-Content $ini $c -Encoding Default
    Ok "City=$City"
}

Step 'Turning the game''s internet weather feature on'
if (-not (Test-Path $RegSetup)) { New-Item -Path $RegSetup -Force | Out-Null }
Set-ItemProperty $RegSetup -Name UseInternetWeatherSystem -Value 0 -Type DWord
Ok 'UseInternetWeatherSystem = 0'

$town = ((Get-Content $ini | Where-Object { $_ -match '^City=' }) -replace '^City=', '').Trim()
Write-Host ''
if ($town) { Write-Host "BWWeather installed. The game will use the weather in $town." -ForegroundColor Green }
else {
    Write-Host 'BWWeather installed. Opening the settings window - pick your town there.' -ForegroundColor Yellow
    if (-not $env:BWWEATHER_NO_GUI) { Start-Process (Join-Path $PSScriptRoot 'BWWeather-Settings.bat') }
}
Write-Host '  BWWeather-Settings.bat (in the BWWeather folder) picks your town and shows whether it worked.'
