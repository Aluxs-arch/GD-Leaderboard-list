#!/usr/bin/env python3
"""Fail CI if the combined Geode package lacks metadata or native binaries."""

import json
import sys
from pathlib import Path
from zipfile import BadZipFile, ZipFile


def verify(directory: Path, manifest: Path) -> None:
    expected = json.loads(manifest.read_text(encoding="utf-8"))
    packages = list(directory.glob("*.geode"))
    if len(packages) != 1:
        raise ValueError(f"Expected exactly one .geode in {directory}, found {len(packages)}")

    package = packages[0]
    with ZipFile(package) as archive:
        bad_member = archive.testzip()
        if bad_member:
            raise ValueError(f"Corrupt ZIP entry: {bad_member}")
        names = archive.namelist()
        if "mod.json" not in names:
            raise ValueError("Package has no root mod.json")
        actual = json.loads(archive.read("mod.json"))
        for key in ("id", "version", "geode", "gd"):
            if actual.get(key) != expected.get(key):
                raise ValueError(f"mod.json {key}: expected {expected.get(key)!r}, got {actual.get(key)!r}")
        for extension, platform in ((".dll", "Windows"), (".dylib", "macOS/iOS"), (".so", "Android")):
            if not any(name.lower().endswith(extension) for name in names):
                raise ValueError(f"Package has no {platform} binary ({extension})")
    print(f"Verified {package} ({package.stat().st_size} bytes, {len(names)} ZIP entries)")


if __name__ == "__main__":
    try:
        verify(Path(sys.argv[1]), Path(sys.argv[2]))
    except (IndexError, OSError, ValueError, BadZipFile) as error:
        print(f"Invalid Geode package: {error}", file=sys.stderr)
        sys.exit(1)
