#!/usr/bin/env python3
"""Offline, evidence-constrained release report for the experimental PS5 title.

This is a delivery gate, NOT a PS5 hardware check. A green CI run must never
silently claim that firmware 13.60, gallery databases, or deletion are safe.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


def inspect(manifest_path: Path) -> dict:
    if not manifest_path.is_file() or manifest_path.is_symlink():
        raise ValueError("Native release manifest not found as regular file")
    raw = manifest_path.read_bytes()
    manifest = json.loads(raw)
    if manifest.get("schemaVersion") != 1 or manifest.get("titleId") != "PPSA99784":
        raise ValueError("Unsupported native title manifest")
    if manifest.get("format") != "unsigned-homebrew-title-folder":
        raise ValueError("Only the unsigned native homebrew folder is supported")
    if manifest.get("firmware1360HardwareValidated") is not False:
        raise ValueError("Unsubstantiated on-console validation assertion")
    if manifest.get("cleanupEnabled") is not False:
        raise ValueError("Unsafe/destructive cleanup release assertion")
    if manifest.get("runtimePermission") != "not-guaranteed":
        raise ValueError("Unknown runtime privileges must not be asserted")
    if manifest.get("helper") != "bundled-readonly-opt-in":
        raise ValueError("Native title requires an opt-in, non-escalating bundled helper")
    artifacts = manifest.get("files")
    if not isinstance(artifacts, list) or not artifacts:
        raise ValueError("No native release artifacts in manifest")
    names = [item.get("name") for item in artifacts if isinstance(item, dict)]
    if (len(names) != len(artifacts) or
            any(not isinstance(name, str) for name in names) or
            len(names) != len(set(names))):
        raise ValueError("Missing or duplicated artifact names")
    allowed = {"PPSA99784.zip", "cleanmyps5-helper.elf", "PPSA99784.ffpkg"}
    if set(names) - allowed:
        raise ValueError("Unexpected artifact name in release manifest")
    if "PPSA99784.zip" not in names:
        raise ValueError("Required self-contained native title ZIP missing")
    for entry in artifacts:
        if type(entry.get("bytes")) is not int or entry["bytes"] <= 0:
            raise ValueError("Invalid release artifact length")
        digest = entry.get("sha256")
        if not isinstance(digest, str) or len(digest) != 64 or any(
            c not in "0123456789abcdef" for c in digest
        ):
            raise ValueError("Invalid SHA-256 in release manifest")

    blockers = [
        {
            "id": "HARDWARE_RUNTIME",
            "status": "not_verified",
            "acceptance": "Install and run title with DualSense on actual PS5 firmware 13.60",
        },
        {
            "id": "PERMISSIONS",
            "status": "not_verified",
            "acceptance": "Verify actual title sandbox permissions independently of optional helper",
        },
        {
            "id": "GALLERY_DEPENDENCIES",
            "status": "unknown",
            "acceptance": "Map capture, database, index and thumbnail lifecycle without guessing paths",
        },
        {
            "id": "REVERSIBLE_CLEANUP",
            "status": "not_implemented",
            "acceptance": "Prove safe private backup, real restore and confirmed space reclamation",
        },
        {
            "id": "INSTALLER_COMPATIBILITY",
            "status": "not_verified",
            "acceptance": "Confirm install/uninstall and restart behavior on compatible title manager",
        },
    ]
    return {
        "schemaVersion": 1,
        "titleId": "PPSA99784",
        "sourceEvidence": "native-release-manifest",
        "manifestSha256": hashlib.sha256(raw).hexdigest(),
        "nativeArtifact": {
            "status": "packaged-not-console-tested",
            "format": "unsigned-homebrew-title-folder",
            "hasExperimentalFfpkg": "PPSA99784.ffpkg" in names,
            "optionalStandaloneHelper": "cleanmyps5-helper.elf" in names,
            "bundledHelperRequiresSecondInstall": False,
        },
        "readOnlyDiagnosticRelease": {
            "status": "hardware-validation-required",
            "destructiveFunctionsEnabled": False,
        },
        "reversibleCleanerRelease": {
            "status": "blocked",
            "destructiveFunctionsEnabled": False,
        },
        "blockingConditions": blockers,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    try:
        report = inspect(args.manifest)
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        print("Native diagnostics: packaged, PS5 validation REQUIRED")
        print("Reversible cleanup: BLOCKED (5 release acceptance conditions)")
    except (ValueError, OSError, json.JSONDecodeError) as error:
        print(f"Release readiness rejected: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
