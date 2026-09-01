#pragma once
// Serves the custom web pages from flash and stores custom banners in the "storage" flash
// partition. All same-origin against the device's web_server.
// GET / -> dashboard (web/dashboard.html)
// GET /convert -> banner converter (web/image-tool.html)
// Gallery — 10 four-region slots; see the "custom-cutout" section below:
// POST /gallery/dash|saver|hero|thumb, /gallery/commit, /gallery/select|delete|favorite;
// GET /gallery (JSON index), /gallery/hero|thumb?id.
// Legacy GET-only (slot-0 fallback for the web hero): /banner, /banner-full, /banner-thumb.
// (The old POST /banner/* upload handlers were removed in the 0219646 heap fix.)
// Storage layout is per-slot, 4 semi-uniform regions (sizes DERIVED per-board from the banner dims) — see the
// SLOT_SPAN / R_DASH/R_SAVER/R_HERO/R_THUMB block below; the legacy FULL_OFF/THUMB_OFF aliases remain only for
// the slot-0 GET fallbacks.
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/core/string_ref.h"
#include "dashboard_html.h"
#include "convert_html.h"
#ifdef WD_BAKE_MINIMAL
#include "banners_baked_minimal.h"   // 4MB T-Display: Battery only (web hero matches the LCD bake-1)
// The set of baked presets this board actually ships — published in GET /gallery's board:{} so the SHARED
// dashboard.html builds the picker grid from THIS list, not a hard-coded 10 (else a 4MB unit renders 9 phantom
// tiles whose /img/<name> 404s + a broken hero that sticks until reload). Keep in lock-step with the select
// options in banner_preset_minimal.yaml / _full.yaml and the on-LCD bake set (presets_tdisplay_extra.yaml).
#define WD_BAKED_JSON "\"Battery\""
#else
#include "banners_baked.h"           // 16MB T-Display + headless: the full 10-preset gallery
#define WD_BAKED_JSON "\"Battery\",\"Sedan\",\"SUV\",\"Pickup\",\"Van\",\"Classic\",\"Sports\",\"Quad\",\"Motorcycle\",\"Boat\""
#endif
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <atomic>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "esphome/core/hal.h"   // esphome::millis()

// Watchdog network-liveness signal (defined in netwatch_rtc.h, which is #included AFTER this header in every
// board master). Stamped from the httpd task in JsonSlotGet::handleRequest below; read by loopTask's netwatch.
// See netwatch_rtc.h for the full rationale (the httpd task freezes on a network-stack wedge = the detectable signal).
extern std::atomic<uint32_t> g_net_ok_ms;

namespace esphome {
namespace web_dashboard {

// --- Per-slot storage geometry in the "storage" partition (semi-uniform, DERIVE-FROM-DIMS) ----------
// Each slot holds 4 assets at DIFFERENT, build-time-DERIVED region sizes, packed
// back-to-back. Sizes derive from the board's already-declared banner dims (WD_DASH/SAVER_W/H) + two webp
// caps — a board writes NO region sizes by hand (single source of truth = the dims it already declares for
// the converter). This replaces the v2b uniform 4xREGION design, whose saver-fitting REGION knob padded the
// dash/hero/thumb up to the saver size (~300KB/slot wasted on CYD; the difference between 0 and >=1 dual-OTA
// custom slot on 4MB).
// R_DASH dash cut-out .wdb RGB565A8 -- word0 == "WDB1" is the USED flag + commit anchor (MUST be region 0)
// R_SAVER saver cut-out .wdb RGB565A8 -- screensaver hero; mmap-RENDERED on display boards
// R_HERO hero WebP [512B magic 'WFUL' + u32 len][bytes @ +512] -- web only (esp_partition_read + mmap-served)
// R_THUMB thumb WebP [512B magic 'WTHB' + u32 len][bytes @ +512] -- web only
// No on-flash index: R_DASH "WDB1" magic is written LAST (atomic commit) -> USED iff word0=="WDB1". Name/category
// live in the R_DASH header. Mutable state (selection, favorites) in NVS. NO migration: dev wipes + re-saves.
//
// ALIGNMENT: esp_partition_mmap() accepts an arbitrary offset — it maps the
// enclosing 64KB MMU page and returns an adjusted pointer (PROVEN on this exact IDF 5.5.4 build by the webp
// serve at region+512, a non-page-aligned offset). So the only HARD floor is SECTOR (4KB erase granularity),
// for every region on every board. The two mmap-RENDERED .wdb regions round to WD_WDB_ALIGN (default 64KB =
// MMU page): a whole-page .wdb keeps its render pointer page-clean AND reproduces TTGO/headless' legacy
// 4x64KB=256KB slot byte-for-byte, so field-saved banners survive with no reflash. The webp regions round to
// 4KB. A board with NO field data — or the headless multi-slot TEST — may set -DWD_WDB_ALIGN=0x1000 to pack
// tighter (proven safe; the .wdb are still 4KB-aligned, and headless never mmap-renders them anyway).
static constexpr size_t SECTOR = 0x1000;          // 4KB flash erase sector = the hard alignment floor
#ifndef WD_WDB_ALIGN
#define WD_WDB_ALIGN 0x10000                       // .wdb region rounding: 64KB (MMU page) — compat + mmap-clean default
#endif
#ifndef WD_HERO_CAP
#define WD_HERO_CAP 0x10000                        // hero webp region cap: 64KB default (TTGO/headless byte-identical)
#endif
#ifndef WD_THUMB_CAP
#define WD_THUMB_CAP 0x10000                       // thumb webp region cap: 64KB default
#endif
static constexpr size_t WEBP_DATA = 512;          // webp bytes start after a 512B header
static const uint32_t MAGIC_FULL = 0x4C554657;    // 'WFUL' little-endian (hero WebP)
static const uint32_t MAGIC_THUMB = 0x42485457;   // 'WTHB' little-endian (thumb WebP)

// Converter/device cut-out dims — per-board via board_*.yaml build_flags (v2a). REQUIRED. Bind to the CONVERTER
// dims (what actually lands in R_DASH/R_SAVER), not the baked-preset resize.
#if !defined(WD_DASH_W) || !defined(WD_DASH_H) || !defined(WD_SAVER_W) || !defined(WD_SAVER_H)
#error "WD_DASH_W/H + WD_SAVER_W/H must be set by the board package build_flags (v2a per-board banner sizes)"
#endif
static constexpr int WDB_DASH_W = WD_DASH_W,  WDB_DASH_H = WD_DASH_H;    // per-board (board_*.yaml build_flags)
static constexpr int WDB_SAVER_W = WD_SAVER_W, WDB_SAVER_H = WD_SAVER_H;

// constexpr geometry: every region SIZE derived from the dims/caps; offsets = running prefix sums (never k*REGION).
static constexpr size_t wd_align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }
static constexpr size_t R_DASH_SIZE  = wd_align_up((size_t) WDB_DASH_W  * WDB_DASH_H  * 3 + 48, WD_WDB_ALIGN);
static constexpr size_t R_SAVER_SIZE = wd_align_up((size_t) WDB_SAVER_W * WDB_SAVER_H * 3 + 48, WD_WDB_ALIGN);
static constexpr size_t R_HERO_SIZE  = wd_align_up((size_t) WD_HERO_CAP,  SECTOR);
static constexpr size_t R_THUMB_SIZE = wd_align_up((size_t) WD_THUMB_CAP, SECTOR);
static constexpr size_t R_DASH  = 0;                                     // MUST be 0 (commit anchor + used-flag scan)
static constexpr size_t R_SAVER = R_DASH  + R_DASH_SIZE;
static constexpr size_t R_HERO  = R_SAVER + R_SAVER_SIZE;
static constexpr size_t R_THUMB = R_HERO  + R_HERO_SIZE;
static constexpr size_t SLOT_SPAN = wd_align_up(R_THUMB + R_THUMB_SIZE, SECTOR);  // 4KB-aligned -> every slot base is too
static constexpr int    NUM_SLOTS = 10;           // logical max; the physical cap is min(NUM_SLOTS, part/SLOT_SPAN)
// Legacy single-banner (slot 0) region names — GET fallback for the web hero. Derived, not literals.
static constexpr size_t WDB_OFF = R_DASH, FULL_OFF = R_HERO, THUMB_OFF = R_THUMB;

// Invariants (the correctness surface of the semi-uniform layout):
static_assert(R_DASH == 0, "R_DASH must be slot offset 0 (the WDB1 commit anchor + used-flag scan read the slot base)");
static_assert((WD_WDB_ALIGN & (WD_WDB_ALIGN - 1)) == 0 && WD_WDB_ALIGN >= SECTOR, "WD_WDB_ALIGN must be a power-of-two >= 4KB");
static_assert(SLOT_SPAN % SECTOR == 0, "SLOT_SPAN must be 4KB-aligned so every slot base stays sector-aligned for erase/mmap");
static_assert((size_t) WD_HERO_CAP  >= WEBP_DATA + SECTOR, "hero webp region cap too small (needs the 512B header + >=1 sector of data)");
static_assert((size_t) WD_THUMB_CAP >= WEBP_DATA + SECTOR, "thumb webp region cap too small");
static_assert((size_t) WDB_DASH_W  * WDB_DASH_H  * 3 + 48 <= R_DASH_SIZE,  "dash .wdb exceeds its derived region (should be impossible — region grows to fit)");
static_assert((size_t) WDB_SAVER_W * WDB_SAVER_H * 3 + 48 <= R_SAVER_SIZE, "saver .wdb exceeds its derived region (should be impossible — region grows to fit)");
// Per-board COMPAT LOCK. A board that has shipped field-saved banners declares its
// expected SLOT_SPAN here; then a later dim/cap tweak that would silently move region offsets (and misalign
// those field banners on the very next OTA — no other compile warning would fire) fails the BUILD instead.
// The "byte-identical, no reflash" guarantee for TTGO/headless rests on a 5.6%-under-64KB coincidence; this
// turns that coincidence into an enforced contract. Boards with NO field data (CYD, pre-launch) omit the flag.
#ifdef WD_EXPECT_SLOT_SPAN
static_assert(SLOT_SPAN == (size_t) WD_EXPECT_SLOT_SPAN,
              "SLOT_SPAN drifted from this board's shipped geometry (WD_EXPECT_SLOT_SPAN) — a dim/cap change would "
              "misalign field-saved banners on the next OTA. Revert it, or plan a wipe and bump the lock.");
#endif

// httpd task -> 250ms main loop. A SINGLE aligned 32-bit word each (atomic R/W on Xtensa) — never
// split one intent across two volatiles. Main loop reads-and-resets to -1.
inline volatile int32_t gallery_pending = -1;      // -1 idle | 0..9 = select N | (0x100|N) = delete N
inline volatile int32_t gallery_fav_pending = -1;  // -1 idle | N | (0x100 if fav=1)
// main loop -> httpd task (read by GET /gallery + the alloc/erase scan). Published by the loop.
inline volatile int      gallery_active_slot = -1; // slot currently mmapped for render (excluded from alloc/erase)
inline volatile int      gallery_sel = -1;         // selected user slot, or -1 when a baked preset is live
inline volatile uint64_t gallery_fav = 0;          // favorites bitmask (mirrors NVS)

// ONE shared 512B flash-write staging buffer for all upload handlers. Safe because web_server_idf
// has a SINGLE httpd worker -> exactly one upload handler runs at a time. Keeping it off both the
// handler objects and the small httpd task stack saves heap headroom (this board is ~58KB-tight).
inline uint8_t g_upbuf[512];

// Storage helpers (httpd task). All reads are header-only unless noted.
inline const esp_partition_t *gallery_part() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
}
inline size_t gallery_slot_off(int slot) { return static_cast<size_t>(slot) * SLOT_SPAN; }
// Physical slot cap: how many whole 256KB slots the "storage" partition ACTUALLY holds, floored to the
// fixed logical max NUM_SLOTS. On a tight-flash board (headless storage=320KB) this is 1, not 10 — every id
// range-check MUST use this, not NUM_SLOTS, or a save/select for slot>=cap addresses PAST the partition ->
// gallery_erase_slot fails ("erase failed") instead of the graceful "gallery_full". (Bug fix .)
inline int gallery_slot_cap(const esp_partition_t *p) {
  if (p == nullptr) return 0;
  size_t phys = p->size / SLOT_SPAN;
  return static_cast<int>(phys < static_cast<size_t>(NUM_SLOTS) ? phys : NUM_SLOTS);
}
inline int gallery_slot_cap() { return gallery_slot_cap(gallery_part()); }
inline bool gallery_slot_used(const esp_partition_t *p, int slot) {
  uint8_t m[4];
  return p != nullptr && slot >= 0 && slot < gallery_slot_cap(p) &&
         esp_partition_read(p, gallery_slot_off(slot), m, 4) == ESP_OK && memcmp(m, "WDB1", 4) == 0;
}
// First free slot for a new save, EXCLUDING the currently-rendered slot (never clobber the live
// banner even if its magic were torn). Uncommitted (0xFF word0) and deleted (0x00) both read free.
inline int gallery_next_free(const esp_partition_t *p) {
  for (int i = 0; i < gallery_slot_cap(p); i++) {
    if (i == gallery_active_slot) continue;
    if (!gallery_slot_used(p, i)) return i;
  }
  return -1;
}
// Erase a slot's whole span up-front, 4KB at a time, yielding between sectors so WiFi/LwIP keep
// breathing (bounds the single-connection stall window). NOT lazy-per-write (that had an
// empty-body erase hole). SLOT_SPAN is 4KB-aligned -> base = slot*SLOT_SPAN is always sector-aligned.
inline bool gallery_erase_slot(const esp_partition_t *p, int slot) {
  size_t base = gallery_slot_off(slot);
  for (size_t o = 0; o < SLOT_SPAN; o += SECTOR) {
    if (esp_partition_erase_range(p, base + o, SECTOR) != ESP_OK) return false;
    vTaskDelay(1);
  }
  return true;
}

// --- main-loop -> httpd JSON handoff (off SSE) -------------------------------
// The big charger JSON blobs (event log, BLE scan list) used to ride the
// web_server SSE /events stream as text_sensor states. On this tight-heap board a >640B entity
// JSON forced the SSE SerializationBuffer to spill onto the heap and abort() under fragmentation
// (the "reboot on banner change / hard-reload" crash). They're now `internal:true` (off SSE + API)
// and served on demand from the endpoints below. A template text_sensor still BUILDS each blob on
// the MAIN LOOP (its update_interval); an on_value tees the result into one of these slots, and the
// httpd task reads the slot when a browser fetches /wd/*.json.
//
// Double-buffered with an atomic active index: the single main-loop writer only ever mutates the
// INACTIVE buffer, then publishes it by flipping the index; the httpd reader copies whichever buffer
// is active. Builds are seconds apart and a read completes in microseconds, so the reader is always
// done long before the writer could cycle back and reuse that buffer. This honours the codebase rule
// that the main loop owns all mutation and the httpd task only reads stable/atomic state.
struct JsonSlot {
  std::string buf[2];
  std::atomic<uint8_t> active{0};
  void publish(const std::string &s) {                    // MAIN LOOP (text_sensor on_value)
    uint8_t next = active.load(std::memory_order_relaxed) ^ 1u;
    buf[next] = s;
    active.store(next, std::memory_order_release);
  }
  void read(std::string &out) const {                     // httpd task
    out = buf[active.load(std::memory_order_acquire)];
  }
};
inline JsonSlot slot_log, slot_scan;
// About/Network read-only info + diagnostics, assembled on the main loop into one JSON doc
// (a 5s interval builds it) and served at /wd/info.json. Off SSE + API for the whole cluster.
inline JsonSlot slot_info;
// Fast-live tier: the small, frequently-changing mirrors (uptime + el/age/offel/brssi/ha), built on a
// short interval and served at /wd/live.json. The web polls this fast (heartbeat + offline responsiveness)
// while the heavier static/diag /wd/info.json polls slowly. Uptime rides here so the web can detect a
// device restart (uptime dropped) and re-sync the SSE.
inline JsonSlot slot_live;

// GET /wd/<x>.json -> serve a JsonSlot's current buffer. Empty 200 body until the first main-loop
// build; the dashboard treats "" as "nothing yet". no-store: the browser polls on its own cadence.
class JsonSlotGet : public AsyncWebHandler {
 public:
  JsonSlotGet(const char *url, JsonSlot *slot, const char *ctype) : url_(url), slot_(slot), ctype_(ctype) {}
  bool canHandle(AsyncWebServerRequest *request) const override {
    char b[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->method() == HTTP_GET && request->url_to(b) == url_;
  }
  bool isRequestHandlerTrivial() const override { return false; }
  void handleRequest(AsyncWebServerRequest *request) override {
    // Watchdog liveness: the httpd task served a /wd/*.json request => the network stack is making inbound
    // progress on a task SEPARATE from loopTask. On a network-stack wedge this handler stops running -> the stamp
    // goes stale while loopTask keeps ticking = the signal netwatch detects. memory_order_relaxed: a single
    // monotonic timestamp, no other memory is published through it.
    ::g_net_ok_ms.store(::esphome::millis(), std::memory_order_relaxed);
#ifdef WD_NETB_TASKPROBE
    // STEP-1 pre-ship gate 1 (temporary): confirm this handler runs OFF loopTask. Logs the FreeRTOS task name
    // once. Expected: NOT "loopTask" (e.g. "httpd"/"async_tcp"). Compile-gated so it never ships.
    static bool probe_logged = false;
    if (!probe_logged) { probe_logged = true;
      ESP_LOGI("netB", "JsonSlotGet handler task = '%s'", pcTaskGetName(nullptr)); }
#endif
    std::string body;
    slot_->read(body);                                    // copy the stable buffer, then send
    auto *resp = request->beginResponse(200, ctype_, body);
    resp->addHeader("Cache-Control", "no-store");
    request->send(resp);
  }
 protected:
  const char *url_;
  JsonSlot *slot_;
  const char *ctype_;
};

// --- Static page handlers ----------------------------------------------------
class StaticPage : public AsyncWebHandler {
 public:
  StaticPage(const char *url, const uint8_t *body, size_t len, bool gzip = false)
      : url_(url), body_(body), len_(len), gzip_(gzip) {}
  bool canHandle(AsyncWebServerRequest *request) const override {
    char b[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->method() == HTTP_GET && request->url_to(b) == url_;
  }
  void handleRequest(AsyncWebServerRequest *request) override {
    // no-store: the page changes with every OTA and the browser was serving stale copies (heuristic
    // caching, no validator). Always refetch — the page is small and LAN-served, so it's cheap.
    auto *resp = request->beginResponse(200, "text/html", body_, len_);
    resp->addHeader("Cache-Control", "no-store");
    if (gzip_) resp->addHeader("Content-Encoding", "gzip");   // body_ is gzipped in flash; browser inflates
    request->send(resp);
  }
  bool isRequestHandlerTrivial() const override { return false; }
 protected:
  const char *url_;
  const uint8_t *body_;
  size_t len_;
  bool gzip_{false};
};

// (The branded Wi-Fi onboarding page is served by the VENDORED captive_portal — see
// components/captive_portal/captive_index.h, generated from web/onboarding.html. Competing with
// captive via our own handler did not win the AP-mode dispatch, so we replace captive's bytes.)

// --- Serve a stored WebP (mmap, byte range after the 512B header) ------------
class BannerWebpGet : public AsyncWebHandler {
 public:
  BannerWebpGet(const char *url, size_t off, size_t rsize, uint32_t magic) : url_(url), off_(off), rsize_(rsize), magic_(magic) {}
  bool canHandle(AsyncWebServerRequest *request) const override {
    char b[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->method() == HTTP_GET && request->url_to(b) == url_;
  }
  bool isRequestHandlerTrivial() const override { return false; }
  void handleRequest(AsyncWebServerRequest *request) override {
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    uint8_t hdr[8];
    if (part != nullptr && esp_partition_read(part, off_, hdr, 8) == ESP_OK) {
      uint32_t mg, len;
      memcpy(&mg, hdr, 4); memcpy(&len, hdr + 4, 4);
      if (mg == magic_ && len > 0 && len < (rsize_ - WEBP_DATA)) {
        static esp_partition_mmap_handle_t gh = 0;
        const void *ptr = nullptr;
        if (gh) { esp_partition_munmap(gh); gh = 0; }
        if (esp_partition_mmap(part, off_ + WEBP_DATA, len, ESP_PARTITION_MMAP_DATA, &ptr, &gh) == ESP_OK) {
          request->send(request->beginResponse(200, "image/webp",
                        reinterpret_cast<const uint8_t *>(ptr), len));
          return;
        }
      }
    }
    request->send(404, "text/plain", "no image");
  }
 protected:
  const char *url_;
  size_t off_;
  size_t rsize_;   // this region's byte span (webp len bound); per-board via registration
  uint32_t magic_;
};

// --- Serve a BAKED preset WebP from flash rodata at /img/<name> --------------
// De-inlined from dashboard.html (the 10 base64 blobs were ~66% of the page and OOM-crashed the board
// when served as one huge response). Bytes live in banners_baked.h (memory-mapped flash rodata -> no
// heap copy). Long cache so the browser fetches each preset once. Presets change only on a reflash;
// if the art is regenerated, hard-refresh once (or bump the cache window). Names are simple ASCII.
class BannerBakedGet : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *request) const override {
    if (request->method() != HTTP_GET) return false;
    char b[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(b);
    return strncmp(b, "/img/", 5) == 0;
  }
  bool isRequestHandlerTrivial() const override { return false; }
  void handleRequest(AsyncWebServerRequest *request) override {
    char b[AsyncWebServerRequest::URL_BUF_SIZE];
    request->url_to(b);
    char name[24];
    const char *p = b + 5;  // after "/img/"
    size_t i = 0;
    for (; p[i] != '\0' && p[i] != '?' && i < sizeof(name) - 1; i++) name[i] = p[i];
    name[i] = '\0';
    size_t len = 0;
    const uint8_t *data = banners_baked::webp(name, len);
    if (data == nullptr) { request->send(404, "text/plain", "no image"); return; }
    auto *resp = request->beginResponse(200, "image/webp", data, len);
    resp->addHeader("Cache-Control", "public, max-age=604800");  // 7d: presets are ~immutable per build
    request->send(resp);
  }
};

// GET /banner — serve the stored .wdb (mmap, no RAM copy) for download-your-own.
class BannerGet : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *request) const override {
    char b[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->method() == HTTP_GET && request->url_to(b) == "/banner";
  }
  bool isRequestHandlerTrivial() const override { return false; }
  void handleRequest(AsyncWebServerRequest *request) override {
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    uint8_t hdr[16];
    if (part != nullptr && esp_partition_read(part, WDB_OFF, hdr, 16) == ESP_OK && memcmp(hdr, "WDB1", 4) == 0) {
      uint16_t w = hdr[6] | (hdr[7] << 8), h = hdr[8] | (hdr[9] << 8);
      size_t sz = 48 + static_cast<size_t>(w) * h * 3;
      static esp_partition_mmap_handle_t gh = 0;
      const void *ptr = nullptr;
      if (gh) { esp_partition_munmap(gh); gh = 0; }
      if (esp_partition_mmap(part, WDB_OFF, sz, ESP_PARTITION_MMAP_DATA, &ptr, &gh) == ESP_OK) {
        request->send(request->beginResponse(200, "application/octet-stream",
                      reinterpret_cast<const uint8_t *>(ptr), sz));
        return;
      }
    }
    request->send(404, "text/plain", "no custom banner");
  }
};

// ============================ Gallery handlers ==============================
// Serving GETs rely on IDF's SYNCHRONOUS httpd_resp_send + a SINGLE httpd worker: the mmap
// handle is created and torn down within one handleRequest, so its lifetime is bounded and
// never overlaps another request. Do not port these to an async/multi-worker server as-is.

// GET /gallery -> JSON index (header-only, no mmap): {next_free, slots:[{id,w,h,cat,sel,fav,name}]}
class GalleryList : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *request) const override {
    char b[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->method() == HTTP_GET && request->url_to(b) == "/gallery";
  }
  bool isRequestHandlerTrivial() const override { return false; }
  void handleRequest(AsyncWebServerRequest *request) override {
    const esp_partition_t *p = gallery_part();
    static char buf[1536];  // off the small httpd stack; single httpd worker -> safe to reuse. 768->1024->1536: a
                            // full 10-slot gallery + the board:{} field (incl. the ~110B baked[]) fits ~1156B with
                            // PLAIN names; 1536 also covers the pathological case (10 slots x 32-char names that
                            // all escape-double to 64B). Every snprintf is n>=sizeof(buf) clamped -> truncation is
                            // graceful (client rejects the JSON + retries), never an overflow.
    size_t n = 0;
    // board:{} = this board's converter cut-out sizes + baked[] (the presets this board ships) — emitted BEFORE the
    // truncatable slots loop so it's never dropped on a full gallery; the shared image-tool.html fits its .wdb to
    // dash/saver (v2a board-adaptive converter) and dashboard.html builds the picker grid from baked[].
    n += snprintf(buf + n, sizeof(buf) - n,
                  "{\"next_free\":%d,\"board\":{\"dash\":[%d,%d],\"saver\":[%d,%d],\"baked\":[" WD_BAKED_JSON "]},\"slots\":[",
                  p ? gallery_next_free(p) : -1, WDB_DASH_W, WDB_DASH_H, WDB_SAVER_W, WDB_SAVER_H);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    bool first = true;
    if (p != nullptr) {
      for (int i = 0; i < gallery_slot_cap(p); i++) {
        if (!gallery_slot_used(p, i)) continue;
        uint8_t h[48];
        if (esp_partition_read(p, gallery_slot_off(i), h, 48) != ESP_OK) continue;
        uint16_t w = h[6] | (h[7] << 8), ht = h[8] | (h[9] << 8);
        uint8_t cat = h[10];
        bool sel = (gallery_sel == i);
        bool fav = (gallery_fav >> i) & 1ULL;
        n += snprintf(buf + n, sizeof(buf) - n,
                      "%s{\"id\":%d,\"w\":%u,\"h\":%u,\"cat\":%u,\"sel\":%s,\"fav\":%s,\"name\":\"",
                      first ? "" : ",", i, w, ht, cat, sel ? "true" : "false", fav ? "true" : "false");
        if (n >= sizeof(buf)) { n = sizeof(buf) - 1; break; }
        first = false;
        for (int k = 0; k < 32 && h[16 + k]; k++) {  // name: bounded, ASCII-only, JSON-escaped
          char c = static_cast<char>(h[16 + k]);
          if (n + 2 >= sizeof(buf)) break;
          if (c == '"' || c == '\\') { buf[n++] = '\\'; buf[n++] = c; }
          else if (c >= 0x20 && c < 0x7f) buf[n++] = c;
        }
        n += snprintf(buf + n, sizeof(buf) - n, "\"}");
        if (n >= sizeof(buf)) { n = sizeof(buf) - 1; break; }
      }
    }
    if (n + 2 < sizeof(buf)) { buf[n++] = ']'; buf[n++] = '}'; }
    buf[n] = 0;
    request->send(200, "application/json", buf);
  }
};

// GET /gallery/full?id=N , /gallery/thumb?id=N -> serve a slot's WebP (mmap byte-range).
// 404 unless the slot's .wdb word0 == "WDB1" (a deleted/torn slot must not serve stale pixels).
class GalleryWebpGet : public AsyncWebHandler {
 public:
  GalleryWebpGet(const char *url, size_t region, size_t rsize, uint32_t magic) : url_(url), region_(region), rsize_(rsize), magic_(magic) {}
  bool canHandle(AsyncWebServerRequest *request) const override {
    char b[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->method() == HTTP_GET && request->url_to(b) == url_;
  }
  bool isRequestHandlerTrivial() const override { return false; }
  void handleRequest(AsyncWebServerRequest *request) override {
    const esp_partition_t *p = gallery_part();
    auto *pid = request->getParam("id");
    int id = pid ? atoi(pid->value().c_str()) : -1;
    if (p != nullptr && id >= 0 && id < gallery_slot_cap(p) && gallery_slot_used(p, id)) {
      size_t off = gallery_slot_off(id) + region_;
      uint8_t hdr[8];
      if (esp_partition_read(p, off, hdr, 8) == ESP_OK) {
        uint32_t mg, len;
        memcpy(&mg, hdr, 4);
        memcpy(&len, hdr + 4, 4);
        if (mg == magic_ && len > 0 && len < (rsize_ - WEBP_DATA)) {
          esp_partition_mmap_handle_t h = 0;
          const void *ptr = nullptr;
          if (esp_partition_mmap(p, off + WEBP_DATA, len, ESP_PARTITION_MMAP_DATA, &ptr, &h) == ESP_OK) {
            request->send(request->beginResponse(200, "image/webp",
                          reinterpret_cast<const uint8_t *>(ptr), len));
            esp_partition_munmap(h);  // send is synchronous -> data already flushed, safe to unmap
            return;
          }
        }
      }
    }
    request->send(404, "text/plain", "no image");
  }
 protected:
  const char *url_;
  size_t region_;
  size_t rsize_;   // this region's byte span (webp len bound)
  uint32_t magic_;
};

// POST /gallery/dash (id-LESS, multipart body = dash cut-out .wdb) -> allocate next_free, erase its
// 4 regions, stream pixels into R_DASH but LEAVE word0 = 0xFFFFFFFF (uncommitted; the commit anchor).
// Returns {"id":N}, or 409 if full. This is the first + allocating step of a save.
class GalleryWdbPost : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *request) const override {
    char b[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->method() == HTTP_POST && request->url_to(b) == "/gallery/dash";
  }
  bool isRequestHandlerTrivial() const override { return false; }
  void handleUpload(AsyncWebServerRequest *request, const std::string &filename, size_t index,
                    uint8_t *data, size_t len, bool final) override {
    if (index == 0) {
      part_ = gallery_part(); slot_ = -1; wpos_ = 0; buflen_ = 0; total_ = 0;
      body_seen_ = true; full_ = false; err_ = "";
      if (part_ == nullptr) { ok_ = false; err_ = "no storage partition"; return; }
      slot_ = gallery_next_free(part_);
      if (slot_ < 0) { ok_ = false; full_ = true; err_ = "gallery full"; return; }
      if (!gallery_erase_slot(part_, slot_)) { ok_ = false; err_ = "erase failed"; return; }
      ok_ = true;
    }
    if (!ok_ || part_ == nullptr) return;
    total_ += len;
    size_t o = 0;
    while (o < len) {
      size_t nn = 512 - buflen_;
      if (nn > len - o) nn = len - o;
      memcpy(g_upbuf + buflen_, data + o, nn);
      buflen_ += nn; o += nn;
      if (buflen_ == 512) {
        if (wpos_ + 512 > R_DASH_SIZE) { ok_ = false; err_ = "too large"; return; }  // never spill past R_DASH into R_SAVER
        if (wpos_ == 0) { g_upbuf[0] = g_upbuf[1] = g_upbuf[2] = g_upbuf[3] = 0xFF; }  // hold word0 uncommitted
        if (esp_partition_write(part_, gallery_slot_off(slot_) + R_DASH + wpos_, g_upbuf, 512) != ESP_OK) {
          ok_ = false; err_ = "write failed"; return;
        }
        wpos_ += 512; buflen_ = 0;
      }
    }
  }
  void handleRequest(AsyncWebServerRequest *request) override {
    if (!body_seen_) { request->send(400, "text/plain", "no body received"); reset_(); return; }
    if (full_) { request->send(409, "application/json", "{\"error\":\"gallery_full\"}"); reset_(); return; }
    if (ok_ && part_ != nullptr && buflen_ > 0) {  // flush tail, zero-padded to 4-byte boundary
      size_t pad = (buflen_ + 3) & ~static_cast<size_t>(3);
      if (wpos_ + pad > R_DASH_SIZE) {              // never spill past R_DASH into R_SAVER (parity with the chunk guard)
        ok_ = false; err_ = "too large (tail)";
      } else {
        memset(g_upbuf + buflen_, 0, pad - buflen_);
        if (wpos_ == 0) { g_upbuf[0] = g_upbuf[1] = g_upbuf[2] = g_upbuf[3] = 0xFF; }
        if (esp_partition_write(part_, gallery_slot_off(slot_) + R_DASH + wpos_, g_upbuf, pad) != ESP_OK) {
          ok_ = false; err_ = "write failed (tail)";
        }
      }
    }
    if (ok_) {
      char b[32];
      snprintf(b, sizeof(b), "{\"id\":%d}", slot_);
      request->send(200, "application/json", b);
    } else {
      request->send(400, "text/plain", err_[0] ? err_ : "upload failed");
    }
    reset_();
  }
 protected:
  void reset_() { part_ = nullptr; slot_ = -1; wpos_ = 0; buflen_ = 0; total_ = 0;
                  ok_ = false; full_ = false; body_seen_ = false; err_ = ""; }
  const esp_partition_t *part_{nullptr};
  int slot_{-1};
  size_t wpos_{0}, buflen_{0}, total_{0};
  bool ok_{false}, full_{false}, body_seen_{false};
  const char *err_{""};
};

// POST /gallery/saver?id=N (multipart body = saver cut-out .wdb) -> stream raw into R_SAVER from
// offset 0 (its own "WDB1" magic written normally; NOT the commit anchor). Region already erased by
// the /gallery/dash alloc.
class GalleryRawPost : public AsyncWebHandler {
 public:
  GalleryRawPost(const char *url, size_t region, size_t rsize) : url_(url), region_(region), rsize_(rsize) {}
  bool canHandle(AsyncWebServerRequest *request) const override {
    char b[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->method() == HTTP_POST && request->url_to(b) == url_;
  }
  bool isRequestHandlerTrivial() const override { return false; }
  void handleUpload(AsyncWebServerRequest *request, const std::string &filename, size_t index,
                    uint8_t *data, size_t len, bool final) override {
    if (index == 0) {
      part_ = gallery_part(); wpos_ = 0; buflen_ = 0; body_seen_ = true; err_ = "";
      auto *pid = request->getParam("id");
      slot_ = pid ? atoi(pid->value().c_str()) : -1;
      if (part_ == nullptr) { ok_ = false; err_ = "no storage partition"; return; }
      if (slot_ < 0 || slot_ >= gallery_slot_cap(part_)) { ok_ = false; err_ = "bad id"; return; }
      ok_ = true;
    }
    if (!ok_ || part_ == nullptr) return;
    size_t o = 0;
    while (o < len) {
      size_t nn = 512 - buflen_;
      if (nn > len - o) nn = len - o;
      memcpy(g_upbuf + buflen_, data + o, nn);
      buflen_ += nn; o += nn;
      if (buflen_ == 512) {
        if (wpos_ + 512 > rsize_) { ok_ = false; err_ = "too large"; return; }
        if (esp_partition_write(part_, gallery_slot_off(slot_) + region_ + wpos_, g_upbuf, 512) != ESP_OK) {
          ok_ = false; err_ = "write failed"; return;
        }
        wpos_ += 512; buflen_ = 0;
      }
    }
  }
  void handleRequest(AsyncWebServerRequest *request) override {
    if (!body_seen_) { request->send(400, "text/plain", "no body received"); reset_(); return; }
    if (ok_ && part_ != nullptr && buflen_ > 0) {  // flush tail, zero-padded to a 4-byte boundary
      size_t pad = (buflen_ + 3) & ~static_cast<size_t>(3);
      if (wpos_ + pad > rsize_) {                  // tail-flush parity: never spill past this region
        ok_ = false; err_ = "too large (tail)";
      } else {
        memset(g_upbuf + buflen_, 0, pad - buflen_);
        if (esp_partition_write(part_, gallery_slot_off(slot_) + region_ + wpos_, g_upbuf, pad) != ESP_OK) {
          ok_ = false; err_ = "write failed (tail)";
        }
      }
    }
    if (ok_) request->send(200, "text/plain", "OK");
    else request->send(400, "text/plain", err_[0] ? err_ : "upload failed");
    reset_();
  }
 protected:
  void reset_() { part_ = nullptr; slot_ = -1; wpos_ = 0; buflen_ = 0; ok_ = false; body_seen_ = false; err_ = ""; }
  const char *url_;
  size_t region_;
  size_t rsize_;   // this region's byte span (overflow guard)
  const esp_partition_t *part_{nullptr};
  int slot_{-1};
  size_t wpos_{0}, buflen_{0};
  bool ok_{false}, body_seen_{false};
  const char *err_{""};
};

// POST /gallery/hero?id=N , /gallery/thumb?id=N (multipart body = WebP) -> stream at region+512,
// write a 512B [magic|len] header at region+0 on completion. Region was erased by /gallery/dash.
class GalleryWebpPost : public AsyncWebHandler {
 public:
  GalleryWebpPost(const char *url, size_t region, size_t rsize, uint32_t magic) : url_(url), region_(region), rsize_(rsize), magic_(magic) {}
  bool canHandle(AsyncWebServerRequest *request) const override {
    char b[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->method() == HTTP_POST && request->url_to(b) == url_;
  }
  bool isRequestHandlerTrivial() const override { return false; }
  void handleUpload(AsyncWebServerRequest *request, const std::string &filename, size_t index,
                    uint8_t *data, size_t len, bool final) override {
    if (index == 0) {
      part_ = gallery_part(); wpos_ = WEBP_DATA; buflen_ = 0; total_ = 0; body_seen_ = true; err_ = "";
      auto *pid = request->getParam("id");
      slot_ = pid ? atoi(pid->value().c_str()) : -1;
      if (part_ == nullptr) { ok_ = false; err_ = "no storage partition"; return; }
      if (slot_ < 0 || slot_ >= gallery_slot_cap(part_)) { ok_ = false; err_ = "bad id"; return; }
      ok_ = true;  // region already erased up-front by /gallery/wdb -> write directly
    }
    if (!ok_ || part_ == nullptr) return;
    total_ += len;
    size_t o = 0;
    while (o < len) {
      size_t nn = 512 - buflen_;
      if (nn > len - o) nn = len - o;
      memcpy(g_upbuf + buflen_, data + o, nn);
      buflen_ += nn; o += nn;
      if (buflen_ == 512) {
        if (wpos_ + 512 > rsize_) { ok_ = false; err_ = "too large"; return; }  // never spill into the next region
        if (esp_partition_write(part_, gallery_slot_off(slot_) + region_ + wpos_, g_upbuf, 512) != ESP_OK) {
          ok_ = false; err_ = "write failed"; return;
        }
        wpos_ += 512; buflen_ = 0;
      }
    }
  }
  void handleRequest(AsyncWebServerRequest *request) override {
    if (!body_seen_) { request->send(400, "text/plain", "no body received"); reset_(); return; }
    if (ok_ && part_ != nullptr && buflen_ > 0) {  // flush tail
      size_t pad = (buflen_ + 3) & ~static_cast<size_t>(3);
      if (wpos_ + pad > rsize_) {                  // tail-flush parity: never spill past this region
        ok_ = false; err_ = "too large (tail)";
      } else {
        memset(g_upbuf + buflen_, 0, pad - buflen_);
        if (esp_partition_write(part_, gallery_slot_off(slot_) + region_ + wpos_, g_upbuf, pad) != ESP_OK) {
          ok_ = false; err_ = "write failed (tail)";
        }
      }
    }
    if (ok_ && part_ != nullptr) {  // write the 512B magic+len header (reuse the shared buffer)
      memset(g_upbuf, 0, WEBP_DATA);
      memcpy(g_upbuf, &magic_, 4);
      uint32_t l = static_cast<uint32_t>(total_);
      memcpy(g_upbuf + 4, &l, 4);
      if (esp_partition_write(part_, gallery_slot_off(slot_) + region_, g_upbuf, WEBP_DATA) != ESP_OK) {
        ok_ = false; err_ = "hdr write failed";
      }
    }
    if (ok_) request->send(200, "text/plain", "OK");
    else request->send(400, "text/plain", err_[0] ? err_ : "upload failed");
    reset_();
  }
 protected:
  void reset_() { part_ = nullptr; slot_ = -1; wpos_ = 0; buflen_ = 0; total_ = 0; ok_ = false; body_seen_ = false; err_ = ""; }
  const char *url_;
  size_t region_;
  size_t rsize_;   // this region's byte span (overflow guard)
  uint32_t magic_;
  const esp_partition_t *part_{nullptr};
  int slot_{-1};
  size_t wpos_{0}, buflen_{0}, total_{0};
  bool ok_{false}, body_seen_{false};
  const char *err_{""};
};

// POST /gallery/commit?id=N -> verify .wdb (sane header) + full + thumb all present, then program
// word0 0xFFFFFFFF -> "WDB1" (all 1->0, valid on NOR, no erase). The atomic commit + LAST write.
class GalleryCommit : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *request) const override {
    char b[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->method() == HTTP_POST && request->url_to(b) == "/gallery/commit";
  }
  bool isRequestHandlerTrivial() const override { return false; }
  void handleRequest(AsyncWebServerRequest *request) override {
    const esp_partition_t *p = gallery_part();
    auto *pid = request->getParam("id");
    int id = pid ? atoi(pid->value().c_str()) : -1;
    if (p == nullptr || id < 0 || id >= gallery_slot_cap(p)) { request->send(400, "text/plain", "bad id"); return; }
    size_t base = gallery_slot_off(id);
    // dash cut-out: sane header (version>=2, plausible dims) — word0 is still the 0xFF placeholder
    uint8_t wh[10];
    bool dash_ok = esp_partition_read(p, base + R_DASH, wh, 10) == ESP_OK && wh[4] >= 2;
    if (dash_ok) {
      uint16_t w = wh[6] | (wh[7] << 8), h = wh[8] | (wh[9] << 8);
      dash_ok = w > 0 && h > 0 && w <= WDB_DASH_W && h <= WDB_DASH_H;  // configured box (region-fit is static_assert'd)
    }
    // saver cut-out: its own "WDB1" magic + sane version (written normally, it's not the anchor)
    uint8_t sh[10];
    bool saver_ok = esp_partition_read(p, base + R_SAVER, sh, 10) == ESP_OK &&
                    memcmp(sh, "WDB1", 4) == 0 && sh[4] >= 2;
    if (saver_ok) {
      uint16_t sw = sh[6] | (sh[7] << 8), svh = sh[8] | (sh[9] << 8);
      saver_ok = sw > 0 && svh > 0 && sw <= WDB_SAVER_W && svh <= WDB_SAVER_H;  // was unchecked for size
    }
    uint8_t fh[8], th[8];
    uint32_t fmg = 0, flen = 0, tmg = 0, tlen = 0;
    if (esp_partition_read(p, base + R_HERO, fh, 8) == ESP_OK) { memcpy(&fmg, fh, 4); memcpy(&flen, fh + 4, 4); }
    if (esp_partition_read(p, base + R_THUMB, th, 8) == ESP_OK) { memcpy(&tmg, th, 4); memcpy(&tlen, th + 4, 4); }
    bool hero_ok = fmg == MAGIC_FULL && flen > 0;
    bool thumb_ok = tmg == MAGIC_THUMB && tlen > 0;
    if (dash_ok && saver_ok && hero_ok && thumb_ok) {
      uint8_t m[4] = {'W', 'D', 'B', '1'};
      if (esp_partition_write(p, base + R_DASH, m, 4) == ESP_OK) {
        gallery_pending = id;   // F6: auto-select the just-committed slot HERE (server-side). The
                                // client still POSTs /gallery/select, but that can be dropped/raced
                                // during the post-save WiFi hiccup; setting it in commit (right after
                                // the WDB1 write, same handler) makes the device switch reliably.
        char b[32];
        snprintf(b, sizeof(b), "{\"id\":%d}", id);
        request->send(200, "application/json", b);
        return;
      }
    }
    request->send(409, "application/json", "{\"error\":\"incomplete\"}");
  }
};

// POST /gallery/select?id=N , /gallery/delete?id=N , /gallery/favorite?id=N&fav=0|1 (body-less).
// Sets the volatile intent; a main-loop consumer applies it: the board-agnostic data part (bnr_sel /
// banner_preset / bnr_fav / flash erase / publish gallery_sel|fav) runs in core.yaml's 500ms housekeeping
// (so headless works too); the LVGL render + mmap lifecycle run in display_ttgo's draw lambda.
class GalleryCmd : public AsyncWebHandler {
 public:
  enum Kind { SELECT, DELETE, FAVORITE };
  GalleryCmd(const char *url, Kind kind) : url_(url), kind_(kind) {}
  bool canHandle(AsyncWebServerRequest *request) const override {
    char b[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->method() == HTTP_POST && request->url_to(b) == url_;
  }
  bool isRequestHandlerTrivial() const override { return false; }
  void handleRequest(AsyncWebServerRequest *request) override {
    auto *pid = request->getParam("id");
    int id = pid ? atoi(pid->value().c_str()) : -1;
    if (id < 0 || id >= gallery_slot_cap()) { request->send(400, "text/plain", "bad id"); return; }
    if (kind_ == FAVORITE) {
      auto *pf = request->getParam("fav");
      int fav = pf ? atoi(pf->value().c_str()) : 0;
      gallery_fav_pending = id | (fav ? 0x100 : 0);
      request->send(200, "text/plain", "OK");
      return;
    }
    if (!gallery_slot_used(gallery_part(), id)) { request->send(404, "text/plain", "no such slot"); return; }
    gallery_pending = (kind_ == DELETE) ? (0x100 | id) : id;
    request->send(200, "text/plain", "OK");
  }
 protected:
  const char *url_;
  Kind kind_;
};

// Bindkey write-only (opsec): the victron_bindkey entity is internal: so its value never streams over
// SSE/API. The web writes it here instead of /text/.../set. httpd validates (empty = clear, or exactly
// 32 hex) + parks it in a fixed buffer + sets a flag; the 250ms main loop applies it via the entity's
// make_call (reusing the entity's proven restore_value NVS). Reader never gets the value back — only a
// "keyset" bool in /wd/info.json.
inline std::atomic<bool> bindkey_pending{false};   // release/acquire so the buf writes are visible cross-task
inline char bindkey_buf[33] = {0};                 // up to 32 hex + NUL; empty clears the key

class BindkeyPost : public AsyncWebHandler {
 public:
  bool canHandle(AsyncWebServerRequest *request) const override {
    char b[AsyncWebServerRequest::URL_BUF_SIZE];
    return request->method() == HTTP_POST && request->url_to(b) == url_;
  }
  bool isRequestHandlerTrivial() const override { return false; }
  void handleRequest(AsyncWebServerRequest *request) override {
    auto *p = request->getParam("value");
    std::string v = p ? std::string(p->value().c_str()) : std::string();
    bool ok = v.empty();
    if (v.size() == 32) { ok = true; for (char c : v) if (!isxdigit((unsigned char) c)) { ok = false; break; } }
    if (!ok) { request->send(400, "text/plain", "bad key"); return; }
    memset(bindkey_buf, 0, sizeof(bindkey_buf));
    memcpy(bindkey_buf, v.c_str(), v.size());   // v.size() <= 32
    bindkey_pending.store(true, std::memory_order_release);   // publish the buffer writes before the flag
    request->send(200, "text/plain", "OK");
  }
 protected:
  const char *url_ = "/wd/bindkey";
};

// Registered from on_boot at a priority above web_server's, so our handlers win.
inline void register_dashboard() {
  auto *ws = web_server_base::global_web_server_base;
  if (ws == nullptr) return;
  ws->add_handler(new StaticPage("/", reinterpret_cast<const uint8_t *>(DASHBOARD_HTML), DASHBOARD_HTML_SIZE, true));  // NOLINT gzip
  ws->add_handler(new StaticPage("/convert", reinterpret_cast<const uint8_t *>(CONVERT_HTML), CONVERT_HTML_SIZE, true));  // NOLINT gzip
  ws->add_handler(new BannerBakedGet());  // NOLINT (/img/<name> — baked presets, de-inlined from the page)
  // On-demand JSON endpoints (off SSE): event log, BLE scan list.
  ws->add_handler(new JsonSlotGet("/wd/log.json", &slot_log, "application/json"));    // NOLINT
  ws->add_handler(new JsonSlotGet("/wd/scan.json", &slot_scan, "application/json"));  // NOLINT
  ws->add_handler(new JsonSlotGet("/wd/info.json", &slot_info, "application/json"));   // NOLINT
  ws->add_handler(new JsonSlotGet("/wd/live.json", &slot_live, "application/json"));   // NOLINT (fast-live heartbeat)
  ws->add_handler(new BindkeyPost());   // NOLINT (Phase 3: write-only bindkey; value never streams)
  // Legacy GET-only handlers kept for the web hero's fallback path (slot 0 == the original banner).
  ws->add_handler(new BannerWebpGet("/banner-full", FULL_OFF, R_HERO_SIZE, MAGIC_FULL));   // NOLINT
  ws->add_handler(new BannerWebpGet("/banner-thumb", THUMB_OFF, R_THUMB_SIZE, MAGIC_THUMB));  // NOLINT
  ws->add_handler(new BannerGet());  // NOLINT
  // --- gallery (custom-cutout 4-asset slots) --- (each handler carries its region's OFFSET + SIZE; see the
  // semi-uniform geometry block up top — the size is the per-region overflow/len bound, no longer a global REGION)
  ws->add_handler(new GalleryList());  // NOLINT
  ws->add_handler(new GalleryWebpGet("/gallery/hero", R_HERO, R_HERO_SIZE, MAGIC_FULL));    // NOLINT
  ws->add_handler(new GalleryWebpGet("/gallery/thumb", R_THUMB, R_THUMB_SIZE, MAGIC_THUMB));  // NOLINT
  ws->add_handler(new GalleryWdbPost());  // NOLINT (/gallery/dash — id-less alloc anchor; dash region is R_DASH_SIZE)
  ws->add_handler(new GalleryRawPost("/gallery/saver", R_SAVER, R_SAVER_SIZE));  // NOLINT
  ws->add_handler(new GalleryWebpPost("/gallery/hero", R_HERO, R_HERO_SIZE, MAGIC_FULL));    // NOLINT
  ws->add_handler(new GalleryWebpPost("/gallery/thumb", R_THUMB, R_THUMB_SIZE, MAGIC_THUMB));  // NOLINT
  ws->add_handler(new GalleryCommit());  // NOLINT
  ws->add_handler(new GalleryCmd("/gallery/select", GalleryCmd::SELECT));    // NOLINT
  ws->add_handler(new GalleryCmd("/gallery/delete", GalleryCmd::DELETE));    // NOLINT
  ws->add_handler(new GalleryCmd("/gallery/favorite", GalleryCmd::FAVORITE));  // NOLINT
}

}  // namespace web_dashboard
}  // namespace esphome
