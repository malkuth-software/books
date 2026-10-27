#!/usr/bin/env python3
"""Adds a finder's batch of books to the lock and the catalogue, in shelf order.

The finder (a worker that looked up editions) writes <batch>.json: per book its id,
title, author, category, first publication, upstream URL, size and SHA-256. The checker
writes <batch>-check.json: a verdict per book (pass, warn or fail). Every book found and
not failed is added:

  - its file, taken from <candidates>/<id>/, checked against the finder's size and
    SHA-256, is placed where the importer reads pinned sources (<sources>/<source id>/);
  - sources.lock.json gains its pin (a book public only de facto says so in its status);
  - catalogue.json gains its entry, read by the general EPUB importer.

The catalogue is then put in shelf order: Scripture, Rabbinic writings, Church Fathers
and Reference as they stand, then Philosophy, History, Society, Science, Fiction, Science
fiction, Fantasy, Drama, Poetry and Children's; within a category by the author's
surname, then first publication, then title. A category's share-alike books (a licence
naming -SA) stand together at its end. Ids already in the catalogue are kept as they are.

A book under a Creative Commons licence carries it into the catalogue (`licence`,
`licence_url`, `credit`) for its About card; public-domain and CC0 books need none.

The batch's books share a new block (`block`, one more than the highest in the
catalogue): `make_library block` codes them against the frozen base into one new part,
and nothing already published is coded again.

    add_batch.py <batch.json> <check.json> <candidates dir> [--sources DIR]
"""

import argparse
import hashlib
import json
import os
import shutil
import sys
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))

CATEGORIES = {"fiction": "Fiction", "science-fiction": "Science fiction", "fantasy": "Fantasy",
              "drama": "Drama", "poetry": "Poetry", "children": "Children's", "scripture": "Scripture",
              "rabbinic": "Rabbinic writings", "church-fathers": "Church Fathers", "reference": "Reference",
              "philosophy": "Philosophy", "history": "History", "society": "Society", "science": "Science"}
SHELF_ORDER = ["Scripture", "Rabbinic writings", "Church Fathers", "Reference", "Philosophy", "History", "Society",
               "Science", "Fiction", "Science fiction", "Fantasy", "Drama", "Poetry", "Children's"]
# Shelved as they stand in the catalogue, in the order they were added, not by author.
CURATED = ("Scripture", "Rabbinic writings", "Church Fathers", "Reference")
SUFFIXES = {"jr.", "jr", "sr.", "sr", "ii", "iii"}


def surname(author: str) -> str:
    """The word an author is shelved under: the last of the name, without notes in brackets,
    a suffix or an accent ("Karel Čapek" under C, "Lucian of Samosata" under L, "Jacob and
    Wilhelm Grimm" under G). Empty for an anonymous work, which is shelved by its title."""
    name = spine_title(author).split(";")[0].split(",")[0].strip()
    if " of " in name:
        name = name.split(" of ")[0]
    words = [word for word in name.split() if word.lower() not in SUFFIXES]
    if not words or words[-1].lower() in ("anonymous", "various", "unknown"):
        return ""
    folded = unicodedata.normalize("NFKD", words[-1])
    return "".join(c for c in folded if not unicodedata.combining(c)).lower()


def spine_title(title: str) -> str:
    """The finder's title without its notes in brackets: "Father Goriot (Le Père Goriot)"."""
    while "(" in title and ")" in title[title.index("("):]:
        start = title.index("(")
        end = title.index(")", start)
        title = (title[:start].rstrip() + " " + title[end + 1:].lstrip()).strip()
    return title


def sort_title(title: str) -> str:
    words = title.split()
    if len(words) > 1 and words[0].lower() in ("the", "a", "an"):
        words = words[1:]
    return " ".join(words).lower()


def share_alike(entry: dict) -> bool:
    return "-SA" in entry.get("licence", "")


def shelf_key(entry: dict, position: int) -> tuple:
    category = entry.get("category", "")
    rank = SHELF_ORDER.index(category) if category in SHELF_ORDER else len(SHELF_ORDER)
    if category in CURATED:
        return (rank, share_alike(entry), position)
    author = surname(entry.get("author", ""))
    return (rank, share_alike(entry), author or sort_title(entry["title"]), entry.get("first_published", 0),
            sort_title(entry["title"]))


def sha256_of(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as file:
        for chunk in iter(lambda: file.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("batch")
    parser.add_argument("check")
    parser.add_argument("candidates")
    parser.add_argument("--sources", default=os.path.join(ROOT, ".build", "library", "sources"))
    arguments = parser.parse_args()
    with open(arguments.batch, encoding="utf-8") as file:
        batch = json.load(file)
    with open(arguments.check, encoding="utf-8") as file:
        verdicts = {check["id"]: check["verdict"] for check in json.load(file)["checks"]}
    lock_path = os.path.join(HERE, "sources.lock.json")
    catalogue_path = os.path.join(HERE, "catalogue.json")
    with open(lock_path, encoding="utf-8") as file:
        lock = json.load(file)
    with open(catalogue_path, encoding="utf-8") as file:
        catalogue = json.load(file)
    known_ids = {entry["id"] for entry in catalogue["books"]}
    known_sources = {source["id"] for source in lock["sources"]}
    block = max(entry["block"] for entry in catalogue["books"]) + 1
    added = skipped = 0
    for found in batch["entries"]:
        book_id = found["id"]
        if book_id in known_ids:
            # Already catalogued: only what sorting needs is taken from the finder.
            for entry in catalogue["books"]:
                if entry["id"] == book_id:
                    entry.setdefault("author", found["author"])
                    entry.setdefault("first_published", found["first_published"])
            skipped += 1
            continue
        if found["status"] != "found" or verdicts.get(book_id) == "fail":
            skipped += 1
            continue
        folder = os.path.join(arguments.candidates, book_id)
        names = sorted(os.listdir(folder))
        if len(names) != 1:
            raise ValueError("%s: expected one downloaded file, found %s" % (book_id, names))
        name = names[0]
        path = os.path.join(folder, name)
        if os.path.getsize(path) != found["size"] or sha256_of(path) != found["sha256"]:
            raise ValueError("%s: %s does not match the finder's pin" % (book_id, name))
        source_id = "batch%s-%s" % (batch["batch"], book_id)
        os.makedirs(os.path.join(arguments.sources, source_id), exist_ok=True)
        shutil.copyfile(path, os.path.join(arguments.sources, source_id, name))
        de_facto = "DE FACTO" in (found.get("public_domain_basis") or "") + (found.get("notes") or "")
        work = "%s, %s (%s); %s" % (found["author"], found["title"], found["first_published"], found["edition"])
        if source_id not in known_sources:
            lock["sources"].append({
                "id": source_id,
                "work": work,
                "upstream": found.get("alternative_url") or found["url"],
                "licence": "US public domain: " + found["public_domain_basis"],
                "status": "de facto" if de_facto else "cleared",
                "files": [{"name": name, "url": found["url"], "retrieved": "2026-10-15",
                           "size": found["size"], "sha256": found["sha256"]}],
            })
        entry = {"id": book_id, "title": spine_title(found["title"]), "subtitle": found["author"],
                 "category": CATEGORIES[found["category"]], "importer": "epub", "source": source_id, "file": name,
                 "author": found["author"], "first_published": found["first_published"], "block": block}
        if found.get("translator"):
            entry["translator"] = found["translator"]
        if (found.get("licence") or "").startswith("CC BY"):
            for key in ("licence", "licence_url", "credit"):
                if found.get(key):
                    entry[key] = found[key]
        catalogue["books"].append(entry)
        added += 1
    # Books already catalogued take their author from their subtitle, for sorting.
    for entry in catalogue["books"]:
        if "author" not in entry and entry.get("importer") != "own":
            entry["author"] = entry.get("subtitle", "")
    indexed = list(enumerate(catalogue["books"]))
    indexed.sort(key=lambda pair: shelf_key(pair[1], pair[0]))
    catalogue["books"] = [entry for _, entry in indexed]
    with open(lock_path, "w", encoding="utf-8", newline="\n") as file:
        json.dump(lock, file, indent=1, ensure_ascii=False)
        file.write("\n")
    with open(catalogue_path, "w", encoding="utf-8", newline="\n") as file:
        json.dump(catalogue, file, indent=1, ensure_ascii=False)
        file.write("\n")
    print("%d books added to block %d, %d skipped; the catalogue holds %d"
          % (added, block, skipped, len(catalogue["books"])))
    return 0


if __name__ == "__main__":
    sys.exit(main())
