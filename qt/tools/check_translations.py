#!/usr/bin/env python3
"""Verify the bundled English catalog against uiTr literals and placeholders."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
catalog = json.loads((root / 'resources/translations/en.json').read_text())['translations']
pattern = re.compile(r'uiTr\(\s*("(?:[^"\\]|\\.)*"(?:\s*"(?:[^"\\]|\\.)*")*)\)')
sources = set()
for path in (root / 'src').glob('*.cpp'):
    for match in pattern.finditer(path.read_text()):
        sources.add(''.join(json.loads(part) for part in re.findall(r'"(?:[^"\\]|\\.)*"', match[1])))
missing = sorted(sources - catalog.keys())
invalid = [key for key, value in catalog.items() if not isinstance(value, str)
           or sorted(re.findall(r'%L?[1-9][0-9]*|%n', key))
           != sorted(re.findall(r'%L?[1-9][0-9]*|%n', value))]
if missing or invalid:
    raise SystemExit(f'Missing translations: {missing}\nInvalid placeholders: {invalid}')
print(f'English catalog: {len(sources)} source strings covered; {len(catalog)} entries; placeholders valid')
