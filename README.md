# CleanMyPS5

A controller-first PS5 storage analysis and maintenance project, with an original UI inspired by the clarity of CleanMyMac.

## Status (development preview — see `VERSION`)

**The dashboard now talks directly to a read-only native C HTTP scanner.** It also offers a PS5 Smart Care-style *read-only* guided check: inventory, mounted storage, known-path preflight, etaHEN autoload and bounded exact screenshot duplicates. Unavailable checks are shown explicitly. A PS5 Payload SDK build target exists, but an ELF has not yet been executed or verified on a real PS5 13.60.

- **No delete, reset, registry edit, repair, defragmentation or compression.**
- Controller-friendly review dialog; optionally exports a local JSON manifest for manual inspection, explicitly **not** an executable cleanup script.
- Tested controller sorting: cycle size, modification date or filename in the media section. Date represents filesystem modification time, not guaranteed capture creation time.
- Controller pagination: max 36 visible items per page; L2/R2 advances without losing selections.
- Media filters for large captures (50 or 250 MiB), 90+ day modification age, title IDs and same-size candidates. **Same size is not a verified duplicate.**
- Local, opt-in **JPEG/PNG preview** of allowlisted captures from the native service (32 MiB per file, file type checked by signature, no arbitrary path access). JXR is not yet previewable. MP4/WebM video preview uses strict bounded HTTP byte ranges (maximum 1 MiB per GET), browser decode support permitting.
- Asynchronous comparisons and scanner responses are invalidated when a new report is opened, so stale results cannot reselect previous files.
- Verified identical groups display the sum of additional-copy bytes as **review candidates, not free space**; extra copies can be added to the review while keeping one arbitrary original, with no cleanup action exposed.
- Opt-in **exact duplicate comparison**: checks a capped sample of screenshots byte for byte (128 images, max 32 MiB/image, max 128 MiB read, approximately six-second soft deadline). Not a whole-console exhaustive search and never deletes files.
- Read-only bounded catalog of top-level homebrews and etaHEN plugins (`/data/homebrew`, `/data/etaHEN/plugins`); folder/file bytes are nominal and every entry remains protected, never classified as orphaned.
- Opt-in **etaHEN diagnostic**: inspect known `/data/ps5_autoloader`, `/data/pldmgr`, USB autoloads and `/data/etaHEN/config.ini` without exposing file contents. Neither missing configs nor references conclusively prove runtime status.
- File modification timestamps and bounded scan coverage/partial warnings.
- Local **PS5 preflight** report: opens only 15 preconfigured candidate directories (including all eight USB autoloader slots) and identifies accessible, absent, symlinked or denied paths. The UI can export the path-status-only JSON to help firmware 13.60 testing; no private file contents are read.
- On-demand read-only `statvfs` measurements for two known mount paths, each reported separately and flagged if sharing one filesystem (not the whole PS5 SSD).
- Controller-friendly capture-space map by title ID with separate photo/video segments and drilldown, never shown as whole-console storage.
- Scan is limited to capture folders and explicitly classified etaHEN files. No system-wide recursive scan.
- No claim of SSD-wide free space: counts are for **enumerated items only**.
- The demo is fictitious and clearly marked as such.
- Native service uses four bounded worker threads so a slow video/scan does not block all requests. Excess clients receive HTTP 503. It listens on `127.0.0.1:8877` only: no network exposure or authentication assumptions. SIGINT/SIGTERM close the listener gracefully; the program never installs persistent ShellUI entries.

## Host development

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/cleanmyps5-server --root /path/to/your/fixture-or-mounted-PS5-namespace
# Then open http://127.0.0.1:8877
```

On the embedded service, **Analyser le dossier** reads the selected root via `/api/report` automatically. Without a native server, the UI can also be previewed as static files:

```sh
python3 -m http.server 8080 --directory web
# http://127.0.0.1:8080 — JSON import or explicit fictional demo
```

## PS5 payload build (experimental)

Requires the official community [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) and `PS5_PAYLOAD_SDK` environment variable:

```sh
make ps5
# server only: cleanmyps5.elf
make ps5-browser
# experimental server + browser launch: cleanmyps5-browser.elf
# neither variant has been verified on physical PS5 13.60 yet
```

The native payload defaults to filesystem namespace `/` and **loopback only**, with URL `http://127.0.0.1:8877/`. The optional `cleanmyps5-browser.elf` uses `sceUserServiceInitialize` and `sceSystemServiceLaunchWebBrowser` from the community SDK sample to attempt opening that URL after the service starts; failure does not stop the local server. **Do not use both payload variants together**. Opening the browser may conflict with WebKit autoloaders; the original ELF remains server-only. Reaching it from a PS5 user interface requires an accessible browser/deeplink, to be verified on device. No launcher icon is installed automatically and no existing applications are touched. Loading a payload has the normal risks of an exploited console.

The CI uploads both experimental ELF variants **only after** host integration tests and ASan/UBSan pass. The artifact also includes `cleanmyps5.elf.sha256` and `VERSION` for traceability. CodeQL scans the native C and JavaScript sources separately. A compiled ELF still **does not** prove PS5 runtime compatibility. Do not replace an existing payload or install an untested binary expecting production behavior.

## Sources and safety

[Functional comparison with CleanMyMac](docs/FUNCTIONAL_PARITY_CLEANMYMAC.md) · [Architecture and supported paths](docs/ARCHITECTURE.md) · [DualSense UX specification](docs/UX.md) · [PS5 13.60 test checklist](docs/VALIDATION_PS5_13_60.md) · [Delivery and validation matrix](docs/ROADMAP.md)

Licensed GPL-3.0. Files, capture metadata, saves and etaHEN configurations are never silently considered disposable.

## Honest scope

The program is an **exploratory, read-only PS5 cleaner prototype**, not a finished CCleaner/CleanMyMac replacement. The current release does not delete screenshots, uninstall apps, repair failed PKG installs, delete caches, compact storage, or restore backups. Follow the [feature parity audit](docs/FUNCTIONAL_PARITY_CLEANMYMAC.md) before interpreting a scan result as safe to remove. The PS5 payloads remain unverified on firmware 13.60.
