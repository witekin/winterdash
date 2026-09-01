#pragma once
// "Forget WiFi" for the creds-less generic bin. ESPHome's clear_sta() only clears the in-RAM STA
// list; the captive-saved creds live in NVS (namespace "esphome") and reload on the next boot, so a
// clear_sta()+reboot silently reconnects to the old network instead of re-entering onboarding
// (verified against ESPHome source).
//
// To actually re-onboard we must make WiFiComponent::start()'s pref_.load() FAIL on the next boot so
// has_sta() stays false and setup() takes the has_ap() branch (AP + captive portal). load() fails
// only when the NVS key is ABSENT, so we ERASE the key (not blank it: an empty-SSID blob loads fine,
// leaves has_sta() TRUE, and with ap_timeout:0s strands the device with no STA and no AP).
//
// The key is ESPHome's wifi SavedWifiSettings pref: namespace "esphome", key = the decimal string of
// the pref hash (esp32/preferences.cpp). On a bin with NO baked STA credentials, has_sta() is false
// at wifi start() so that hash is the fixed fallback constant 88491487 (wifi_component.cpp). Erasing
// only this key preserves the Victron MAC/bindkey + all display settings (Factory reset is the
// nuclear whole-NVS wipe).
//
// ⚠ The "88491487" key is an ESPHome-internal constant, valid ONLY while the bin stays creds-less. If
// baked STA creds are ever added, the hash becomes config_version_hash and this erase silently misses
// (same symptom as the original bug, no worse). Re-verify on ESPHome bumps.
#include <nvs.h>

namespace esphome {
namespace wifi_reset {

// Returns the esp_err_t of the erase (ESP_OK, or ESP_ERR_NVS_NOT_FOUND when never onboarded — both
// harmless; the caller reboots regardless).
inline int forget_wifi_creds() {
  nvs_handle_t h;
  if (nvs_open("esphome", NVS_READWRITE, &h) != ESP_OK)
    return -1;
  esp_err_t err = nvs_erase_key(h, "88491487");
  nvs_commit(h);
  nvs_close(h);
  return (int) err;
}

}  // namespace wifi_reset
}  // namespace esphome
