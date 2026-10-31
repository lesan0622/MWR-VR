"""Package a Release build into a one-click installer.

Usage: python tools/build_installer.py [Release]
The installer also takes /silent (detected folder, no dialogs; exit code 0 on success).
Output: build/dist/OverlordMWR-VR-Setup.exe (and the same files as a .zip for
manual installs). Build the solution first (generate.bat, then the Release
configuration); this only packages what the game folder needs.
"""
from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CONFIG = sys.argv[1] if len(sys.argv) > 1 else "Release"
BIN = ROOT / "build/bin/x64" / CONFIG
DIST = ROOT / "build/dist"
# The same set port/deploy.sh copies into the game folder (no .pdb/.lib).
FILES = ["overlord.exe", "openxr_loader.dll", "overlord.vrmanifest"]
FOLDERS = ["steamvr", "vr_input", "overlord-mwr"]
CSC = Path(r"C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe")


def payload(target: Path) -> None:
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as archive:
        for name in FILES:
            source = BIN / name
            if not source.is_file():
                raise SystemExit(f"missing {source} (build {CONFIG} first)")
            archive.write(source, name)
        for folder in FOLDERS:
            base = BIN / folder
            if not base.is_dir():
                raise SystemExit(f"missing {base} (build {CONFIG} first)")
            for path in sorted(base.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(BIN).as_posix())


def main() -> None:
    if not CSC.is_file():
        raise SystemExit(f"missing {CSC} (.NET Framework 4.x)")
    DIST.mkdir(parents=True, exist_ok=True)
    version = json.loads((ROOT / "version.json").read_text(encoding="utf-8")).get("name", "dev").replace(" ", "-")
    zip_path = DIST / "payload.zip"
    payload(zip_path)
    setup = DIST / "OverlordMWR-VR-Setup.exe"
    subprocess.run([str(CSC), "/nologo", "/target:winexe", "/optimize+", "/platform:anycpu",
                    f"/out:{setup}", f"/win32icon:{ROOT / 'assets/icon/Icon.ico'}",
                    f"/resource:{zip_path},payload.zip",
                    "/reference:System.IO.Compression.dll", "/reference:System.IO.Compression.FileSystem.dll",
                    "/reference:System.Windows.Forms.dll",
                    str(ROOT / "tools/installer/Installer.cs")], check=True)
    manual = DIST / f"OverlordMWR-VR-{version}.zip"
    zip_path.replace(manual)
    print(f"{setup} ({setup.stat().st_size // 1024} KB)\n{manual}")


if __name__ == "__main__":
    main()
