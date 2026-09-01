#pragma once
#include <cstdint>
// Network-watchdog Phase 2 — CLIENT-INDEPENDENT tcpip-thread liveness probe.
//
// Closes the "web-only / no-HA" blind spot: the passive g_net_ok_ms signal is only fed by httpd requests + API
// traffic, so a device with NO Home Assistant AND no open browser tab has nothing keeping it fresh -> the watchdog
// can't safely arm. The probe becomes that feed WITHOUT any external client.
//
// Mechanism: each ~5s loopTask posts a `tcpip_try_callback` to the lwip tcpip
// thread's mailbox (non-blocking, NO core-lock — unlike a socket sendto). The callback RUNS ON the tcpip
// thread and stamps a timestamp; its execution PROVES the thread is draining its mailbox = alive. A network-stack
// wedge (tcpip thread stuck) stops draining -> the stamp freezes while loopTask keeps ticking = the exact wedge
// signature, generated on demand. FEAS-verified: tcpip_try_callback always mbox-schedules (tcpip.c:350),
// independent of LWIP_TCPIP_CORE_LOCKING; a full mbox/MEMP_TCPIP_MSG_API pool just returns ERR_MEM (skip).
//
// FAIL-SAFE (the single most important property): `netwatch_probe_ever_ok()` gates arming — the probe path
// arms ONLY after a callback has run at least once. If the probe is ever infeasible/broken on some board it
// simply never arms -> the watchdog falls back to today's client-present behaviour (status quo), NEVER a false
// reboot on a healthy device. A bench with any network client attached masks a dead probe, so validate the
// zero-client soak over SERIAL (a UART cable is not a network client).
namespace esphome {
namespace netwatch {
void netwatch_probe_send();        // post a probe callback to the tcpip thread (call from loopTask, throttled ~5s)
uint32_t netwatch_probe_ok_ms();   // millis() when a probe callback last RAN on the tcpip thread (0 = never)
bool netwatch_probe_ever_ok();     // true once a callback has run >=1x — the fail-safe arm latch
}  // namespace netwatch
}  // namespace esphome
