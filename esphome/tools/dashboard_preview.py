#!/usr/bin/env python3
"""
dashboard_preview.py — LIVE-DATA, MULTI-CLIENT preview proxy for iterating web/dashboard.html without reflashing.

WHY
    The dashboard (web/dashboard.html) is served gzipped from device flash and pulls all its data from same-origin
    endpoints (/wd/*, /events SSE, /gallery, /img/*). Iterating on the LOOK by reflashing every change is slow. This
    proxy serves web/dashboard.html from disk (edit + refresh, no reflash) and feeds it REAL LIVE DATA from a running
    device.

    MULTI-CLIENT: several browsers (you + the assistant, several tabs) can open the preview at once WITHOUT multiplying
    the connections to the ESP. The device has a tiny socket budget (~5) and panics under concurrent load; so the proxy
    "takes over" the fan-out:
      - ONE upstream /events (SSE) is held and BROADCAST to every connected client. N clients -> 1 device SSE.
      - Identical GET polls (/wd/live.json, /wd/info.json, /gallery, /img/*, …) are CACHED for a short TTL, so many
        clients polling collapse into ~1 upstream fetch per TTL window.
      - POSTs (writes: /wd/cfg, /wd/bindkey, …) pass straight through to the device.
    So the device sees roughly a single viewer's worth of load no matter how many preview tabs are open.

HOW (request routing)
    GET /            -> web/dashboard.html from disk (re-read each request; edits show on refresh). /convert -> image-tool.html.
    GET /events      -> subscribe to the shared broadcaster (see above). One upstream connection, fanned out.
    GET everything   -> served from the short-TTL cache, or fetched once and cached.
    POST *           -> forwarded to the device, not cached.
    No CORS / mixed-content: the browser only talks to http://localhost (same origin as the page); the device hop is
    server-side. Response headers (incl. Content-Encoding: gzip) pass through unchanged.

RUN
    .venv/Scripts/python esphome/tools/dashboard_preview.py --device winterdash-85e9cc.local --port 8770
    then open http://localhost:8770/ (in-app Browser pane and/or your own browser — as many as you like).
    --device is DHCP/mDNS: pass <name>.local or the current IP (device Network screen). Note: from a build host whose
    resolver lacks mDNS (e.g. Git-Bash curl), pass the IP instead. --ttl sets the poll cache seconds (default 1.5).

MAINTAIN
    - Pure stdlib (http.server + urllib + threading + queue), no deps; Python 3.8+.
    - New device endpoints need NO change (anything not in LOCAL_FILES is proxied). To serve a new path LOCALLY, add it
      to LOCAL_FILES.
    - SSE fan-out / state replay: the device sends a full entity DUMP once per upstream connect. The proxy PARSES it,
      caches the latest `event: state` per entity id, and REPLAYS the whole cache to every NEW subscriber — so a client
      that joins late (a reload) still gets the complete current state on connect, not just future deltas. The cache is
      deliberately NOT cleared on an upstream reconnect: static config (text-victron_mac, the select-* entities) is
      emitted only in the dump and rarely again, so clearing it would blank a freshly-loaded client (a paired charger
      reads "not set") until the next full dump. It only ever converges. (One caveat: an entity never yet captured
      since proxy start is absent until its first dump lands.)
    - Sensitive spots: `_fetch()` header copy, the `cached_get` TTL, and `Sse._run()`'s stream loop — all deliberately
      dumb passthroughs. See docs/internal/dashboard-preview-proxy.md.
"""
import argparse
import os
import queue
import re
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib import request as urlrequest
from urllib.error import HTTPError, URLError

HERE = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.normpath(os.path.join(HERE, "..", "..", "web"))
LOCAL_FILES = {
    "/": os.path.join(WEB, "dashboard.html"),
    "/index.html": os.path.join(WEB, "dashboard.html"),
    "/convert": os.path.join(WEB, "image-tool.html"),
}
HOP_BY_HOP = {"connection", "keep-alive", "transfer-encoding", "te", "trailer", "upgrade",
              "proxy-authorization", "proxy-authenticate"}

DEVICE = ""        # set from the required --device in main() (no default board — this is a multi-board tool)
CACHE_TTL = 1.5                             # set by --ttl


# ---- one-shot fetch to the device ----------------------------------------------------
def _fetch(path, method="GET", body=None, headers=None):
    req = urlrequest.Request(DEVICE + path, data=body, method=method)
    if headers:
        for k, v in headers:
            req.add_header(k, v)
    try:
        r = urlrequest.urlopen(req, timeout=8)   # fail fast on a route blip -> clean 502, not a 30s hang
        code = r.status if hasattr(r, "status") else r.code
        hd = [(k, v) for k, v in r.headers.items() if k.lower() not in HOP_BY_HOP]
        return code, hd, r.read()
    except HTTPError as e:                                  # forward device 4xx/5xx verbatim
        hd = [(k, v) for k, v in e.headers.items() if k.lower() not in HOP_BY_HOP]
        return e.code, hd, e.read()
    except (URLError, OSError) as e:                         # device unreachable (mDNS blip / VPN drop / timeout):
        msg = ("device unreachable: %s" % e).encode()       # return a clean 502 so the handler thread survives —
        return 502, [("Content-Type", "text/plain"),        # a naked raise here would kill the thread ("Remote end
                     ("Content-Length", str(len(msg)))], msg  # closed connection") and blank the whole preview.


# ---- poll cache: collapse N clients' identical GETs into ~1 upstream fetch / TTL ------
# TTL is per-entry, chosen from the device's own Cache-Control: fast-changing JSON keeps the short poll TTL, but
# STATIC ASSETS the device marks immutable / long-max-age (versioned hero `?v=<crc>`, baked `/img/*`) are held far
# longer so a browser HARD-reload (Ctrl+Shift+R / DevTools "Disable cache" — which bypass the *browser* cache and so
# re-hit the proxy) still costs the ESP only ONE fetch per content version, not a full re-pull every reload.
_cache = {}                     # path -> (ts, ttl, code, headers, body)
_cache_lock = threading.Lock()
_ASSET_TTL = 86400              # in-proxy hold for immutable / long-lived assets; content-addressed URLs make it safe


def _entry_ttl(code, headers):
    if code != 200:
        return CACHE_TTL
    cc = ""
    for k, v in headers:
        if k.lower() == "cache-control":
            cc = v.lower()
            break
    if "no-store" in cc or "no-cache" in cc:
        return CACHE_TTL
    if "immutable" in cc:
        return _ASSET_TTL
    m = re.search(r"max-age=(\d+)", cc)
    if m and int(m.group(1)) >= 3600:           # long-lived static asset (baked /img/* = 7d)
        return _ASSET_TTL
    return CACHE_TTL


def cached_get(path):
    now = time.time()
    with _cache_lock:
        e = _cache.get(path)
        if e and now - e[0] < e[1]:
            return e[2], e[3], e[4]
    code, hd, body = _fetch(path)               # fetch outside the lock (don't serialize clients)
    with _cache_lock:
        _cache[path] = (now, _entry_ttl(code, hd), code, hd, body)
    return code, hd, body


# ---- SSE broadcaster: ONE upstream /events fanned out to every client -----------------
# The device sends a full entity DUMP once, on connect. With a single shared upstream, only the FIRST subscriber's
# connect carries it — so we PARSE the stream into events (delimited by a blank line), cache the latest `event: state`
# per entity `id`, and REPLAY that cache to every new subscriber. Result: every client gets the full current state on
# connect + live deltas, from one upstream connection.
_ID_RE = re.compile(rb'"id":"([^"]+)"')


class Sse:
    def __init__(self):
        self.subs = set()                       # set[queue.Queue]
        self.lock = threading.Lock()
        self.thread = None
        self.state = {}                         # entity id -> latest raw event block (bytes), replayed to new clients

    def subscribe(self):
        q = queue.Queue(maxsize=2048)
        with self.lock:
            for ev in self.state.values():      # replay the current entity states = the on-connect dump
                try:
                    q.put_nowait(ev)
                except queue.Full:
                    pass
            self.subs.add(q)
            if self.thread is None or not self.thread.is_alive():
                self.thread = threading.Thread(target=self._run, daemon=True)
                self.thread.start()
        return q

    def unsubscribe(self, q):
        with self.lock:
            self.subs.discard(q)

    def _run(self):
        while True:
            with self.lock:
                if not self.subs:
                    self.thread = None
                    return
            try:
                r = urlrequest.urlopen(DEVICE + "/events", timeout=90)
                buf = b""
                while True:
                    chunk = r.read(256)                     # streams; returns as data arrives
                    if not chunk:
                        break
                    with self.lock:                        # 1) broadcast the RAW chunk live (always flows, delimiter-agnostic)
                        if not self.subs:
                            r.close()
                            self.thread = None
                            return
                        for q in list(self.subs):
                            try:
                                q.put_nowait(chunk)
                            except queue.Full:               # a slow/dead client — drop, don't stall the rest
                                pass
                    # 2) parse (CRLF-normalized) for the state cache -> replayed to future clients on connect
                    buf += chunk.replace(b"\r\n", b"\n")
                    while b"\n\n" in buf:
                        raw, buf = buf.split(b"\n\n", 1)
                        if b"event: state" in raw:
                            m = _ID_RE.search(raw)
                            if m:
                                with self.lock:
                                    self.state[m.group(1)] = raw + b"\n\n"
            except Exception:                               # upstream dropped: KEEP the last-known state — do NOT clear.
                time.sleep(1)                               # Static config (text-victron_mac, the select-* entities)
                #                                             is emitted only in the on-connect dump and rarely again;
                #                                             clearing here would blank a freshly-loaded client's initial
                #                                             replay (e.g. paired charger reads "not set") until the next
                #                                             full dump lands. The reconnect's dump overwrites each entity
                #                                             as it returns, so the cache only ever converges — never
                #                                             regresses. Worst case a value is briefly stale, not missing.


SSE = Sse()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        sys.stderr.write("  %s %s\n" % (self.command, self.path))

    def _write_head(self, code, headers):
        self.send_response(code)
        for k, v in headers:
            self.send_header(k, v)
        self.end_headers()

    def _serve_local(self, path):
        try:
            with open(LOCAL_FILES[path], "rb") as f:
                body = f.read()
        except OSError as e:
            self.send_error(500, "cannot read %s: %s" % (LOCAL_FILES[path], e))
            return
        self._write_head(200, [("Content-Type", "text/html; charset=utf-8"),
                               ("Content-Length", str(len(body))), ("Cache-Control", "no-store")])
        self._safe_write(body)

    def _serve_sse(self):
        q = SSE.subscribe()
        self._write_head(200, [("Content-Type", "text/event-stream"),
                               ("Cache-Control", "no-store"), ("Connection", "keep-alive")])
        try:
            while True:
                chunk = q.get()                             # blocks until the broadcaster pushes
                self.wfile.write(chunk)
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass                                            # client closed the tab / reconnected
        finally:
            SSE.unsubscribe(q)

    def _safe_write(self, body):
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, OSError):
            pass

    def do_GET(self):
        p = self.path
        if p in LOCAL_FILES:
            self._serve_local(p)
        elif p == "/events" or p.startswith("/events?"):
            self._serve_sse()
        else:
            code, hd, body = cached_get(p)
            self._write_head(code, hd)
            self._safe_write(body)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0) or 0)
        body = self.rfile.read(length) if length else None
        hd = [(k, v) for k, v in self.headers.items()
              if k.lower() not in HOP_BY_HOP and k.lower() != "host"]
        code, rhd, rbody = _fetch(self.path, "POST", body, hd)
        self._write_head(code, rhd)
        self._safe_write(rbody)


def main():
    global DEVICE, CACHE_TTL
    ap = argparse.ArgumentParser(description="Live-data, multi-client preview proxy for web/dashboard.html")
    ap.add_argument("--device", required=True,
                    help="device host — mDNS name (winterdash-<mac>.local) or IP (off the device Network screen). "
                         "Required: name the board explicitly (this is a multi-board tool — no default board).")
    ap.add_argument("--port", type=int, default=8770, help="local port (default 8770)")
    ap.add_argument("--ttl", type=float, default=1.5, help="poll-cache seconds (default 1.5)")
    args = ap.parse_args()
    DEVICE = "http://" + args.device.rstrip("/")
    CACHE_TTL = args.ttl

    srv = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    srv.daemon_threads = True
    print("dashboard preview (multi-client): http://localhost:%d/  ->  %s" % (args.port, DEVICE))
    print("one upstream SSE fanned out; polls cached %.1fs. Edit web/dashboard.html + refresh. Ctrl+C to stop." % CACHE_TTL)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped.")


if __name__ == "__main__":
    main()
