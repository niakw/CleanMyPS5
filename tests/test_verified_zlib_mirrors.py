#!/usr/bin/env python3
"""Offline guard for official mirrored zlib dependency downloads."""
from pathlib import Path
import importlib.util
import tempfile

script=Path(__file__).resolve().parents[1]/"scripts/enable_verified_zlib_mirrors.py"
spec=importlib.util.spec_from_file_location("zlib_mirrors",script)
module=importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)

fixture=(
    'zlib_version="1.3.2"\n'
    'zlib_hash="bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16"\n'
    +module.ORIGINAL+"\n"
)
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp)/"setup-native-dependencies.sh"
    path.write_text(fixture)
    module.patch(path)
    s=path.read_text()
    assert "https://www.zlib.net/fossils/zlib-$zlib_version.tar.gz" in s
    assert 'wget -q --tries=2 --timeout=25 "$url"' in s
    assert 'sha256sum --check --strict >/dev/null 2>&1' in s
    assert s.index('sha256sum --check --strict >/dev/null 2>&1') < s.index('mv "$zlib_archive.download"')
    assert 'rm -f -- "$zlib_archive.download"' in s
    assert 'if [[ $verified != true ]]; then' in s
    assert 'exit 1' in s
    try:
        module.patch(path)
        raise AssertionError("Double patch unexpectedly allowed")
    except ValueError:
        pass
    path.write_text(fixture.replace("bb329a0a2cd0","fakemodified"),encoding="utf-8")
    try:
        module.patch(path)
        raise AssertionError("Changed pinned checksum unexpectedly allowed")
    except ValueError:
        pass
print("Pinned official zlib mirror fail-closed guard: PASS")
