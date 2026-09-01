# Credits & third-party notices

WinterDash builds on other people's open work. We use these as **declared dependencies**
(fetched at build), **bundled assets** (vendored into the repo — the icon font and the
browser-flasher scripts), or **research references**. Thank you to the authors.

## Trademarks & non-affiliation
**WinterDash is an independent open-source project — not affiliated with, endorsed by, or
sponsored by Victron Energy.** "Victron" and "Blue Smart" are trademarks of Victron Energy
B.V.; we use them **only descriptively**, to state that WinterDash is compatible with Victron
Blue Smart chargers. The product name is **WinterDash** (never "Victron WinterDash").

## Dependencies compiled into the firmware
- **esphome-victron_ble** — Fabian-Schmidt — **GPL-3.0** —
  https://github.com/Fabian-Schmidt/esphome-victron_ble
  Used via ESPHome `external_components` (fetched at build) to decode Victron Instant
  Readout BLE. Its GPL-3.0 makes the distributed firmware a GPL-3.0 combined work — see
  **Licensing** below.
- **ESPHome** — GPL-3.0 — https://esphome.io — the firmware framework we build on.
  Includes a **vendored `captive_portal`** component (GPL-3.0) — a local fork under
  `esphome/components/captive_portal/` for the branded onboarding page (see its `VENDORED.md`).
- **LVGL** — MIT — https://lvgl.io — the on-device UI library (via ESPHome).
- **ESP-IDF / Arduino-ESP32** — Apache-2.0 / LGPL — Espressif — the SoC SDK.

## Bundled assets
- **Material Design Icons** — Apache-2.0 —
  https://github.com/Templarian/MaterialDesign — icon font in `esphome/fonts/`
  (`MDI-LICENSE` kept alongside it).

### Web flasher (`site/`)
The browser installer at `site/flash/` vendors these (minified) scripts:
- **esptool-js** — Espressif Systems — **Apache-2.0** —
  https://github.com/espressif/esptool-js — `site/assets/esptool-bundle.js`; flashes the
  firmware over Web Serial.
- **pako** — Vitaly Puzrin &amp; Andrei Tuputcyn — **(MIT AND Zlib)** —
  https://github.com/nodeca/pako — bundled inside `esptool-bundle.js` (deflate).
- **js-md5** — Chen, Yi-Cyuan — **MIT** —
  https://github.com/emn178/js-md5 — `site/assets/md5.min.js`; verifies the flashed image.

## Research references (informed our design; no code copied)
- **keshavdv/victron-ble** (Python) — Instant Readout decrypt reference —
  https://github.com/keshavdv/victron-ble
- **Victron "Extra Manufacturer Data" spec** + Blue Smart IP65 manuals —
  https://www.victronenergy.com
- **Theengs**, chrisj7903/Read-Victron-advertised-data, felixwatts/victron_ble — protocol
  cross-references.

## Licensing (this project)
Because the firmware links **GPL-3.0** components (esphome-victron_ble, ESPHome), the
combined **distributed binary is GPL-3.0**. Therefore:
- This project is licensed **GPL-3.0** — see the `LICENSE` file at the repo root.
- Full **source is public** (this repo is the source).
- **GitHub Releases** (the generic bin) include this notices file + a link to the
  source and the GPL-3.0 text.

This keeps us open, credited, and compliant — which is the point.
