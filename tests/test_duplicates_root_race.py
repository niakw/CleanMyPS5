#!/usr/bin/env python3
"""Replace the root pathname after open; comparisons must stay on its FD."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as temporary:
    base=Path(temporary)
    original=base/'root'
    backup=base/'original-moved'
    replacement=base/'replacement'
    for root,names,content in [
        (original,('a.png','b.png'),b'ORIGINAL CAPTURE'),
        (replacement,('secret1.png','secret2.png'),b'PRIVATE REPLACEMENT'),
    ]:
        images=root/'user/av_contents/photo/NPXS99999/GAME'
        images.mkdir(parents=True)
        for name in names:
            (images/name).write_bytes(content)
    result=subprocess.run([sys.argv[1],str(original),str(backup),str(replacement)],
                          check=True,capture_output=True,text=True,timeout=10)
    report=json.loads(result.stdout)
    assert report['partial'] is False,report
    assert report['filesConsidered']==2
    assert len(report['groups'])==1
    assert report['groups'][0]['bytesEach']==len(b'ORIGINAL CAPTURE')
    assert set(report['groups'][0]['paths'])=={
        '/user/av_contents/photo/NPXS99999/GAME/a.png',
        '/user/av_contents/photo/NPXS99999/GAME/b.png',
    }
    assert 'secret' not in result.stdout and temporary not in result.stdout
    assert (original/'user/av_contents/photo/NPXS99999/GAME/secret1.png').read_bytes()==b'PRIVATE REPLACEMENT'
    assert (backup/'user/av_contents/photo/NPXS99999/GAME/a.png').read_bytes()==b'ORIGINAL CAPTURE'
    # Invalid roots must fail before the injected namespace change.
    link=base/'symbolic-root'
    link.symlink_to(original,target_is_directory=True)
    ordinary_file=base/'not-a-directory'
    ordinary_file.write_text('fixture')
    for invalid in (link,ordinary_file,base/'absent'):
        result=subprocess.run([sys.argv[1],str(invalid),str(base/'unused'),str(original)],
                              capture_output=True,text=True,timeout=10)
        assert result.returncode==4,(invalid,result)
        assert result.stdout==''
print('Duplicate comparisons retain the opened root during replacement; unsafe roots rejected: PASS')
