"""Package portable distributions and the source tree."""

import argparse
import hashlib
from pathlib import Path
import shutil
import zipfile


ROOT = Path(__file__).resolve().parent
OUT = ROOT / "dist"
VERSION = "0.2.0_alpha"
WINDOWS_VIEWER = "AirConflicts_Viewer_Windows_x64"
EXCLUDE_DIRS = {"dist", "__pycache__", ".git"}
EXCLUDE_EXTENSIONS = {".pyc", ".blend1", ".bmf", ".msh", ".dds", ".blend", ".exe", ".max"}


def files(root):
    return sorted(
        path for path in root.rglob("*") if path.is_file()
        and not EXCLUDE_DIRS.intersection(path.relative_to(root).parts)
        and path.suffix.lower() not in EXCLUDE_EXTENSIONS
    )


def pack(name, entries):
    output = OUT / name
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path, member in entries:
            archive.write(path, member)
    return output


def main(windows_exe=None):
    OUT.mkdir(exist_ok=True)
    shutil.copyfile(ROOT / "ac_formats.py", ROOT / "blender_addon/ac_formats.py")
    docs = [(ROOT / name, name) for name in ("README.md", "LICENSE")]
    source = [(path, path.relative_to(ROOT).as_posix()) for path in files(ROOT)]
    blender = pack("AirConflicts_Blender_Addon.zip", [
        (path, "air_conflicts_workshop/" + path.relative_to(ROOT / "blender_addon").as_posix())
        for path in files(ROOT / "blender_addon")
    ])
    web = pack("AirConflicts_Viewer.zip", [
        (ROOT / "viewer" / name, name) for name in ("index.html", "codec.js", "viewer.js")
    ] + docs)
    max_addon = pack("AirConflicts_Max_Adapter.zip", [
        (ROOT / name, name) for name in ("max/AirConflictsWorkshop.ms", "max_bridge.py", "ac_formats.py")
    ] + docs)
    source_zip = pack("AirConflicts_Workshop_Source.zip", source)
    packages = [blender, web, max_addon, source_zip]
    toolkit_entries = source + [(blender, "Install_Blender_Addon.zip")]
    if windows_exe:
        binary = OUT / f"{WINDOWS_VIEWER}.exe"
        if windows_exe.resolve() != binary.resolve():
            shutil.copyfile(windows_exe, binary)
        quickstart = ROOT / "viewer/native/QUICK_START_zh-CN.txt"
        packages.append(binary)
        packages.append(pack(f"{WINDOWS_VIEWER}.zip", [
            (binary, binary.name), (quickstart, quickstart.name), (ROOT / "LICENSE", "LICENSE")
        ]))
        toolkit_entries.append((binary, f"viewer/windows/{binary.name}"))
    packages.append(pack(f"AirConflicts_Workshop_v{VERSION}.zip", toolkit_entries))
    (OUT / "SHA256SUMS.txt").write_text(
        "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in sorted(packages)),
        encoding="ascii",
    )
    for path in packages:
        print(f"{path.name}: {path.stat().st_size} bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--windows-exe", type=Path, help="Include the compiled native Windows x64 viewer")
    main(parser.parse_args().windows_exe)
