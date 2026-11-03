# Web installer

The install-from-browser page lives in `../docs/` and is deployed via
GitHub Pages (it must be served over HTTPS for WebMIDI).

- `../docs/index.html` + `../docs/installer.js` — the installer UI and logic (ours, GPL-3.0)
- `vendor/` — the upstream protocol modules, vendored verbatim:
  - `fm1ota.js` — the FM-1 USB-MIDI update protocol over WebMIDI
  - `fm1pkg.js` — `.fwsc` package parsing
  - Both (c) 2026 Leo Kuroshita / Hügelton Instruments, GPL-3.0
- `../docs/vendor/` is the deployed copy of these files.
- `../docs/fw/` holds the installable `.fwsc` builds.
