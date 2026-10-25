#!/usr/bin/env python3
"""Write a reproducible FoxicMP family release manifest.

The tool only reads artifacts and source metadata.  It never copies, installs,
or changes profiles.  Paths may point to Linux or Windows artifacts when the
manifest is generated on the corresponding host.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from datetime import datetime, timezone


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def version(path: Path) -> str | None:
    try:
        result = subprocess.run(
            [str(path), "--version"], capture_output=True, text=True,
            timeout=5, check=False,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    output = (result.stdout + result.stderr).splitlines()
    return next((line.strip() for line in output if line.strip()), None)


def artifact(path: str | None) -> dict | None:
    if not path:
        return None
    file = Path(path).expanduser().resolve()
    result = {"path": str(file), "exists": file.is_file()}
    if file.is_file():
        result.update({"sha256": sha256(file), "version": version(file)})
    return result


def git(root: Path, args: list[str]) -> str | None:
    try:
        return subprocess.check_output(
            ["git", "-C", str(root), *args], text=True,
            stderr=subprocess.DEVNULL,
        ).strip()
    except (OSError, subprocess.SubprocessError):
        return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--qt-linux")
    parser.add_argument("--qt-windows")
    parser.add_argument("--classic")
    args = parser.parse_args()
    root = args.root.resolve()
    manifest = {
        "schemaVersion": 1,
        "product": "FoxicMP",
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "source": {
            "root": str(root),
            "commit": git(root, ["rev-parse", "HEAD"]),
            "dirty": bool(git(root, ["status", "--porcelain"])),
        },
        "artifacts": {
            "qtLinux": artifact(args.qt_linux),
            "qtWindows": artifact(args.qt_windows),
            "classicWindows": artifact(args.classic),
        },
    }
    output = args.output.expanduser()
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")
    temporary.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
