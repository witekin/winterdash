#!/usr/bin/env python3
"""Generic CI hygiene scan for the PUBLIC repo — defense-in-depth, NOT the primary control.

Scans the source tree + any built firmware.factory.bin passed as an arg for GENERIC leak tells
that could slip into an official artifact: a developer machine's user-home path, or an embedded
private key. Exit 2 on any hit.

  python .github/scripts/ci_scan.py dist/esp32-wroom32-0.1.factory.bin

Design notes:
- Deliberately GENERIC — it holds NO author-specific identity string (spelling one here would
  publish the very thing we protect). The authoritative author-identity scrub is the dev-side
  export gate (tools/publish/gate.py), which runs BEFORE the public snapshot; CI builds from that
  already-scrubbed source on a clean Linux runner, so little author-specific data can even reach a
  bin. This is a cheap extra net, not the wall.
- Does NOT scan for the OTA password: official builds bake a PUBLIC documented default (it collides
  with branding). The real control is that the gitignored dev secrets.yaml never ships.
"""
import re, sys, pathlib

PATTERNS = [
    ("home-path",   re.compile(rb"[A-Za-z]:\\Users\\[^\\\s\"'<>]+")),        # a Windows user-home build path
    ("private-key", re.compile(rb"-----BEGIN [A-Z ]*PRIVATE KEY-----")),
]
SKIP_DIRS = {".git", ".esphome", ".venv", "node_modules", "dist"}
TEXT_SUFFIX = {".py", ".yaml", ".yml", ".html", ".js", ".json", ".md", ".csv",
               ".txt", ".h", ".cpp", ".c", ".example", ".svg", ".cfg", ""}


def scan(data, where):
    return [(kind, where) for kind, rx in PATTERNS if rx.search(data)]


def main(argv):
    hits = []
    for p in pathlib.Path(".").rglob("*"):
        if any(part in SKIP_DIRS for part in p.parts):
            continue
        if not p.is_file() or p.suffix.lower() not in TEXT_SUFFIX:
            continue
        try:
            hits += scan(p.read_bytes(), str(p))
        except OSError:
            pass
    for arg in argv:                                    # the built bin(s)
        b = pathlib.Path(arg)
        if b.is_file():
            hits += scan(b.read_bytes(), arg)

    if hits:
        print("ci_scan: REFUSE — generic leak tell(s) found:")
        for kind, where in hits:
            print(f"  {kind:12} {where}")
        return 2
    print("ci_scan: PASS (no generic leak tells in source or artifact)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
