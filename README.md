# CleanMyPS5

**Controller-first, read-only PS5 storage inspection (early developer preview).** Inspired by the simplicity of modern maintenance dashboards, designed independently for PS5 and DualSense.

> **Current status:** v0.1 UI plus a host-buildable, **read-only C++ filesystem inventory**, **not yet an installable PS5 payload/app**. No deletion, defragmentation, compression, fake SSD figures or console writes. PS5 firmware 13.60 integration is planned and not yet validated.

## Try the UI

\`\`\`sh
python3 -m http.server 8080 --directory web
# Open http://localhost:8080
\`\`\`

Choose **Explorer la démo** for clearly fictional sample entries, or **Importer un rapport** to inspect your own JSON inventory. Works offline with local assets. The UI is usable with a PS5/DualSense gamepad on browsers supporting Gamepad API, plus keyboard, mouse and touch.

## Scan a mounted PS5-style folder (host development)

\`\`\`sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/cleanmyps5-scan --root /path/to/fixture-or-mounted-namespace > report.json
\`\`\`

The scanner only enumerates whitelisted relative locations:
\`user/av_contents/{photo,video}/NPXS40087\` and \`data/etaHEN\`.
Missing paths are reported as warnings, not assumed absent on the console.
It will not delete or modify any file. Import \`report.json\` in the UI.

**Not a native PS5 payload yet.** The host binary and its directory assumptions need PS5 Payload SDK porting and real console testing.

### Goals

- Review photos/videos, oversized recordings, caches, leftover installer data and obsolete payload artifacts.
- Explain risk and provenance for every candidate; protect game saves, titles, trophies and homebrew data by default.
- Show real, verified reclaimable bytes before any future write operation.
- Be easy to navigate from a TV with a controller.

See [architecture and verified vs unverified paths](docs/ARCHITECTURE.md).

## Contributing

GPL-3.0. All scans default to read-only; do not implement arbitrary filesystem deletion, unconfirmed OS paths or unattended cleanup.