"""CounterChao save lab: snapshot, diff and restore SA2 save files, with a written log.

Run from the CounterChao folder:
  python save_lab.py init                 create lab folders, tidy old files, back up SAVEDATA (verified), snapshot #1
  python save_lab.py snap LABEL [NOTE]    copy the current save files into a numbered snapshot and log it
  python save_lab.py list                 show all snapshots
  python save_lab.py diff A B             byte diff of two snapshots (number or label) -> lab/reports/
  python save_lab.py clear                MOVE live save files into the archive (fresh start). Game must be closed.
  python save_lab.py restore LABEL        put a snapshot back into SAVEDATA. Game must be closed.

Nothing here ever deletes a save: 'clear' and 'restore' move whatever was there into lab/archive/.
Optional: add "sa2_savedata_dir": "C:\\\\path\\\\to\\\\SAVEDATA" to config.json if your install isn't in the default Steam spot.
"""
import hashlib
import json
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent  # repo root
LAB = ROOT / "lab"
SNAPS = LAB / "snapshots"
REPORTS = LAB / "reports"
ARCHIVE = LAB / "archive"
LOG = LAB / "log.md"
DEFAULT_SAVEDATA = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Sonic Adventure 2\resource\gd_PC\SAVEDATA")
GAME_EXE = "sonic2app.exe"


# ---------- helpers ----------
def now():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def stamp():
    return datetime.now().strftime("%Y%m%d_%H%M%S")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def clean(label):
    return "".join(c if c.isalnum() or c in "-_" else "_" for c in label)


def savedata_dir():
    try:
        cfg = json.loads((ROOT / "config.json").read_text(encoding="utf-8"))
    except Exception:
        cfg = {}
    p = Path(cfg.get("sa2_savedata_dir", DEFAULT_SAVEDATA))
    if not p.is_dir():
        sys.exit(f"SAVEDATA folder not found: {p}\nAdd \"sa2_savedata_dir\" to config.json with the right path.")
    return p


def game_running():
    try:
        out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {GAME_EXE}"],
                             capture_output=True, text=True).stdout
        return GAME_EXE.lower() in out.lower()
    except Exception:
        return False


def require_closed():
    if game_running():
        sys.exit("Sonic Adventure 2 is running. Close it completely first (the game rewrites saves on exit).")


def log(text):
    LAB.mkdir(exist_ok=True)
    if not LOG.exists():
        LOG.write_text("# CounterChao save lab log\n\n", encoding="utf-8")
    with LOG.open("a", encoding="utf-8") as f:
        f.write(f"- {now()}  {text}\n")


def snapshots():
    return sorted(p for p in SNAPS.glob("[0-9][0-9][0-9]_*") if p.is_dir()) if SNAPS.exists() else []


def find_snap(ref):
    snaps = snapshots()
    if ref.isdigit():
        hits = [s for s in snaps if int(s.name[:3]) == int(ref)]
    else:
        hits = [s for s in snaps if s.name[4:] == clean(ref) or s.name == ref]
    if not hits:
        sys.exit(f"No snapshot matches '{ref}'. Try: python save_lab.py list")
    return hits[-1]


def count_diff(a, b):
    return sum(1 for x, y in zip(a, b) if x != y) + abs(len(a) - len(b))


def runs(a, b):
    out = []
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            if out and i - out[-1][1] <= 4:
                out[-1][1] = i
            else:
                out.append([i, i])
    return out


def report(name, a, b):
    rs = runs(a, b)
    lines = [f"== {name}: {len(a)} vs {len(b)} bytes, {count_diff(a, b)} changed in {len(rs)} runs =="]
    for s, e in rs:
        line = f"0x{s:06X}-0x{e:06X} ({e - s + 1}b)  {a[s:e + 1].hex(' ')} -> {b[s:e + 1].hex(' ')}"
        if e - s + 1 <= 4:
            line += f"   [as number: {int.from_bytes(a[s:e + 1], 'little')} -> {int.from_bytes(b[s:e + 1], 'little')}]"
        lines.append(line)
    return lines


def snap_files(snap):
    return {p.name: p for p in snap.iterdir() if p.is_file() and p.name != "manifest.json"}


# ---------- commands ----------
def tidy():
    """Move the loose files from the first experiments into the lab/data folders."""
    early = ARCHIVE / "early_experiments"
    leet = ROOT / "data" / "leetify"
    plan = [("before.*", early), ("after_*", early), ("control.*", early),
            ("history_summary*", leet), ("match_detail_sample*", leet), ("last_match_sample.json", leet)]
    for pattern, dest in plan:
        for f in ROOT.glob(pattern):
            if f.is_file():
                dest.mkdir(parents=True, exist_ok=True)
                shutil.move(str(f), dest / f.name)
                print(f"moved {f.name} -> {dest.relative_to(ROOT)}")


def cmd_init():
    for d in (SNAPS, REPORTS, ARCHIVE, ROOT / "data" / "leetify"):
        d.mkdir(parents=True, exist_ok=True)
    tidy()
    src = savedata_dir()
    if not any(ARCHIVE.glob("original_*")):
        dest = ARCHIVE / f"original_{stamp()}"
        dest.mkdir()
        files = [p for p in src.iterdir() if p.is_file()]
        for f in files:
            shutil.copy2(f, dest / f.name)
            if sha(f) != sha(dest / f.name):
                sys.exit(f"Backup verification FAILED for {f.name}. Stopping.")
        log(f"init: backed up {len(files)} original save files to archive/{dest.name} (hashes verified)")
        print(f"Backed up and verified {len(files)} files -> {dest.relative_to(ROOT)}")
    else:
        print("Original backup already exists; leaving it alone.")
    if not snapshots():
        cmd_snap("baseline_original", "state before any experiments")
    print(f"\nLab ready at {LAB}. Log: {LOG}")


def cmd_snap(label, note=""):
    src = savedata_dir()
    SNAPS.mkdir(parents=True, exist_ok=True)
    prev = snapshots()
    n = int(prev[-1].name[:3]) + 1 if prev else 1
    dest = SNAPS / f"{n:03d}_{clean(label)}"
    dest.mkdir()
    running = game_running()
    manifest = {"taken": now(), "label": label, "note": note, "game_running": running, "files": {}}
    for f in sorted(p for p in src.iterdir() if p.is_file()):
        shutil.copy2(f, dest / f.name)
        manifest["files"][f.name] = {"bytes": f.stat().st_size, "sha256": sha(f)}
    (dest / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    summary = f"snapshot {dest.name}: {len(manifest['files'])} files, game {'RUNNING' if running else 'closed'}"
    if prev:
        old_files, new_files = snap_files(prev[-1]), snap_files(dest)
        changes = []
        for name in sorted(set(old_files) | set(new_files)):
            if name not in old_files:
                changes.append(f"{name} new")
            elif name not in new_files:
                changes.append(f"{name} removed")
            else:
                d = count_diff(old_files[name].read_bytes(), new_files[name].read_bytes())
                if d:
                    changes.append(f"{name} {d} bytes changed")
        summary += f" | vs {prev[-1].name}: " + ("; ".join(changes) or "no changes")
    log(summary + (f" | note: {note}" if note else ""))
    print(summary)


def cmd_list():
    for s in snapshots():
        m = json.loads((s / "manifest.json").read_text(encoding="utf-8"))
        print(f"{s.name:40} {m['taken']}  {len(m['files'])} files  {m['note']}")
    if not snapshots():
        print("No snapshots yet. Run: python save_lab.py init")


def cmd_diff(ra, rb):
    sa, sb = find_snap(ra), find_snap(rb)
    fa, fb = snap_files(sa), snap_files(sb)
    lines = [f"# {sa.name}  vs  {sb.name}"]
    for name in sorted(set(fa) | set(fb)):
        if name not in fa:
            lines.append(f"== {name}: only in {sb.name}")
        elif name not in fb:
            lines.append(f"== {name}: only in {sa.name}")
        else:
            lines += report(name, fa[name].read_bytes(), fb[name].read_bytes())
    REPORTS.mkdir(parents=True, exist_ok=True)
    out = REPORTS / f"{sa.name}__vs__{sb.name}.txt"
    out.write_text("\n".join(lines), encoding="utf-8")
    print("\n".join(lines[:150]))
    if len(lines) > 150:
        print(f"... {len(lines) - 150} more lines in the report file")
    print(f"\nFull report: {out}")


def cmd_clear():
    require_closed()
    src = savedata_dir()
    if not any(ARCHIVE.glob("original_*")):
        sys.exit("Run 'python save_lab.py init' first so a verified backup exists.")
    files = [p for p in src.iterdir() if p.is_file()]
    if not files:
        sys.exit("SAVEDATA is already empty.")
    if input(f"This MOVES {len(files)} save files out of SAVEDATA into the lab archive. Type CLEAR to continue: ").strip() != "CLEAR":
        sys.exit("Cancelled.")
    dest = ARCHIVE / f"moved_{stamp()}"
    dest.mkdir(parents=True)
    for f in files:
        shutil.move(str(f), dest / f.name)
    log(f"clear: moved {len(files)} live save files to archive/{dest.name}")
    print(f"Moved {len(files)} files to {dest.relative_to(ROOT)}. Now run: python save_lab.py snap cleared \"empty SAVEDATA\"")


def cmd_restore(ref):
    require_closed()
    snap, src = find_snap(ref), savedata_dir()
    pre = ARCHIVE / f"pre_restore_{stamp()}"
    pre.mkdir(parents=True)
    for f in [p for p in src.iterdir() if p.is_file()]:
        shutil.move(str(f), pre / f.name)
    for name, f in snap_files(snap).items():
        shutil.copy2(f, src / name)
    log(f"restore: put {snap.name} into SAVEDATA (previous contents moved to archive/{pre.name})")
    print(f"Restored {snap.name}. Previous contents saved in {pre.relative_to(ROOT)}")


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
    elif a[0] == "init":
        cmd_init()
    elif a[0] == "snap" and len(a) >= 2:
        cmd_snap(a[1], " ".join(a[2:]))
    elif a[0] == "list":
        cmd_list()
    elif a[0] == "diff" and len(a) == 3:
        cmd_diff(a[1], a[2])
    elif a[0] == "clear":
        cmd_clear()
    elif a[0] == "restore" and len(a) == 2:
        cmd_restore(a[1])
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
