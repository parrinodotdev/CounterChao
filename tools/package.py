r"""
package.py - build the CounterChao release zip.

Run from the repo root, with the game closed or open (it does not touch the game folder):

    python tools\package.py
    python tools\package.py --version 0.1.0-beta
    python tools\package.py --dll C:\path\to\CounterChao.dll

What it does:
  1. Finds the Release|Win32 DLL you just built.
  2. Checks the DLL does not depend on the Visual C++ Redistributable (/MT worked).
  3. Builds dist\CounterChao-v<version>.zip with one top-level folder, CounterChao\,
     so players can extract it straight into the game's mods folder.
  4. Writes dist\CounterChao-v<version>.zip.sha256

Zip contents:
  CounterChao\CounterChao.dll
  CounterChao\mod.ini
  CounterChao\configschema.xml
  CounterChao\LICENSE.txt                      (this project's license)
  CounterChao\LICENSE-X-Hax-template.txt       (SA2 Mod Loader template license)
  CounterChao\THIRD-PARTY-NOTICES.txt          (nlohmann/json, MIT)

It never includes config.ini, state.json or any log file.
"""
import argparse
import hashlib
import re
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
TOP = "CounterChao"

# Where Visual Studio may have put the Release|Win32 DLL (newest one wins).
DLL_CANDIDATES = [
    ROOT / "mod" / "Release" / "CounterChao.dll",
    ROOT / "mod" / "CounterChao" / "Release" / "CounterChao.dll",
    ROOT / "mod" / "x86" / "Release" / "CounterChao.dll",
]

SOURCES = {
    "mod.ini": ROOT / "package" / "mod.ini",
    "configschema.xml": ROOT / "package" / "configschema.xml",
    "LICENSE.txt": ROOT / "LICENSE",
    "LICENSE-X-Hax-template.txt": ROOT / "mod" / "LICENSE-X-Hax-template",
}
JSON_HEADER = ROOT / "mod" / "CounterChao" / "json.hpp"

MIT_TEXT = """\
MIT License

Copyright (c) {copyright}

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""


def fail(msg):
    print(f"\nSTOPPED: {msg}")
    sys.exit(1)


def find_dll(explicit):
    if explicit:
        p = Path(explicit)
        if not p.is_file():
            fail(f"--dll file not found: {p}")
        return p
    found = [p for p in DLL_CANDIDATES if p.is_file()]
    if not found:
        tried = "\n  ".join(str(p) for p in DLL_CANDIDATES)
        fail("could not find CounterChao.dll. Build Release|x86 first. Looked in:\n  " + tried +
             "\nIf it is somewhere else, run: python tools\\package.py --dll <full path>")
    return max(found, key=lambda p: p.stat().st_mtime)


def check_runtime(dll_bytes):
    """With /MT the DLL must not import the Visual C++ runtime DLLs."""
    low = dll_bytes.lower()
    hits = [n.decode() for n in (b"vcruntime140", b"msvcp140") if n in low]
    if hits:
        fail("the DLL still imports " + ", ".join(hits) + ".dll, so players would need the "
             "Visual C++ Redistributable. Check Release|Win32 > C/C++ > Code Generation > "
             "Runtime Library is Multi-threaded (/MT), then Rebuild Solution.")
    print("  runtime check: OK (no vcruntime140 / msvcp140 import)")


def json_notice():
    copyright_line = "2013-2022 Niels Lohmann"
    version = "unknown"
    if JSON_HEADER.is_file():
        head = JSON_HEADER.read_text(encoding="utf-8", errors="ignore")[:6000]
        m = re.search(r"SPDX-FileCopyrightText:\s*(.+)", head)
        if m:
            copyright_line = m.group(1).strip()
        v = [re.search(rf"#define NLOHMANN_JSON_VERSION_{k}\s+(\d+)", head) for k in ("MAJOR", "MINOR", "PATCH")]
        if all(v):
            version = ".".join(x.group(1) for x in v)
    return (
        "CounterChao includes the following third-party software.\n\n"
        f"JSON for Modern C++ (nlohmann/json) version {version}\n"
        "https://github.com/nlohmann/json\n\n" + MIT_TEXT.format(copyright=copyright_line)
    )


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="0.1.0-beta")
    ap.add_argument("--dll", default=None)
    args = ap.parse_args()

    print(f"Packaging CounterChao v{args.version}")
    dll = find_dll(args.dll)
    print(f"  DLL: {dll}  ({dll.stat().st_size:,} bytes)")
    dll_bytes = dll.read_bytes()
    check_runtime(dll_bytes)

    for name, path in SOURCES.items():
        if not path.is_file():
            fail(f"missing file for {name}: {path}")

    ini = SOURCES["mod.ini"].read_text(encoding="utf-8", errors="ignore")
    if "counterchao.dll" not in ini.lower():
        print("  WARNING: package\\mod.ini does not mention CounterChao.dll - check DLLFile=")

    DIST.mkdir(exist_ok=True)
    zip_path = DIST / f"CounterChao-v{args.version}.zip"
    if zip_path.exists():
        zip_path.unlink()

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(f"{TOP}/CounterChao.dll", dll_bytes)
        for name, path in SOURCES.items():
            z.write(path, f"{TOP}/{name}")
        z.writestr(f"{TOP}/THIRD-PARTY-NOTICES.txt", json_notice())

    digest = hashlib.sha256(zip_path.read_bytes()).hexdigest()
    sha_path = zip_path.with_name(zip_path.name + ".sha256")
    sha_path.write_text(f"{digest}  {zip_path.name}\n", encoding="utf-8")

    print("\nZip contents:")
    with zipfile.ZipFile(zip_path) as z:
        for info in z.infolist():
            print(f"  {info.filename}  ({info.file_size:,} bytes)")
    print(f"\nCreated: {zip_path}")
    print(f"Created: {sha_path}")
    print(f"SHA-256: {digest}")
    print("\nNext: clean-install test. Extract the zip into a NEW empty folder first and"
          "\ncheck the file list above, then test it in the game's mods folder.")


if __name__ == "__main__":
    main()
