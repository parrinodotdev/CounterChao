# CounterChao clean-slate run

Rule: after every step, run `python save_lab.py snap <label> "what I just did"`.
Snapshots work while the game is open. `clear` and `restore` need the game fully closed.
Everything is logged to `lab/log.md` automatically. Write extra observations (what you saw on screen) in the notes part of the snap command.

## Steps

0. **Setup (game closed):** `python save_lab.py init`  -> verified backup + snapshot 001 `baseline_original`
1. **Clear (game closed):** `python save_lab.py clear`, then `python save_lab.py snap cleared "empty SAVEDATA"`
2. **New save:** launch the game, create a new save slot, do NOT play yet.
   - at the main menu: `snap newsave_menu "new slot created, main menu"`
   - close the game: `snap newsave_closed "game closed, nothing played"`
3. **Unlock run** (a fresh save is locked: 1P Play only offers Hero/Dark story). Start Hero story, play City Escape
   without going out of your way for drives, finish it and save when the game asks.
   - `snap cityescape_cleared "first stage cleared, picked up: <anything you noticed>"`
   - back at the menu, look for Stage Select -> Chao Garden: `snap menu_after_clear "chao garden option: yes/no"`
   - If the garden is still locked, stop and tell Claude before continuing.
   - `python save_lab.py diff newsave_closed cityescape_cleared` shows what a first clear unlocks.
4. **First garden visit (two unhatched eggs):** enter the garden and snapshot before touching anything.
   - `snap garden_entered "2 eggs unhatched; drive seen at: ..."`
   - hatch egg 1 by picking it up and shaking it (no throwing, no waiting), then `snap egg1_hatched`; same for egg 2, `snap egg2_hatched`
   - leave the purple drive alone. Don't feed, pet, or play with the Chao.
   - let the eggshells despawn (note if they don't), leave to the lobby, `snap garden1_lobby`
   - close the game, `snap garden1_closed`
5. **Revisit with nothing carried:** relaunch, Stage Select -> Chao Garden, enter and leave without touching anything.
   - `snap garden2_entered "is the drive still there? ..."`, close the game, `snap garden2_closed`
   - `python save_lab.py diff garden1_closed garden2_closed` shows idle noise, and whether the drive survived a restart.
6. **One drive:** play City Escape, collect exactly 1 Power Drive, finish the stage and save.
   - `snap stage_1drive` (game running). Enter the garden, leave, `snap garden_after_1drive`. Close the game, `snap closed_after_1drive`.
   - `python save_lab.py diff garden2_closed closed_after_1drive`
7. **Two drives:** repeat step 6 collecting exactly 2. Offsets that move by +1 then +2 are the item counter.

## Safety
- The originals are in `lab/archive/original_*`. `python save_lab.py restore baseline_original` puts them back.
- Never edit a save while the game is running.
