<h1 align="center">WinterDash</h1>
<p align="center"><em>Winter-charge dashboard for Victron Blue Smart chargers</em></p>
<p align="center">
  ESPHome firmware · LilyGO T-Display · on-device web UI · <a href="LICENSE">GPL-3.0</a>
</p>

---

ESPHome firmware for a **LilyGO TTGO T-Display** (ESP32, ST7789V 135×240 IPS, 16MB flash) that shows
**Victron Blue Smart charger** data ("Instant Readout" BLE) on the built-in display via LVGL
(6 screens + 2-button nav) plus a device-hosted web dashboard, aimed at non-technical friends
wintering cars / motorcycles / boats. Optional Home Assistant link over the native API.

> Independent open-source project — not affiliated with, endorsed by, or sponsored by Victron Energy.
> "Victron" and "Blue Smart" are trademarks of Victron Energy B.V., used only to describe compatibility.

## Status

**Working on real hardware; real BLE verified.** Victron decode is live-verified on a Blue Smart
IP65s 12/5 (fw 3.65) through a full charge cycle (Bulk→Absorption→Float). Shipped: the web dashboard,
10 baked banner presets (Battery default) + an on-device converter (`/convert`), runtime timezone,
screensaver, the on-device System menu, and branded Wi-Fi onboarding (no app/cable — a friend flashes
a generic bin and joins Wi-Fi from a phone). See the
**[project wiki](https://github.com/witekin/winterdash/wiki)** for install, flashing, and usage.

## Repository layout

```
.
├── web/              # dashboard.html · onboarding.html (Wi-Fi setup) · image-tool.html (/convert)
├── LICENSE           # GPL-3.0 (the firmware links GPL-3.0 components — see CREDITS.md)
├── CREDITS.md        # third-party notices + trademark / non-affiliation
└── esphome/
    ├── esp32-tdisplay-16mb.yaml       # TTGO T-Display build (screen + 2-button nav)
    ├── esp32-wroom32.yaml   # screenless ESP32 build (status LED + web dashboard)
    ├── packages/                  # board / display / input / core / producer packages
    ├── components/captive_portal/ # vendored ESPHome captive — branded Wi-Fi onboarding page
    ├── tools/                     # generators for the flash-baked HTML + setup QR
    ├── *.h                        # web pages baked to flash + BLE scan + charge log
    └── images/*.webp              # baked banner cut-outs (Battery = default + fallback)
```

## Hardware summary

- **Board:** LilyGO TTGO T-Display, 16MB flash variant
- **MCU:** ESP32-D0WD-Q6 (WiFi + BLE, no PSRAM)
- **Display:** IPS ST7789V, 135×240, SPI (ESPHome `mipi_spi` model `T-DISPLAY`)
- **Input:** BLE broadcast from a Victron charger (read-only, AES-encrypted)
- **Output:** local LVGL display + optional Home Assistant (native API)

## Charger pairing

Done at runtime in the web Settings (no rebuild): scan → pick the Victron MAC, paste the bindkey
(VictronConnect → Product info → "Instant readout via Bluetooth" → Show; needs charger fw ≥ 3.61).
Keys live in NVS, never in the firmware image. Tested on a Blue Smart IP65s 12/5 (fw 3.65); other
single-output Blue Smart 12V/24V are expected to work (same BLE record).

## License

**GPL-3.0** — the distributed firmware links GPL-3.0 components (ESPHome, esphome-victron_ble), so the
combined binary is GPL-3.0 and the full source is this repository. Third-party notices, dependencies,
and the trademark / non-affiliation statement are in [CREDITS.md](CREDITS.md).
