"""Reads books of a USFM Bible (the World English Bible's deuterocanon, from ebible.org)
into the library's plain form (import_books.py).

Line markers: \\id, \\ide, \\h and \\toc1-3 name the book (kept for its division's title);
\\mt1-3 are its title lines; \\ip and \\is1 its introduction; \\c a chapter (\\cp, a
chapter's printed label, is dropped: the number stands); \\s1 a heading; \\p, \\m, \\nb,
\\pi1, \\pc, \\q1-3, \\b and \\d paragraphs and lines of poetry; \\v a verse, wherever it
falls. Inline: \\f ... \\f* a footnote (its \\fr reference is its label; \\fqa and \\fq,
the words it quotes, italic), \\x ... \\x* a cross-reference (dropped, counted), \\bk a
book's name (italic), \\wh a word list entry (its text kept). Anything else is an error.
"""

import io
import re
import typing
import unicodedata
import zipfile

PARAGRAPHS = {"p": "p", "m": "flush", "nb": "flush", "pi1": "p", "pc": "centre", "q1": "q1", "q2": "q2", "q3": "q2",
              "b": "blank", "d": "title", "ip": "p"}
# The space after an opening marker separates it from its text; after a closing one it is
# the text's own.
INLINE = re.compile(r"\\(\+?)([a-z]+[0-9]*)(?:(\*)| ?)")


def space_before_verse(book: typing.Any) -> None:
    """A verse line's own trailing space is lost with the line; inside a paragraph the
    running text before a verse ends in a space (as import_books does for the BSB)."""
    index = len(book.records) - 1
    while index >= 0:
        kind = book.records[index][0]
        if kind == "T":
            if not book.records[index][1].endswith(" "):
                book.records[index] = ("T", book.records[index][1] + " ")
            return
        if kind in ("P", "S", "R", "C", "B", "V", "M"):
            return
        if kind == "E":
            # The footnote is passed over: its text is not the verse's.
            while index > 0 and book.records[index][0] != "N":
                index -= 1
        index -= 1


class Line:
    """The inline markup of one line, carried across lines for an open span."""

    def __init__(self, book: typing.Any) -> None:
        self.book = book
        self.note = False
        self.cross = False
        self.italic: typing.List[str] = []

    def text(self, text: str) -> None:
        if self.cross or not text:
            return
        self.book.text(text)

    def run(self, line: str) -> None:
        position = 0
        for match in INLINE.finditer(line):
            if match.start() < position:
                continue  # consumed with the marker before it (a footnote's reference)
            self.text(line[position:match.start()])
            position = match.end()
            nested, marker, closing = match.group(1), match.group(2), match.group(3) == "*"
            if marker == "v" and not closing:
                space_before_verse(self.book)
                number = re.match(r"(\S+) ?", line[position:])
                self.book.add("V", number.group(1))
                position += number.end()
            elif marker == "f":
                if closing:
                    self.close_spans()
                    self.book.add("E")
                    self.note = False
                else:
                    # "\f + \fr 1:2 \ft ..."; the caller (+) is the reader's own.
                    reference = re.match(r"\+ \\fr ([^\\]+)", line[position:])
                    label = reference.group(1).strip() if reference else "*"
                    position += reference.end() if reference else 0
                    self.book.add("N", label)
                    self.note = True
            elif marker in ("fr", "ft", "fl"):
                self.close_spans()
            elif marker in ("fqa", "fq"):
                self.close_spans()
                self.book.add("Y", "italic", "1")
                self.italic.append(marker)
            elif marker == "x":
                if not closing:
                    self.book.dropped["cross-references (\\x)"] += 1
                self.cross = not closing
            elif marker in ("xo", "xt"):
                pass
            elif marker == "bk":
                self.book.add("Y", "italic", "0" if closing else "1")
            elif marker == "wh":
                pass
            else:
                raise ValueError("an unknown inline marker \\%s%s%s" % (nested, marker, "*" if closing else ""))
        self.text(line[position:])

    def close_spans(self) -> None:
        while self.italic:
            self.italic.pop()
            self.book.add("Y", "italic", "0")


def import_usfm(data: bytes, book: typing.Any, codes: typing.List[str]) -> None:
    """The books named by their USFM codes, in that order, each a division."""
    archive = zipfile.ZipFile(io.BytesIO(data))
    names = {}
    for name in archive.namelist():
        match = re.match(r"\d+-([0-9A-Z]{3})", name.rsplit("/", 1)[-1])
        if match:
            names[match.group(1)] = name
    verses = 0
    for code in codes:
        text = unicodedata.normalize("NFC", archive.read(names[code]).decode("utf-8-sig"))
        titles = {}
        opened = False
        inline = Line(book)
        for raw in text.split("\n"):
            line = raw.rstrip("\r").strip()
            if not line:
                continue
            match = re.match(r"\\([a-z]+[0-9]*)(?: (.*))?$", line)
            if match is None:
                raise ValueError("%s: a line without a marker: %r" % (code, line[:40]))
            marker, rest = match.group(1), (match.group(2) or "").strip()
            if marker in ("id", "ide"):
                book.dropped["\\" + marker] += 1
            elif marker in ("h", "toc1", "toc2", "toc3"):
                titles[marker] = rest
            elif marker in ("mt1", "mt2", "mt3"):
                if not opened:
                    book.add("B", code, titles.get("toc1", rest), titles.get("h", rest), titles.get("toc3", code))
                    opened = True
                book.add("M", "1" if marker == "mt1" else "2", rest)
            elif marker == "is1":
                book.add("S", "1", rest)
            elif marker == "c":
                book.add("C", rest)
            elif marker == "cp":
                book.dropped["\\cp (a chapter's printed label)"] += 1
            elif marker == "s1":
                book.add("S", "1", rest)
            elif marker == "v":
                if not book.records or book.records[-1][0] in ("C", "S", "M"):
                    book.add("P", "p")
                inline.run(line)
                verses += 1
            elif marker in PARAGRAPHS:
                book.add("P", PARAGRAPHS[marker])
                inline.run(rest)
                verses += rest.count("\\v ")
            else:
                raise ValueError("%s: an unknown line marker \\%s" % (code, marker))
            if inline.note:
                raise ValueError("%s: a footnote runs past its line" % code)
        if not opened:
            raise ValueError("%s: no title line" % code)
    book.head.append(("books", len(codes)))
    book.head.append(("verses", verses))
