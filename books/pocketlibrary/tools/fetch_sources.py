#!/usr/bin/env python3
"""Downloads the Pocket Library's pinned sources and checks every byte.

The lock (sources.lock.json, beside this file) names each upstream file with its URL,
size and SHA-256. Files go to <out>/<source id>/<name>; one already there is checked,
not fetched again. A file whose hash differs is an error: the build must notice when
upstream changes (bereanbible.com and gutenberg.org serve mutable files at fixed URLs).

    fetch_sources.py [--out DIR] [--verify]

--out defaults to .build/library/sources under the repository. --verify reads no
network. Exit status 0 when every file agrees with the lock, 1 otherwise.
"""

import argparse
import hashlib
import json
import os
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))


def sha256_of(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        while True:
            chunk = file.read(1 << 20)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def load_lock() -> dict:
    with open(os.path.join(HERE, "sources.lock.json"), encoding="utf-8") as file:
        return json.load(file)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", default=os.path.join(ROOT, ".build", "library", "sources"))
    parser.add_argument("--verify", action="store_true")
    arguments = parser.parse_args()
    failures = 0
    for source in load_lock()["sources"]:
        folder = os.path.join(arguments.out, source["id"])
        os.makedirs(folder, exist_ok=True)
        for entry in source["files"]:
            path = os.path.join(folder, entry["name"])
            if not os.path.exists(path):
                if arguments.verify:
                    print("absent   %s/%s" % (source["id"], entry["name"]))
                    failures += 1
                    continue
                print("fetching %s/%s" % (source["id"], entry["name"]))
                partial = path + ".partial"
                request = urllib.request.Request(entry["url"], headers={"User-Agent": "PlaySuite library sources"})
                with urllib.request.urlopen(request, timeout=120) as response, open(partial, "wb") as file:
                    while True:
                        chunk = response.read(1 << 20)
                        if not chunk:
                            break
                        file.write(chunk)
                os.replace(partial, path)
            if os.path.getsize(path) != entry["size"] or sha256_of(path) != entry["sha256"]:
                print("MISMATCH %s/%s (the upstream file has changed; see the lock's note)" % (source["id"], entry["name"]))
                failures += 1
    print("%s" % ("all sources agree with the lock" if failures == 0 else "%d file(s) do not" % failures))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
