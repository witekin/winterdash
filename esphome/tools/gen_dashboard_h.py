import pathlib
import gzip

# Regenerate the flash-served web pages as GZIPPED C byte-array headers. The device serves them with
# Content-Encoding: gzip (see web_dashboard.h StaticPage) -> the browser decompresses. This cuts flash
# (~2.5-3x), the transient send-buffer heap, and LAN transfer. Run after editing any web/*.html served
# by the device (dashboard, converter). One command keeps both in sync.
# NOTE: mtime=0 keeps the gzip header deterministic (reproducible builds; no spurious diffs).
# web/onboarding.html is NOT built here — it is gzipped into the vendored captive_portal page by
# tools/gen_captive_index.py (the AP captive page). Run that tool after editing onboarding.html.
ROOT = pathlib.Path(__file__).resolve().parents[2]  # repo root (esphome/tools/ -> ../../)
PAGES = [
    # (source html, output header, C symbol prefix)
    ("web/dashboard.html", "esphome/dashboard_html.h", "DASHBOARD_HTML"),
    ("web/image-tool.html", "esphome/convert_html.h", "CONVERT_HTML"),
]

for src, dst, sym in PAGES:
    html = (ROOT / src).read_text(encoding="utf-8")
    gz = gzip.compress(html.encode("utf-8"), compresslevel=9, mtime=0)
    out = f"#pragma once\n// Generated (gzip) from {src} — do not edit here. Served with Content-Encoding: gzip.\n"
    out += f"const unsigned char {sym}[] = {{\n"
    for i in range(0, len(gz), 20):
        out += "  " + ",".join(str(b) for b in gz[i:i + 20]) + ",\n"
    out += "};\n"
    out += f"const unsigned int {sym}_SIZE = {len(gz)};\n"
    p = ROOT / dst
    p.write_text(out, encoding="utf-8")
    print(f"wrote {dst}: gz {len(gz)} bytes (from html {len(html)} bytes, {100 * len(gz) // len(html)}%)")
