# chaomod

Kills, headshots, flashes and multi-kills become Chaos Drives and animals that appear in your garden!
After 25 years of replaying City Escape, we're playing Counter-Strike 2 to feed the colony.

Not affiliated with SEGA, Valve, or Leetify

## What it does

Reads your latest matches from the Leetify API, works out rewards from rules set in rewards.json, and places the items in your running game's Chao save. The game grants items, saves and checksums them itself.

## Status

Working prototype - reward logic has unit tests, memory delivery "works on my machine :)" \[windows 11, SA2 steam version]
Check back soon for an easier user experience; right now everything happens by python script

## Requirements

* Windows, Sonic Adventure 2 from Steam
* Python 3 (developed on 3.14, no extra packages)
* Leetify profile set to public
* Leetify API key (optional! if you get rate-limited, getting your own API key will help)

## Setup
1. Open config.example.json and paste your digits (SteamID64 is required but you can leave Leetify API blank)
2. Save as config.json (Keep this private!)

Look up your SteamID64 at steamid.io. You can request a key from Leetify's API page.

## Usage
1. Start SA2, go to Stage Select -> Chao Garden, stand in the Chao World lobby.
2. Run `python -m chaomod`. Add `--plan` to preview rewards without touching the game.
3. Answer `y` when asked if you're in the lobby (injection happens here but the game won't react immediately)
4. Walk into the garden, rewards should be spawned on the ground.

Matches are rewarded once. If your garden's 10 item slots are full, leftovers wait for next time.

## Customizing rewards
Edit rewards.json: stat-to-items rates, animal pools, rare drops, minimum rounds to register match, per-match caps

## How it works
The game keeps the whole Chao save in memory while you're in the Chao World. chaomod writes item records into the free slots of that image, and the game saves them normally when you leave.

## Security
Writes only to the running game's memory, chaomod never saves files on your disk. No admin rights needed. Talks only to Leetify by sending your API key and SteamID64. No SEGA files are included in this project.

## Current limitations :(
Windows only. It cannot currently tell whether the lobby is loaded, so it asks you.

## Credits
- Chao Island's SA2 Chao hacking guide, for the save layout
- SA2 Mod Loader
- Leetify, for the public API

## License
MIT. See LICENSE.
