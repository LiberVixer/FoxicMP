#!/usr/bin/env python3
"""Generate the machine-readable FoxicMP family command catalog."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

PATTERN = re.compile(
    r'^FOXICMP_FAMILY_COMMAND\(([^,]+),\s*([^,]+),\s*"([^"]*)",\s*([^,]+),\s*([^\)]+)\)\s*$'
)


def load(path: Path) -> list[dict]:
    commands = []
    for line in path.read_text(encoding="utf-8").splitlines():
        match = PATTERN.match(line.strip())
        if not match:
            continue
        qt_id, classic_id, shortcut, vkey, modifiers = match.groups()
        commands.append({"id": qt_id.strip(), "classicId": classic_id.strip(),
                         "shortcut": shortcut, "virtualKey": int(vkey.strip(), 0),
                         "modifiers": int(modifiers.strip(), 0)})
    if not commands:
        raise ValueError(f"No family commands found in {path}")
    return commands


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=Path("shared/familycommands.inc"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    commands = load(args.input)
    ids = [item["id"] for item in commands]
    classic = [item["classicId"] for item in commands]
    if len(ids) != len(set(ids)) or len(classic) != len(set(classic)):
        raise SystemExit("Duplicate family command identifier")
    result = {"schemaVersion": 1, "product": "FoxicMP",
              "source": str(args.input), "commands": commands}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
