#include "api_traffic.h"
#include "esphome/core/defines.h"   // USE_API

#ifdef USE_API
// Reach APIConnection::last_traffic_ (protected) + APIServer::clients_ (protected). Both are protected and
// APIConnection is `final` (so the usual "derive to expose a protected member" trick is impossible), so we
// widen access with a preprocessor shim that is CONTAINED to this ONE translation unit: main.cpp includes only
// api_traffic.h (the clean declaration), never this macro, so nothing else sees widened access. Access
// specifiers do not affect object layout -> the linked binary is consistent across TUs.
//
// FRAGILE (accepted, guarded): this depends on the private names APIConnection::last_traffic_ and
// APIServer::clients_. It is USE_API-gated and, if an ESPHome API refactor renames/moves either, THIS FILE
// fails to COMPILE (loud, not silent) — the same symbol-check discipline as the netwatch WDT-ISR handler.
// Re-point it then; do not paper over with a reinterpret/offset hack.
#define protected public
#define private public
#include "esphome/components/api/api_server.h"
#include "esphome/components/api/api_connection.h"
#undef private
#undef protected

namespace esphome {
namespace netwatch {

uint32_t api_last_traffic_ms() {
  auto *srv = ::esphome::api::global_api_server;
  if (srv == nullptr)
    return 0;
  uint32_t best = 0;
  for (auto &c : srv->clients_) {          // protected vector<unique_ptr<APIConnection>>, widened above
    if (!c)
      continue;
    uint32_t t = c->last_traffic_;         // protected, widened above; millis-domain
    if (t > best)
      best = t;                            // plain max; the ~49-day millis wrap self-corrects within a period
  }
  return best;                             // 0 = no connected client stamped anything yet
}

bool api_is_connected() {
  auto *srv = ::esphome::api::global_api_server;
  return srv != nullptr && srv->is_connected();   // is_connected() is public
}

}  // namespace netwatch
}  // namespace esphome

#else  // !USE_API — DEAD CODE on all current boards (core.yaml declares api: unconditionally). Kept as a guard
       // so a future api-less board still links; it would simply never let the watchdog arm (client_present=false).
namespace esphome {
namespace netwatch {
uint32_t api_last_traffic_ms() { return 0; }
bool api_is_connected() { return false; }
}  // namespace netwatch
}  // namespace esphome
#endif
