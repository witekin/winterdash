# Vendored `captive_portal` — READ BEFORE BUMPING ESPHome

This is a **local fork** of ESPHome's built-in `captive_portal` component. It exists only to make the
AP onboarding page the **branded WinterDash "Wi-Fi setup" UI** instead of the stock ESPHome page.

`esp32-tdisplay-16mb.yaml` pulls it in via:

```yaml
external_components:
  - source: { type: local, path: components }
    components: [captive_portal]
```

ESPHome then uses **this** `captive_portal` instead of the built-in one.

## Why fork at all
The stock captive page has **no config theming**, and our own web handler **cannot out-prioritise
captive's** in AP mode (dispatch is first-`canHandle`-wins in registration order, and captive's handler
is dispatched before an `on_boot`-registered one in AP mode — empirically confirmed with a `/wdz`
probe). So the only way to brand it is to replace the bytes captive serves.

## What diverges from upstream — EXACTLY ONE FILE
- **`captive_index.h`** — the served page blob (`INDEX_GZ`). We replace ESPHome's stock gzip page with
  our own, generated from `web/onboarding.html` by `esphome/tools/gen_captive_index.py`.

Everything else here (`captive_portal.cpp`, `captive_portal.h`, `dns_server_esp32_idf.cpp/.h`,
`__init__.py`) is a **pristine, byte-identical copy of upstream** at the ESPHome version noted below.
Do not hand-edit them — that grows the merge surface. All branding lives in `captive_index.h` (generated)
and `web/onboarding.html` (source).

Requires `captive_portal: compression: gzip` in the YAML (so `USE_CAPTIVE_PORTAL_GZIP` is defined and
`captive_portal.cpp` serves `INDEX_GZ`). Keep the generator (`gen_captive_index.py`) emitting gzip.

**Vendored from ESPHome:** `2026.4` (copied 2026-08-28).

## Re-sync when you bump ESPHome  ⚠️
This fork is **frozen** at the version above. On any ESPHome upgrade the shared APIs captive uses
(`AsyncWebHandler`, `wifi::save_wifi_sta`, `web_server_base`, the `INDEX_GZ` contract) may change, and a
stale copy here will **fail to compile** — locally AND in **GitHub Actions / CI**. If a build breaks in
`components/captive_portal/*` after changing the ESPHome version, this fork is the prime suspect.

Re-sync (a copy, not a merge — only `captive_index.h` is ours):

```bash
# 1. Overwrite the 5 upstream files from the NEW ESPHome install (leave captive_index.h alone):
#    <site-packages>/esphome/components/captive_portal/{__init__.py,captive_portal.cpp,captive_portal.h,\
#      dns_server_esp32_idf.cpp,dns_server_esp32_idf.h}  ->  esphome/components/captive_portal/
# 2. Regenerate our branded blob:
python esphome/tools/gen_captive_index.py
# 3. Rebuild and confirm the branded captive still serves + pairing still works:
python -m esphome compile esphome/esp32-tdisplay-16mb.yaml
```

Then bump the "Vendored from ESPHome" line above. If step 2's format ever mismatches (upstream renames
`INDEX_GZ`, drops the gzip branch, changes the `#include`), update `gen_captive_index.py` to match the
new `captive_index.h` skeleton.

## How to tell it went wrong (failure signals)
- **Build/CI error** referencing `captive_portal/*` right after an ESPHome version change → re-sync.
- Device boots but the AP page is **stock again** (not branded) → the override isn't being used; check the
  `external_components` block + that the build-dir `captive_index.h` is ours (~30KB), not upstream (~16KB).
- Onboarding stops saving creds / no networks listed → the `/config.json` or `/wifisave` contract changed
  upstream; re-sync and re-verify `web/onboarding.html` still targets them (`GET /wifisave?ssid=..&psk=..`).
