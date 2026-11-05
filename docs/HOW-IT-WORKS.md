# How BWWeather works

This page documents how *Black & White*'s live weather feature works, as reverse-engineered from `runblack.exe` v1.42, and what BWWeather changes. To install, see the [README](../README.md).

## Contents

- [The game's weather feature](#the-games-weather-feature)
- [The protocol](#the-protocol)
- [What BWWeather replaces](#what-bwweather-replaces)
- [Weather types](#weather-types)
- [What gets installed](#what-gets-installed)
- [Building](#building)

## The game's weather feature

The weather code lives in `runblack.exe` (source file `LHInetWeather.cpp`, around `0x005E7F00`–`0x005EA4A0`). It uses the `LHHttp2` HTTP class from `LHMultiplayerR.dll`. At startup it:

1. Reads its settings from `HKCU\Software\Lionhead Studios Ltd\Black & White\BWSetup`:

   | Value | Default |
   |---|---|
   | `WeatherSystemServerName` | `weather.bwgame.com` |
   | `WeatherSystemServerURL` | `/query/` |
   | `WeatherSystemServerPort` | `80` |
   | `UseInternetWeatherSystem` | `1` |

   The feature only runs when `UseInternetWeatherSystem` is `0`. A fresh install writes `1`.
2. Loads `Scripts\weatherinfo.lhw`, the table of weather types.
3. Reads the bwgame.com **login name** of the current multiplayer profile. Without one, it skips the feature.
4. Checks whether it's online. The result comes from `LHCheckForInternetConnection`, which tries Lionhead's old `www.bwgame.com`, so today it always says "offline".
5. Asks the server for the player's location, then for the weather there (see below).
6. Copies the record into a global (`0x00D016F0`) and applies it to the game's climate. Its debug message for this is:
   *"Got weather for Country: %s, Area: %s, City: %s Weathertype: %ld, Temp: %ld, Visibility: %ld"*.

## The protocol

The game makes two HTTP `GET` requests to the weather server.

### 1. Location

```
GET /query/?action=queryuserdata&username=<login>&userpassword=<password>&date=dd.MM.yyyy
```

The expected reply has four lines:

```
1
<two-letter country code>
<area>
<town>
```

The first line must be `1`.

### 2. Weather

```
GET /query/?action=queryweather&country=<cc>&area=<area>&city=<town>&date=dd.MM.yyyy
```

The expected reply is one line per day, with nine fields separated by `:`:

```
day:month:year:0:0:temperature:visibility:type:0
```

| Field | Meaning | Stored at (in the record) |
|---|---|---|
| 1–3 | day, month, year | `+0x139`, `+0x13A`, `+0x13C` |
| 4, 5, 9 | not used by the game | `+0x132`, `+0x138`, `+0x136` |
| 6 | temperature (°C) | `+0x130` |
| 7 | visibility (km) | `+0x134` |
| 8 | weather type (2–12) | `+0x140` |

The `LHHttp2` calls must return particular status codes:

| Call | Must return |
|---|---|
| `Open(host, port)` | `0` |
| `PrepareRequest` | `8` |
| `HelperGetDocument` | `7` |

After that, the game asks for the reply's size (`GetDocumentSize`) and then the reply itself (`GetDocument`).

## What BWWeather replaces

BWWeather builds a `LHMultiplayerR.dll` that takes the original's place. The original is renamed to `LHMultiplayerR_orig.dll`. [`src/LHMultiplayerR.def`](../src/LHMultiplayerR.def) forwards all 883 of its exports by name and ordinal. Only these are replaced:

| What | Change |
|---|---|
| `LHHttp2::Open` | For the weather server: doesn't connect, just remembers the connection. Anything else goes to the original. |
| `PrepareRequest`, `HelperGetDocument`, `GetDocumentSize`, `GetDocument`, `Close`, `Reset`, destructor | For a weather connection: builds and returns the reply. Anything else goes to the original. |
| `LHNetGetCurrentProfileWString` | When the weather code asks for `login name` and there is none, it returns the placeholder `BWWeather`. Only for calls from the weather code. |
| `runblack.exe` at `0x005E9B54` | The instruction that copies the game's "offline" result into the weather code's own flag is changed in memory, from `sete cl` to `xor cl,cl; nop`. Only the weather code then counts as online. The rest of the game keeps its own check. This is applied only when the surrounding bytes match v1.42. |

To answer the requests, BWWeather:

1. Finds the town's coordinates with Open-Meteo's geocoding API, unless the settings window already saved them.
2. Fetches the current weather (`temperature_2m`, `weather_code`, `wind_speed_10m`, `visibility`).
3. Builds both replies.

This runs on a background thread at startup, so the weather is usually ready before the game asks. Everything is logged to `bwweather.log`.

## Weather types

The weather types are the rows of `Scripts\weatherinfo.lhw`, numbered 2–12. Nothing in the game names them. At `0x005E9470` the game copies a row into its climate map. Each value in a row has a mode (set, add or subtract) and an amount. The rain amount comes from the row's value at `+04`, and the snow amount from `+14`.

**Rain or snow comes from the row, not from the temperature.** The climate code checks those amounts directly: `0x007714B0` tests for rain, `0x007714F0` for snow.

| Type | The row sets | Meaning |
|---|---|---|
| 2 | rain 40 | showers |
| 3 | snow 50 | light snow |
| 4 | nothing | clear |
| 5 | rain 100, storm | thunderstorm |
| 6 | rain 50 | rain |
| 7 | rain 30, snow 30 | sleet |
| 8 | no rain, some wind | cloudy |
| 9 | rain 40, more cloud | light rain |
| 10 | snow 100, storm | blizzard |
| 11 | snow 100 | heavy snow |
| 12 | nothing | calm |

How Open-Meteo's [WMO weather codes](https://open-meteo.com/en/docs) are mapped:

| WMO code | Weather | Game type |
|---|---|---|
| 0, 1 | clear, mainly clear | 4 |
| 2, 3, 45, 48 | partly cloudy, overcast, fog | 8 |
| 51, 53, 55, 61 | drizzle, light rain | 9 |
| 80 | light showers | 2 |
| 63, 65, 81, 82 | rain, heavy rain, showers | 6 |
| 56, 57, 66, 67 | freezing drizzle or rain | 7 |
| 71, 77, 85 | light snow, snow grains, light snow showers | 3 |
| 73, 75, 86 | snow, heavy snow | 11 |
| 95, 96, 99 | thunderstorm | 5 |

With wind of 60 km/h or more, rain becomes type 5 (thunderstorm) and snow becomes type 10 (blizzard).

`ForceType` (2–12) and `ForceTemperature` in `bwweather.ini` override the real weather for testing.

## What gets installed

| Change | Detail |
|---|---|
| `LHMultiplayerR.dll` | The original is renamed to `LHMultiplayerR_orig.dll`, and BWWeather's DLL takes its name. The installer checks the original's SHA-256 against v1.42 and refuses other versions unless you use `-Force`. |
| `bwweather.ini` | Settings: town, region, country, coordinates, test options. It's kept on reinstall. |
| Registry | `UseInternetWeatherSystem = 0` |

Uninstall deletes BWWeather's DLL and renames the original back.

## Building

`build.bat` builds `bin\LHMultiplayerR.dll` (32-bit) with the Visual Studio C++ tools. Most exports are forwarded through [`src/LHMultiplayerR.def`](../src/LHMultiplayerR.def).

The replaced functions are exported from [`src/bwweather.c`](../src/bwweather.c) with `/EXPORT` linker directives instead, for two reasons:

- A `.def` file cuts these C++ names at the first `@`.
- The internal symbols need decorated names (`__fastcall`) for the linker to keep the full export name.

The `LHHttp2` methods are `__thiscall`, so they're written as `__fastcall` functions with an unused `edx` argument. `LHNetGetCurrentProfileWString` is `__cdecl`. It's declared as a `__fastcall` function with no stack arguments, and reads its three real arguments from the stack.
