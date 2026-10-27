#!/usr/bin/env python3
"""Adds a resolved selection of books to the lock and the catalogue, as one new block.

A selection is a JSON list of books, each with what the shelf shows (id, title, subtitle,
category, author, first_published, and a licence, licence_url and credit when it is not
public domain), how it is read (importer: epub or sefaria) and where it comes from
(source_id, work, upstream, source_licence, and files: each a name and a URL). Books
that share a source_id share one entry in the lock (a Sefaria translation read as
several volumes).

Every file is downloaded to <sources>/<source id>/<name>, unless it is there already, and
pinned in sources.lock.json by its size and SHA-256 (a book whose files cannot all be
fetched is reported and left out); every book joins catalogue.json in
the next block (one more than the highest), shelved as add_batch.py shelves (the
curated shelves in the order the selection gives, the others by author). A book id
already catalogued is an error.

    add_selection.py <selection.json> [--sources DIR] [--retrieved YYYY-MM-DD] [--block N]
"""

import argparse
import datetime
import hashlib
import json
import os
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, HERE)
from add_batch import shelf_key  # noqa: E402

SHELF_FIELDS = ("id", "title", "subtitle", "category", "importer", "source")
OPTIONAL = ("author", "first_published", "translator", "licence", "licence_url", "credit")


def sha256_of(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for chunk in iter(lambda: file.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url: str, path: str) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": "Pocket Library sources"})
    partial = path + ".partial"
    for attempt in range(4):
        try:
            with urllib.request.urlopen(request, timeout=600) as response, open(partial, "wb") as file:
                for chunk in iter(lambda: response.read(1 << 20), b""):
                    file.write(chunk)
            os.replace(partial, path)
            return
        except OSError:
            if attempt == 3:
                raise
            time.sleep(10 * (attempt + 1))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("selection")
    parser.add_argument("--sources", default=os.path.join(ROOT, ".build", "library", "sources"))
    parser.add_argument("--retrieved", default=datetime.date.today().isoformat())
    parser.add_argument("--block", type=int, help="the block to add to (default: a new one), for a batch added in parts")
    arguments = parser.parse_args()
    with open(arguments.selection, encoding="utf-8") as file:
        selection = json.load(file)
    lock_path = os.path.join(HERE, "sources.lock.json")
    catalogue_path = os.path.join(HERE, "catalogue.json")
    with open(lock_path, encoding="utf-8") as file:
        lock = json.load(file)
    with open(catalogue_path, encoding="utf-8") as file:
        catalogue = json.load(file)
    known = {entry["id"] for entry in catalogue["books"]}
    sources = {source["id"]: source for source in lock["sources"]}
    block = arguments.block or max(entry["block"] for entry in catalogue["books"]) + 1
    added = 0
    failed = []
    for book in selection:
        if book["id"] in known:
            raise ValueError("%s is already catalogued" % book["id"])
        known.add(book["id"])
        folder = os.path.join(arguments.sources, book["source_id"])
        os.makedirs(folder, exist_ok=True)
        pinned = []
        try:
            for wanted in book["files"]:
                path = os.path.join(folder, wanted["name"])
                if not os.path.exists(path):
                    print("fetching %s/%s" % (book["source_id"], wanted["name"]), flush=True)
                    download(wanted["url"], path)
                    time.sleep(1)
        except OSError as error:
            print("FAILED %s: %s" % (book["id"], error), flush=True)
            failed.append(book["id"])
            continue
        for wanted in book["files"]:
            path = os.path.join(folder, wanted["name"])
            pinned.append({"name": wanted["name"], "url": wanted["url"], "retrieved": arguments.retrieved,
                           "size": os.path.getsize(path), "sha256": sha256_of(path)})
        source = sources.get(book["source_id"])
        if source is None:
            source = {"id": book["source_id"], "work": book["work"], "upstream": book["upstream"],
                      "licence": book["source_licence"], "status": "cleared", "files": []}
            sources[source["id"]] = source
            lock["sources"].append(source)
        names = {entry["name"] for entry in source["files"]}
        source["files"] += [entry for entry in pinned if entry["name"] not in names]
        entry = {"id": book["id"], "title": book["title"], "subtitle": book["subtitle"],
                 "category": book["category"], "importer": book["importer"], "source": book["source_id"]}
        if book["importer"] == "sefaria" or len(book["files"]) > 1:
            entry["files"] = [wanted["name"] for wanted in book["files"]]
        else:
            entry["file"] = book["files"][0]["name"]
        for key in OPTIONAL:
            if book.get(key) is not None:
                entry[key] = book[key]
        entry["block"] = block
        catalogue["books"].append(entry)
        added += 1
    indexed = list(enumerate(catalogue["books"]))
    indexed.sort(key=lambda pair: shelf_key(pair[1], pair[0]))
    catalogue["books"] = [entry for _, entry in indexed]
    with open(lock_path, "w", encoding="utf-8", newline="\n") as file:
        json.dump(lock, file, indent=1, ensure_ascii=False)
        file.write("\n")
    with open(catalogue_path, "w", encoding="utf-8", newline="\n") as file:
        json.dump(catalogue, file, indent=1, ensure_ascii=False)
        file.write("\n")
    print("%d books added to block %d; the catalogue holds %d" % (added, block, len(catalogue["books"])))
    if failed:
        print("left out, their files not fetched: " + ", ".join(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
