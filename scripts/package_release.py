#!/usr/bin/env python3
"""Release exactly one executable; never disguise a payload as eboot.bin."""
from pathlib import Path
import hashlib
import json
import struct
import sys
import zipfile

binary, destination = map(Path, sys.argv[1:])
root = Path(__file__).resolve().parents[1]
blob = binary.read_bytes()
if len(blob)<64 or blob[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',blob,18)[0]!=62:
    raise SystemExit('Expected a real little-endian x86-64 PS5 ELF')
version=(root/'VERSION').read_text().strip()
manifest={
    'application':'CleanMyPS5','version':version,'format':'PS5 payload ELF',
    'executables':1,'sha256':hashlib.sha256(blob).hexdigest(),
    'consoleRuntimeVerified':False,'mode':'read-only','cleanupValidated':False,
    'ruleVersion':json.loads((root/'rules/metadata-v1.json').read_text())['version']
}
destination.mkdir(parents=True,exist_ok=True)
with zipfile.ZipFile(destination/'CleanMyPS5-elf.zip','w',zipfile.ZIP_DEFLATED) as archive:
    archive.writestr('CleanMyPS5/cleanmyps5.elf',blob)
    archive.writestr('CleanMyPS5/manifest.json',json.dumps(manifest,indent=2)+'\n')
    archive.writestr('CleanMyPS5/SHA256SUMS',manifest['sha256']+'  cleanmyps5.elf\n')
    for file in ['VERSION','LICENSE','README.md','docs/DELIVERY.md','docs/FEATURE_MATRIX.md']:
        archive.write(root/file,'CleanMyPS5/'+file)
print(destination/'CleanMyPS5-elf.zip')
