# Make it yours: name, release, get listed

(Adapted from Dinghy's `docs/MAKE-IT-YOURS.md` for this project.)

## 1. Name it

"Maestro" is a working title. Pick the real name, then:

* `README.md`, `SETUP.md`, docs: find/replace.
* `firmware/src/maestro.c` → `<name>.c` (keep `app_main`/`app_audio`
  entry names the platform expects).
* Loader screen name + colour (platform draws it during updates).

## 2. Package identity

Give the firmware its own package identity string (the platform's
update format; Dinghy's docs show where). The FM-1 and the installers
use it to tell your firmware from every other one. Never ship two
different firmwares under one identity.

## 3. Test on a real FM-1

**The hub does not list untested firmware.** Checklist before release:

* [ ] installs from the web installer and from `tools/fm1_install.py`
* [ ] every lesson type runs end-to-end on device
* [ ] wrong notes / missed timing behave (no hangs, no stuck notes)
* [ ] power off mid-lesson → progress blob CRC rejects or recovers
* [ ] OCT- rescue still reachable; two boot crashes → rescue by itself
* [ ] return to stock firmware works (hub one-click / M-UPGRADE)
* [ ] SMK-37 build boots (if claiming the target)

## 4. Release

Tag `vX.Y` → CI builds, runs host tests, and publishes the `.fwsc`
as a GitHub Release with a build attestation and SHA-256.

## 5. Get listed

* **FM-1 Firmware Hub** (thefirmwarehub.com): "Submit your firmware" —
  open source (GPL-3.0 here) gets a one-click install card; include
  name, links, what it does, and confirmation of real-hardware testing.
* **fwradar** (fwradar.com): tracks projects with a working build and
  a real following on daily checks — the release + hub listing is what
  gets you found.
* Keep a changelog; answer issues. A teaching firmware lives or dies
  on trust — parents and teachers will check that it's maintained.
