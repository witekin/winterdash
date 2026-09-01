import gzip
import pathlib

# Regenerate the VENDORED captive_portal page (esphome/components/captive_portal/captive_index.h) from
# web/onboarding.html, gzip-compressed. Our local captive_portal overrides the built-in via
# external_components, so the AP captive page becomes our branded WinterDash setup UI while keeping
# ESPHome's DNS hijack + /config.json + /wifisave + ota.web_server. Run after editing onboarding.html.
# Compression MUST stay gzip (captive_portal: compression: gzip) so USE_CAPTIVE_PORTAL_GZIP is defined
# and captive_portal.cpp serves INDEX_GZ with Content-Encoding: gzip.
ROOT = pathlib.Path(__file__).resolve().parents[2]  # repo root (esphome/tools/ -> ../../)
SRC = ROOT / "web/onboarding.html"
DST = ROOT / "esphome/components/captive_portal/captive_index.h"

html = SRC.read_bytes()
gz = gzip.compress(html, compresslevel=9, mtime=0)  # mtime=0 -> reproducible output

rows = []
for i in range(0, len(gz), 16):
    rows.append("    " + " ".join(f"0x{b:02x}," for b in gz[i : i + 16]))
body = "\n".join(rows)

out = (
    "#pragma once\n"
    "// GENERATED from web/onboarding.html by tools/gen_captive_index.py — do not edit here.\n"
    "// WinterDash branded captive page (gzip). Shadows the built-in captive_index.h via\n"
    "// external_components. captive_portal.cpp serves INDEX_GZ; keep captive_portal: compression: gzip.\n"
    '#include "esphome/core/hal.h"\n\n'
    "namespace esphome::captive_portal {\n\n"
    "constexpr uint8_t INDEX_GZ[] PROGMEM = {\n"
    f"{body}\n"
    "};\n\n"
    "}  // namespace esphome::captive_portal\n"
)
DST.write_text(out, encoding="utf-8")
print(f"wrote {DST}  ({len(out)} bytes header; html {len(html)} -> gz {len(gz)})")
