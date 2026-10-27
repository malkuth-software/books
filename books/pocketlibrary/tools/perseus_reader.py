"""Reads Perseus' TEI translations (PerseusDL canonical-greekLit and canonical-latinLit,
CC BY-SA) into the library's plain form (import_books.py): Plautus, Cicero's speeches,
letters and treatises, Quintilian, Aulus Gellius, Appian and the Attic orators.

Each file is one work, a division of the book, titled by its teiHeader. Inside the body's
translation <div>, the outermost textparts (a book, an act, a speech, a letter) are its
chapters, and any textpart below them (a chapter, a section, a scene) adds its number in
the margin of the paragraph it opens, as a verse number does. Blocks: <p> a paragraph,
<l> a line of verse, <sp> a speech (its <speaker> in small capitals, then its lines or
paragraphs), <stage> a stage direction (italic), <head> a heading, <quote> or <q>
set as the source has them (a <quote> holding lines or paragraphs is set in), <list> and
<listPerson> (a cast) one item to a line. Inline: <note> a footnote (a marginal summary,
type="marg", too), <emph>, <hi>, <foreign> and <title> italic, <lb/> a space; <pb/>,
<milestone/> and <gap/> carry nothing and are counted. Anything else is an error.
"""

import html.entities
import re
import typing
import xml.etree.ElementTree as ElementTree

TEI = "{http://www.tei-c.org/ns/1.0}"
XML_LANG = "{http://www.w3.org/XML/1998/namespace}lang"
INLINE_ITALIC = ("emph", "hi", "foreign", "title", "term", "mentioned", "soCalled")
INLINE_PLAIN = ("name", "persName", "placeName", "orgName", "rs", "date", "num", "bibl", "ref", "cit", "seg",
                "add", "del", "corr", "sic", "unclear", "supplied", "choice", "abbr", "expan", "orig", "reg",
                "roleName", "geogName", "measure", "w", "c", "author", "said", "label", "surname", "forename",
                "s", "span", "date", "time", "q", "quote", "lg", "salute", "gloss", "ab", "signed", "dateline",
                "opener", "closer", "address", "addrLine", "hi", "sic", "damage", "app", "lem", "rdg")
NUMBERED_ONLY = ("section", "chapter", "card", "line", "paragraph")
SKIPPED = ("pb", "milestone", "gap", "anchor", "figure", "graphic", "index", "space", "delSpan", "addSpan")


def local(tag: str) -> str:
    return tag.split("}", 1)[1] if "}" in tag else tag


def named(root: ElementTree.Element, name: str) -> typing.Iterator[ElementTree.Element]:
    """Every element of that name, with or without the TEI namespace (older Perseus files
    have none)."""
    return (element for element in root.iter() if isinstance(element.tag, str) and local(element.tag) == name)


def spaced(text: typing.Optional[str]) -> str:
    return re.sub(r"\s+", " ", text or "")


class Walker:
    def __init__(self, book: typing.Any) -> None:
        self.book = book
        self.in_note = False
        self.open = False  # a paragraph is open and has text
        self.chaptered = False  # the one chapter of a work cut only into sections is open
        self.pending = ""

    # ---- inline ----------------------------------------------------------------------

    def text(self, text: str) -> None:
        text = spaced(text)
        records = self.book.records
        if not self.open or (records and records[-1][0] == "T" and records[-1][1].endswith(" ")):
            text = text.lstrip()
        if text:
            self.book.text(text)
            self.open = True

    def inlines(self, element: ElementTree.Element) -> None:
        self.text(element.text)
        for child in element:
            self.inline(child)
            self.text(child.tail)

    def inline(self, element: ElementTree.Element) -> None:
        tag = local(element.tag)
        if tag == "note":
            if self.in_note:
                self.text(" [")
                self.inlines(element)
                self.text("]")
                self.book.dropped["notes on notes (each set in brackets inside its note)"] += 1
                return
            self.book.add("N", element.get("n") or "*")
            self.in_note = True
            was_open = self.open
            self.open = False
            self.inlines(element)
            self.in_note = False
            self.open = was_open
            self.book.add("E")
            if element.get("type") == "marg":
                self.book.head_counts["marginal summaries set as footnotes"] += 1
        elif tag in INLINE_ITALIC:
            self.book.add("Y", "italic", "1")
            self.inlines(element)
            self.book.add("Y", "italic", "0")
        elif tag == "lb":
            self.text(" ")
        elif tag in SKIPPED:
            self.book.dropped["<%s> (a page, a reference point or a gap: nothing to set)" % tag] += 1
        elif tag == "table":
            self.table(element)
        elif tag in ("listPerson", "castList", "castGroup", "list", "person", "castItem", "item"):
            # A list standing inside a line (a cast in a play's first line): its items run on.
            self.inlines(element)
            if tag in ("person", "castItem", "item"):
                self.text(" ")
        elif tag in INLINE_PLAIN or tag in ("l", "p", "speaker", "stage", "sp", "head", "text", "body", "group",
                                            "front", "back", "div", "lg"):
            # A block standing inside a paragraph runs on in it.
            self.inlines(element)
        else:
            raise ValueError("an unknown inline element <%s>" % tag)

    # ---- blocks ----------------------------------------------------------------------

    def paragraph(self, style: str) -> None:
        self.book.add("P", style)
        self.open = False

    def numbered(self, element: ElementTree.Element, label: str) -> None:
        """A textpart's number in the margin of its first block."""
        if label:
            self.pending = label

    def start(self, style: str) -> None:
        self.paragraph(style)
        label = getattr(self, "pending", "")
        if label:
            self.book.add("V", label)
            self.pending = ""

    def blocks(self, element: ElementTree.Element, depth: int, quote: bool = False) -> None:
        if element.text and element.text.strip():
            self.start("p")
            self.text(element.text)
        for child in element:
            self.block(child, depth, quote)
            if child.tail and child.tail.strip():
                if not self.open:
                    self.start("p")
                self.text(child.tail)

    def block(self, element: ElementTree.Element, depth: int, quote: bool) -> None:
        tag = local(element.tag)
        numbered_div = re.fullmatch(r"div[1-7]", tag) is not None
        if tag == "div" or numbered_div:
            # Older files number their divisions (div1, div2...) and name their kind in
            # `type`: each is a text part.
            textpart = numbered_div or element.get("type") == "textpart"
            subtype = (element.get("type") if numbered_div else element.get("subtype") or element.get("type")) or ""
            n = element.get("n") or ""
            heads = [child for child in element if local(child.tag) == "head"]
            if depth == 0 and textpart and subtype in NUMBERED_ONLY and not heads:
                # A speech or a treatise cut only into numbered sections: one chapter, its
                # sections numbered in the margin.
                if not self.chaptered:
                    self.book.add("C", "")
                    self.chaptered = True
                if n:
                    self.numbered(element, n)
                self.blocks_without_heads(element, depth + 1, quote)
            elif depth == 0 and textpart:
                if n == "front":
                    n = ""
                    subtype = "the persons and the subject" if subtype == "act" else subtype
                label = heading_of(heads) or ("%s %s" % (subtype.capitalize(), n)).strip()
                self.book.add("C", label)
                self.open = False
                for head in heads[1:]:
                    self.book.add("S", "1", spaced(head_text(head)).strip())
                self.blocks_without_heads(element, depth + 1, quote)
            else:
                for head in heads:
                    self.book.add("S", "1", spaced(head_text(head)).strip())
                    self.open = False
                if n and n.isdigit():
                    self.numbered(element, n)
                self.blocks_without_heads(element, depth + 1 if textpart else depth, quote)
        elif tag == "head":
            self.book.add("S", "1", spaced(head_text(element)).strip())
            self.open = False
        elif tag == "p":
            self.start("flush" if quote else "p")
            self.inlines(element)
        elif tag == "l":
            self.start("q2" if quote else "q1")
            self.inlines(element)
        elif tag == "lg":
            self.blocks(element, depth, quote)
            self.paragraph("blank")
        elif tag == "sp":
            speakers = [child for child in element if local(child.tag) == "speaker"]
            self.start("flush")
            self.book.add("Y", "small", "1")
            self.text(" ".join(spaced("".join(s.itertext())).strip() for s in speakers))
            self.book.add("Y", "small", "0")
            for child in element:
                if local(child.tag) == "speaker":
                    continue
                if local(child.tag) in ("p", "l", "lg", "stage", "quote", "q", "list"):
                    self.block(child, depth, quote)
                else:
                    self.text(" ")
                    self.inline(child)
                if child.tail and child.tail.strip():
                    self.text(child.tail)
        elif tag == "stage":
            self.start("flush")
            self.book.add("Y", "italic", "1")
            self.inlines(element)
            self.book.add("Y", "italic", "0")
        elif tag in ("quote", "q", "cit", "epigraph", "opener", "closer", "salute", "signed", "dateline", "byline",
                     "argument", "trailer", "postscript"):
            if any(local(child.tag) in ("p", "l", "lg", "list") for child in element):
                self.blocks(element, depth, True)
            else:
                self.start("flush")
                self.inlines(element)
        elif tag in ("list", "listPerson", "castList", "castGroup"):
            for child in element:
                if local(child.tag) in ("item", "person", "castItem", "listPerson", "castGroup", "head"):
                    if local(child.tag) in ("listPerson", "castGroup"):
                        self.block(child, depth, quote)
                        continue
                    self.start("list1")
                    self.inlines(child)
        elif tag == "table":
            self.table(element)
        elif tag in ("note",):
            if not self.open:
                self.start("p")
            self.inline(element)
        elif tag in SKIPPED:
            self.book.dropped["<%s> (a page, a reference point or a gap: nothing to set)" % tag] += 1
        elif tag in INLINE_PLAIN or tag in INLINE_ITALIC or tag == "lb":
            if not self.open:
                self.start("p")
            self.inline(element)
        else:
            raise ValueError("an unknown block element <%s>" % tag)

    def table(self, element: ElementTree.Element) -> None:
        """A table, each row a line, its cells one after another."""
        for row in named(element, "row"):
            cells = [spaced("".join(cell.itertext())).strip() for cell in row if local(cell.tag) == "cell"]
            if any(cells):
                self.start("flush")
                self.text("   ".join(cell for cell in cells if cell))
        self.book.dropped["tables (each row set as a line)"] += 1

    def blocks_without_heads(self, element: ElementTree.Element, depth: int, quote: bool) -> None:
        if element.text and element.text.strip():
            self.start("p")
            self.text(element.text)
        for child in element:
            if local(child.tag) != "head":
                self.block(child, depth, quote)
            if child.tail and child.tail.strip():
                if not self.open:
                    self.start("p")
                self.text(child.tail)


def head_text(head: ElementTree.Element) -> str:
    """A heading's words, without its notes."""
    parts = [head.text or ""]
    for child in head:
        if local(child.tag) != "note":
            parts.append(head_text(child))
        parts.append(child.tail or "")
    return "".join(parts)


def heading_of(heads: list) -> str:
    return spaced(head_text(heads[0])).strip() if heads else ""


def title_of(root: ElementTree.Element) -> str:
    for title in named(root, "title"):
        text = spaced("".join(title.itertext())).strip()
        if text and title.get("type") != "sub":
            return re.sub(r"\.?\s*Machine readable text$", "", text)
    return "Untitled"


XML_ENTITIES = ("amp", "lt", "gt", "quot", "apos")


def parse(data: bytes, book: typing.Any) -> ElementTree.Element:
    """A TEI file, its doctype left out: an older file names characters by the entities its
    DTD defined (&mdash;), which are the HTML ones, and Perseus' own boilerplate by entities
    of its own (&Perseus.publish;), which carry nothing of the text and are counted."""
    text = data.decode("utf-8")
    text = re.sub(r"<!DOCTYPE[^\[>]*(\[.*?\])?\s*>", "", text, count=1, flags=re.S)

    def entity(match: re.Match) -> str:
        name = match.group(1)
        if name in XML_ENTITIES:
            return match.group(0)
        if name + ";" in html.entities.html5:
            return html.entities.html5[name + ";"]
        book.dropped["Perseus' own entities (its imprint, not the text)"] += 1
        return ""
    text = re.sub(r"&([A-Za-z][A-Za-z0-9._-]*);", entity, text)
    return ElementTree.fromstring(text)


def import_perseus(documents: typing.List[bytes], book: typing.Any) -> None:
    """Appends each document as a division."""
    for data in documents:
        root = parse(data, book)
        title = title_of(root)
        body = next(named(root, "body"))
        translation = None
        for div in named(body, "div"):
            if div.get("type") in ("translation", "edition"):
                translation = div
                break
        book.add("B", re.sub(r"[^A-Za-z0-9]+", "-", title).strip("-")[:40] or "work", title, title)
        opened = len(book.records)
        walker = Walker(book)
        if translation is not None and not any(local(child.tag) == "div" for child in translation):
            book.add("C", "")
            walker.blocks(translation, 1)
        else:
            walker.blocks_without_heads(translation if translation is not None else body, 0, False)
        if not any(record[0] == "C" for record in book.records[opened:]):
            # A work in no parts at all: one chapter.
            book.records.insert(opened, ("C", ""))
    book.head.append(("works", len(documents)))
