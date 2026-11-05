# BWWeather: live weather for Black & White (2001)

*Black & White* could make the weather in the game match the real weather where you
live. It asked Lionhead's server `weather.bwgame.com` for your town's weather, and
that server has been offline for about 20 years. BWWeather brings the feature back. It
answers the game's weather requests itself, with the current weather from
[Open-Meteo](https://open-meteo.com), a free weather service that needs no account.

Made for **Black & White v1.42** (the unofficial patch from bwfiles.com) on Windows 10/11.

> Fan-made and free. Not affiliated with or endorsed by Lionhead Studios or Electronic
> Arts. You need your own copy of *Black & White*. No game files are included.

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
- **Testing:** force one of the game's weather types, or a temperature, to see what each looks like in the game. Set *Real weather* and an empty
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
3. checks that the game is online. The result comes from `LHCheckForInternetConnection`,
   which tries Lionhead's old `www.bwgame.com`, so today it always says "offline". The
   weather code then skips both requests;
4. sends `GET /query/?action=queryuserdata&username=…&userpassword=…&date=dd.MM.yyyy`
   and expects `1`, then the 2-letter country code, the area and the town, each on its
   own line;
5. sends `GET /query/?action=queryweather&country=…&area=…&city=…&date=dd.MM.yyyy` and
   expects one line per day: `day:month:year:0:0:temperature:visibility:type:0`
   (the fourth, fifth and ninth fields are not used by the game);
6. uses the weather type to pick a row of `weatherinfo.lhw` (clouds, rain, lightning,
   storms, wind) and combines it with the temperature and visibility. Its debug message
   for this is *"Got weather for Country: %s, Area: %s, City: %s Weathertype: %ld,
   Temp: %ld, Visibility: %ld"*.

BWWeather replaces these functions:

| Function | What BWWeather does |
|---|---|
| `LHHttp2::Open` | For the weather server: doesn't connect and remembers the connection. Anything else goes to the original. |
| `LHHttp2::PrepareRequest`, `HelperGetDocument`, `GetDocumentSize`, `GetDocument`, `Close`, `Reset`, destructor | For a weather connection: builds and returns the reply. Anything else goes to the original. |
| weather code in `runblack.exe` (`005E9B54`) | When a town is set, the instruction that copies the game's "offline" result into the weather code's own flag is changed in memory, from `sete cl` to `xor cl,cl; nop`. So the weather code always counts as online, while the rest of the game keeps its own check. It's only applied if the bytes there are exactly those of v1.42. |
| `LHNetGetCurrentProfileWString` | When the game's weather code asks for the `login name` and there is none, it returns the placeholder `BWWeather`. All other calls go to the original. |

### Weather types

The game's weather types are the rows of `Scripts\weatherinfo.lhw`, numbered 2 to 12.
Nothing in the game names them. The game's code (`005E9470`) copies a row into its
climate map: the rain amount comes from the row's value at offset `+04`, the snow amount
from `+14`, and so on. **Rain or snow comes from the row, not from the temperature.** What
the rows set:

| Type | Row sets | Meaning |
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

| Real weather (WMO code) | Game type |
|---|---|
| clear, mainly clear (0, 1) | 4 clear |
| partly cloudy, overcast, fog (2, 3, 45, 48) | 8 cloudy |
| drizzle, light rain (51, 53, 55, 61) | 9 light rain |
| light showers (80) | 2 showers |
| rain, heavy rain, showers (63, 65, 81, 82) | 6 rain |
| freezing drizzle or rain (56, 57, 66, 67) | 7 sleet |
| light snow, snow grains, light snow showers (71, 77, 85) | 3 light snow |
| snow, heavy snow (73, 75, 86) | 11 heavy snow |
| thunderstorm (95–99) | 5 thunderstorm |
| rain with wind of 60 km/h or more | 5 thunderstorm |
| snow with wind of 60 km/h or more | 10 blizzard |

To see what each type looks like, use *Testing* in the settings window (or `ForceType` 2–12
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
  Check that `UseInternetWeatherSystem` is 0. The log should also contain *"weather code
  set to always count as online"*. If it says the exe is not the expected v1.42, the
  game's own online check (which always fails now) is still in the way.

## Antivirus warnings

Some antivirus programs may flag BWWeather's `LHMultiplayerR.dll`. It stands in for one
of the game's own DLLs and makes web requests, and antivirus heuristics can mistake that
for malware behaviour. The full source is in `src/`. If you'd rather not trust the
prebuilt file, build it yourself with `build.bat`.

## Credits

- Weather data by [Open-Meteo.com](https://open-meteo.com/), licensed under
  [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). BWWeather uses Open-Meteo's
  free API, which is for non-commercial use. It makes about two requests each time the
  game starts, plus one or two each time you search or pick a town in the settings window.
- Town search by Open-Meteo's geocoding API, based on [GeoNames](https://www.geonames.org/)
  (CC BY 4.0).
- The Black & White community at [bwgame.net](https://www.bwgame.net/) and
  [bwfiles.com](https://bwfiles.com/), and the unofficial v1.42 patch.

## Licence and disclaimer

BWWeather's code is released under the [MIT licence](LICENSE). It covers only this
repository's own files.

*Black & White* is a trademark of its owners. BWWeather is not affiliated with or endorsed
by Lionhead Studios or Electronic Arts. It contains no game files: it uses the game's
original `LHMultiplayerR.dll` from your own installation. The game is only named to say
what BWWeather is for. `weather.bwgame.com` is named because the game asks for that
address. BWWeather never contacts it. Use at your own risk.
