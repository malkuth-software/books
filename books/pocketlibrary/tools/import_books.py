#!/usr/bin/env python3
"""Reads the pinned sources and writes each book of the Pocket Library in one plain form.

Every source file is checked against sources.lock.json before it is read; a file whose
hash differs is refused. The text of an edition is never changed: what each importer
carries, what it drops and why, is written into the head of its output, and a marker it
does not know is an error, not a silent drop.

    import_books.py [--sources DIR] [--out DIR]

--sources defaults to .build/library/sources (tools/fetch_sources.py fills it); --out to
.build/library/normalized, one file per book, `<book>.tsv`.

The form, one record a line, fields separated by TAB, the first field the record's kind:

    H key value        the head: source, hash, licence, counts, what was dropped
    B id title short [abbreviation]
                       a division of the book opens (a book of the Bible, a
                       treatise, a dictionary); `short` is its running head, the
                       abbreviation (the SBL abbreviation for a book of the BSB) names it in references
    M level text       a title line of that division (level 1 the main title)
    C label            a chapter (or a folio side of the Talmud) opens
    K key              a dictionary entry opens
    S level text       a section heading (level 0 a major one, as Psalms' BOOK I)
    R text             the parallel passages under a heading
    P style            a paragraph or a line opens; the styles are listed in STYLES
    V label            a verse opens
    T text             running text, exactly as the source has it
    Y style 1|0        a span opens or closes: italic, small, wj (words of Christ),
                       hebrew, greek, bold
    N label            a footnote opens, called here; `label` is the source's reference
    U label            a verse number printed inside a footnote
    E                  the open footnote closes

Text never holds a TAB or a line break; a source line break inside a paragraph is one
space, and that is the only change made to any text.
"""

import argparse
import collections
import hashlib
import html
import io
import json
import os
import re
import sys
import typing
import unicodedata
import zipfile
import xml.etree.ElementTree as ElementTree

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))

STYLES = ("p", "flush", "centre", "q1", "q2", "right", "acrostic", "list1", "list2", "blank", "title",
          "verse", "note", "entry", "folio")


# Scripts the library's faces cannot set. Hebrew is given in Latin letters, in italic
# (lexicon_reader.hebrew_in_latin); the others (a translator's Japanese or Arabic
# characters beside his romanization) are left out, counted in the book's head.
HEBREW_RUN = re.compile(r"[\u0591-\u05f4\ufb1d-\ufb4f]+(?:[\s\u05be][\u0591-\u05f4\ufb1d-\ufb4f]+)*")
UNSET_RUN = re.compile(r"[\u0600-\u06ff\u0700-\u074f\u0750-\u077f\u3000-\u30ff\u3400-\u4dbf\u4e00-\u9fff"
                       r"\uf900-\ufaff\uff00-\uffef]+")
UNSET_NAMES = (("\u0600", "\u077f", "Arabic or Syriac"), ("\u3000", "\uffef", "Japanese or Chinese"))


class Book:
    """One book's records, with the head written first."""

    def __init__(self, name: str) -> None:
        self.name = name
        self.head = []
        self.records = []
        self.dropped = collections.Counter()
        self.head_counts = collections.Counter()
        self.italic = 0  # italic spans open, so a transliteration does not open another
        self.unset = False  # characters were left out: brackets they leave empty go too

    def add(self, kind: str, *fields: object) -> None:
        for field in fields:
            text = str(field)
            if "\t" in text or "\n" in text or "\r" in text:
                raise ValueError("%s: a field holds a TAB or line break: %r" % (self.name, text[:60]))
        self.records.append((kind,) + tuple(str(field) for field in fields))
        if kind == "Y" and fields and fields[0] == "italic":
            self.italic = max(0, self.italic + (1 if str(fields[1]) == "1" else -1))

    def text(self, text: str) -> None:
        if not text:
            return
        if UNSET_RUN.search(text):
            def unset(match: re.Match) -> str:
                for low, high, name in UNSET_NAMES:
                    if low <= match.group(0)[0] <= high:
                        self.dropped["%s characters (the faces have none; the translator's own words kept)" % name] += 1
                return ""
            text = UNSET_RUN.sub(unset, text)
            self.unset = True
        if HEBREW_RUN.search(text):
            import lexicon_reader
            at = 0
            for match in HEBREW_RUN.finditer(text):
                self.append_text(text[at:match.start()])
                words = [lexicon_reader.hebrew_in_latin(word) for word in re.split(r"[\s\u05be]+", match.group(0))]
                latin = " ".join(word for word in words if word)
                if latin:
                    opened = self.italic == 0
                    if opened:
                        self.add("Y", "italic", "1")
                    self.append_text(latin)
                    if opened:
                        self.add("Y", "italic", "0")
                    self.head_counts["Hebrew given in Latin letters (the faces have no Hebrew)"] += 1
                at = match.end()
            text = text[at:]
        self.append_text(text)

    def append_text(self, text: str) -> None:
        if text:
            if self.records and self.records[-1][0] == "T":
                self.records[-1] = ("T", self.records[-1][1] + text)
            else:
                self.add("T", text)
        if self.unset and self.records and self.records[-1][0] == "T":
            tidy = re.sub(r" ?[(\[]\s*[,;:]?\s*[)\]]", "", self.records[-1][1])
            tidy = re.sub(r"(?<=\S)  +", " ", tidy)
            if tidy != self.records[-1][1]:
                self.records[-1] = ("T", tidy)

    def write(self, folder: str) -> str:
        path = os.path.join(folder, self.name + ".tsv")
        with open(path, "w", encoding="utf-8", newline="\n") as file:
            for key, value in self.head:
                file.write("H\t%s\t%s\n" % (key, value))
            for change, count in sorted(self.head_counts.items()):
                file.write("H\tcorrected\t%s: %d\n" % (change, count))
            for marker, count in sorted(self.dropped.items()):
                file.write("H\tdropped\t%s %d\n" % (marker, count))
            for record in self.records:
                file.write("\t".join(record) + "\n")
        return path


# ---- Sources -----------------------------------------------------------------------

def load_lock() -> dict:
    with open(os.path.join(HERE, "sources.lock.json"), encoding="utf-8") as file:
        lock = json.load(file)
    sources = {}
    for source in lock["sources"]:
        sources[source["id"]] = source
    return sources


def read_pinned(sources: dict, folder: str, source_id: str, name: str) -> tuple:
    entry = None
    for candidate in sources[source_id]["files"]:
        if candidate["name"] == name:
            entry = candidate
    if entry is None:
        raise KeyError("%s/%s is not in the lock" % (source_id, name))
    with open(os.path.join(folder, source_id, name), "rb") as file:
        data = file.read()
    if len(data) != entry["size"] or hashlib.sha256(data).hexdigest() != entry["sha256"]:
        raise ValueError("%s/%s does not match its pin" % (source_id, name))
    return data, entry


def head_for(book: Book, source: dict, entries: list) -> None:
    book.head.append(("source", source["id"]))
    book.head.append(("work", source["work"]))
    book.head.append(("licence", source["licence"]))
    for entry in entries:
        book.head.append(("file", "%s %s" % (entry["name"], entry["sha256"])))


# ---- The Berean Standard Bible, from USFM ------------------------------------------

USFM_MARKER = re.compile(r"\\([a-z]+[0-9]*)(\*?)")
PARAGRAPH_STYLE = {"p": "p", "pmo": "flush", "pc": "centre", "q1": "q1", "q2": "q2", "qr": "right",
                   "qa": "acrostic", "li1": "list1", "li2": "list2", "b": "blank", "d": "title"}
LINE_MARKERS = ("id", "usfm", "h", "toc1", "toc2", "toc3", "mt1", "mt2", "s1", "s2", "ms", "mr", "r", "c")


def strip_refs(text: str, book: Book) -> str:
    """`\\ref display|target\\ref*` reads as its display; the machine target is dropped."""
    def display(match: re.Match) -> str:
        book.dropped["\\ref target"] += 1
        return match.group(1)
    return re.sub(r"\\ref (.*?)\|[^\\]*\\ref\*", display, text)


def space_before_verse(book: Book, state: dict) -> None:
    """The USFM sometimes joins a verse to the text before it with no space ("God.\\f
    ...\\f*\\wj \\v 15", where Berean's plain text has "God. \u201cThe"). Inside a
    paragraph, the running text before a verse ends in a space."""
    if state["note"]:
        return
    index = len(book.records) - 1
    while index >= 0:
        kind = book.records[index][0]
        if kind == "T":
            if not book.records[index][1].endswith(" "):
                book.records[index] = ("T", book.records[index][1] + " ")
                book.head_counts["a space put between a verse and the text before it"] += 1
            return
        if kind in ("P", "S", "R", "C", "B", "V", "M"):
            return
        if kind == "E":
            # the whole footnote is passed over: its text is not the verse's
            while index > 0 and book.records[index][0] != "N":
                index -= 1
        index -= 1


def usfm_inline(line: str, book: Book, state: dict) -> None:
    """The text of one paragraph line, from just after its paragraph marker."""
    position = 0
    while position < len(line):
        match = USFM_MARKER.search(line, position)
        if match is None:
            book.text(line[position:])
            break
        book.text(line[position:match.start()])
        marker, closing = match.group(1), match.group(2) == "*"
        position = match.end()
        if not closing and position < len(line) and line[position] == " ":
            position += 1
        if marker == "v":
            space_before_verse(book, state)
            number = re.match(r"(\S+) ?", line[position:])
            book.add("V", number.group(1))
            position += number.end()
        elif marker == "wj":
            book.add("Y", "wj", "0" if closing else "1")
        elif marker == "f" and not closing:
            caller = re.match(r"\+ ", line[position:])
            if caller is None:
                raise ValueError("a footnote without the + caller: %r" % line[position:position + 20])
            position += caller.end()
            origin = re.match(r"\\fr (\S+) ", line[position:])
            if origin is None:
                raise ValueError("a footnote without \\fr: %r" % line[position:position + 30])
            book.add("N", origin.group(1))
            position += origin.end()
            state["note"] = True
        elif marker == "f" and closing:
            if state["italic"]:
                book.add("Y", "italic", "0")
                state["italic"] = False
            book.add("E")
            state["note"] = False
        elif marker == "ft":
            if state["italic"]:
                book.add("Y", "italic", "0")
                state["italic"] = False
        elif marker == "fqa":
            if not state["italic"]:
                book.add("Y", "italic", "1")
                state["italic"] = True
        elif marker == "fv" and not closing:
            end = line.index("\\fv*", position)
            book.add("U", line[position:end])
            position = end + len("\\fv*")
        elif marker == "ref" and not closing:
            end = line.index("\\ref*", position)
            target = line[position:end]
            book.text(target.split("|", 1)[0])
            book.dropped["\\ref target"] += 1
            position = end + len("\\ref*")
        else:
            raise ValueError("an unknown USFM marker \\%s%s" % (marker, "*" if closing else ""))


# How references name the books (the SBL Handbook's abbreviations); the BSB's own \\toc3
# is inconsistent in its capitals ("Gen", "JER", "1SA").
ABBREVIATIONS = dict(zip(
    ("GEN EXO LEV NUM DEU JOS JDG RUT 1SA 2SA 1KI 2KI 1CH 2CH EZR NEH EST JOB PSA PRO ECC SNG ISA JER "
     "LAM EZK DAN HOS JOL AMO OBA JON MIC NAM HAB ZEP HAG ZEC MAL MAT MRK LUK JHN ACT ROM 1CO 2CO GAL "
     "EPH PHP COL 1TH 2TH 1TI 2TI TIT PHM HEB JAS 1PE 2PE 1JN 2JN 3JN JUD REV").split(),
    ["Gen", "Exod", "Lev", "Num", "Deut", "Josh", "Judg", "Ruth", "1 Sam", "2 Sam", "1 Kgs", "2 Kgs", "1 Chr",
     "2 Chr", "Ezra", "Neh", "Esth", "Job", "Ps", "Prov", "Eccl", "Song", "Isa", "Jer", "Lam", "Ezek", "Dan",
     "Hos", "Joel", "Amos", "Obad", "Jonah", "Mic", "Nah", "Hab", "Zeph", "Hag", "Zech", "Mal", "Matt", "Mark",
     "Luke", "John", "Acts", "Rom", "1 Cor", "2 Cor", "Gal", "Eph", "Phil", "Col", "1 Thess", "2 Thess",
     "1 Tim", "2 Tim", "Titus", "Phlm", "Heb", "Jas", "1 Pet", "2 Pet", "1 John", "2 John", "3 John", "Jude",
     "Rev"]))


def import_bsb(sources: dict, folder: str, out: str, make: bool = False) -> str:
    data, entry = read_pinned(sources, folder, "berean", "bsb_usfm.zip")
    book = Book("bsb")
    head_for(book, sources["berean"], [entry])
    archive = zipfile.ZipFile(io.BytesIO(data))
    order = bsb_book_order(sources, folder)
    names = {}
    for name in archive.namelist():
        if name.endswith(".usfm"):
            names[os.path.basename(name)[:-5]] = name
    counts = collections.Counter()
    for code in order:
        text = archive.read(names[code]).decode("utf-8")
        if unicodedata.normalize("NFC", text) != text:
            raise ValueError("%s is not in normal form C" % code)
        titles = {}
        state = {"note": False, "italic": False}
        opened = False
        for raw in text.split("\n"):
            line = raw.rstrip("\r").rstrip(" ")
            if not line:
                continue
            match = re.match(r"\\([a-z]+[0-9]*) ?(.*)$", line)
            if match is None:
                raise ValueError("%s: a line without a marker: %r" % (code, line[:40]))
            marker, rest = match.group(1), match.group(2)
            if marker in ("id", "usfm"):
                book.dropped["\\" + marker] += 1
            elif marker in ("h", "toc1", "toc2", "toc3"):
                titles[marker] = rest
            elif marker in ("mt2", "mt1"):
                if not opened:
                    book.add("B", code, titles["toc1"], titles["h"], ABBREVIATIONS[code])
                    book.dropped["\\toc3 (references use the SBL abbreviations)"] += 1
                    opened = True
                book.add("M", "2" if marker == "mt2" else "1", rest)
            elif marker == "c":
                book.add("C", rest)
                counts["chapters"] += 1
            elif marker in ("s1", "s2"):
                book.add("S", marker[1], rest)
            elif marker == "ms":
                book.add("S", "0", rest)
            elif marker == "r":
                book.add("R", strip_refs(rest, book))
            elif marker == "mr":
                book.add("P", "title")
                usfm_inline(rest, book, state)
            elif marker in PARAGRAPH_STYLE:
                book.add("P", PARAGRAPH_STYLE[marker])
                usfm_inline(rest, book, state)
            else:
                raise ValueError("%s: an unknown line marker \\%s" % (code, marker))
            if state["note"]:
                raise ValueError("%s: a footnote runs past its line" % code)
        counts["books"] += 1
    for record in book.records:
        if record[0] == "V":
            counts["verses"] += 1
        elif record[0] == "N":
            counts["footnotes"] += 1
    for key in ("books", "chapters", "verses", "footnotes"):
        book.head.append((key, counts[key]))
    close_dashes(book)
    if make:
        print("wrote %d corrections" % make_corrections(sources, folder, book, order))
    apply_corrections(book)
    return book.write(out)


CORRECTIONS = os.path.join(HERE, "bsb_corrections.tsv")


def verse_records(book: Book) -> typing.Iterator[tuple]:
    """Yields (reference, index) for every running-text record of a verse, notes left out."""
    code = chapter = verse = None
    note = False
    for index, record in enumerate(book.records):
        kind = record[0]
        if kind == "B":
            code, verse = record[1], None
        elif kind == "C":
            chapter, verse = record[1], None
        elif kind == "N":
            note = True
        elif kind == "E":
            note = False
        elif kind == "V" and not note:
            verse = record[1]
        elif kind == "P" and record[1] == "acrostic":
            verse = None
        elif kind == "T" and not note and verse is not None:
            yield "%s %s:%s" % (code, chapter, verse), index


def close_dashes(book: Book) -> None:
    """The USFM edition's own defects are made right by the plain-text edition (bsb.txt).
    First a rule: the USFM spaces the em dash on one side, both or neither, bsb.txt
    never; every space beside an em dash in running text is removed. Then
    apply_corrections applies each correction of bsb_corrections.tsv once, to the verse
    it names."""
    dashes = 0
    for index, record in enumerate(book.records):
        if record[0] == "T" and "\u2014" in record[1]:
            text = record[1]
            closed = re.sub(r" *\u2014 *", "\u2014", text)
            if closed != text:
                dashes += len(text) - len(closed)
                book.records[index] = ("T", closed)
    doubled = 0
    for index, record in enumerate(book.records):
        if record[0] == "T" and "  " in record[1]:
            single = re.sub(r"  +", " ", record[1])
            doubled += len(record[1]) - len(single)
            book.records[index] = ("T", single)
    book.head.append(("corrected", "spaces beside an em dash removed: %d" % dashes))
    book.head.append(("corrected", "doubled spaces made single: %d" % doubled))


def apply_corrections(book: Book) -> int:
    if not os.path.exists(CORRECTIONS):
        return 0
    wanted = collections.defaultdict(list)
    with open(CORRECTIONS, encoding="utf-8") as file:
        for line in file:
            if line.startswith("#") or not line.strip():
                continue
            reference, old, new = line.rstrip("\n").split("\t")
            wanted[reference].append([old, new, False])
    for reference, index in list(verse_records(book)):
        for correction in wanted.get(reference, ()):
            if not correction[2] and correction[0] in book.records[index][1]:
                book.records[index] = ("T", book.records[index][1].replace(correction[0], correction[1], 1))
                correction[2] = True
    applied = 0
    for reference in wanted:
        for old, new, done in wanted[reference]:
            if not done:
                raise ValueError("the correction of %s %r was not applied" % (reference, old))
            applied += 1
    book.head.append(("corrected", "from bsb_corrections.tsv: %d" % applied))
    return applied


def verse_texts(book: Book) -> dict:
    """For each verse, its running-text records in order and, for each, whether a
    paragraph or line break stands before it (a space in bsb.txt)."""
    verses = collections.OrderedDict()
    broken = False
    last = None
    code = chapter = verse = None
    note = False
    for index, record in enumerate(book.records):
        kind = record[0]
        if kind == "B":
            code, verse = record[1], None
        elif kind == "C":
            chapter, verse = record[1], None
        elif kind == "N":
            note = True
        elif kind == "E":
            note = False
        elif kind == "V" and not note:
            verse = record[1]
            broken = False
        elif kind == "P":
            if record[1] == "acrostic":
                verse = None
            broken = True
        elif kind == "T" and not note and verse is not None:
            reference = "%s %s:%s" % (code, chapter, verse)
            verses.setdefault(reference, []).append((index, broken))
            broken = False
    return verses


def make_corrections(sources: dict, folder: str, book: Book, codes: list) -> int:
    """Writes bsb_corrections.tsv: for each verse whose running text differs from
    bsb.txt, the smallest replacements inside one record, each with enough context to
    be found once."""
    import difflib
    data, entry = read_pinned(sources, folder, "berean", "bsb.txt")
    expected = {}
    names = []
    for line in data.decode("utf-8-sig").split("\r\n")[3:]:
        if line:
            reference, text = line.split("\t", 1)
            name, place = reference.rsplit(" ", 1)
            if not names or names[-1] != name:
                names.append(name)
            expected["%s %s" % (codes[len(names) - 1], place)] = text
    lines = ["# Corrections of the BSB's USFM edition (bsb_usfm.zip, \"Autogenerated BSB by",
             "# bsb2usfm\") to Berean's plain-text edition (bsb.txt): reference, the USFM's",
             "# text, the plain text's. Generated by import_books.py --make-corrections, then",
             "# read and kept. Each is applied once, inside the verse's running text."]
    unplaced = 0
    for reference, pieces in verse_texts(book).items():
        target = expected.get(reference, "")
        joined = ""
        owner = []
        for index, broken in pieces:
            if broken and joined:
                joined += " "
                owner.append(None)
            text = book.records[index][1]
            for offset in range(len(text)):
                owner.append((index, offset))
            joined += text
        joined_plain = joined.strip()
        lead = len(joined) - len(joined.lstrip())
        if joined_plain == target:
            continue
        edits = collections.OrderedDict()
        for op, i1, i2, j1, j2 in difflib.SequenceMatcher(None, joined_plain, target, autojunk=False).get_opcodes():
            if op == "equal":
                continue
            span = [owner[k + lead] for k in range(i1, i2)] if i2 > i1 else []
            here = owner[i1 + lead] if i1 + lead < len(owner) else None
            before = owner[i1 + lead - 1] if i1 + lead > 0 else None
            anchor = here if here else before
            if anchor is None or any(item is None or item[0] != anchor[0] for item in span):
                if joined_plain[i1:i2].strip() == "" and target[j1:j2].strip() == "":
                    continue
                unplaced += 1
                print("  %s: a difference across records: %r -> %r" % (reference, joined_plain[i1:i2], target[j1:j2]))
                continue
            start = span[0][1] if span else (anchor[1] if here else anchor[1] + 1)
            edits.setdefault(anchor[0], []).append((start, start + (i2 - i1), target[j1:j2]))
        for index, changes in edits.items():
            text = book.records[index][1]
            groups = []
            for change in changes:
                if groups and change[0] - groups[-1][-1][1] <= 12:
                    groups[-1].append(change)
                else:
                    groups.append([change])
            for group in groups:
                left, right = max(group[0][0] - 6, 0), min(group[-1][1] + 6, len(text))
                while text.count(text[left:right]) > 1 and (left > 0 or right < len(text)):
                    left, right = max(left - 4, 0), min(right + 4, len(text))
                new = text[left:group[0][0]]
                for number, (start, end, replacement) in enumerate(group):
                    new += replacement
                    new += text[end:group[number + 1][0]] if number + 1 < len(group) else text[end:right]
                lines.append("%s\t%s\t%s" % (reference, text[left:right], new))
    with open(CORRECTIONS, "w", encoding="utf-8", newline="\n") as file:
        file.write("\n".join(lines) + "\n")
    if unplaced:
        print("  %d differences could not be placed" % unplaced)
    return len(lines) - 4


def bsb_book_order(sources: dict, folder: str) -> list:
    """The BSB's own order, from its plain text edition (the first verse of each book)."""
    data, entry = read_pinned(sources, folder, "berean", "bsb.txt")
    codes = ("GEN EXO LEV NUM DEU JOS JDG RUT 1SA 2SA 1KI 2KI 1CH 2CH EZR NEH EST JOB PSA PRO ECC SNG ISA JER "
             "LAM EZK DAN HOS JOL AMO OBA JON MIC NAM HAB ZEP HAG ZEC MAL MAT MRK LUK JHN ACT ROM 1CO 2CO GAL "
             "EPH PHP COL 1TH 2TH 1TI 2TI TIT PHM HEB JAS 1PE 2PE 1JN 2JN 3JN JUD REV").split()
    books = []
    for line in data.decode("utf-8-sig").split("\r\n")[3:]:
        if line:
            name = line.split("\t", 1)[0].rsplit(" ", 1)[0]
            if not books or books[-1] != name:
                books.append(name)
    if len(books) != len(codes):
        raise ValueError("bsb.txt names %d books" % len(books))
    return codes


def bsb_verse_check(sources: dict, folder: str, normalized: str, codes: list) -> tuple:
    """The round trip: every verse's running text, notes left out and white space
    collapsed, equals the verse line of the BSB's own plain-text edition. The two
    editions space the em dash differently (the USFM sets it between spaces); that is
    the one difference allowed. Returns the counts and the verses the plain text leaves
    empty."""
    data, entry = read_pinned(sources, folder, "berean", "bsb.txt")
    expected = {}
    names = []
    for line in data.decode("utf-8-sig").split("\r\n")[3:]:
        if not line:
            continue
        reference, text = line.split("\t", 1)
        name, place = reference.rsplit(" ", 1)
        if not names or names[-1] != name:
            names.append(name)
        expected[(codes[len(names) - 1], place)] = text
    found = {}
    note = False
    code = chapter = current = None
    with open(normalized, encoding="utf-8") as file:
        for line in file:
            fields = line.rstrip("\n").split("\t")
            kind = fields[0]
            if kind == "N":
                note = True
            elif kind == "E":
                note = False
            elif kind == "B":
                code, current = fields[1], None
            elif kind == "C":
                chapter, current = fields[1], None
            elif kind == "V" and not note:
                current = []
                found[(code, "%s:%s" % (chapter, fields[1]))] = current
            elif kind == "T" and not note and current is not None:
                current.append(fields[1])
            elif kind == "P" and fields[1] == "acrostic":
                current = None
            elif kind == "P" and current is not None:
                current.append(" ")
    def plain(text: str) -> str:
        return " ".join(re.sub(r" *\u2014 *", "\u2014", text).split())
    differences = 0
    empty = []
    for key in expected:
        if key not in found:
            if expected[key].strip():
                differences += 1
                print("  %s %s is missing" % key)
            else:
                empty.append("%s %s" % key)
            continue
        if plain(expected[key]) != plain("".join(found[key])):
            differences += 1
            if differences <= 400:
                print("  %s %s differs:\n    bsb.txt  %r\n    imported %r" % (key[0], key[1], expected[key], "".join(found[key])))
    for key in found:
        if key not in expected:
            differences += 1
            print("  %s %s is not in bsb.txt" % key)
    return len(expected), len(found), differences, empty


# ---- JPS 1917, from Sefaria's export -----------------------------------------------

JPS_ORDER = ("Genesis Exodus Leviticus Numbers Deuteronomy Joshua Judges |I Samuel|II Samuel|I Kings|II Kings| "
             "Isaiah Jeremiah Ezekiel Hosea Joel Amos Obadiah Jonah Micah Nahum Habakkuk Zephaniah Haggai "
             "Zechariah Malachi Psalms Proverbs Job |Song of Songs| Ruth Lamentations Ecclesiastes Esther Daniel "
             "Ezra Nehemiah |I Chronicles|II Chronicles|")


def jps_titles() -> list:
    titles = []
    for part in re.split(r"\|", JPS_ORDER):
        part = part.strip()
        if not part:
            continue
        if " " in part and part.split(" ")[0] in ("I", "II", "Song"):
            titles.append(part)
        else:
            titles.extend(part.split())
    return titles


def sefaria_inline(text: str, book: Book) -> None:
    """Sefaria's text: `<small>` and `<br>` are the only markup met; anything else is an error.
    A line break in the text (one, in Lamentations 1:22) reads as one space."""
    if "\n" in text:
        book.dropped["a line break inside the text, read as a space"] += text.count("\n")
        text = text.replace("\n", " ")
    position = 0
    for match in re.finditer(r"<(/?)([a-z]+)\s*/?>", text):
        book.text(html.unescape(text[position:match.start()]))
        tag, closing = match.group(2), match.group(1) == "/"
        if tag == "small":
            book.add("Y", "small", "0" if closing else "1")
        elif tag == "br":
            book.add("P", "verse")
        else:
            raise ValueError("unknown markup <%s> in Sefaria text" % tag)
        position = match.end()
    book.text(html.unescape(text[position:]))


def import_jps(sources: dict, folder: str, out: str) -> str:
    book = Book("jps1917")
    entries = []
    verses = 0
    for title in jps_titles():
        name = "jps1917-" + title.replace(" ", "_") + ".json"
        data, entry = read_pinned(sources, folder, "jps1917", name)
        entries.append(entry)
        document = json.loads(data.decode("utf-8"))
        book.add("B", title, title, title)
        for chapter_index, chapter in enumerate(document["text"]):
            book.add("C", chapter_index + 1)
            for verse_index, verse in enumerate(chapter):
                book.add("P", "verse")
                book.add("V", verse_index + 1)
                sefaria_inline(verse, book)
                verses += 1
    head_for(book, sources["jps1917"], entries)
    book.head.append(("books", len(entries)))
    book.head.append(("verses", verses))
    book.dropped["Sefaria's version metadata"] = len(entries)
    return book.write(out)


def import_cohen(sources: dict, folder: str, out: str) -> str:
    data, entry = read_pinned(sources, folder, "cohen-berakot", "berakot-cohen-1921.json")
    book = Book("berakot")
    head_for(book, sources["cohen-berakot"], [entry])
    document = json.loads(data.decode("utf-8"))
    book.add("B", "Berakot", "Tractate Berakot", "Berakot")
    segments = 0
    for index, page in enumerate(document["text"]):
        if not any(segment.strip() for segment in page):
            continue
        book.add("C", "%d%s" % (index // 2 + 1, "ab"[index % 2]))
        for segment in page:
            if segment.strip():
                book.add("P", "folio")
                sefaria_inline(segment, book)
                segments += 1
    book.head.append(("segments", segments))
    return book.write(out)


# ---- Gorfinkle's Pirke Abot, from Project Gutenberg --------------------------------

def gutenberg_inline(text: str, book: Book) -> None:
    """Gutenberg's plain text marks italic with underscores; a run of three or more is a
    printed rule and stays as it is."""
    pieces = re.split(r"(_{3,})", text)
    italic = False
    for number, piece in enumerate(pieces):
        if number % 2 == 1:
            book.text(piece)
            continue
        parts = piece.split("_")
        for index, part in enumerate(parts):
            if index > 0:
                italic = not italic
                book.add("Y", "italic", "1" if italic else "0")
            book.text(part)
    if italic:
        raise ValueError("an unpaired underscore in %r" % text[:80])


def import_gorfinkle(sources: dict, folder: str, out: str) -> str:
    data, entry = read_pinned(sources, folder, "gorfinkle-avot", "pg8547.txt")
    book = Book("avot")
    head_for(book, sources["gorfinkle-avot"], [entry])
    text = data.decode("utf-8-sig").replace("\r\n", "\n")
    start = text.index("\n", text.index("*** START OF THE PROJECT GUTENBERG EBOOK")) + 1
    end = text.index("*** END OF THE PROJECT GUTENBERG EBOOK")
    book.dropped["Project Gutenberg's header and licence"] = 1
    corrected = 0
    with open(os.path.join(HERE, "avot_corrections.tsv"), encoding="utf-8") as file:
        for line in file:
            if line.startswith("#") or not line.strip():
                continue
            old, new = line.rstrip("\n").split("\t")
            if text.count(old) != 1:
                raise ValueError("the Avot correction %r matches %d times" % (old, text.count(old)))
            text = text.replace(old, new)
            corrected += 1
    book.head.append(("corrected", "from avot_corrections.tsv: %d" % corrected))
    book.add("B", "Avot", "The Sayings of the Jewish Fathers", "Pirke Abot")
    blocks = re.split(r"\n[ \t]*\n", text[start:end])
    paragraphs = 0
    for block in blocks:
        lines = block.split("\n")
        while lines and not lines[0].strip():
            lines.pop(0)
        if not lines:
            continue
        first = lines[0]
        joined = " ".join(line.strip() for line in lines)
        if re.fullmatch(r"CHAPTER [IVX]+", joined):
            book.add("C", joined.split(" ")[1])
            continue
        indent = len(first) - len(first.lstrip(" "))
        if indent >= 6 and re.match(r"\(\d+\)", first.strip()):
            style = "note"
        elif indent > 0 and all(len(line) - len(line.lstrip(" ")) > 0 for line in lines if line.strip()):
            style = "centre"
        else:
            style = "p"
        book.add("P", style)
        gutenberg_inline(joined, book)
        paragraphs += 1
    book.head.append(("paragraphs", paragraphs))
    return book.write(out)


# ---- Strong's dictionaries ---------------------------------------------------------

def element_text(element: ElementTree.Element, book: Book, language: str) -> None:
    """The running text of a Greek dictionary element, its references as numbers."""
    if element.text:
        book.text(" ".join(element.text.split("\n")))
    for child in element:
        if child.tag == "greek":
            # The library sets English only: a Greek word is given by its
            # transliteration (the word itself is listed as dropped).
            book.dropped["Greek words in Greek letters (each kept as its transliteration)"] += 1
            book.add("Y", "italic", "1")
            book.text(child.get("translit") or child.get("BETA"))
            book.add("Y", "italic", "0")
        elif child.tag == "strongsref":
            book.text(("G" if child.get("language") == "GREEK" else "H") + child.get("strongs").lstrip("0"))
        elif child.tag in ("strongs_derivation", "strongs_def", "kjv_def", "latin"):
            element_text(child, book, language)
        elif child.tag == "see":
            book.dropped["<see> (repeats the entry's references)"] += 1
        elif child.tag == "strongs":
            pass
        elif child.tag == "pronunciation":
            book.text(child.get("strongs"))
        else:
            raise ValueError("an unknown element <%s> in the Greek dictionary" % child.tag)
        if child.tail:
            book.text(" ".join(child.tail.split("\n")))


def import_strongs_greek(sources: dict, folder: str, out: str) -> str:
    data, entry = read_pinned(sources, folder, "strongs", "strongsgreek.xml")
    book = Book("strongs-greek")
    head_for(book, sources["strongs"], [entry])
    root = ElementTree.fromstring(data)
    book.add("B", "Greek", "A Concise Dictionary of the Words in the Greek Testament", "Greek Dictionary")
    count = 0
    for item in root.iter("entry"):
        number = item.findtext("strongs").strip()
        book.add("K", "G" + number)
        book.add("P", "entry")
        book.add("Y", "bold", "1")
        book.text(number + ".")
        book.add("Y", "bold", "0")
        headword = item.find("greek")
        sounded = item.find("pronunciation")
        for child in item:
            if child.tag == "strongs":
                pass
            elif child is headword:
                book.dropped["Greek words in Greek letters (each kept as its transliteration)"] += 1
                book.text(" ")
                book.add("Y", "italic", "1")
                book.text(child.get("translit"))
                book.add("Y", "italic", "0")
            elif child is sounded:
                book.text("; " + child.get("strongs") + ";")
            elif child.tag == "see":
                book.dropped["<see> (repeats the entry's references)"] += 1
            elif child.tag in ("strongs_derivation", "strongs_def", "kjv_def", "strongsref", "greek", "latin",
                               "pronunciation"):
                holder = ElementTree.Element("holder")
                holder.append(child)
                element_text(holder, book, "greek")
                continue
            else:
                raise ValueError("an unknown element <%s> in a Greek entry" % child.tag)
            # The headword's own trailing space would stand before its semicolon.
            if child.tail and child.tag != "strongs" and (child.tail.strip() or child is not headword):
                book.text(" ".join(child.tail.split("\n")))
        collapse_spaces(book)
        count += 1
    book.head.append(("entries", count))
    return book.write(out)


def collapse_spaces(book: Book) -> None:
    """The XML's own line breaks and indentation fall inside the text; runs of white space
    from them read as one space, as the printed dictionary has it."""
    index = len(book.records) - 1
    while index >= 0 and book.records[index][0] != "K":
        record = book.records[index]
        if record[0] == "T":
            book.records[index] = ("T", re.sub(r"\s{2,}", " ", record[1]))
        index -= 1


OSIS = "{http://www.bibletechnologies.net/2003/OSIS/namespace}"


def import_strongs_hebrew(sources: dict, folder: str, out: str) -> str:
    data, entry = read_pinned(sources, folder, "strongs", "StrongHebrewG.xml")
    book = Book("strongs-hebrew")
    head_for(book, sources["strongs"], [entry])
    root = ElementTree.fromstring(data)
    book.add("B", "Hebrew", "A Concise Dictionary of the Words in the Hebrew Bible", "Hebrew Dictionary")
    count = 0
    for item in root.iter(OSIS + "div"):
        if item.get("type") != "entry":
            continue
        number = item.get("n")
        word = item.find(OSIS + "w")
        book.add("K", "H" + number)
        book.add("P", "entry")
        book.add("Y", "bold", "1")
        book.text(number + ".")
        book.add("Y", "bold", "0")
        book.dropped["Hebrew words in Hebrew letters (each kept as its transliteration)"] += 1
        book.text(" ")
        book.add("Y", "italic", "1")
        book.text(word.get("xlit"))
        book.add("Y", "italic", "0")
        book.text("; " + word.get("POS") + ";")
        if word.get("gloss"):
            book.dropped["gloss= (the Theological Wordbook's numbers, not Strong's)"] += 1
        for child in item:
            tag = child.tag.replace(OSIS, "")
            if child is word:
                continue
            if tag == "foreign":
                book.dropped["<foreign> (Greek equivalents, not Strong's)"] += 1
            elif tag == "list":
                book.dropped["<list> (a numbered outline of senses, not Strong's)"] += 1
            elif tag == "note" and child.get("type") == "x-typo":
                book.dropped["<note x-typo> (the e-text editor's record of a correction already made)"] += 1
            elif tag == "note" and child.get("type") in ("exegesis", "explanation", "translation"):
                if child.get("type") == "translation":
                    book.text(" :—")
                else:
                    book.text(" ")
                hebrew_note(child, book)
            else:
                raise ValueError("an unknown element <%s %s> in a Hebrew entry" % (tag, child.get("type")))
        collapse_spaces(book)
        count += 1
    book.head.append(("entries", count))
    return book.write(out)


def hebrew_note(element: ElementTree.Element, book: Book) -> None:
    if element.text:
        book.text(element.text)
    for child in element:
        tag = child.tag.replace(OSIS, "")
        if tag == "w":
            if child.get("xlit"):
                book.dropped["Hebrew words in Hebrew letters (each kept as its transliteration)"] += 1
                book.add("Y", "italic", "1")
                book.text(child.get("xlit"))
                book.add("Y", "italic", "0")
                book.text(" ")
            if child.get("src"):
                book.text("(H" + child.get("src") + ")")
            elif child.text:
                book.text(child.text)
        elif tag == "hi":
            book.add("Y", "italic", "1")
            hebrew_note(child, book)
            book.add("Y", "italic", "0")
        elif tag == "foreign":
            hebrew_note(child, book)
        elif tag == "note" and child.get("type") == "x-typo":
            book.dropped["<note x-typo> (the e-text editor's record of a correction already made)"] += 1
        else:
            raise ValueError("an unknown element <%s> in a Hebrew note" % tag)
        if child.tail:
            book.text(child.tail)


# ---- The concordance, from the Berean tables ---------------------------------------

def strongs_transliterations(sources: dict, folder: str) -> dict:
    """Each Strong's number's word, transliterated, from the dictionaries."""
    words = {}
    data, entry = read_pinned(sources, folder, "strongs", "StrongHebrewG.xml")
    for item in ElementTree.fromstring(data).iter(OSIS + "div"):
        if item.get("type") == "entry":
            word = item.find(OSIS + "w")
            if word is not None and word.get("xlit"):
                words["H" + item.get("n")] = word.get("xlit")
    data, entry = read_pinned(sources, folder, "strongs", "strongsgreek.xml")
    for item in ElementTree.fromstring(data).iter("entry"):
        greek = item.find("greek")
        if greek is not None and greek.get("translit"):
            words["G" + item.findtext("strongs").strip().lstrip("0")] = greek.get("translit")
    return words


def import_concordance(sources: dict, folder: str, out: str, bsb_path: str) -> str:
    """Every Strong's number and, for each English rendering the BSB gives it, every verse
    in which the BSB so renders it, as the verse's place in the BSB (1 for Genesis 1:1).
    Built from the tables' alignment of each original word to its English. The reader
    prints the references; nothing here is text but the renderings.

        K key                   a Strong's number opens (H430, G26)
        G rendering places      the BSB's English for it and the verses, ascending"""
    data, entry = read_pinned(sources, folder, "berean", "bsb_tables.tsv")
    places_of = {}
    with open(bsb_path, encoding="utf-8") as file:
        code = chapter = None
        note = False
        for line in file:
            fields = line.rstrip("\n").split("\t")
            if fields[0] == "B":
                code = fields[1]
            elif fields[0] == "C":
                chapter = fields[1]
            elif fields[0] == "N":
                note = True
            elif fields[0] == "E":
                note = False
            elif fields[0] == "V" and not note:
                places_of["%s %s:%s" % (code, chapter, fields[1])] = len(places_of) + 1
    codes = bsb_book_order(sources, folder)
    names = []
    renderings = collections.defaultdict(lambda: collections.defaultdict(list))
    unplaced = collections.Counter()
    reference = None
    for row in data.decode("utf-8").split("\r\n")[1:]:
        if not row:
            continue
        fields = row.split("\t")
        if fields[12]:
            name, place = fields[12].rsplit(" ", 1)
            if not names or names[-1] != name:
                names.append(name)
            reference = "%s %s" % (codes[len(names) - 1], place)
        language = fields[4]
        number = fields[10] if language in ("Hebrew", "Aramaic") else fields[11]
        english = " ".join(fields[18].replace("[", "").replace("]", "").split())
        if not number or not english or english in ("-", "vvv", ". . ."):
            continue
        if reference not in places_of:
            unplaced[reference] += 1
            continue
        key = ("H" if language in ("Hebrew", "Aramaic") else "G") + number
        places = renderings[key][english]
        place = places_of[reference]
        if not places or places[-1] != place:
            places.append(place)
    book = Book("concordance")
    head_for(book, sources["berean"], [entry])
    book.add("B", "Concordance", "A Concordance of the Berean Standard Bible by Strong's Numbers", "Concordance")
    def order(key: str) -> tuple:
        return (key[0] != "H", int(re.match(r"\d+", key[1:]).group(0)), key)
    words = strongs_transliterations(sources, folder)
    total = 0
    for key in sorted(renderings, key=order):
        book.add("K", key)
        book.add("P", "entry")
        book.add("Y", "bold", "1")
        book.text(key)
        book.add("Y", "bold", "0")
        if key in words:
            book.text(" ")
            book.add("Y", "italic", "1")
            book.text(words[key])
            book.add("Y", "italic", "0")
        for english in sorted(renderings[key], key=lambda word: (-len(renderings[key][word]), word)):
            places = renderings[key][english]
            book.add("G", english, " ".join(str(place) for place in places))
            total += len(places)
    book.head.append(("numbers", len(renderings)))
    book.head.append(("places", total))
    for reference, count in sorted(unplaced.items()):
        book.dropped["words of %s, a verse the BSB leaves out of its text" % reference] = count
    book.dropped["every column but the verse, the language, the Strong's number and the English"] = 1
    return book.write(out)


# ---- Main --------------------------------------------------------------------------

def load_catalogue() -> list:
    with open(os.path.join(HERE, "catalogue.json"), encoding="utf-8") as file:
        return json.load(file)["books"]


def shelf_record(entry: dict, sources: dict) -> str:
    """The lines the bookcase reads without opening the book (the shelf index, library.hpp),
    and what the book's About card shows: the edition (the lock's account of the source),
    the licence (public domain unless the catalogue names one), its address and the
    credit it asks; and the block whose code holds the book (make_library.cpp)."""
    lines = ["title\t" + entry["title"]]
    if entry.get("subtitle"):
        lines.append("subtitle\t" + entry["subtitle"])
    if entry.get("category"):
        lines.append("category\t" + entry["category"])
    if entry.get("binding"):
        lines.append("binding\t" + entry["binding"])
    if entry.get("leather"):
        lines.append("leather\t1")
    source = sources.get(entry.get("source", ""))
    if source:
        lines.append("edition\t" + source["work"])
    for key in ("licence", "licence_url", "credit"):
        if entry.get(key):
            lines.append(key + "\t" + entry[key])
    lines.append("block\t%d" % entry["block"])
    return "\n".join(lines) + "\n"


def write_shelf(path: str, catalogue: list, sources: dict) -> None:
    """The shelf index (library.hpp), in order: per book "book<TAB>id", its shelf record's
    lines, then a blank line. The suite ships it beside the library's parts."""
    with open(path, "w", encoding="utf-8", newline="\n") as file:
        for entry in catalogue:
            file.write("book\t" + entry["id"] + "\n" + shelf_record(entry, sources) + "\n")


def import_entry(sources: dict, folder: str, out: str, entry: dict) -> str:
    """A catalogue book read by a general importer: `epub` (an EPUB), `sefaria` (Sefaria's
    JSON, one file a division, in the order `files` gives), `perseus` (Perseus' TEI, one
    file a work, in the order `files` gives), `mit` (the Internet Classics Archive's plain
    texts, one file a work), `tbes` (a STEPBible brief
    lexicon, its `language` G or H), `abbott-smith` (Abbott-Smith's TEI) or `usfm` (the
    books `codes` names from a zipped USFM Bible)."""
    import epub_reader
    import lexicon_reader
    import mit_reader
    import perseus_reader
    import sefaria_reader
    import usfm_reader
    names = entry["files"] if "files" in entry else [entry["file"]]
    pinned = [read_pinned(sources, folder, entry["source"], name) for name in names]
    book = Book(entry["id"])
    head_for(book, sources[entry["source"]], [pin for _, pin in pinned])
    data = pinned[0][0]
    importer = entry["importer"]
    if importer == "epub":
        epub_reader.import_epub(data, book, entry["id"])
    elif importer == "sefaria":
        sefaria_reader.import_sefaria([sefaria_reader.load(bytes_) for bytes_, _ in pinned], book)
    elif importer == "mit":
        mit_reader.import_mit([bytes_ for bytes_, _ in pinned], book)
    elif importer == "perseus":
        perseus_reader.import_perseus([bytes_ for bytes_, _ in pinned], book)
    elif importer == "tbes":
        lexicon_reader.import_tbes(data, book, entry["language"])
    elif importer == "abbott-smith":
        lexicon_reader.import_abbott_smith(data, book)
    elif importer == "usfm":
        usfm_reader.import_usfm(data, book, entry["codes"])
    else:
        raise ValueError("%s: no importer named %r" % (entry["id"], importer))
    return book.write(out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--sources", default=os.path.join(ROOT, ".build", "library", "sources"))
    parser.add_argument("--out", default=os.path.join(ROOT, ".build", "library", "normalized"))
    parser.add_argument("--make-corrections", action="store_true")
    parser.add_argument("--block", type=int, metavar="N",
                        help="import only the books of block N (a new batch), and the shelf index")
    parser.add_argument("--shelf", metavar="FILE",
                        help="only write the shelf index there (from the catalogue and the lock; no source is read)")
    arguments = parser.parse_args()
    if arguments.shelf:
        write_shelf(arguments.shelf, load_catalogue(), load_lock())
        return 0
    os.makedirs(arguments.out, exist_ok=True)
    sources = load_lock()
    if arguments.block:
        catalogue = load_catalogue()
        failures = 0
        for entry in catalogue:
            if entry["block"] != arguments.block or entry["importer"] == "own":
                continue
            try:
                path = import_entry(sources, arguments.sources, arguments.out, entry)
                print("%9d bytes  %s" % (os.path.getsize(path), entry["id"]), flush=True)
            except Exception as error:  # report every book that does not import, then fail
                print("FAILED %s: %s: %s" % (entry["id"], type(error).__name__, error), flush=True)
                failures += 1
        write_shelf(os.path.join(arguments.out, "shelf.txt"), catalogue, sources)
        return 1 if failures else 0
    bsb = import_bsb(sources, arguments.sources, arguments.out, arguments.make_corrections)
    expected, found, differences, empty = bsb_verse_check(sources, arguments.sources, bsb, bsb_book_order(sources, arguments.sources))
    print("bsb: %d verses in bsb.txt, %d imported, %d differ; empty in bsb.txt and absent from the USFM: %s" % (expected, found, differences, ", ".join(empty)))
    paths = [bsb,
             import_jps(sources, arguments.sources, arguments.out),
             import_gorfinkle(sources, arguments.sources, arguments.out),
             import_cohen(sources, arguments.sources, arguments.out),
             import_strongs_hebrew(sources, arguments.sources, arguments.out),
             import_strongs_greek(sources, arguments.sources, arguments.out),
             import_concordance(sources, arguments.sources, arguments.out, bsb)]
    catalogue = load_catalogue()
    for entry in catalogue:
        if entry["importer"] != "own":
            paths.append(import_entry(sources, arguments.sources, arguments.out, entry))
    write_shelf(os.path.join(arguments.out, "shelf.txt"), catalogue, sources)
    for path in paths:
        print("%9d bytes  %s" % (os.path.getsize(path), os.path.relpath(path, ROOT)))
    return 1 if differences else 0


if __name__ == "__main__":
    sys.exit(main())
