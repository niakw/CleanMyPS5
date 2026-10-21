# CleanMyPS5

**Controller-first, read-only PS5 storage inspection (early developer preview).** Inspired by the simplicity of modern maintenance dashboards, designed independently for PS5 and DualSense.

> **Current status:** v0.1 UI and JSON report viewer, **not yet an installable PS5 payload/app**. No deletion, defragmentation, compression, fake SSD figures or console writes. PS5 firmware 13.60 integration is planned and not yet validated.

## Try the UI

\`\`\`sh
python3 -m http.server 8080 --directory web
# Open http://localhost:8080
\`\`\`

Choose **Explorer la démo** for clearly fictional sample entries, or **Importer un rapport** to inspect your own JSON inventory. Works offline with local assets. The UI is usable with a PS5/DualSense gamepad on browsers supporting Gamepad API, plus keyboard, mouse and touch.

### Goals

- Review photos/videos, oversized recordings, caches, leftover installer data and obsolete payload artifacts.
- Explain risk and provenance for every candidate; protect game saves, titles, trophies and homebrew data by default.
- Show real, verified reclaimable bytes before any future write operation.
- Be easy to navigate from a TV with a controller.

See [architecture and verified vs unverified paths](docs/ARCHITECTURE.md).

## Contributing

GPL-3.0. All scans default to read-only; do not implement arbitrary filesystem deletion, unconfirmed OS paths or unattended cleanup.