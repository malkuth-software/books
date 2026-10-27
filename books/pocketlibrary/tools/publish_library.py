#!/usr/bin/env python3
"""Publishes the library's parts and pins them, so Git keeps a few hundred bytes for each.

The parts (make_library.cpp: base-<hash>.plb and block-<n>-<hash>.plb) are tens of
megabytes and never change once made: a new batch of books is a new block, and a fixed
book a new copy of its block. Kept in Git, every one would stay in the history for
good. Each is published instead as a download of this repository's `library-data`
release, named by its content, and pinned in ../pinned_assets.json; every build
fetches and checks the pinned parts (tools/pinned_assets.py, called by
tools/prepare_portable_assets.py) and ships them inside the application's packages.

    publish_library.py <parts dir> [<part> ...]

The parts named (file names in the parts dir), or every part there, replace the pins of
the same kind: a base replaces the base, block-<n> replaces block <n>. The base comes
first in assets/books/parts.txt, then the blocks by number; a part already published is
not uploaded again. A new base (a new generation) needs every block made with it.

Needs the GitHub CLI signed in with permission to edit releases. The release is created
the first time, never marked latest, and must not be removed with old app releases.
"""

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MODULE = HERE.parent
RELEASE = "library-data"
PART = re.compile(r"(base|block-([1-9][0-9]*))-[0-9a-f]{16}\.plb\Z")


def gh(*arguments: str, check: bool = True) -> subprocess.CompletedProcess:
    return subprocess.run(["gh", *arguments], cwd=MODULE, capture_output=True, text=True, check=check)


def order(name: str) -> tuple:
    match = PART.match(name)
    return (0, 0) if match.group(1) == "base" else (1, int(match.group(2)))


def kind(name: str) -> str:
    return PART.match(name).group(1)


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    folder = Path(sys.argv[1]).resolve()
    names = sys.argv[2:] or sorted(path.name for path in folder.glob("*.plb"))
    for name in names:
        if not PART.match(name):
            raise ValueError("not a part's name: " + name)
    manifest = MODULE / "pinned_assets.json"
    pins = json.loads(manifest.read_text(encoding="utf-8"))["files"] if manifest.is_file() else []
    by_kind = {kind(Path(pin["path"]).name): pin for pin in pins}
    repository = gh("repo", "view", "--json", "nameWithOwner", "-q", ".nameWithOwner").stdout.strip()
    if gh("release", "view", RELEASE, check=False).returncode != 0:
        gh("release", "create", RELEASE, "--title", "Pocket Library data", "--latest=false",
           "--notes", "The Pocket Library's books: a base and blocks of books, one file per part, "
           "named by its content. Builds fetch the parts books/pocketlibrary/pinned_assets.json names "
           "and ship them inside the application. Not an app release: keep it when old releases are removed.")
    published = {asset["name"] for asset in json.loads(gh("release", "view", RELEASE, "--json", "assets").stdout)["assets"]}
    for name in names:
        data = (folder / name).read_bytes()
        if name not in published:
            gh("release", "upload", RELEASE, str(folder / name))
        by_kind[kind(name)] = {"path": "books/" + name,
                               "url": "https://github.com/%s/releases/download/%s/%s" % (repository, RELEASE, name),
                               "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
        print("pinned %s (%d bytes)" % (name, len(data)))
    files = sorted(by_kind.values(), key=lambda pin: order(Path(pin["path"]).name))
    manifest.write_text(json.dumps({"files": files}, indent=1) + "\n", encoding="utf-8")
    parts = MODULE / "assets" / "books" / "parts.txt"
    parts.write_text("".join(Path(pin["path"]).name + "\n" for pin in files), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
