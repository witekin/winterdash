#pragma once
// Static A8 glyph draw buffer for LVGL font rasterization (CYD 8-bit-heap crash mitigation).
//
// WHY: to draw a character/icon glyph, LVGL rasterizes it into a temporary A8 buffer via
// font_draw_buf_handlers.buf_malloc_cb (lv_draw_label.c:646 -> lv_draw_buf_create_ex). By default that is
// lv_malloc from the SHARED, fragmentable 8-bit heap. Size = box_w * round_up(box_h,32): mdi_status ~768B,
// m_big up to ~2.5KB. On a fragmented heap that alloc FAILS; LVGL returns NULL (no abort) and the font
// renderer derefs NULL (bitmap_out = draw_buf->data) -> loopTask fault -> task-WDT -> reboot. This is the
// "Failed to allocate 768 bytes for draw buffer" crater (see docs/internal/cyd-web-load-plan.md) -- it is the
// STATUS-ICON GLYPH, not the hero image (the RGB565A8 hero takes LVGL's malloc-free fast path).
//
// FIX: re-point the FONT draw-buf handlers to hand out ONE reserved static buffer, so glyph rasterization
// never touches the fragmented heap -> the failed-malloc NULL-deref can't happen. GLOBAL: every text/icon on
// every screen, so it also removes a per-glyph heap churner (a determinism/fragmentation win, not just the
// saver). Safe as a SINGLE buffer because this build renders synchronously with NO threads (LV_OS_NONE=y) ->
// the one SW draw unit dispatches glyph draws serially -> at most one glyph buffer is live at a time. Mirrors the exact
// technique ESPHome itself uses (lvgl_esphome.cpp re-points these same handlers); public API, no LVGL patch,
// survives managed-component regen. Oversize glyphs fall back to the heap (harmless, rare -- and none of the
// CYD fonts hit it). RESIDUAL (not closable via this API): create_ex still mallocs a ~40B lv_draw_buf_t
// descriptor from the shared heap, so this is a strong mitigation (~20x smaller alloc), not a full cure.
//
// Install from an on_boot lambda BEFORE the first paint (display_cyd.yaml splash lv_refr_now); it re-overrides
// ESPHome's own setup-time font-handler override, which runs earlier.
#include <lvgl.h>
// The public <lvgl.h> only FORWARD-declares lv_draw_buf_handlers_t (lv_types.h) — the getter returns a pointer
// but the struct fields (buf_malloc_cb/buf_free_cb) are in a private header. Pull the same private header ESPHome
// itself uses to set these very fields (lvgl_esphome.cpp: `LV_GLOBAL_DEFAULT()->font_draw_buf_handlers.*`), which
// completes the type (lv_global_t embeds lv_draw_buf_handlers_t by value). As durable as ESPHome's own LVGL glue.
#include <core/lv_global.h>
#include "esp_heap_caps.h"

// MEASURED worst real CYD glyph = 1312 B (m_big 'W' from the splash "WinterDash", drawn every boot; confirmed
// across all screens + saver via the WD_MEM_LOG maxglyph probe, 2026-09-07). 1536 = 1312 + ~17% margin. Anything
// larger -> heap fallback (harmless; never happens on this board — m_big size 36 is the largest font and its
// glyph box_h<=32 -> round_up(box_h,32)=32, so ~40x32, not the 64-tall the pre-measurement guess assumed).
#ifndef WD_FONT_BUF_SZ
#define WD_FONT_BUF_SZ 1536
#endif

namespace wd_font_buf {

alignas(LV_DRAW_BUF_ALIGN) inline uint8_t s_buf[WD_FONT_BUF_SZ];

#ifdef WD_MEM_LOG
// DEV: the largest glyph-buffer size ever requested (read from the wdmem lambda). Used ONCE to size s_buf (=1312
// measured); kept behind the flag so a release build carries neither the counter nor its per-glyph update.
inline size_t s_max_glyph = 0;
#endif

// Signature per lv_draw_buf.h: void *(*lv_draw_buf_malloc_cb_t)(size_t, lv_color_format_t).
inline void *malloc_cb(size_t size, lv_color_format_t cf) {
  (void) cf;
#ifdef WD_MEM_LOG
  if (size > s_max_glyph)
    s_max_glyph = size;   // DEV: track worst-case for exact buffer sizing (logged by the wdmem lambda)
#endif
  if (size <= sizeof(s_buf))
    return s_buf;   // the reserved buffer (LV_DRAW_BUF_ALIGN-aligned via alignas; create_ex aligns the returned ptr -> data==unaligned)
  // Oversize fallback: identical shape to ESPHome's own lv_alloc_draw_buf (heap_caps_aligned_alloc, 8-bit).
  return heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, LV_ROUND_UP(size, LV_DRAW_BUF_ALIGN), MALLOC_CAP_8BIT);
}

inline void free_cb(void *p) {
  if (p != s_buf)
    heap_caps_free(p);   // no-op for the static buffer; only frees a fallback alloc
}

// Re-point the global FONT draw-buf handlers. Call once, before the first glyph paint.
inline void install() {
  lv_draw_buf_handlers_t *h = lv_draw_buf_get_font_handlers();
  h->buf_malloc_cb = malloc_cb;
  h->buf_free_cb = free_cb;
}

}  // namespace wd_font_buf
