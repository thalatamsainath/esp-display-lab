#!/usr/bin/env python3
"""Check candidate public source files for common private inputs and identifiers."""
import argparse
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ALLOWED_IPS = {'127.0.0.1', '192.168.4.1', '192.168.1.50'}
PERSONAL_PATH = re.compile(r'/(?:Users|home)/[^/\s<>"\']+')
EMAIL = re.compile(r'\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b')
TOKEN = re.compile(r'\b(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|sk-(?:proj-|ant-)?[A-Za-z0-9_-]{20,})\b')
IP = re.compile(r'(?<![\w.])(?:\d{1,3}\.){3}\d{1,3}(?![\w.])')
PRIVATE_NAMES = {'ota_credentials.h', 'sender-config.json', 'config.json', '.env'}
GENERATED_SUFFIXES = {'.bin', '.elf', '.map', '.zip', '.gz', '.pyc', '.log', '.jsonl', '.jpg', '.jpeg'}


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--staged', action='store_true', help='Read the Git index, not worktree copies')
    args = parser.parse_args()
    if args.staged:
        names = git('ls-files', '-z').decode().split('\0')
    else:
        names = git('ls-files', '--cached', '--others', '--exclude-standard', '-z').decode().split('\0')
    issues = []
    checked = 0
    for name in sorted(set(filter(None, names))):
        path = Path(name)
        if path.name in PRIVATE_NAMES or path.suffix in GENERATED_SUFFIXES or name.startswith(('build/', 'toolchains/', '.arduino/')):
            issues.append((name, 'private or generated file'))
            continue
        raw = git('show', ':' + name) if args.staged else (ROOT / name).read_bytes()
        if b'\0' in raw:
            issues.append((name, 'unexpected binary file; review manually'))
            continue
        try:
            text = raw.decode('utf-8')
        except UnicodeDecodeError:
            issues.append((name, 'unexpected non-UTF-8 file; review manually'))
            continue
        checked += 1
        for label, pattern in [('personal home path', PERSONAL_PATH), ('email address', EMAIL), ('credential token', TOKEN)]:
            if pattern.search(text):
                issues.append((name, label))
        if any(value not in ALLOWED_IPS and all(0 <= int(part) <= 255 for part in value.split('.')) for value in IP.findall(text)):
            issues.append((name, 'non-example IP address'))
        if path.name.endswith(('.h', '.h.example', '.ino')):
            for password in re.findall(r'^#define\s+OTA_PASSWORD\s+"([^"\n]+)"', text, re.M):
                if password != 'CHANGE_ME':
                    issues.append((name, 'embedded updater password'))
    if issues:
        for name, label in issues:
            print('%s: %s' % (name, label))  # Never echo a detected value.
        raise SystemExit('Public-tree check failed; remove or anonymize these inputs.')
    print('Public-tree check passed for %d source/template/documentation files.' % checked)


if __name__ == '__main__':
    main()
