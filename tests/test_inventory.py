#!/usr/bin/env python3
"""Generic metadata contract, privacy exclusions, budgets and no writes."""
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
from urllib.request import urlopen

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    fixtures = {
        'data/BrandNewApp/eboot.elf': b'ELF fixture',
        'data/BrandNewApp/logs/run.log': b'PRIVATE LOG CONTENT',
        'data/BrandNewApp/cache/blob': b'CACHE CONTENT',
        'data/BrandNewApp/unknown.asset': b'unknown resource',
        'data/BrandNewApp/saves/PRIVATE_SAVE_ID/save.bin': b'PRIVATE SAVE',
        'data/BrandNewApp/profiles/PRIVATE_PROFILE_ID/token.json': b'PRIVATE TOKEN',
        'data/BrandNewApp/retroachievements.ini': b'PRIVATE ACCOUNT',
        'system_data/priv/system_logger2/nobackup/database/sl2_log.db': b'PRIVATE DATABASE',
        'system_data/priv/vshlog/vshlog.0.txt': b'PRIVATE VSH LOG',
        'system_data/priv/cache/httpcache/nobackup/cache.db': b'PRIVATE CACHE',
        'system_data/priv/cache/httpcache/nobackup/cache.db-wal': b'PRIVATE WAL',
        'system_data/priv/cache/profile/PRIVATE_PROFILE_ID': b'PRIVATE PROFILE',
        'user/home/PRIVATE_USER_ID/secret': b'PRIVATE USER',
        'user/av_contents/photo/NPXS99999/OTHER/capture.png': b'\x89PNG\r\n\x1a\nFAKE IMAGE',
        'user/av_contents/thumbnails/photo/NPXS99999/OTHER/thumb.jpg': b'FAKE THUMBNAIL',
        'mnt/usb6/homebrew/UnknownUSB/eboot.elf': b'USB ELF',
        'mnt/ext3/homebrew/UnknownExternal/eboot.elf': b'EXTERNAL ELF',
        'system_tmp/state': b'RUNTIME STATE',
    }
    for path, content in fixtures.items():
        full = root / path
        full.parent.mkdir(parents=True, exist_ok=True)
        full.write_bytes(content)
        os.utime(full, (1700000000, 1700000000))
    (root/'data/BrandNewApp/alias').symlink_to(root/'user/home', target_is_directory=True)
    os.mkfifo(root/'system_tmp/redis.sock') # a FIFO must never be opened/read
    (root/'data/BrandNewApp/hardlink.asset').hardlink_to(root/'data/BrandNewApp/unknown.asset')
    def snapshot():
        return {path:(hashlib.sha256((root/path).read_bytes()).hexdigest(),(root/path).stat().st_mtime_ns)
                for path in fixtures}
    # Sparse fixture: metadata tests avoid allocating or reading large media contents.
    large_capture=root/'user/av_contents/photo/NPXS99999/OTHER/large-old.jpg'
    with large_capture.open('wb') as stream:
        stream.truncate(51*1024*1024)
    os.utime(large_capture,(1600000000,1600000000))
    before = snapshot()
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0))
        port=sock.getsockname()[1]
    process=subprocess.Popen([sys.argv[1],'--root',str(root),'--port',str(port)],stdout=subprocess.DEVNULL)
    base=f'http://127.0.0.1:{port}'
    def get(path):
        with urlopen(base+path,timeout=12) as response:
            return json.load(response)
    try:
        for _ in range(60):
            try:
                get('/api/status')
                break
            except OSError:
                time.sleep(.05)
        else:
            raise AssertionError('server did not start')
        report=get('/api/report')
        overview=get('/api/overview')
        assert overview['mode']=='metadata-only'
        assert overview['cleanupValidated'] is False
        assert overview['scope']=='bounded-known-roots'
        assert overview['review']['largeMedia']==1
        assert overview['review']['oldMedia']>=1
        assert overview['review']['oldLargeMedia']==1
        assert overview['review']['largeMediaNominalBytes']==51*1024*1024
        assert overview['review']['validatedForDeletion'] is False
        assert overview['review']['minLargeBytes']==50*1024*1024
        assert overview['review']['oldDays']==90
        assert overview['files']==report['stats']['items']
        assert overview['visited']==report['stats']['visited']
        assert overview['partial']==report['stats']['partial']
        assert sum(overview['categories'].values())==overview['files']
        assert overview['nominalBytes']==sum(item['bytes'] for item in report['items'])
        assert sum(overview['coverage'].values())==len(report['coverage'])
        assert 'items' not in overview and 'PRIVATE' not in json.dumps(overview)
        assert str(root) not in json.dumps(overview)
        assert report['mode']=='metadata-only' and report['cleanupValidated'] is False
        assert report['ruleVersion']=='2026.10.21.1'
        items={item['path']:item for item in report['items']}
        assert items['/data/BrandNewApp/logs/run.log']['category']=='logs'
        assert items['/data/BrandNewApp/cache/blob']['category']=='cache'
        assert items['/data/BrandNewApp/eboot.elf']['category']=='residuals'
        assert items['/system_data/priv/vshlog/vshlog.0.txt']['category']=='logs'
        assert items['/system_data/priv/cache/httpcache/nobackup/cache.db-wal']['risk']=='protected'
        assert items['/system_tmp/state']['risk']=='protected'
        assert items['/user/av_contents/thumbnails/photo/NPXS99999/OTHER/thumb.jpg']['risk']=='protected'
        media=items['/user/av_contents/photo/NPXS99999/OTHER/capture.png']
        assert media['titleId']=='OTHER' and media['modifiedAt']==1700000000
        assert media['previewPath']==media['path']
        assert all(item['risk']=='protected' for item in report['items'] if item['category']!='media')
        assert '/mnt/usb6/homebrew/UnknownUSB/eboot.elf' in items
        assert '/mnt/ext3/homebrew/UnknownExternal/eboot.elf' in items
        assert report['stats']['specialFilesSkipped']==1
        assert report['stats']['symlinksSkipped']==1
        assert report['stats']['aliasesSkipped']==1
        assert report['stats']['privateEntriesExcluded']>=3
        assert not report['stats']['partial'],report
        assert len(report['coverage'])==30
        assert all(item['path'].startswith('/') and temporary not in item['path'] for item in report['items'])
        assert sum(item['bytes'] for item in report['items'])==sum(row['nominalBytes'] for row in report['coverage'])
        catalogue=get('/api/homebrew')
        catalogue_paths={item['path'] for item in catalogue['items']}
        assert '/data/BrandNewApp' in catalogue_paths
        assert '/mnt/usb6/homebrew/UnknownUSB' in catalogue_paths
        assert '/mnt/ext3/homebrew/UnknownExternal' in catalogue_paths
        combined=json.dumps([report,catalogue])
        assert 'PRIVATE' not in combined and 'redis.sock' not in combined and 'retroachievements' not in combined
        assert snapshot()==before,'metadata scans changed fixture contents or modification times'
        # Truncation of one root is explicit, and later roots are still visited.
        photos=root/'user/av_contents/photo/many'
        photos.mkdir()
        for i in range(2510):
            (photos/f'{i:04d}.jxr').touch()
        capped=get('/api/report')
        capped_overview=get('/api/overview')
        assert capped_overview['partial'] is True
        assert capped_overview['review']['validatedForDeletion'] is False
        assert capped_overview['coverage']['partial']>=1
        assert capped_overview['files']==capped['stats']['items']
        assert capped_overview['nominalBytes']==sum(x['bytes'] for x in capped['items'])
        assert 'many/' not in json.dumps(capped_overview)
        coverage={row['path']:row for row in capped['coverage']}
        assert capped['stats']['partial'] is True
        assert coverage['/user/av_contents/photo']['status']=='partial'
        assert coverage['/user/av_contents/photo']['visited']==2500
        assert coverage['/system_data/priv/vshlog']['status']=='complete'
        assert '/system_data/priv/vshlog/vshlog.0.txt' in {item['path'] for item in capped['items']}
    finally:
        process.terminate()
        assert process.wait(timeout=5)==0
print('Generic metadata, privacy, IPC, hardlinks, alternate roots and bounded coverage: PASS')
