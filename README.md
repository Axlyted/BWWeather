# BWWeather

**Live weather for *Black & White* (2001): the game's weather matches the real weather where you live, as it did in 2001.**

![Windows 10/11](https://img.shields.io/badge/Windows-10%20%7C%2011-0078D6)
![Game version](https://img.shields.io/badge/Black%20%26%20White-v1.42-8B4513)
![Weather data: Open-Meteo](https://img.shields.io/badge/weather-Open--Meteo-orange)
![License: MIT](https://img.shields.io/badge/license-MIT-green)

> [!NOTE]
> Fan-made and free. Not affiliated with or endorsed by Lionhead Studios or Electronic Arts.
> You need your own copy of the game. No game files are included.

## About

*Black & White* had a feature few players remember: it could make the weather in the game match the real weather in your town. It fetched your weather from Lionhead's server, `weather.bwgame.com`, which has been offline for about 20 years.

BWWeather brings the feature back. It answers the game's weather requests itself, with live data from [Open-Meteo](https://open-meteo.com), a free weather service that needs no account.

## Features

- **Real weather in your land:** clear skies, clouds, rain, thunderstorms, snow and blizzards
- **Settings window:** search for your town, see its weather right now, and test any weather type
- **"Did it work?" check:** tells you in plain words what happened the last time the game started
- **Easy to remove:** the uninstaller puts the game's original file back

## Requirements

- *Black & White* with the **unofficial v1.42 patch** from [bwfiles.com](https://bwfiles.com/)
- Windows 10 or 11
- An internet connection

## Installation

1. Copy the `BWWeather` folder into the game folder, next to `runblack.exe`.
2. Close the game and double-click **`Install-BWWeather.bat`**. Allow administrator rights if Windows asks.
3. The settings window opens. Search for your town, pick it from the list, and press **Save**.
4. Start the game and play into a land.

To check that it worked, open **`BWWeather-Settings.bat`** again and look under *Did it work?*:

> The game asked for the weather and got 20°C, visibility 36 km, weather type 8 (cloudy). It's working.

### Uninstall

Double-click **`Uninstall-BWWeather.bat`**. It puts the game's original `LHMultiplayerR.dll` back.

## Settings window

Open it with **`BWWeather-Settings.bat`**.

| Section | What it does |
|---|---|
| **Your town** | Search by name. The results show region and country, so you pick the right town. |
| **Weather there right now** | The current weather in your town, and the game weather it becomes |
| **Testing** | Force any weather type, to see what it looks like in the game |
| **Did it work?** | What happened the last time the game started. *Open log* shows the full log. |

**Turn off** clears your town, so BWWeather stops sending requests. Settings are saved in `bwweather.ini` in the game folder, which you can also edit by hand.

## Weather in the game

| Real weather | In the game |
|---|---|
| Clear | Clear |
| Partly cloudy, overcast, fog | Cloudy |
| Drizzle, light rain | Light rain |
| Showers | Showers |
| Rain, heavy rain | Rain |
| Freezing rain | Sleet |
| Light snow | Light snow |
| Snow, heavy snow | Heavy snow |
| Thunderstorm, or rain with gale-force wind | Thunderstorm |
| Snow with gale-force wind | Blizzard |

> [!TIP]
> To see each weather in the game, choose a type under **Testing**. Set it back to *Real weather* afterwards.

## Privacy

When the game starts, BWWeather sends the town you chose to Open-Meteo and asks for the current weather there. Like any web request, this includes your internet address.

- Nothing is sent to Lionhead's old servers.
- The game's bwgame.com login details never leave your PC.
- With no town set, BWWeather sends nothing at all.

## Troubleshooting

| Problem | Solution |
|---|---|
| The game doesn't start after installing | Run `Uninstall-BWWeather.bat`. |
| *Did it work?* says BWWeather isn't installed | Run `Install-BWWeather.bat` again. Check that `LHMultiplayerR_orig.dll` is in the game folder. |
| *Did it work?* says the game didn't ask for the weather | Play into a land. The game asks while it loads one. |
| *Open log* says the exe isn't the expected v1.42 | BWWeather needs the v1.42 `runblack.exe`. Reinstall the unofficial v1.42 patch. |
| The weather in the game looks wrong | Check the type under *Weather there right now*. Land you've already played may keep its old weather for a while. |

## Antivirus warnings

Some antivirus programs may flag BWWeather's `LHMultiplayerR.dll`. It stands in for one of the game's own DLLs and makes web requests, and that can look suspicious to antivirus heuristics. The full source is in [`src/`](src/), so you can also build it yourself.

## How it works

The game's weather code still works. It only needs someone to answer it. BWWeather takes the place of `LHMultiplayerR.dll`, the game's networking library: the original is renamed and all 883 of its functions are passed through. BWWeather answers only the requests for `weather.bwgame.com`, and gets past two checks the game can no longer pass today: a bwgame.com login, and an online test that tries Lionhead's old website.

The full protocol, the functions it replaces and the game's weather table are documented in **[docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md)**.

## Building from source

Install Visual Studio Build Tools with the C++ workload and run `build.bat`. It builds `bin\LHMultiplayerR.dll`.

## Credits

- Weather data by [Open-Meteo.com](https://open-meteo.com/), licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). The free API is for non-commercial use. BWWeather makes about two requests each time the game starts.
- Town search by Open-Meteo, based on [GeoNames](https://www.geonames.org/) (CC BY 4.0)
- The *Black & White* community at [bwgame.net](https://www.bwgame.net/) and [bwfiles.com](https://bwfiles.com/), and the unofficial v1.42 patch

## License

BWWeather's code is released under the [MIT License](LICENSE).

*Black & White* is a trademark of its owners. BWWeather isn't affiliated with or endorsed by Lionhead Studios or Electronic Arts. It uses the game's original `LHMultiplayerR.dll` from your own installation. `weather.bwgame.com` is named only because the game asks for it. BWWeather never contacts it. Use at your own risk.
