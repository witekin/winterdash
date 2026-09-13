#pragma once
#include <algorithm>
// =============================================================================
// backlight_model.h — the ONE consolidated screen-backlight ladder for WinterDash.
// PURE (no id(), no lv_*): the YAML draw/touch lambdas read id() and pass the fractions in, so every board that has a
// backlight reuses this verbatim (same idiom as status_compute.h). Single source of truth for every set_brightness site.
//
// Model (settled 2026-09-04): the user has ONE brightness knob = DAY brightness (the ceiling). A CONTINUOUS ambient
// "darkness" 0..1 (0=bright, 1=full-dark; from the LDR, or a fixed 0 on a board with no sensor) scales brightness DOWN
// along a curve — a dimly-lit garage dims moderately, a dark garage more. Nothing readable ever goes below MIN_READABLE
// (below that is a deliberate OFF, handled elsewhere). Idle-dim is RELATIVE to the day setting (IDLE_FRACTION*day) so a
// bright-preferring user gets a brighter idle too, and it is darkness-corrected the same way. Faults ignore darkness.
// =============================================================================
namespace wd_bl {

  constexpr float NIGHT_FACTOR  = 0.40f;   // ACTIVE brightness at FULL dark = day * this (the dark end of the curve)
  constexpr float MIN_READABLE  = 0.30f;   // absolute readable floor: nothing readable dims below this (below = a deliberate OFF)
  constexpr float IDLE_FRACTION = 0.65f;   // idle-dim ≈ this * day (reproduces the old "Dim level 65%" but relative to day)
  constexpr float IDLE_CAP      = 0.70f;   // idle ceiling guard (only bites if IDLE_FRACTION is raised)

  enum class BlMode { ACTIVE, IDLE, KEEP_LIT };

  // day     = backlight_level/100 (the manual "Backlight" ceiling, 0..1).
  // dark01  = continuous darkness 0(bright)..1(full-dark). A no-sensor board passes 0 (always day).
  // mode    = ACTIVE (someone viewing) / IDLE (idle-dim) / KEEP_LIT (fault/warn/netfault/AP -> full, ignore darkness).
  // ha_max  = a future global-max clamp for an external setter (defaults open = 1.0; the HA-clamp slot).
  // idle_fraction = idle-dim as a fraction of day (defaults to IDLE_FRACTION 0.65). CYD passes DIM_FRAC[level] for the
  //           user 4-level idle-dim; OFF is handled in the dim machine as turn_off (not here — this path always floors).
  inline float eff_backlight(float day, float dark01, BlMode mode, float ha_max = 1.0f,
                             float idle_fraction = IDLE_FRACTION) {
    day    = std::min(std::max(day, 0.0f), 1.0f);
    day    = std::min(day, ha_max);                              // <-- future HA global-max clamp lives here
    dark01 = std::min(std::max(dark01, 0.0f), 1.0f);
    if (mode == BlMode::KEEP_LIT) return std::max(day, MIN_READABLE);   // faults: full day, darkness ignored
    // shared darkness dim multiplier: 1.0 (bright) -> NIGHT_FACTOR (full dark), linear.
    const float m = 1.0f - (1.0f - NIGHT_FACTOR) * dark01;
    float base = (mode == BlMode::IDLE) ? std::min(idle_fraction * day, IDLE_CAP) : day;
    return std::max(base * m, MIN_READABLE);                     // readable-state floor guard
  }

}  // namespace wd_bl
