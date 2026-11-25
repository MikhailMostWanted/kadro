"""Inventory the native binaries in an already staged release candidate."""

import argparse
import hashlib
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dist", type=Path, help="Directory produced by cmake --install")
    dist = parser.parse_args().dist.resolve()
    licenses = dist / "licenses"
    mac = (dist / "Kadro.app/Contents/MacOS/Kadro").is_file()
    windows = (dist / "bin/Kadro.exe").is_file()
    if mac == windows or not licenses.is_dir():
        parser.error("expected one installed macOS or Windows runtime with its licenses directory")

    binaries = {}
    for path in sorted(dist.rglob("*")):
        if path.is_symlink() or not path.is_file():
            continue
        if (path.suffix.lower() in {".dll", ".exe", ".dylib"} or
                path == dist / "Kadro.app/Contents/MacOS/Kadro" or
                path.name.startswith("Qt") and path.parent.parent.name == "Versions"):
            digest = hashlib.sha256()
            with path.open("rb") as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(block)
            binaries[path.relative_to(dist).as_posix()] = digest.hexdigest()

    manifest = {
        "schema_version": 1,
        "platform": "macOS" if mac else "Windows",
        "binaries_sha256": binaries,
        "sdk_sboms": sorted(path.name for path in (licenses / "sbom").glob("*.spdx.json")),
    }
    (licenses / "runtime.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Recorded {len(binaries)} binaries and {len(manifest['sdk_sboms'])} SDK SBOMs.")


if __name__ == "__main__":
    main()
