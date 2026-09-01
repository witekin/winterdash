#pragma once
// =============================================================================
// status_compute.h — DATA-TRANSFER OBJECT for the compute/draw decouple
// -----------------------------------------------------------------------------
// This header holds ONLY a shared struct + a singleton accessor — NO logic, NO
// lv_*, NO id(). It is the seam between the board-agnostic COMPUTE lambda (which
// maps charger state → status word/verdict/flags and publishes status_word_pub/
// status_note_pub) and the TTGO DRAW lambda (which paints LVGL from these values).
//
// WHY a bare DTO and not a compute_status() free function: ESPHome #includes these
// headers at the TOP of main.cpp, BEFORE the generated id() globals exist, so a
// header function cannot read id(bat_voltage) etc. The proven repo idiom (see
// charge_log.h, web_dashboard.h) is: the header carries data + pure helpers, the
// id() glue lives in a YAML lambda. So the COMPUTE stays a YAML lambda (relocated
// to core.yaml at S4a-2); it fills status_out(); the DRAW lambda reads it.
//
// Colour is stored as a 0xRRGGBB uint32_t (not lv_color_t) to keep this header
// board-agnostic — the draw does lv_color_hex(o.col_hex). Same treatment as
// charge_log::color_for(). word/icon point at YAML string literals (static
// storage → safe to hold as const char*).
//
// The ≤250ms staleness note: once compute (core interval) and draw (display
// interval) are separate 250ms intervals, the draw reads a status_out() at most
// one tick old. That is imperceptible on a 4Hz display and the web/HA publish is
// always fresh (done inside compute).
// =============================================================================
#include <string>

namespace esphome {
namespace status_compute {

struct StatusOut {
  // --- snapshot of canonical inputs the draw re-reads ---
  bool  have{false};              // bat_voltage.has_state()
  float v{0}, ibat{0}, pw{0};     // voltage / current / power
  char  tstr[16]{"-"};            // preformatted temp "%.0f°C" / "-"
  std::string err;                // charger_error_raw (fault line)
  // --- charge-profile window ---
  float sc{1.0f};                 // nominal scale 1.0 / 2.0 (axis + redlines)
  float red_lo{0}, red_hi{0};     // danger lines
  bool  show_band{false};         // expected-window band visible
  float band_lo{0}, band_hi{0};   // window edges
  bool  is_recond{false};         // RECOND high-V-on-purpose (suppress redline verdict)
  // --- mapped status ---
  const char *word{"IDLE"};       // status word literal
  uint32_t    col_hex{0x888888};  // status colour 0xRRGGBB (was lv_color_t)
  const char *icon{nullptr};      // mdi glyph literal
  int         scode{0};           // 0 idle · 1 charging · 2 charged · 3 fault · 4 recond
  // --- verdict + link flags ---
  std::string note;               // anomaly / aux line (shared with status_note_pub)
  bool  offline{false};           // BLE stale > timeout (draw overlay + early-return)
  bool  ble_stale{false};         // step-1 stale (link dot / BLE page)
  bool  serious_warn{false};      // over/undervoltage keep-lit (draw writes serious_warn_active)
  // --- timing ---
  uint32_t now{0}, age{0};        // millis() + age since last packet
};

// The seam: compute writes, draw reads. One process-wide instance.
inline StatusOut &status_out() {
  static StatusOut o;
  return o;
}

}  // namespace status_compute
}  // namespace esphome
