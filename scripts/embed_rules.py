#!/usr/bin/env python3
"""Compile reviewed declarative metadata rules; no runtime scripts or downloads."""
import json
import re
import sys
from pathlib import Path
source, target = map(Path, sys.argv[1:])
rules = json.loads(source.read_text())
assert rules['mode'] == 'metadata-only' and rules['cleanupValidated'] is False
assert re.fullmatch(r'[0-9.]+', rules['version'])
assert 0 < len(rules['roots']) <= 48, 'At most 48 rule roots plus 16 external slots'
lines = ['/* Generated from rules/metadata-v1.json. */', '#ifndef CMP5_SCAN_RULES_H', '#define CMP5_SCAN_RULES_H', 'static const char cmp5_rule_version[] = ' + json.dumps(rules['version']) + ';', 'typedef struct { const char *id, *path, *kind; } Cmp5Root;', 'static const Cmp5Root cmp5_roots[] = {']
for entry in rules['roots']:
    assert len(entry) == 3
    assert all(re.fullmatch(r'[A-Za-z0-9_/-]+', part) for part in entry)
    assert len(entry[1]) < 200 and all(part not in ('', '.', '..') for part in entry[1].split('/'))
    assert entry[2] in {'photo', 'video', 'associated', 'system-log', 'system-cache', 'application', 'runtime'}
    lines.append(' {' + ','.join(json.dumps(part) for part in entry) + '},')
lines.append('};')
for key, symbol in [('excludedDirectories', 'cmp5_private_dirs'), ('excludedFiles', 'cmp5_private_files')]:
    assert all(re.fullmatch(r'[a-z0-9_.-]+', part) for part in rules[key])
    lines.append('static const char *const ' + symbol + '[] = {' + ','.join(json.dumps(part) for part in rules[key]) + '};')
lines.append('#endif')
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text('\n'.join(lines) + '\n')
