"""Reads Sefaria's exported texts (one JSON file per text and version) into the library's
plain form (import_books.py): the Mishnah, the Talmud, the Midrash Rabbah, the Targums
and the commentaries on the Torah of the Rabbinic writings shelf.

A file's `text` is nested lists, as deep as its `sectionNames` say:

- two levels, [chapter][segment]: a mishnah, a verse of a Targum, a paragraph of a
  midrash, each a numbered paragraph; for the Talmud, [daf][line], each daf side (2a,
  2b...) a chapter and each line a paragraph;
- three levels, [chapter][verse][comment] for a commentary (each verse's comments
  follow its number) or [parasha][chapter][midrash] for the larger midrashim;
- or a dictionary of named parts (an Introduction, a Foreword, the Petichta of a
  midrash) with the body under the empty name, or (Targum Jerusalem) a part for each
  book of the Torah.

Sefaria's English carries a little HTML: <b> and <strong> (in the Talmud, the bold is
the translation and the plain the Steinsaltz explanation), <i>, <small>, <u>, <br>
(a new line), <span> and <a> (their text kept), <img> (dropped, counted), and footnotes, a
<sup class="footnote-marker"> followed by an <i class="footnote">. Anything else in
angle brackets (the Midrash Rabbah's <Variant reading: ...>) is text. Hebrew is given in
Latin letters, in italic (lexicon_reader.hebrew_in_latin): the library's faces have none.
"""

import html.parser
import json
import re
import typing

import lexicon_reader

KNOWN_TAGS = ("b", "strong", "i", "em", "small", "u", "br", "span", "sup", "sub", "img", "big", "a")
# A run of Hebrew (letters, points, cantillation, maqaf), with the spaces and gershayim
# inside it: the library's faces have no Hebrew, so it is given in Latin letters.
HEBREW = re.compile(r"[\u0591-\u05f4\ufb1d-\ufb4f]+(?:[\s\"'\u05be-][\u0591-\u05f4\ufb1d-\ufb4f]+)*")
NOT_A_TAG = re.compile(r"<(?!/?(?:%s)\b)" % "|".join(KNOWN_TAGS), re.I)


LETTER_NAMES = dict(zip("אבגדהוזחטיכךלמםנןסעפףצץקרשת",
                        ["aleph", "bet", "gimel", "dalet", "he", "vav", "zayin", "het", "tet", "yod", "kaf", "kaf",
                         "lamed", "mem", "mem", "nun", "nun", "samekh", "ayin", "pe", "pe", "tsade", "tsade", "qof",
                         "resh", "shin", "tav"]))


def latin(word: str) -> str:
    """A Hebrew word in Latin letters; a letter standing alone (as a grammarian names one)
    by its name."""
    letters = [char for char in word if char in LETTER_NAMES]
    if len(letters) == 1:
        return LETTER_NAMES[letters[0]]
    return lexicon_reader.hebrew_in_latin(word)


class Inline(html.parser.HTMLParser):
    """One segment's HTML as records: spans, line breaks, footnotes."""

    def __init__(self, book: typing.Any, style: str) -> None:
        super().__init__(convert_charrefs=True)
        self.book = book
        self.style = style
        self.stack: typing.List[str] = []   # the open tags, as what each closes
        self.marker = False                 # inside a footnote marker
        self.label = ""
        self.note_depth = 0                 # >0 inside a footnote's text

    def handle_starttag(self, tag: str, attrs: list) -> None:
        classes = dict(attrs).get("class", "") or ""
        if tag == "sup" and "footnote-marker" in classes:
            if self.note_depth:
                # A footnote whose <i> Sefaria's text never closes runs on to the next
                # marker; it is closed there.
                self.book.dropped["footnotes left open in Sefaria's text, closed at the next footnote"] += 1
                while self.note_depth:
                    self.handle_endtag("")
            self.stack.append("marker")
            self.marker = True
            self.label = ""
        elif tag == "i" and "footnote" in classes and "marker" not in classes:
            self.stack.append("note")
            self.book.add("N", self.label.strip() or "*")
            self.note_depth += 1
        elif tag in ("b", "strong"):
            self.stack.append("bold")
            self.book.add("Y", "bold", "1")
        elif tag in ("i", "em"):
            self.stack.append("italic")
            self.book.add("Y", "italic", "1")
        elif tag == "small":
            self.stack.append("small")
            self.book.add("Y", "small", "1")
        elif tag == "br":
            if self.note_depth:
                self.book.text(" ")
            else:
                self.book.add("P", self.style if self.style != "verse" else "flush")
        elif tag == "img":
            self.book.dropped["pictures in Sefaria's text"] += 1
        else:
            # <span>, <u>, <sup>, <sub>, <big>: the text is kept as it stands.
            self.stack.append("plain")

    def handle_startendtag(self, tag: str, attrs: list) -> None:
        self.handle_starttag(tag, attrs)
        if tag not in ("br", "img"):
            self.handle_endtag(tag)

    def handle_endtag(self, tag: str) -> None:
        if tag in ("br", "img") or not self.stack:
            return
        what = self.stack.pop()
        if what == "marker":
            self.marker = False
        elif what == "note":
            self.note_depth -= 1
            self.book.add("E")
        elif what in ("bold", "italic", "small"):
            self.book.add("Y", what, "0")

    def handle_data(self, data: str) -> None:
        data = data.replace("\n", " ").replace("\t", " ")
        if self.marker:
            self.label += data
            return
        at = 0
        for match in HEBREW.finditer(data):
            self.book.text(data[at:match.start()])
            words = [latin(word) for word in re.split(r"[\s\u05be]+", match.group(0))]
            if not any(words):
                at = match.end()
                continue
            # In italic already (a quotation, a title), it stays so; a span is not opened twice.
            italic = "italic" not in self.stack
            if italic:
                self.book.add("Y", "italic", "1")
            self.book.text(" ".join(word for word in words if word))
            if italic:
                self.book.add("Y", "italic", "0")
            self.book.head_counts["Hebrew given in Latin letters (the library's faces have no Hebrew)"] += 1
            at = match.end()
        self.book.text(data[at:])

    def finish(self) -> None:
        self.close()
        while self.stack:
            self.handle_endtag("")


def inline(text: str, book: typing.Any, style: str) -> None:
    parser = Inline(book, style)
    parser.feed(NOT_A_TAG.sub("&lt;", text))
    parser.finish()


def segments(node: typing.Any) -> typing.Iterator[str]:
    """Every non-empty string under a node, in order."""
    if isinstance(node, str):
        if node.strip():
            yield node
    elif isinstance(node, list):
        for child in node:
            yield from segments(child)


def depth(node: typing.Any) -> int:
    if isinstance(node, list):
        return 1 + max([depth(child) for child in node] or [0])
    return 0


def paragraph(book: typing.Any, style: str, label: str, text: str) -> None:
    book.add("P", style)
    if label:
        book.add("V", label)
    inline(text, book, style)


def body(book: typing.Any, node: list, talmud: bool) -> int:
    """The main text of one file; returns its segment count."""
    count = 0
    levels = depth(node)
    for chapter_index, chapter in enumerate(node):
        if not any(True for _ in segments(chapter)):
            continue
        if talmud:
            book.add("C", "%d%s" % (chapter_index // 2 + 1, "ab"[chapter_index % 2]))
        else:
            book.add("C", chapter_index + 1)
        if levels == 2:
            for index, text in enumerate(chapter):
                if not text.strip():
                    continue
                paragraph(book, "folio" if talmud else "verse", "" if talmud else str(index + 1), text)
                count += 1
        else:
            for index, group in enumerate(chapter):
                first = True
                for text in segments(group):
                    # A verse's comments follow its number; a midrash's sections theirs.
                    paragraph(book, "verse" if first else "p", str(index + 1) if first else "", text)
                    first = False
                    count += 1
    return count


def named_part(book: typing.Any, name: str, node: typing.Any) -> int:
    """An Introduction, a Foreword or a Petichta: a chapter of paragraphs under its name."""
    texts = list(segments(node))
    if not texts:
        return 0
    book.add("C", name)
    for text in texts:
        paragraph(book, "p", "", text)
    return len(texts)


def division_title(document: dict) -> typing.Tuple[str, str]:
    """The division's title and running head from the file's title: "Mishnah Berakhot" is
    Berakhot, "Rashi on Genesis" Genesis, "Targum Jonathan on Isaiah" Isaiah."""
    title = document["title"]
    short = re.sub(r"^(Mishnah |.* on )", "", title)
    return title, short


def import_sefaria(documents: typing.List[dict], book: typing.Any) -> None:
    """Appends each document as a division (or, for a text in named books, as several)."""
    segments_read = 0
    for document in documents:
        text = document["text"]
        talmud = (document.get("sectionNames") or [""])[0] == "Daf"
        title, short = division_title(document)
        if isinstance(text, dict) and "" not in text:
            # Targum Jerusalem: a part for each book.
            for name, node in text.items():
                book.add("B", name, name, name)
                segments_read += body(book, node, False)
            continue
        book.add("B", short, title, short)
        if isinstance(text, dict):
            for name, node in text.items():
                if name:
                    segments_read += named_part(book, name, node)
            text = text[""]
        segments_read += body(book, text, talmud)
    book.head.append(("segments", segments_read))
    book.dropped["Sefaria's version metadata"] = len(documents)


def load(data: bytes) -> dict:
    return json.loads(data.decode("utf-8"))
