# CounterChao

CS2 kills, headshots, flashes, and multi-kills -> Chao Garden animals and Chaos Drives

Match data provided by [Leetify](https://leetify.com). Not affiliated with SEGA, Valve, or Leetify.

## What it does

CounterChao is a Sonic Adventure 2 mod. When you start the game it reads your latest CS2 matches from the Leetify API, works out rewards from your stats, and places the items in your Chao Garden. The game grants items, saves and checksums them itself.

## Status

Beta, works on my machine :) \[Windows 11, Steam version of Sonic Adventure 2]
Found a bug or something confusing? Please open an issue.

## Requirements

* Windows, Sonic Adventure 2 from Steam, with the [SA Mod Manager](https://github.com/X-Hax/SA-Mod-Manager) installed
* A Leetify profile set to public, with your CS2 matches on it
* An internet connection when you start the game

## Setup

0. Back up your saves first! Copy the `SAVEDATA` folder from `Sonic Adventure 2\resource\gd_PC\` somewhere safe. To undo anything, close the game and copy it back.
1. Download the latest `CounterChao` zip from the [Releases page](https://github.com/parrinodotdev/CounterChao/releases).
2. Extract it into your SA2 `mods` folder, so you end up with `mods\CounterChao\CounterChao.dll`.
3. Open the SA Mod Manager and tick **CounterChao**.
4. Select it and click **Configure**. Paste your SteamID64 (look it up at steamid.io) and save.
5. Launch the game from the Mod Manager.

## Playing

1. Play some CS2. Let Leetify pick up your matches.
2. Start SA2 and go to Stage Select -> Chao Garden, then into the Chao World lobby.
3. A message shows up for a few seconds telling you what happened (see Messages below).
4. Walk into the garden. Your items are there!

The mod checks Leetify once, when the game starts. If you play a match while the game is open, restart the game to pick it up.
Matches are rewarded once. If your garden's 10 item slots are full, the leftovers wait, and they show up the next time you walk through the lobby with free slots.
The first time you run it, only your latest match counts, so you don't get a mountain of items at once. You can raise that (up to 5 matches) in Configure.

## How rewards work

* **Red Chaos Drive**: every 5 kills
* **Purple Chaos Drive**: every 4 headshot kills
* **Green Chaos Drive**: every successful trade kill
* **Yellow Chaos Drive**: every 3 enemies flashed
* **Animals**: multi-kill rounds (3K, 4K, 5K), flashes that lead to a kill, and flash assists
* **Rare animals**: a standout match can drop one

A match needs at least 10 rounds to count, a match gives at most 10 items, and up to 3 new matches are rewarded each time you start the game.
Been away a while? Only your 3 newest matches count. Older ones are skipped.

## Messages

|Message|What to do|
|-|-|
|**N items delivered**|Walk into the garden.|
|**N items waiting**|Your garden is full. Use or feed some items, then walk back through the lobby.|
|**No new matches**|Play a CS2 match, let Leetify pick it up, then restart the game.|
|**Setup needed**|Open Configure in the Mod Manager and enter your SteamID64.|
|**No matches found**|Check your SteamID64 in Configure and make sure your Leetify profile is public.|
|**Can't reach Leetify**|Check your internet and restart the game. Waiting items still show up.|
|**Unexpected reply**|Leetify sent something weird. Try again later.|
|**State file problem**|`state.json` couldn't be read. Nothing was delivered. Check `CounterChao.log`.|
|**Something went wrong**|Couldn't save. Nothing was delivered. Check `CounterChao.log`.|

## Troubleshooting

**"An Application Control policy has blocked this file" or a "Bad Image" error (0xc0e90002) when the game starts.**
Windows 11's Smart App Control is blocking the mod. Your saves are fine.
The only fix I know of is turning Smart App Control off (Windows Security -> App \& browser control -> Smart App Control settings).

## Privacy and security

The mod sends your SteamID64 to Leetify's public API over HTTPS, once per game launch, and nothing else. No API key is needed. It never shows or changes any Leetify numbers; it only uses your stats to decide rewards.
Items are written into the running game's memory, and the game saves them normally. No admin rights needed. No SEGA files are included in this project.
In its own mod folder the mod keeps three small files: `config.ini` (your settings), `state.json` (which matches were already rewarded, and any items waiting), and `CounterChao.log` (turn it off in Configure if you like).

## Current limitations :(

* Windows only.
* Items are handed to the game as soon as they appear in the garden. If the game crashes before it saves, they can be lost.
* Tested with the SA2 Steam version and the default Chao garden. Other Chao mods haven't been tested.

## Python prototype (deprecated)

<details>
<summary>The first version was a Python script. It's kept here as a proof of concept and isn't maintained.</summary>
It worked by writing into the running game's memory from outside. It needed Python 3 and asked you to confirm you were standing in the lobby.

1. Copy `config.example.json` to `config.json` and fill in your SteamID64 (keep it private; the API key is optional).
2. Start SA2 and stand in the Chao World lobby.
3. Run `python -m counterchao`. Add `--plan` to preview rewards without touching the game.
4. Edit `rewards.json` to change the rates, animal pools, rare drops and caps.

</details>

## Credits

* Chao Island's SA2 Chao hacking guide, for the save layout
* [SA2 Mod Loader and SA Mod Manager](https://github.com/X-Hax/sa2-mod-loader) by X-Hax, and their mod template
* [nlohmann/json](https://github.com/nlohmann/json) for JSON parsing
* [Leetify](https://leetify.com), for the public API. Match data provided by Leetify.
Sonic Adventure 2 belongs to SEGA. Counter-Strike and CS2 belong to Valve.

## License

MIT. See LICENSE. Third-party notices for the mod template and nlohmann/json are included in the repo.

