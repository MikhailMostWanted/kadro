"""Launch installed KADRO with developer Qt paths removed from its environment.

Requires fixtures from kadro_ui_tests. It does not modify associations or
user files; the isolated process exits after a genuinely decoded media frame.
This is a CI packaging test, not a clean-OS installation/signing certificate.
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("usage: check_package.py DIST FIXTURE_DIR")
    dist, fixtures = (Path(value).resolve() for value in sys.argv[1:])
    executable = (dist / "Kadro.app/Contents/MacOS/Kadro" if sys.platform == "darwin"
                  else dist / "bin/Kadro.exe")
    if not executable.is_file():
        raise SystemExit(f"Missing installed executable: {executable}")
    env = dict(os.environ)
    qt_root = env.get("QT_ROOT_DIR", "").replace("\\", "/").lower().rstrip("/")
    env["PATH"] = os.pathsep.join(
        part for part in env.get("PATH", "").split(os.pathsep)
        if not (qt_root and part.replace("\\", "/").lower().rstrip("/").startswith(qt_root))
    )
    for key in ("QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML2_IMPORT_PATH",
                "QML_IMPORT_PATH", "QT_ROOT_DIR", "QTDIR", "LD_LIBRARY_PATH",
                "DYLD_LIBRARY_PATH", "DYLD_FRAMEWORK_PATH", "QT_QPA_PLATFORM"):
        env.pop(key, None)
    # Native platform plugin: Windows/macOS runners have a desktop session.
    with tempfile.TemporaryDirectory(prefix="kadro-package-") as working:
        for media in ("photo.png", "video.avi"):
            fixture = fixtures / media
            if not fixture.is_file():
                raise SystemExit(f"Missing test fixture: {fixture}")
            screenshot = fixtures / f"packaged-{Path(media).stem}.png"
            result = subprocess.run(
                [str(executable), "--isolated", "--smoke-screenshot", str(screenshot), str(fixture)],
                env=env, cwd=working, capture_output=True, timeout=30,
            )
            print(result.stdout.decode("utf-8", errors="replace"))
            print(result.stderr.decode("utf-8", errors="replace"), file=sys.stderr)
            if result.returncode != 0 or not screenshot.is_file():
                raise SystemExit(f"Installed media test failed: {media}, exit={result.returncode}")
            print(f"PASS installed package: {media}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
