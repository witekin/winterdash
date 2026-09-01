#!/usr/bin/env python3
"""Generate the Wi-Fi-join QR baked onto the on-device setup screen (page_setup).

The QR encodes an OPEN Wi-Fi network so a phone that scans it joins the fallback
AP directly; the captive portal then pops the setup page (fallback: 192.168.4.1).

Baked as a small white-bg / black-module PNG that ESPHome renders as RGB565 from
flash (like the banner presets) -> zero DRAM, ~25 KB flash. box_size=3 keeps the
whole image ~111 px so it fits the left half of the 240x135 screen; ECC-L keeps
the version (module count) low so 3 px/module stays scannable. Regenerate after
changing the AP SSID:  python esphome/tools/gen_setup_qr.py
"""
import os
import qrcode

# Must match wifi.ap.ssid in esp32-tdisplay-16mb.yaml. Open network -> T:nopass, no P:.
AP_SSID = "WinterDashAP"
PAYLOAD = f"WIFI:S:{AP_SSID};T:nopass;;"

OUT = os.path.join(os.path.dirname(__file__), "..", "images", "wifi_setup_qr.png")

qr = qrcode.QRCode(
    error_correction=qrcode.constants.ERROR_CORRECT_L,
    box_size=3,   # px per module -> keep the whole image ~99 px on the 240-wide panel
    border=2,     # quiet zone in modules — trimmed 4->2 for a smaller white frame on-screen
                  # (2 still scans for most phones; bump back to 3-4 if the re-eyeball scan fails)
)
qr.add_data(PAYLOAD)
qr.make(fit=True)
img = qr.make_image(fill_color="black", back_color="white").convert("RGB")
img.save(os.path.normpath(OUT))
print(f"payload: {PAYLOAD}")
print(f"version: {qr.version} ({qr.modules_count} modules), image: {img.size[0]}x{img.size[1]} px")
print(f"wrote:   {os.path.normpath(OUT)}")
