# BWWeather: live weather for Black & White (2001)

*Black & White* could make the weather in the game match the real weather where you
live. It asked Lionhead's server `weather.bwgame.com` for your town's weather, and
that server has been offline for about 20 years. BWWeather brings the feature back. It
answers the game's weather requests itself, with the current weather from
[Open-Meteo](https://open-meteo.com), a free weather service that needs no account.

Made for **Black & White v1.42** (the unofficial patch from bwfiles.com) on Windows 10/11.

## Install

1. Copy this `BWWeather` folder into the game folder (next to `runblack.exe`).
2. Close the game and double-click **`Install-BWWeather.bat`**. If Windows asks for
   administrator rights, say yes.
3. The **settings window** opens. Type your town, press **Search**, pick it from the list
   (it shows the region and country, so you get the right one) and press **Save**. The
   window also shows the weather there right now, and which weather the game will get.
4. Start the game and play into a land.
5. Open the settings window again (**`BWWeather-Settings.bat`** in this folder). Under
   *Did it work?* it tells you what happened the last time the game started. For
   example: *"The game asked for the weather and got 7°C, visibility 20 km, weather type
   2 (light rain or snow, showers). It's working."*

To remove it, double-click **`Uninstall-BWWeather.bat`**. It puts the game's original
`LHMultiplayerR.dll` back.

## Settings window

`BWWeather-Settings.bat` opens a small window with four parts:

- **Your town:** search by name and pick from the matches. It saves the town, region,
  country and exact coordinates to `bwweather.ini`.
- **Weather there right now:** the current weather and the game weather type it becomes.
- **Testing:** force one of the game's weather types, or a temperature (for example −5 to
  try snow), to see what each looks like in the game. Set *Real weather* and an empty
  temperature to go back.
- **Did it work?:** reads `bwweather.log` and says in plain words whether the game asked for
  the weather and what it got. *Open log* shows the full log.

*Turn off* clears the town, so BWWeather sends nothing and the game gets no weather.
You can also edit `bwweather.ini` by hand.

## What it changes

- **`LHMultiplayerR.dll`:** the original is renamed to `LHMultiplayerR_orig.dll` and
  BWWeather's DLL takes its name. It passes all 883 of the original's functions
  straight through, except the few listed under *How it works*.
- **`bwweather.ini`:** your settings (town, region, country, coordinates, test options).
- **Registry:** `UseInternetWeatherSystem = 0` in
  `HKCU\Software\Lionhead Studios Ltd\Black & White\BWSetup`. The game only runs its
  weather feature when this is 0. A fresh install writes 1, which turns it off.

## Privacy

When the game starts, BWWeather sends the town from `bwweather.ini` to Open-Meteo to
find its location, then asks Open-Meteo for the current weather there. Like any web
request, that includes your internet address. Nothing is sent to the old Lionhead
servers, and the game's bwgame.com login details never leave your PC. With no town
set, BWWeather sends nothing.

## How it works

The game's weather code (`LHInetWeather`, in `runblack.exe`) uses the `LHHttp2` HTTP
class from `LHMultiplayerR.dll`. At startup it:

1. reads `WeatherSystemServerName` (default `weather.bwgame.com`), `WeatherSystemServerURL`
   (`/query/`) and `WeatherSystemServerPort` (80) from the registry, and loads
   `Scripts\weatherinfo.lhw`;
2. reads the bwgame.com **login name** of the current multiplayer profile. Without one it
   skips the weather feature;
3. sends `GET /query/?action=queryuserdata&username=…&userpassword=…&date=dd.MM.yyyy`
   and expects `1`, then the 2-letter country code, the area and the town, each on its
   own line;
4. sends `GET /query/?action=queryweather&country=…&area=…&city=…&date=dd.MM.yyyy` and
   expects one line per day: `day:month:year:0:0:temperature:visibility:type:0`
   (the fourth, fifth and ninth fields are not used by the game);
5. uses the weather type to pick a row of `weatherinfo.lhw` (clouds, rain, lightning,
   storms, wind) and combines it with the temperature and visibility. Its debug message
   for this is *"Got weather for Country: %s, Area: %s, City: %s Weathertype: %ld,
   Temp: %ld, Visibility: %ld"*.

BWWeather replaces these functions:

| Function | What BWWeather does |
|---|---|
| `LHHttp2::Open` | For the weather server: doesn't connect and remembers the connection. Anything else goes to the original. |
| `LHHttp2::PrepareRequest`, `HelperGetDocument`, `GetDocumentSize`, `GetDocument`, `Close`, `Reset`, destructor | For a weather connection: builds and returns the reply. Anything else goes to the original. |
| `LHNetGetCurrentProfileWString` | When the game's weather code asks for the `login name` and there is none, it returns the placeholder `BWWeather`. All other calls go to the original. |

### Weather types

The game's weather types are the rows of `Scripts\weatherinfo.lhw`, numbered 2 to 12.
Nothing in the game names them, so this mapping from Open-Meteo's
[WMO weather codes](https://open-meteo.com/en/docs) is based on what each row sets:

| Real weather (WMO code) | Game type |
|---|---|
| clear, mainly clear (0, 1) | 4 |
| partly cloudy (2) | 7 |
| overcast, fog (3, 45, 48) | 11 |
| drizzle (51–57) | 3 |
| light rain, snow or showers (61, 71, 80, 85) | 2 |
| moderate (63, 73, 77, 81) | 9 |
| heavy (65–67, 75, 82, 86) | 6 |
| thunderstorm (95–99) | 10 |
| wind of 60 km/h or more | 5 (storm) |

The game decides between rain and snow from the temperature. To see what each type looks
like, use *Testing* in the settings window (or `ForceType` 2–12 and `ForceTemperature`
in `bwweather.ini`).

## Building

Install Visual Studio Build Tools with the C++ workload and run `build.bat`. It builds
`bin\LHMultiplayerR.dll` (32-bit). `src\LHMultiplayerR.def` forwards the original's
exports by name and ordinal. The replaced functions are exported from `src\bwweather.c`
with `/EXPORT` linker directives, because a `.def` file cuts these C++ names at the
first `@`.

## Troubleshooting

- **The game doesn't start after installing:** run `Uninstall-BWWeather.bat`.
- **`bwweather.log` is missing:** BWWeather wasn't loaded. Check that
  `LHMultiplayerR_orig.dll` is in the game folder.
- **The log shows no "game connects" line:** the game didn't run its weather code.
  Check that `UseInternetWeatherSystem` is 0, and that the game thinks you're online:
  it checks with `LHCheckForInternetConnection` at startup.
