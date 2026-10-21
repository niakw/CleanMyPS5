# CleanMyPS5 — architecture v0.1

## Purpose

Controller-first PS5 storage maintenance, with a strict boundary between **discovery**, **classification**, **review**, **execution** and **verification**.

### v0.1 delivered

- Offline-friendly static dashboard in \`web/\`, designed for TV readability, DualSense/gamepad and mouse.
- Import of a versioned JSON inventory; optional **explicit** fictional demo, never shown as real PS5 measurements.
- Host-compiled C++17 read-only inventory, hard-limited to selected roots and file types with fixture tests and CI.
- Category detail, opt-in selection, protected items disabled, and selection totals.
- **No delete endpoint, no write permissions, no automatic system cleanups.**

### Planned backend

\`\`\`mermaid
flowchart LR
   A[PS5 filesystem providers] --> B[Bounded read-only scanner]
   B --> C[Risk classification and dependency checks]
   C --> D[Versioned JSON report]
   D --> E[DualSense-first Web UI]
   E --> F{User approves?}
   F -->|No| G[No action]
   F -->|Yes, future milestone| H[Quarantine/export/transactional cleanup]
\`\`\`

### Trust and safety requirements

1. No recursive scan of \`/\` or arbitrary root as a trash folder. Restrict discovery to configured providers.
2. Never follow symlinks; do not traverse kernel/OS partitions or title/save databases.
3. Never claim files are 'junk' by extension alone. Logs, caches, DLC, updates, payloads and game data require independent rules.
4. Preserve user saves, trophies, app installation records, \`/data/homebrew\` and \`/data/prosperoeden\`.
5. Console path inventory must be versioned with source and confidence; don't silently assume FTP view equals system SSD.
6. Real cleanup later must recheck identity/mtime/size, protect against symlink races, support backups, and verify freed bytes.
7. Do not call guessed private PS5 APIs. Every native PS5 integration requires a compatible SDK and FW 13.60 runtime validation.

### Paths and evidence

| Path | Status | Meaning |
|---|---|---|
| \`/data/homebrew/PPSA99008\` | Previously observed via FTP | Eden Encore homebrew app, PROTECTED |
| \`/data/prosperoeden\` | Previously observed via FTP | Eden Encore data, PROTECTED |
| \`/data/etaHEN\` | Public etaHEN documentation, **not verified on this console** | configs/plugins/logs; no automated removal |
| \`user/av_contents/photo/NPXS40087\` | Public ProsperoGallery FW 13.60 example, **not verified on this console** | screenshot discovery candidate |
| \`user/av_contents/video/NPXS40087\` | Public ProsperoGallery FW 13.60 example, **not verified on this console** | clip discovery candidate |

Source references: https://github.com/AmineWallah/ProsperoGallery , https://github.com/itsPLK/ps5-pkg-manager

### Inventory schema

\`\`\`json
{
  "schemaVersion": 1,
  "source": "read-only scanner",
  "scannedAt": "2026-10-21T00:00:00Z",
  "warnings": ["Any incomplete or unverified root is shown here."],
  "items": [
    {"id":"unique","category":"media","path":"/path","bytes":1048576,"risk":"review","reason":"Personal video"}
  ]
}
\`\`\`

\`category\`: media, cache, logs, residuals, other. \`risk\`: review, safe, protected. No report field authorizes actual deletion.

### UI controls

- D-pad / left stick: spatial focus
- Cross: activate focused item
- Circle: back to Overview
- L1 / R1: previous/next tab
- Keyboard: arrows, Enter/Space, Escape, PageUp/PageDown, Tab
- Mouse/touch: standard focus and click

### Next milestones

1. Port the C++ host scanner to the PS5 Payload SDK; add native payload + local web server/PS5 entry. Do not assume std::filesystem support without toolchain validation.
2. Verify captures path, etaHEN ownership, SSD capacity measurement against **real FW 13.60**.
3. Add rich per-file preview and duplicate detection with bounded hashing, with export before deletion.
4. Transactional, opt-in cleanup only after tests on fixtures + real PS5 confirmation.
