#!/usr/bin/env python3
"""Large game assets kept out of Git and baked into every build from a pinned download.

A game whose asset is too large to keep in history (the Pocket Library's books, rebuilt
whole for every change) pins it instead: games/<id>/pinned_assets.json names each file
by its path under the game's assets/ folder, the URL it is published at (a release
download of this repository, which GitHub stores and serves without metering), its size
and its SHA-256. The file itself is ignored by Git.

prepare_portable_assets.py calls fetch() before it gathers the assets, so CI, the M4 and
a local build all get the same bytes, and the suite ships them inside its packages: the
suite itself downloads nothing. A file already in place and matching its pin is left
alone; one that differs is fetched again; a download that does not match is refused.

    pinned_assets.py [--verify]

--verify reads no network: exit status 1 when any pinned file is missing or differs.
"""

import argparse
import hashlib
import json
import os
import sys
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def pins(root: Path = ROOT) -> list:
    """Every pinned file: (the game's id, its pin, the path it belongs at)."""
    found = []
    for manifest in sorted((root / "books").glob("*/pinned_assets.json")):
        game = manifest.parent
        for pin in json.loads(manifest.read_text(encoding="utf-8"))["files"]:
            relative = Path(pin["path"])
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError("%s: a pinned path must stay inside assets/: %s" % (manifest, pin["path"]))
            found.append((game.name, pin, game / "assets" / relative))
    return found


def matches(path: Path, pin: dict) -> bool:
    return path.is_file() and path.stat().st_size == pin["size"] and sha256_of(path) == pin["sha256"]


# A shipped asset must be readable by whoever runs the installed suite (packages install
# as root): a download lands in a private temporary file, so its mode is set explicitly.
READABLE = 0o644


def fetch(root: Path = ROOT, quiet: bool = False) -> None:
    for game, pin, path in pins(root):
        if matches(path, pin):
            if path.stat().st_mode & 0o777 != READABLE:
                path.chmod(READABLE)
            continue
        if not quiet:
            print("%s: fetching %s (%.1f MB)" % (game, pin["path"], pin["size"] / 1048576), flush=True)
        path.parent.mkdir(parents=True, exist_ok=True)
        handle, temporary = tempfile.mkstemp(dir=path.parent, prefix=path.name + ".")
        try:
            with os.fdopen(handle, "wb") as out, urllib.request.urlopen(pin["url"], timeout=300) as response:
                while True:
                    chunk = response.read(1 << 20)
                    if not chunk:
                        break
                    out.write(chunk)
            if not matches(Path(temporary), pin):
                raise ValueError("%s: the download of %s does not match its pin" % (game, pin["url"]))
            os.chmod(temporary, READABLE)
            os.replace(temporary, path)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--verify", action="store_true")
    arguments = parser.parse_args()
    if arguments.verify:
        wrong = [str(path.relative_to(ROOT)) for _, pin, path in pins() if not matches(path, pin)]
        for name in wrong:
            print("missing or different: " + name)
        return 1 if wrong else 0
    fetch()
    return 0


if __name__ == "__main__":
    sys.exit(main())
