#pragma once
#include <cstdint>
#include "esp_attr.h"

// Network self-recovery breaker state in RTC SLOW memory.
//
// Why RTC and not NVS/globals: the watchdog fires precisely when the tcpip stack is wedged. A clean
// ESPHome shutdown (App.safe_reboot -> API/WiFi on_shutdown -> socket send/close) can BLOCK on the LWIP
// core lock the dead task holds, so the reboot may never happen cleanly and a globals/NVS flush (which is
// only staged in the component's poll / on_shutdown) may never commit -> the loop counter would be lost and
// the breaker could never trip. RTC_NOINIT memory is written DIRECTLY (a plain memory store: instant, no
// flash, no sync, cannot block), and it SURVIVES a SW reset AND a task-watchdog hard reset (so the counter
// accumulates even through a dirty reboot), while a PHYSICAL power-cycle clears it (RTC loses power) -> the
// user's natural fix (unplug/replug) auto-re-arms the watchdog. NOINIT = not zeroed at boot, so a magic
// guard distinguishes "valid warm-reset content" from "garbage after power-on".
RTC_NOINIT_ATTR uint32_t netwatch_rtc_magic;   // == NETWATCH_RTC_MAGIC when the RTC content is valid
RTC_NOINIT_ATTR uint8_t  netwatch_rtc_recov;   // watchdog reboots in the current unhealthy window
RTC_NOINIT_ATTR uint8_t  netwatch_rtc_marker;  // 1 = the last reboot was watchdog-initiated (vs OTA/user SW reset)

// --- Instrumentation -----------------------------------------------------------------------
// The wedge kills the httpd task, so every /wd/info.json heap reading is POST-reboot (healthy) — we have
// NEVER seen heap AT the wedge. loopTask caches its current free heap into RTC each 1s tick; on a task-WDT
// reset that value SURVIVES (RTC_NOINIT) and holds the heap ~<=1s before the stall = the "heap at the wedge"
// we could never capture. min_free is the running low-water mark across warm reboots (the erosion trajectory),
// cleared only on a physical power-cycle like the breaker. twdt counts task-WDT reboots in the window so the
// self-healing-but-unbounded WDT loop can be bounded (graceful degrade at K).
RTC_NOINIT_ATTR uint32_t netwatch_rtc_last_free;  // LIVE: free heap cached each tick (~<=1s stale)
RTC_NOINIT_ATTR uint32_t netwatch_rtc_wedge_free; // FROZEN: last_free snapshotted at a TASK_WDT boot = heap at the
                                                  // wedge. Only overwritten on the NEXT wedge, so it stays readable
                                                  // in /wd/info.json long after the reboot (last_free is overwritten
                                                  // by the next healthy tick, so it can't be surfaced as "the wedge").
RTC_NOINIT_ATTR uint32_t netwatch_rtc_min_free;   // min free heap ever (across warm reboots; power-cycle clears)
RTC_NOINIT_ATTR uint8_t  netwatch_rtc_twdt;       // task-WDT reboots in the current window (reset-breaker bound)

// --- Watchdog breadcrumb -------------------------------------------------------------------------------
// trigger: what caused the LAST watchdog-initiated (marker) reboot — 0=none, 2=net-stale (network-stack wedge caught).
// Surfaced in /wd/info.json so a FIELD wedge self-reports "the watchdog fired" (trigger=2 + small uptime + SW reset)
// even when the wedge can't be forced on the bench (ship armed, field-self-validated).
// lifetime_reboots: NON-clearing count of watchdog reboots since the last PHYSICAL power-cycle (cleared on
// `fresh` only, like min_free — NOT on the 300s window clear). Observability for the self-healing intermittent
// wedge, which reboots forever without ever tripping the K=3 breaker (that's the watchdog working, but
// the user is never told it's flapping) — surfaced in /wd/info.json.
RTC_NOINIT_ATTR uint8_t  netwatch_rtc_trigger;          // 0=none, 2=net-stale client-path, 3=net-stale probe-path
RTC_NOINIT_ATTR uint32_t netwatch_rtc_lifetime_reboots; // watchdog reboots since last power-cycle (non-clearing)

// Phase-2 flap-cap: the probe-only (clientless) arm reboots the MOST reboot-visible population (a user whose
// only UI is the LCD). K=3 bounds a TIGHT loop, but a probe that false-fires slowly (hours apart) never trips
// K=3 (the 300s window clears recov) -> invisible flapping. This counts probe-path reboots; cleared on
// power-cycle AND whenever a real client (HA) connects (a client proves the device is serving someone, so the
// clientless-flap concern is moot). At the cap the probe arm auto-disables (falls back to client-present arming).
RTC_NOINIT_ATTR uint8_t  netwatch_rtc_probe_fires;      // probe-path reboots since power-cycle / last client

// BUMP THIS whenever the RTC field schema changes. On the first boot after the OTA that adopts the new layout
// the reset is ESP_RST_SW with the OLD magic still valid -> the `fresh` branch would NOT run -> any newly-added
// RTC_NOINIT field keeps garbage (e.g. lifetime_reboots reading ~4e9 in /wd/info.json). A magic mismatch forces
// `fresh` -> all fields zero-initialized. (Later revisions added trigger + lifetime_reboots, then probe_fires.)
static const uint32_t NETWATCH_RTC_MAGIC = 0x5741'7445UL;  // "WAtE" (bumped on every RTC field-schema change)

// --- Watchdog: passive network-liveness signal ---------------------------------------------
// NOT RTC — this is LIVE liveness, not breaker state (it must reset to "unknown" on every boot). The httpd
// task (a SEPARATE FreeRTOS task from loopTask) stamps this with millis() on every served /wd/*.json request;
// loopTask's netwatch reads it. On a network-stack wedge the httpd task FREEZES -> this stops advancing while
// loopTask keeps ticking = the exact wedge signature the task WDT is blind to (loopTask alive -> feeds the WDT
// -> no reboot -> 9h hang). Lock-free 32-bit atomic: safe cross-task with NO mutex/heap/socket (all forbidden
// while the stack is wedged).
// 0 = NEVER STAMPED (sentinel). The detector MUST NOT evaluate staleness until a real network event has
// stamped it at least once — else 0 reads as MAX-stale at the 120s arm -> a false reboot on a healthy device
// that simply has no client traffic yet. An unfed counter must never read stale.
// Declared here (global scope, like the RTC vars); web_dashboard.h forward-declares `extern` it (that header
// is #included BEFORE this one in every board master, so it can't see the definition, only the extern).
#include <atomic>
std::atomic<uint32_t> g_net_ok_ms{0};
