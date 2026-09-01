#pragma once
#include <cstdint>
// Watchdog — API-liveness feed. Returns the most-recent
// APIConnection::last_traffic_ (millis-domain, App loop-component start time) across all connected API clients,
// or 0 if none connected / the API isn't compiled in. (All current boards DO compile the API — core.yaml
// declares api: unconditionally — so the USE_API=0 stub is only a guard for a hypothetical api-less board.)
// Read from loopTask (netwatch), and the
// underlying value is written only from loopTask (the API is a loopTask Component) -> no atomic needed. The
// access shim lives entirely in api_traffic.cpp so this clean declaration is all main.cpp ever sees.
namespace esphome {
namespace netwatch {
uint32_t api_last_traffic_ms();
// True if >=1 native-API client is connected (HA). false if none / API not compiled in. Wrapping it here keeps
// the api:: namespace + its headers out of main.cpp (the netwatch lambda calls only these two helpers).
bool api_is_connected();
}  // namespace netwatch
}  // namespace esphome
