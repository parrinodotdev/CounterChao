#!/usr/bin/env python3
"""Copy the freshly built mod into your game's mods\\CounterChao folder.

   python tools\\deploy.py                 (uses the default Steam location)
   python tools\\deploy.py --game "D:\\Games\\Sonic Adventure 2"   (if the game is elsewhere)

Copies ONLY: CounterChao.dll, mod.ini, configschema.xml.
Never touches your config.ini, state.json or log files. Removes a leftover my-mod.dll.
Close the game first (Windows locks the DLL while it runs).
"""
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_GAME = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Sonic Adventure 2")
FILES = [  # (source, file name in the mod folder)
    (REPO / "mod" / "Release" / "CounterChao.dll", "CounterChao.dll"),
    (REPO / "package" / "mod.ini", "mod.ini"),
    (REPO / "package" / "configschema.xml", "configschema.xml"),
]


def main():
    game = DEFAULT_GAME
    if "--game" in sys.argv:
        i = sys.argv.index("--game")
        if i + 1 >= len(sys.argv):
            sys.exit("--game needs a folder after it")
        game = Path(sys.argv[i + 1])
    mods = game / "mods"
    if not mods.is_dir():
        sys.exit("Can't find %s\nIs the game installed elsewhere? Use --game \"<game folder>\"" % mods)

    missing = [str(src) for src, _ in FILES if not src.is_file()]
    if missing:
        sys.exit("Missing files (did you build Release x86?):\n  " + "\n  ".join(missing))

    dest = mods / "CounterChao"
    dest.mkdir(exist_ok=True)
    for src, name in FILES:
        try:
            shutil.copy2(src, dest / name)
        except PermissionError:
            sys.exit("Could not write %s - is the game or Mod Manager still running? Close it and try again." % (dest / name))
        print("copied  %s" % name)
    old = dest / "my-mod.dll"
    if old.exists():
        old.unlink()
        print("removed old my-mod.dll")
    print("\nDone: %s" % dest)
    print("Left alone: config.ini, state.json, log files.")


main()
