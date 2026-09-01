#pragma once
// RAM-only rolling history of charger state-change events (newest first) for the
// "Charger detail" web card (and, later, a TTGO page). NOT persisted — it resets on every
// reboot/OTA; the first entry is a "Powered on" baseline pushed at on_boot. Events are
// recorded only on an actual state change (dedup by label), from the 250ms main loop, so
// there is no flapping and no per-tick growth. Kept to the last CAP entries.
#include <cctype>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace esphome {
namespace charge_log {

struct Event {
  uint32_t epoch{0};   // unix ts at capture (0 = SNTP not valid yet -> no wall-clock shown)
  uint32_t up_ms{0};   // millis() at capture -> drives "N ago"
  std::string label;   // "Bulk"/"Absorption"/"Float"/"Storage"/"Off"/"Fault: ..."/"No signal"/"Powered on"
};

static const size_t CAP = 8;  // keep the last 8 (web shows all; the TTGO page shows the last 5)

static const char *const MON[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

inline std::vector<Event> &events() {
  static std::vector<Event> e;  // front = oldest, back = newest
  return e;
}

inline std::string &last_label() {
  static std::string s;
  return s;
}

// Total events recorded this power-cycle (incl. ones since evicted from the ring). Lets the
// UI honestly flag "older events not kept" once we've dropped past CAP. Resets on reboot.
inline uint32_t &total_count() {
  static uint32_t n = 0;
  return n;
}

// Wipe the whole log (ring + dedup label + running count). Called on a Demo<->Live mode change so
// one mode's events never masquerade as the other's history (they only cleared on reboot before).
inline void reset() {
  events().clear();
  last_label().clear();
  total_count() = 0;
}

// Record an event iff the label changed since the last one (no flapping). Ring-trims to CAP.
inline void note(const std::string &label, uint32_t epoch, uint32_t up_ms) {
  if (label.empty() || label == last_label())
    return;
  last_label() = label;
  total_count()++;
  auto &e = events();
  e.push_back({epoch, up_ms, label});
  if (e.size() > CAP)
    e.erase(e.begin());
}

// Effective wall-clock epoch for an event. Events logged before the first SNTP sync store
// epoch 0; once the clock is valid we backfill from the monotonic up_ms so "Powered on" and
// other pre-sync events get a real time instead of "N ago". 0 only until the very first sync.
inline uint32_t eff_epoch(const Event &e, uint32_t now_ms, uint32_t now_epoch) {
  if (e.epoch)
    return e.epoch;
  if (now_epoch && e.up_ms <= now_ms)
    return now_epoch - (now_ms - e.up_ms) / 1000;
  return 0;
}

// Allocation-free case-insensitive helpers (called every render tick -> no std::string temporaries).
inline bool ci_starts(const std::string &h, const char *n) {
  size_t nl = strlen(n);
  if (h.size() < nl) return false;
  for (size_t j = 0; j < nl; j++)
    if (tolower((unsigned char) h[j]) != tolower((unsigned char) n[j])) return false;
  return true;
}
inline bool ci_has(const std::string &h, const char *n) {
  size_t nl = strlen(n);
  if (nl == 0) return true;
  if (h.size() < nl) return false;
  for (size_t i = 0; i + nl <= h.size(); i++) {
    size_t j = 0;
    while (j < nl && tolower((unsigned char) h[i + j]) == tolower((unsigned char) n[j])) j++;
    if (j == nl) return true;
  }
  return false;
}

// State -> colour (0xRRGGBB) for the TTGO rows: amber charging, green charged, red fault,
// grey for Off / No signal / Powered on / unknown. Mirrors the web timeline dot colours.
inline uint32_t color_for(const std::string &s) {
  if (ci_starts(s, "fault")) return 0xef4444;
  if (ci_has(s, "bulk") || ci_has(s, "absorption")) return 0xf5b942;
  if (ci_has(s, "float") || ci_has(s, "storage")) return 0x3ddc84;
  return 0x9aa0aa;  // off / no signal / powered on / unknown
}

// State -> MDI glyph (UTF-8) for the TTGO rows; mirrors color_for. All glyphs live in mdi_14.
inline const char *icon_for(const std::string &s) {
  if (ci_starts(s, "fault")) return "\xF3\xB0\x80\xA6";                      // alert
  if (ci_has(s, "bulk") || ci_has(s, "absorption")) return "\xF3\xB0\x89\x81";  // flash
  if (ci_has(s, "float") || ci_has(s, "storage")) return "\xF3\xB0\x97\xA0";    // check-circle
  if (ci_has(s, "no signal")) return "\xF3\xB0\x82\xB2";                     // bluetooth-off
  return "\xF3\xB0\xA4\x84";                                                 // power-sleep (off/powered on)
}

// Compact right-column time for the TTGO page:
// known epoch, same calendar day as now -> "HH:MM"
// known epoch, another day -> "DD Mon" (disambiguates multi-day-old events)
// no epoch yet (pre-first-SNTP) -> "just now" / "Nm ago" / "Nh ago"
inline void ttgo_time(const Event &e, uint32_t now_ms, uint32_t now_epoch, char *buf, size_t n) {
  uint32_t ep = eff_epoch(e, now_ms, now_epoch);
  if (ep) {
    time_t tt = (time_t) ep;
    struct tm ev;
    localtime_r(&tt, &ev);
    if (now_epoch) {
      time_t nt = (time_t) now_epoch;
      struct tm nw;
      localtime_r(&nt, &nw);
      if (ev.tm_year == nw.tm_year && ev.tm_yday == nw.tm_yday) {
        snprintf(buf, n, "%02d:%02d", ev.tm_hour, ev.tm_min);
        return;
      }
    }
    snprintf(buf, n, "%d %s", ev.tm_mday, MON[ev.tm_mon % 12]);
  } else {
    uint32_t ago = (now_ms - e.up_ms) / 1000;
    if (ago < 60)
      snprintf(buf, n, "just now");
    else if (ago < 3600)
      snprintf(buf, n, "%um ago", (unsigned) (ago / 60));
    else
      snprintf(buf, n, "%uh ago", (unsigned) (ago / 3600));
  }
}

// JSON object: {"more":<dropped>,"e":[{"t":"HH:MM","d":"26 Aug","ago":123,"s":"Bulk"}, ...]}
// e is NEWEST FIRST. "more" = events dropped off the front of the ring (0 = nothing dropped;
// >0 -> the UI shows an "N earlier events not kept" marker). t/d are "" when epoch==0 (logged
// pre-SNTP). ago = seconds since capture. localtime_r honors the TZ set via set_timezone().
inline std::string to_json(uint32_t now_ms, uint32_t now_epoch) {
  auto &e = events();
  uint32_t dropped = (total_count() > e.size()) ? (total_count() - (uint32_t) e.size()) : 0;
  std::string out = "{\"more\":" + std::to_string(dropped) + ",\"e\":[";
  bool first = true;
  for (auto it = e.rbegin(); it != e.rend(); ++it) {
    if (!first)
      out += ",";
    first = false;
    char t[8] = "", d[16] = "";
    uint32_t ep = eff_epoch(*it, now_ms, now_epoch);  // backfill pre-SNTP events once time is known
    if (ep != 0) {
      time_t tt = (time_t) ep;
      struct tm tmv;
      localtime_r(&tt, &tmv);
      snprintf(t, sizeof(t), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
      snprintf(d, sizeof(d), "%d %s", tmv.tm_mday, MON[tmv.tm_mon % 12]);
    }
    uint32_t ago = (now_ms - it->up_ms) / 1000;
    std::string esc;
    for (char c : it->label) {
      if (c == '"' || c == '\\')
        esc += '\\';
      if ((unsigned char) c >= 0x20)
        esc += c;
    }
    out += "{\"t\":\"" + std::string(t) + "\",\"d\":\"" + std::string(d) +
           "\",\"ago\":" + std::to_string(ago) + ",\"s\":\"" + esc + "\"}";
  }
  out += "]}";
  return out;
}

}  // namespace charge_log
}  // namespace esphome
