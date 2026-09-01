#include "netwatch_probe.h"
#include <atomic>
#include "esphome/core/hal.h"   // esphome::millis()
#include "lwip/tcpip.h"         // tcpip_try_callback (lwip API — mbox-scheduled onto the tcpip thread)

namespace esphome {
namespace netwatch {

// Lock-free: written by the tcpip thread (in probe_cb), read by loopTask (netwatch tick). Single timestamps,
// no dependent memory -> relaxed ordering is correct (same rationale as g_net_ok_ms).
static std::atomic<uint32_t> s_probe_ok_ms{0};
static std::atomic<bool> s_probe_ever{false};

// RUNS ON THE lwip tcpip THREAD (posted via tcpip_try_callback). The mere fact that this executes proves the
// tcpip thread dequeued its mailbox = it is alive and processing. A network-stack wedge (link up but stuck) never runs this. millis()
// reads esp_timer and is safe from any task/context.
static void probe_cb(void * /*ctx*/) {
  s_probe_ok_ms.store(esphome::millis(), std::memory_order_relaxed);
  s_probe_ever.store(true, std::memory_order_relaxed);
}

void netwatch_probe_send() {
  // Non-blocking mailbox post; no core-lock, never blocks loopTask. ERR_MEM (mbox full or the small
  // MEMP_TCPIP_MSG_API pool exhausted because a wedged thread isn't draining) -> just skip this cycle; a real
  // wedge manifests as sustained staleness of s_probe_ok_ms, which the loopTask detector handles. Pool is
  // bounded -> a wedge can't make this leak or flood.
  tcpip_try_callback(probe_cb, nullptr);
}

uint32_t netwatch_probe_ok_ms() { return s_probe_ok_ms.load(std::memory_order_relaxed); }
bool netwatch_probe_ever_ok() { return s_probe_ever.load(std::memory_order_relaxed); }

}  // namespace netwatch
}  // namespace esphome
