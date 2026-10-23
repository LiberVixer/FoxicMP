#!/usr/bin/env python3
"""Extract checked-in Classic menu structure; do not guess labels or ordering."""
from pathlib import Path
import json
import re
import argparse

root = Path(__file__).resolve().parents[2]
ids = ['IDR_MAINFRAME', 'IDR_POPUP', 'IDR_POPUPMAIN', 'IDR_POPUP_VIDEOFRAME',
       'IDR_POPUP_PANSCAN', 'IDR_POPUP_AFTERPLAYBACK', 'IDR_POPUP_SHADERS', 'IDR_POPUP_NAVIGATE']

def extract(path):
    text = path.read_text(encoding='utf-8-sig')
    result = {}
    for ident in ids:
        match = re.search(r'^' + ident + r' MENU\s*\nBEGIN\s*\n', text, re.M)
        if not match:
            continue
        stack = [[]]
        pending = None
        for line in text[match.end():].splitlines():
            line = line.strip()
            if line == 'BEGIN':
                assert pending is not None
                stack.append(pending['children'])
                pending = None
            elif line == 'END':
                if len(stack) == 1:
                    break
                stack.pop()
            elif line == 'MENUITEM SEPARATOR':
                stack[-1].append({'separator': True})
            elif m := re.match(r'POPUP "((?:""|[^"\n])*)"', line):
                pending = {'text': m[1].replace('""', '"'), 'children': []}
                stack[-1].append(pending)
            elif m := re.match(r'MENUITEM "((?:""|[^"\n])*)",\s*(\w+)', line):
                stack[-1].append({'text': m[1].replace('""', '"'), 'id': m[2]})
        result[ident] = stack[0]
    labels = dict(re.findall(r'^\s*(IDS_VIEW_\w+)\s+"([^"]+)"', text, re.M))
    result['captionModes'] = [labels[key].split('\\t')[0] for key in
                              ('IDS_VIEW_HIDEMENU', 'IDS_VIEW_FRAMEONLY', 'IDS_VIEW_BORDERLESS', 'IDS_VIEW_CAPTIONMENU')]
    result['settings'] = [{'id': match[1], 'text': match[2]} for match in re.finditer(r'^\s*(IDD_PPAGE\w+)\s+"([^"]+)"', text, re.M)]
    return result

output = {'schema': 1, 'source': 'src/apps/{mplayerc,FoxicMPresources}/mplayerc[.ru].rc',
          'ru': extract(root/'src/apps/FoxicMPresources/mplayerc.ru.rc'),
          'en': extract(root/'src/apps/mplayerc/mplayerc.rc')}
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true', help='check without rewriting the generated resource')
args = parser.parse_args()
encoded = json.dumps(output, ensure_ascii=False, indent=2)+'\n'
target = root/'qt/resources/classic-menus.json'
if args.check:
    if not target.exists() or target.read_text() != encoded:
        raise SystemExit('Classic UI resource is stale; run qt/tools/extract_classic_ui.py')
else:
    target.write_text(encoded)
print('Classic UI:', len(ids), 'menus and', len(output['ru']['settings']), 'settings pages per language')
