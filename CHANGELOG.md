# Changelog

User-facing changes to WinterDash. Dates are release dates.

## v0.1 — 2026-09-01 — first public release

### Added
- Reads **Victron Blue Smart** charger data over Bluetooth ("Instant Readout") and shows it on the
  device screen and a built-in **web dashboard** on your own network.
- Runs on three boards: **LilyGO TTGO T-Display** (16 MB and 4 MB) and a **screenless ESP32** (WROOM-32).
- **No app, no cable to configure:** flash once, then join the `WinterDashAP` Wi-Fi from your phone to
  put it on your home Wi-Fi.
- **Browser installer** — pick your board and click Install in Chrome/Edge; nothing to install on your computer.
- On-device: live status, voltage/current, charge stage (Bulk / Absorption / Float), a screensaver with a
  car/battery banner, and a System menu.
- **Custom banners** — built-in presets or upload your own image (converted on the device).
- Optional **Home Assistant** integration over the native API — shows up automatically, no config.
- **Garage-friendly offline:** when the charger is switched off, the screen shows a brief alert, then relaxes
  to an ambient "offline" view instead of a permanent alarm.
- **Self-recovery:** a network watchdog reboots the device out of a wedged state and shows a clear fault screen.
- **Demo mode** to try it without a charger.

### Notes
- Verified on a Blue Smart IP65s 12/5 (fw 3.65); other single-output Blue Smart 12 V / 24 V chargers are
  expected to work.
- Independent project — **not affiliated with Victron Energy**. Licensed **GPL-3.0**.
