"""Launch installed Wanted View with developer Qt paths removed from its environment.

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
    if len(sys.argv) < 3:
        raise SystemExit("usage: check_package.py DIST FIXTURE_DIR [MEDIA ...]")
    dist, fixtures = (Path(value).resolve() for value in sys.argv[1:3])
    media_files = sys.argv[3:] or ["photo.png", "video.avi"]
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
        profile = Path(working) / "профиль проверки"
        for index, media in enumerate(media_files):
            profile_args = ["--settings-dir", str(profile)] if index == 0 else []
            fixture = fixtures / media
            if not fixture.is_file():
                raise SystemExit(f"Missing test fixture: {fixture}")
            screenshot = fixtures / f"packaged-{Path(media).stem}.png"
            result = subprocess.run(
                [str(executable), *profile_args, "--smoke-screenshot", str(screenshot), str(fixture)],
                env=env, cwd=working, capture_output=True, timeout=30,
            )
            print(result.stdout.decode("utf-8", errors="replace"))
            print(result.stderr.decode("utf-8", errors="replace"), file=sys.stderr)
            if result.returncode != 0 or not screenshot.is_file():
                raise SystemExit(f"Installed media test failed: {media}, exit={result.returncode}")
            print(f"PASS installed package: {fixture.name}")

        settings = profile / "user" / "Kadro" / "Kadro.ini"
        if not settings.is_file() or b"lastFolder=" not in settings.read_bytes():
            raise SystemExit("The diagnostic launch did not write settings into its explicit profile")
        blocked = Path(working) / "not-a-directory"
        blocked.write_bytes(b"keep")
        blocked_capture = Path(working) / "should-not-capture.png"
        result = subprocess.run(
            [str(executable), "--settings-dir", str(blocked),
             "--smoke-screenshot", str(blocked_capture)],
            env=env, cwd=working, capture_output=True, timeout=15,
        )
        if result.returncode != 5 or blocked_capture.exists() or blocked.read_bytes() != b"keep":
            raise SystemExit(f"Invalid profile was not rejected safely: exit={result.returncode}")
        print("PASS explicit profile persistence and invalid profile rejection")
    return 0


if __name__ == "__main__":
    sys.exit(main())
