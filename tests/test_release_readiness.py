#!/usr/bin/env python3
"""Release report tests with synthetic manifests only; never touch PS5 data."""
from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from release_readiness import inspect  # noqa: E402

REPO = Path(__file__).resolve().parents[1]
PACKAGE = {
    "schemaVersion": 1,
    "titleId": "PPSA99784",
    "format": "unsigned-homebrew-title-folder",
    "firmware1360HardwareValidated": False,
    "cleanupEnabled": False,
    "runtimePermission": "not-guaranteed",
    "helper": "separate-browser-free-payload",
    "files": [
        {"name": "PPSA99784.zip", "bytes": 1234, "sha256": "a" * 64},
        {"name": "cleanmyps5-helper.elf", "bytes": 5678, "sha256": "b" * 64},
    ],
}

with tempfile.TemporaryDirectory() as work:
    root = Path(work)
    source, report_path = root / "manifest.json", root / "release-readiness.json"

    def write(payload: dict) -> None:
        source.write_text(json.dumps(payload) + "\n", encoding="utf-8")

    def rejected(changes: dict) -> None:
        candidate = copy.deepcopy(PACKAGE)
        candidate.update(changes)
        write(candidate)
        try:
            inspect(source)
        except ValueError:
            pass
        else:
            raise AssertionError(f"Unsafe release claim accepted: {sorted(changes)}")

    write(PACKAGE)
    result = inspect(source)
    assert result["manifestSha256"] == hashlib.sha256(source.read_bytes()).hexdigest()
    assert result["readOnlyDiagnosticRelease"]["status"] == "hardware-validation-required"
    assert result["reversibleCleanerRelease"]["status"] == "blocked"
    assert not result["reversibleCleanerRelease"]["destructiveFunctionsEnabled"]
    assert len(result["blockingConditions"]) == 5
    assert all(item["status"] != "verified" for item in result["blockingConditions"])
    assert not result["nativeArtifact"]["hasExperimentalFfpkg"]

    candidate = copy.deepcopy(PACKAGE)
    candidate["files"].append(
        {"name": "PPSA99784.ffpkg", "bytes": 8765, "sha256": "c" * 64}
    )
    write(candidate)
    assert inspect(source)["nativeArtifact"]["hasExperimentalFfpkg"]

    for changes in (
        {"firmware1360HardwareValidated": True},
        {"cleanupEnabled": True},
        {"runtimePermission": "root"},
        {"helper": "embedded"},
        {"format": "signed-retail-package"},
        {"titleId": "PPSA99785"},
        {"files": []},
        {"files": [PACKAGE["files"][0]]},
        {"files": [PACKAGE["files"][0], PACKAGE["files"][0]]},
        {"files": [
            dict(PACKAGE["files"][0], sha256="not-a-hash"),
            PACKAGE["files"][1],
        ]},
    ):
        rejected(changes)

    write(PACKAGE)
    subprocess.run(
        [sys.executable, str(REPO / "scripts/release_readiness.py"),
         str(source), str(report_path)],
        check=True, timeout=20,
    )
    written = json.loads(report_path.read_text(encoding="utf-8"))
    assert written == inspect(source)
    # "private" is a legitimate generic description of the host-only
    # backup gate. Exclude actual user/gallery locators, not that word.
    payload = json.dumps(written)
    assert "/user/av_contents/" not in payload
    assert "/home/" not in payload and "/Users/" not in payload
    missing = subprocess.run(
        [sys.executable, str(REPO / "scripts/release_readiness.py"),
         str(root / "missing.json"), str(report_path)],
        capture_output=True, text=True, timeout=20,
    )
    assert missing.returncode != 0
    print("Release readiness cannot claim hardware or cleanup verification: PASS")
