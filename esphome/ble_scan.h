#pragma once
// Rolling list of nearby BLE devices for the pairing UI. Fed by the
// esp32_ble_tracker on_ble_advertise trigger; exposed as JSON via a text_sensor.
// Victron devices (manufacturer id 0x02E1) are flagged and sorted first.
#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace esphome {
namespace ble_scan {

struct Dev {
  std::string name;
  int rssi{-127};
  bool victron{false};
  uint32_t last_ms{0};
};

// Hard cap on the in-RAM device table. Pruning by age only happens in to_json() (every ~5s, 12s
// window), so without this a burst of nearby strangers could spike heap between prunes. RAM-tight board.
static const size_t MAX_DEVICES = 24;   // > the JSON output cap (20); Victron entries are never evicted

// Minimum RSSI to appear in the pairing list. Applies to EVERYTHING incl. Victron (no special-case):
// a weak-signal charger means an unstable BLE link, so hiding it is correct — it steers the user to
// move the device closer rather than pair something that keeps dropping. Also declutters strangers +
// saves heap (fewer map entries). Tune on-device; the live IP65 sat around -50..-53 dBm.
static const int RSSI_FLOOR = -85;

inline std::map<std::string, Dev> &devices() {
  static std::map<std::string, Dev> d;
  return d;
}

inline void clear() { devices().clear(); }

inline void add(const std::string &mac, const std::string &name, int rssi, bool victron, uint32_t now) {
  if (rssi < RSSI_FLOOR)
    return;                              // too far for a stable link -> don't offer it for pairing
  auto &d = devices();
  if (d.find(mac) == d.end() && d.size() >= MAX_DEVICES) {
    // Table full + a NEW device: evict the stalest non-Victron entry (never drop a Victron device;
    // fall back to the stalest overall only if every entry is Victron, which shouldn't happen).
    auto victim = d.end();
    for (auto it = d.begin(); it != d.end(); ++it) {
      if (it->second.victron)
        continue;
      if (victim == d.end() || it->second.last_ms < victim->second.last_ms)
        victim = it;
    }
    if (victim == d.end()) {  // all Victron: evict the stalest overall
      for (auto it = d.begin(); it != d.end(); ++it)
        if (victim == d.end() || it->second.last_ms < victim->second.last_ms)
          victim = it;
    }
    if (victim != d.end())
      d.erase(victim);
  }
  auto &dev = d[mac];
  if (!name.empty())
    dev.name = name;
  dev.rssi = rssi;
  dev.victron = dev.victron || victron;
  dev.last_ms = now;
}

// Prune stale entries, then build a JSON array (Victron first, then by RSSI, capped).
inline std::string to_json(uint32_t now, uint32_t max_age_ms, size_t cap) {
  auto &all = devices();
  for (auto it = all.begin(); it != all.end();) {
    if (now - it->second.last_ms > max_age_ms)
      it = all.erase(it);
    else
      ++it;
  }
  std::vector<std::pair<std::string, Dev>> v(all.begin(), all.end());
  // Group: Victron (2) > named (1) > unnamed (0), highest first; within a group, by RSSI.
  std::sort(v.begin(), v.end(), [](const std::pair<std::string, Dev> &a, const std::pair<std::string, Dev> &b) {
    int ga = a.second.victron ? 2 : (a.second.name.empty() ? 0 : 1);
    int gb = b.second.victron ? 2 : (b.second.name.empty() ? 0 : 1);
    if (ga != gb)
      return ga > gb;
    return a.second.rssi > b.second.rssi;
  });
  std::string out = "[";
  size_t n = 0;
  for (auto &p : v) {
    if (n >= cap)
      break;
    if (n)
      out += ",";
    n++;
    std::string esc;
    for (char c : p.second.name) {
      if (c == '"' || c == '\\')
        esc += '\\';
      if ((unsigned char) c >= 0x20)
        esc += c;
    }
    out += "{\"mac\":\"" + p.first + "\",\"name\":\"" + esc + "\",\"rssi\":" + std::to_string(p.second.rssi) +
           ",\"v\":" + (p.second.victron ? "true" : "false") + "}";
  }
  out += "]";
  return out;
}

}  // namespace ble_scan
}  // namespace esphome
