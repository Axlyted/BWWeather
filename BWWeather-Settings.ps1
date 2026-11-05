<#
.SYNOPSIS
    Settings window for BWWeather: pick your town, see the weather the game will
    get, force weather types for testing, and check what happened the last time
    the game started.
#>
[CmdletBinding()]
param([string]$GameDir)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
$deg = [char]0x00B0

# ---- find the game -------------------------------------------------------------
if (-not $GameDir) {
    $parent = Split-Path -Parent $PSScriptRoot
    if (Test-Path (Join-Path $parent 'runblack.exe')) { $GameDir = $parent }
    else { $GameDir = (Get-ItemProperty 'HKCU:\Software\Lionhead Studios Ltd\Black & White' -ErrorAction SilentlyContinue).GameDir }
}
if (-not $GameDir -or -not (Test-Path (Join-Path $GameDir 'runblack.exe'))) {
    [Windows.Forms.MessageBox]::Show('Could not find the Black & White folder. Put this BWWeather folder inside the game folder.', 'BWWeather') | Out-Null
    return
}
$Ini = Join-Path $GameDir 'bwweather.ini'
$Log = Join-Path $GameDir 'bwweather.log'

# ---- weather codes and game types (same as src\bwweather.c) -------------------
$Wmo = @{
    0 = 'clear sky'; 1 = 'mainly clear'; 2 = 'partly cloudy'; 3 = 'overcast'; 45 = 'fog'; 48 = 'freezing fog'
    51 = 'light drizzle'; 53 = 'drizzle'; 55 = 'heavy drizzle'; 56 = 'freezing drizzle'; 57 = 'heavy freezing drizzle'
    61 = 'light rain'; 63 = 'rain'; 65 = 'heavy rain'; 66 = 'freezing rain'; 67 = 'heavy freezing rain'
    71 = 'light snow'; 73 = 'snow'; 75 = 'heavy snow'; 77 = 'snow grains'
    80 = 'light showers'; 81 = 'showers'; 82 = 'heavy showers'; 85 = 'snow showers'; 86 = 'heavy snow showers'
    95 = 'thunderstorm'; 96 = 'thunderstorm with hail'; 99 = 'thunderstorm with heavy hail'
}
$Types = @{
    2 = 'light rain or snow, showers'; 3 = 'drizzle'; 4 = 'clear'; 5 = 'storm (strong wind)'
    6 = 'heavy rain or snow'; 7 = 'partly cloudy'; 8 = 'windy, hazy (not used for real weather)'
    9 = 'rain or snow'; 10 = 'thunderstorm'; 11 = 'overcast or fog'; 12 = 'unknown (not used for real weather)'
}
function Get-GameType([int]$code, [double]$wind) {
    $t = switch ($code) {
        { $_ -in 0, 1 } { 4; break }
        2 { 7; break }
        { $_ -in 3, 45, 48 } { 11; break }
        { $_ -in 51, 53, 55, 56, 57 } { 3; break }
        { $_ -in 61, 80, 71, 85 } { 2; break }
        { $_ -in 63, 81, 73, 77 } { 9; break }
        { $_ -in 65, 82, 66, 67, 75, 86 } { 6; break }
        { $_ -in 95, 96, 99 } { 10; break }
        default { 11 }
    }
    if ($wind -ge 60 -and $t -ne 10) { $t = 5 }
    return $t
}

# ---- settings file ---------------------------------------------------------------
function Read-Ini {
    $s = @{ City = ''; Country = ''; Area = ''; Latitude = ''; Longitude = ''; ForceType = '0'; ForceTemperature = '' }
    if (Test-Path $Ini) {
        foreach ($l in Get-Content $Ini -Encoding Default) {
            if ($l -match '^\s*([A-Za-z]+)\s*=\s*(.*?)\s*$' -and $s.ContainsKey($Matches[1])) { $s[$Matches[1]] = $Matches[2] }
        }
    }
    return $s
}
function Write-Ini($s) {
    $text = @"
; BWWeather settings. Changes apply the next time the game starts.
; Easiest: use BWWeather-Settings.bat in the BWWeather folder.

[BWWeather]
; Your town, as you would type it into a map search. Leave empty to turn BWWeather off.
City=$($s.City)
; Optional two-letter country code, to pick the right town when names clash (GB, US, DE, IL, ...)
Country=$($s.Country)
; Region shown to the game (filled in by the settings window)
Area=$($s.Area)
; Optional exact location instead of searching for City (decimal degrees)
Latitude=$($s.Latitude)
Longitude=$($s.Longitude)

; --- testing ---
; Force one of the game's weather types (2 to 12) instead of the real weather. 0 = real weather.
ForceType=$($s.ForceType)
; Force a temperature in degrees Celsius (for example -5 to try snow). Empty = real temperature.
ForceTemperature=$($s.ForceTemperature)
"@
    try { [IO.File]::WriteAllText($Ini, ($text -replace "`r?`n", "`r`n"), [Text.Encoding]::Default) }
    catch {
        [Windows.Forms.MessageBox]::Show("Could not save $Ini.`n`n$($_.Exception.Message)`n`nRight-click BWWeather-Settings.bat and choose Run as administrator.", 'BWWeather') | Out-Null
        return $false
    }
    return $true
}

# ---- Open-Meteo --------------------------------------------------------------------
$Agent = 'BWWeather/1.0 (Black & White live weather)'
function Find-Town([string]$name) {
    $u = 'https://geocoding-api.open-meteo.com/v1/search?count=10&language=en&format=json&name=' + [Uri]::EscapeDataString($name)
    $r = Invoke-RestMethod -Uri $u -UserAgent $Agent -TimeoutSec 15
    return @($r.results)
}
function Get-Weather([double]$lat, [double]$lon) {
    $inv = [Globalization.CultureInfo]::InvariantCulture
    $u = 'https://api.open-meteo.com/v1/forecast?latitude={0}&longitude={1}&current=temperature_2m,weather_code,wind_speed_10m,visibility' -f $lat.ToString('0.####', $inv), $lon.ToString('0.####', $inv)
    return (Invoke-RestMethod -Uri $u -UserAgent $Agent -TimeoutSec 15).current
}
function Format-Weather($c, [string]$where) {
    $code = [int]$c.weather_code
    $desc = if ($Wmo.ContainsKey($code)) { $Wmo[$code] } else { "weather code $code" }
    $vis = if ($null -ne $c.visibility) { [math]::Round($c.visibility / 1000) } else { '?' }
    $t = Get-GameType $code ([double]$c.wind_speed_10m)
    return "Now in ${where}: $([math]::Round($c.temperature_2m))$deg" + "C, $desc, wind $([math]::Round($c.wind_speed_10m)) km/h, visibility $vis km.`r`nIn the game: weather type $t ($($Types[$t]))."
}

# ---- what happened last time -------------------------------------------------------
function Get-Status {
    $dll = Join-Path $GameDir 'LHMultiplayerR.dll'
    $installed = (Test-Path (Join-Path $GameDir 'LHMultiplayerR_orig.dll')) -and (Test-Path $dll) -and
                 ([Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($dll)).Contains('BWWeatherVersion'))
    if (-not $installed) { return 'BWWeather is NOT installed in the game. Run Install-BWWeather.bat.' }
    if (-not (Test-Path $Log)) { return 'Installed. The game hasn''t been started since (there is no bwweather.log yet).' }
    $when = (Get-Item $Log).LastWriteTime.ToString('g')
    $l = Get-Content $Log
    $txt = "Last game start ($when):`r`n"
    if ($l -match 'no town set') { return $txt + 'No town was set, so the game got no weather.' }
    $w = $l | Where-Object { $_ -match 'weather for (.*) \(' } | Select-Object -Last 1
    $err = $l | Where-Object { $_ -match 'could not reach|does not know|unexpected weather|HTTP \d' } | Select-Object -Last 1
    if ($w) {
        $town = $w -replace '^.*weather for (.*?) \(.*$', '$1'
        $txt += "- BWWeather fetched the weather for $town.`r`n"
    }
    elseif ($err) { $txt += "- BWWeather could not get the weather: $($err.Substring(9))`r`n" }
    $asked = @($l | Where-Object { $_ -match 'asks for the weather' }).Count
    $ans = $l | Where-Object { $_ -match 'answered: \d+:\d+:\d+:' } | Select-Object -Last 1
    if ($ans) {
        $f = ($ans -replace '^.*answered: ', '').TrimEnd('|').Split(':')
        $t = [int]$f[7]
        $txt += "- The game asked for the weather and got $($f[5])$deg" + "C, visibility $($f[6]) km, weather type $t ($($Types[$t])).`r`n  It's working."
    } elseif ($asked) { $txt += '- The game asked for the weather, but BWWeather had none to give.' }
    elseif ($l -match 'game connects') { $txt += '- The game started asking but didn''t finish.' }
    else { $txt += "- The game hasn't asked for the weather yet. It asks while loading a land, so play into one. If it still doesn't, see Troubleshooting in the README." }
    return $txt
}

# ---- window ------------------------------------------------------------------------
$s = Read-Ini
$f = New-Object Windows.Forms.Form
$f.Text = 'BWWeather - live weather for Black & White'
$f.ClientSize = New-Object Drawing.Size(560, 590)
$f.FormBorderStyle = 'FixedDialog'; $f.MaximizeBox = $false; $f.StartPosition = 'CenterScreen'
$f.Font = New-Object Drawing.Font('Segoe UI', 9)
function Add-Ctl($c, $x, $y, $w, $h, $parent = $f) { $c.Location = New-Object Drawing.Point($x, $y); $c.Size = New-Object Drawing.Size($w, $h); $parent.Controls.Add($c); $c }

$g1 = Add-Ctl (New-Object Windows.Forms.GroupBox) 10 8 540 230; $g1.Text = 'Your town'
$cur = Add-Ctl (New-Object Windows.Forms.Label) 12 22 515 20 $g1
$q = Add-Ctl (New-Object Windows.Forms.TextBox) 12 46 410 24 $g1
$bs = Add-Ctl (New-Object Windows.Forms.Button) 430 45 98 26 $g1; $bs.Text = 'Search'
$lst = Add-Ctl (New-Object Windows.Forms.ListBox) 12 78 516 110 $g1
$hint = Add-Ctl (New-Object Windows.Forms.Label) 12 192 516 32 $g1
$hint.Text = 'Type your town and press Search, then pick it from the list. Searches go to Open-Meteo (open-meteo.com).'

$g2 = Add-Ctl (New-Object Windows.Forms.GroupBox) 10 244 540 70; $g2.Text = 'Weather there right now'
$now = Add-Ctl (New-Object Windows.Forms.Label) 12 20 516 44 $g2

$g3 = Add-Ctl (New-Object Windows.Forms.GroupBox) 10 320 540 82; $g3.Text = 'Testing (to see what each weather looks like in the game)'
$l1 = Add-Ctl (New-Object Windows.Forms.Label) 12 24 110 20 $g3; $l1.Text = 'Weather type:'
$ft = Add-Ctl (New-Object Windows.Forms.ComboBox) 125 21 403 24 $g3; $ft.DropDownStyle = 'DropDownList'
[void]$ft.Items.Add('Real weather')
foreach ($k in ($Types.Keys | Sort-Object)) { [void]$ft.Items.Add("$k - $($Types[$k])") }
$l2 = Add-Ctl (New-Object Windows.Forms.Label) 12 53 110 20 $g3; $l2.Text = 'Temperature:'
$tt = Add-Ctl (New-Object Windows.Forms.TextBox) 125 50 60 24 $g3
$l3 = Add-Ctl (New-Object Windows.Forms.Label) 190 53 340 20 $g3; $l3.Text = "$deg" + 'C   (empty = real temperature; try -5 for snow)'

$g4 = Add-Ctl (New-Object Windows.Forms.GroupBox) 10 408 540 132; $g4.Text = 'Did it work? (last time the game started)'
$st = Add-Ctl (New-Object Windows.Forms.Label) 12 20 516 78 $g4
$br = Add-Ctl (New-Object Windows.Forms.Button) 12 100 90 26 $g4; $br.Text = 'Refresh'
$bl = Add-Ctl (New-Object Windows.Forms.Button) 108 100 90 26 $g4; $bl.Text = 'Open log'

$bsave = Add-Ctl (New-Object Windows.Forms.Button) 250 552 100 30; $bsave.Text = 'Save'
$boff = Add-Ctl (New-Object Windows.Forms.Button) 356 552 100 30; $boff.Text = 'Turn off'
$bclose = Add-Ctl (New-Object Windows.Forms.Button) 462 552 88 30; $bclose.Text = 'Close'
$f.AcceptButton = $bs; $f.CancelButton = $bclose

$script:picked = $null
function Show-Current {
    if ($s.City) { $cur.Text = "Saved town: $($s.City)$(if ($s.Area) { ", $($s.Area)" })$(if ($s.Country) { ", $($s.Country)" })" }
    else { $cur.Text = 'Saved town: none (BWWeather is off)' }
}
function Show-Weather([double]$lat, [double]$lon, [string]$where) {
    $now.Text = 'Getting the weather...'; $f.Refresh()
    try { $now.Text = Format-Weather (Get-Weather $lat $lon) $where }
    catch { $now.Text = "Could not get the weather: $($_.Exception.Message)" }
}
Show-Current
$ft.SelectedIndex = 0
if ($s.ForceType -match '^\d+$' -and [int]$s.ForceType -ge 2 -and [int]$s.ForceType -le 12) { $ft.SelectedIndex = [int]$s.ForceType - 1 }
$tt.Text = $s.ForceTemperature
$st.Text = Get-Status

$bs.Add_Click({
    if (-not $q.Text.Trim()) { return }
    $f.Cursor = 'WaitCursor'; $lst.Items.Clear(); $script:results = @()
    try {
        $script:results = Find-Town $q.Text.Trim()
        if (-not $script:results) { [void]$lst.Items.Add('No town found with that name.') }
        foreach ($r in $script:results) {
            [void]$lst.Items.Add(("{0}, {1}, {2}   ({3:0.00}, {4:0.00})" -f $r.name, $(if ($r.admin1) { $r.admin1 } else { '-' }), $r.country, $r.latitude, $r.longitude))
        }
    } catch { [void]$lst.Items.Add("Search failed: $($_.Exception.Message)") }
    $f.Cursor = 'Default'
})
$lst.Add_SelectedIndexChanged({
    $i = $lst.SelectedIndex
    if ($i -lt 0 -or $i -ge @($script:results).Count) { return }
    $script:picked = $script:results[$i]
    $f.Cursor = 'WaitCursor'
    Show-Weather $script:picked.latitude $script:picked.longitude $script:picked.name
    $f.Cursor = 'Default'
})
$bsave.Add_Click({
    $inv = [Globalization.CultureInfo]::InvariantCulture
    if ($script:picked) {
        $p = $script:picked
        $s.City = $p.name; $s.Country = "$($p.country_code)"; $s.Area = if ($p.admin1) { $p.admin1 } else { $p.name }
        $s.Latitude = ([double]$p.latitude).ToString('0.#####', $inv); $s.Longitude = ([double]$p.longitude).ToString('0.#####', $inv)
    }
    $s.ForceType = if ($ft.SelectedIndex -le 0) { '0' } else { "$($ft.SelectedIndex + 1)" }
    $temp = $tt.Text.Trim()
    if ($temp -and $temp -notmatch '^-?\d+$') { [Windows.Forms.MessageBox]::Show('Temperature must be a whole number, like -5 or 20 (or empty).', 'BWWeather') | Out-Null; return }
    $s.ForceTemperature = $temp
    if (Write-Ini $s) {
        Show-Current
        $msg = if ($s.City) { "Saved. The next time you start the game, it gets the weather in $($s.City)." } else { 'Saved. No town is set, so BWWeather is off.' }
        if ($s.ForceType -ne '0' -or $s.ForceTemperature) { $msg += "`n`nTesting is on: the game gets the forced weather. Set 'Real weather' and an empty temperature to go back." }
        [Windows.Forms.MessageBox]::Show($msg, 'BWWeather') | Out-Null
    }
})
$boff.Add_Click({
    $s.City = ''; $s.Country = ''; $s.Area = ''; $s.Latitude = ''; $s.Longitude = ''; $script:picked = $null
    if (Write-Ini $s) { Show-Current; $now.Text = ''; [Windows.Forms.MessageBox]::Show('BWWeather is off: no town is set, so nothing is sent anywhere.', 'BWWeather') | Out-Null }
})
$br.Add_Click({ $st.Text = Get-Status })
$bl.Add_Click({ if (Test-Path $Log) { Start-Process notepad.exe $Log } else { [Windows.Forms.MessageBox]::Show('There is no bwweather.log yet. Start the game first.', 'BWWeather') | Out-Null } })
$bclose.Add_Click({ $f.Close() })
$f.Add_Shown({
    if ($s.Latitude -and $s.Longitude) {
        $inv = [Globalization.CultureInfo]::InvariantCulture
        Show-Weather ([double]::Parse($s.Latitude, $inv)) ([double]::Parse($s.Longitude, $inv)) $s.City
    }
})
[void]$f.ShowDialog()
