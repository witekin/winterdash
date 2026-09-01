#!/usr/bin/env python3
"""Download each board's NEWEST firmware.factory.bin from GitHub Releases into a dest dir.

Lets GitHub Pages serve the bins same-origin (site/firmware/), so the flasher's versions.json
urls need no CORS. Runs in CI (GH_TOKEN + GITHUB_REPOSITORY). Validated at release #1.

  python .github/scripts/pull_latest_bins.py --dest site/firmware
"""
import argparse, json, os, re, sys, urllib.request

API = "https://api.github.com"
# <board-id>-<version>.factory.bin — splits at the LAST dash; version must be dash-free (scheme "0.1").
ASSET_RE = re.compile(r"^(?P<id>.+)-(?P<ver>[^-]+)\.factory\.bin$")


def get(url, raw=False):
    req = urllib.request.Request(url)
    tok = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if tok:
        req.add_header("Authorization", f"Bearer {tok}")
    req.add_header("Accept", "application/octet-stream" if raw else "application/vnd.github+json")
    with urllib.request.urlopen(req) as r:
        return r.read() if raw else json.load(r)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dest", default="site/firmware")
    a = ap.parse_args()
    repo = os.environ["GITHUB_REPOSITORY"]
    os.makedirs(a.dest, exist_ok=True)

    seen = set()
    # releases newest-first -> the first asset seen per board id is its latest.
    for rel in get(f"{API}/repos/{repo}/releases?per_page=100"):
        if rel.get("draft"):
            continue
        for asset in rel.get("assets", []):
            m = ASSET_RE.match(asset["name"])
            if not m or m.group("id") in seen:
                continue
            seen.add(m.group("id"))
            data = get(asset["url"], raw=True)             # asset API url + octet-stream = the bytes
            with open(os.path.join(a.dest, asset["name"]), "wb") as f:
                f.write(data)
            print(f"pulled {asset['name']} ({len(data)} B)")
    if not seen:
        print("no release bins found (first release not published yet?)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
