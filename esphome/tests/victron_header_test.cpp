// Host unit test for the Victron plaintext product-advertisement header parser
// (esphome/ble_scan.h : parse_victron_header). Pure std, no ESPHome/IDF deps.
//
// Build + run on the host (from the repo root):
//   g++ -std=c++17 -I esphome esphome/tests/victron_header_test.cpp -o /tmp/vhtest && /tmp/vhtest
//
// Test vectors = THREE independent live captures of a real Blue Smart IP65 12/5 (the charger's BLE MAC is
// recorded in the private capture notes), the manufacturer-specific-data payload AFTER the 0x02E1 company id
// (= ESPHome ServiceData.data).
// See docs/internal/ble-name-type-plan.md §3/§5/§8/§11.8. All three MUST decode to
// record_type=0x08 (AC Charger) + product_id=0xA30E; only the data_counter (bytes 5-6) differs.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "ble_scan.h"

using esphome::ble_scan::parse_victron_header;

int main() {
  // payload = data[0]=0x10 (Product Advertisement), [1]=len, [2..3]=product_id LE (0E A3 => 0xA30E),
  //           [4]=record_type (0x08 AC Charger), [5..6]=data_counter (varies), [7]=encryption_key_0 ...
  const std::vector<std::vector<uint8_t>> vectors = {
      {0x10, 0x00, 0x0E, 0xA3, 0x08, 0xA5, 0xB2, 0x91},  // capture 1 (§3)
      {0x10, 0x00, 0x0E, 0xA3, 0x08, 0x77, 0xC8, 0x91},  // capture 2 (§11.8 passive)
      {0x10, 0x00, 0x0E, 0xA3, 0x08, 0x9A, 0xC8, 0x91},  // capture 3 (§11.8 active primary-adv)
  };
  for (const auto &v : vectors) {
    uint8_t rt = 0;
    uint16_t pid = 0;
    assert(parse_victron_header(v, rt, pid) && "product advertisement must parse");
    assert(rt == 0x08 && "record_type must be AC Charger (0x08)");
    assert(pid == 0xA30E && "product_id must be 0xA30E");
  }

  // Negatives: not a product advertisement (data[0] != 0x10) and too short.
  uint8_t rt = 0;
  uint16_t pid = 0;
  const std::vector<uint8_t> not_product = {0x01, 0x02, 0x03, 0x04, 0x05};
  assert(!parse_victron_header(not_product, rt, pid) && "non-0x10 record must be rejected");
  const std::vector<uint8_t> too_short = {0x10, 0x00, 0x0E};
  assert(!parse_victron_header(too_short, rt, pid) && "short payload must be rejected");

  std::printf("victron_header_test: ALL PASS (rt=0x08 AC Charger, pid=0xA30E across 3 live captures)\n");
  return 0;
}
