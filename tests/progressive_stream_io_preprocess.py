"""Compare normal preprocessor tokens, retaining literal contents and spelling.

Run separately for each compiler; its STL expansion need not match another STL.
This checks exclusion of private barriers, not a complete C++ ABI/decoder test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import tempfile

TOKEN = re.compile(
    r'(?P<space>\s+)|(?P<comment>//[^\n]*|/\*[\s\S]*?\*/)|'
    r'(?P<raw>(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\([\s\S]*?\)(?P=delimiter)")|'
    r'(?P<literal>(?:u8|u|U|L)?(?:"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'))|'
    r'(?P<identifier>[^\W\d]\w*)|'
    r'(?P<number>(?:\d|\.\d)(?:[eEpP][+-]|[\w.\'])* )|'
    r'(?P<punct>%:%:|>>=|<<=|<=>|->\*|\.\.\.|::|\.\*|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||\*=|/=|%=|\+=|-=|&=|\^=|\|=|\#\#|<:|:>|<%|%>|%:)|'
    r'(?P<other>.)', re.DOTALL | re.VERBOSE)


def fingerprint(path):
    raw = path.read_bytes()
    text = raw.decode('utf-16' if raw.startswith(b'\xff\xfe') else 'utf-8-sig')
    assert 'privateIoBoundary' not in text, 'Private hook leaked into normal preprocessing'
    digest = hashlib.sha256()
    count = 0
    for token in TOKEN.finditer(text):
        if token.lastgroup in ('space', 'comment'):
            continue
        value = token.group().encode('utf-8')
        digest.update(len(value).to_bytes(8, 'little'))
        digest.update(value)
        count += 1
    return dict(rawSha256=hashlib.sha256(raw).hexdigest(),
                tokenSha256=digest.hexdigest(), tokens=count,
                privateHookPresent=False)


def compare(before, after):
    a, b = fingerprint(before), fingerprint(after)
    assert a['tokenSha256'] == b['tokenSha256'] and a['tokens'] == b['tokens'], (a, b)
    return dict(result='PASS', before=a, after=b,
                comparison='Preprocessor tokens; layout ignored, literal spelling retained')


def self_test():
    cases = [
        ('int a;', 'int\n a ;', True),
        ('int/*comment*/ a;', 'int a;', True),
        ('const char* s="a  b";', 'const char * s = "a  b" ;', True),
        ('auto s=R"tag(a\nb)tag";', 'auto s = R"tag(a\nb)tag" ;', True),
        ('auto s="a  b";', 'auto s="a b";', False),
        ('auto s=R"tag(a\nb)tag";', 'auto s=R"tag(a b)tag";', False),
        ('unsigned int a;', 'unsignedint a;', False),
        ('a + +b;', 'a++b;', False),
        ('double a=1e+2;', 'double a=1e+3;', False),
        ('L "x";', 'L"x";', False),
        ("char a=' ';", "char a='x';", False),
        (r'auto s="\n";', r'auto s="\t";', False),
    ]
    with tempfile.TemporaryDirectory(prefix='foxicmp-pp-checker-') as root:
        before, after = Path(root, 'before'), Path(root, 'after')
        for left, right, expected in cases:
            before.write_text(left, encoding='utf-8')
            after.write_text(right, encoding='utf-8')
            try:
                compare(before, after)
                actual = True
            except AssertionError:
                actual = False
            assert actual == expected, (left, right, expected)
        before.write_text('privateIoBoundary(1);', encoding='utf-8')
        try:
            fingerprint(before)
        except AssertionError:
            pass
        else:
            raise AssertionError('Leaked hook was accepted')
    return dict(result='PASS', checks=len(cases) + 1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path, nargs='?')
    parser.add_argument('after', type=Path, nargs='?')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        print(json.dumps(self_test()))
    else:
        if args.before is None or args.after is None:
            parser.error('before and after are required')
        print(json.dumps(compare(args.before, args.after)))
