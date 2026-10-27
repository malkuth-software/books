"""Reads an EPUB into the library's normalized records (import_books.py's grammar).

The spine is followed in order. The publisher's own pages (title page, imprint,
colophon, licence, table of contents, cover) are left out and listed as dropped. Each
remaining spine document is a chapter, titled by its first heading. Endnotes are set as
footnotes at the place that calls them. Text is carried exactly, with four exceptions,
each counted in the book's head: runs of white space read as one space (as a browser
reads them), word joiners and byte-order marks are removed (they carry no text), soft
hyphens are removed (the page does not hyphenate), and a line break (<br/>) inside a
footnote reads as one space (a footnote is one run of text, as its paragraphs are).

Written first for Standard Ebooks' EPUBs, and their source repositories (a tarball
whose src/ folder is the EPUB unzipped); Project Gutenberg's work through the same
walker with plainer results.
"""

import collections
import posixpath
import re
import typing
import zipfile
import xml.etree.ElementTree as ElementTree

XHTML = "{http://www.w3.org/1999/xhtml}"
EPUB_TYPE = "{http://www.idpf.org/2007/ops}type"
OPF = "{http://www.idpf.org/2007/opf}"
DC = "{http://purl.org/dc/elements/1.1/}"
CONTAINER = "{urn:oasis:names:tc:opendocument:xmlns:container}"

# Spine documents of these kinds are the publisher's, not the book's.
SKIPPED_TYPES = ("titlepage", "halftitlepage", "imprint", "colophon", "copyright-page", "cover", "toc",
                 "loi", "lot", "landmarks")
NOTE_TYPES = ("endnotes", "footnotes", "rearnotes")
INVISIBLE = {"⁠": "word joiners", "﻿": "byte-order marks", "­": "soft hyphens"}


def epub_types(element: ElementTree.Element) -> list:
    return (element.get(EPUB_TYPE) or "").split()


def local(tag: str) -> str:
    return tag.split("}", 1)[1] if "}" in tag else tag


class Writer:
    """Collects records with the same rules as import_books.Book."""

    def __init__(self, book: typing.Any) -> None:
        self.book = book
        self.open_paragraph = False

    def paragraph(self, style: str) -> None:
        self.book.add("P", style)
        self.open_paragraph = True

    def text(self, text: str) -> None:
        for character, name in INVISIBLE.items():
            if character in text:
                self.book.head_counts[name + " removed"] += text.count(character)
                text = text.replace(character, "")
        collapsed = re.sub(r"[ \t\r\n]+", " ", text)
        if not collapsed:
            return
        # A paragraph does not open with a space, and spaces do not double.
        if collapsed.startswith(" ") and self.space_before_is_wanted() is False:
            collapsed = collapsed[1:]
        if collapsed:
            self.book.text(collapsed)

    def space_before_is_wanted(self) -> bool:
        """False at the start of a paragraph or a footnote, or after a space already
        written; spans and the ends of footnotes do not count."""
        for index in range(len(self.book.records) - 1, -1, -1):
            record = self.book.records[index]
            if record[0] == "T":
                return not record[1].endswith(" ")
            if record[0] in ("P", "C", "S", "B", "R", "M", "N"):
                return False
            if record[0] == "E":
                # The text before the footnote is what the space follows.
                while index > 0 and self.book.records[index][0] != "N":
                    index -= 1
        return False

    def trim(self) -> None:
        """Removes a trailing space from the paragraph just written."""
        for index in range(len(self.book.records) - 1, -1, -1):
            record = self.book.records[index]
            if record[0] == "T":
                stripped = record[1].rstrip(" ")
                if stripped:
                    self.book.records[index] = ("T", stripped)
                else:
                    del self.book.records[index]
                return
            if record[0] in ("P", "C", "S", "B"):
                return


class Package:
    """The files of an EPUB: a zip, or a Standard Ebooks source repository (a tarball
    whose src/ folder is the unzipped EPUB)."""

    def __init__(self, data: bytes) -> None:
        import io
        import tarfile
        self.files = {}
        if data[:2] == b"PK":
            archive = zipfile.ZipFile(io.BytesIO(data))
            for name in archive.namelist():
                self.files[name] = archive.read(name)
            return
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
            for member in archive.getmembers():
                if not member.isfile():
                    continue
                parts = member.name.split("/", 1)
                if len(parts) == 2 and parts[1].startswith("src/"):
                    self.files[parts[1][len("src/"):]] = archive.extractfile(member).read()
        if "META-INF/container.xml" not in self.files:
            raise ValueError("neither an EPUB nor a Standard Ebooks source tree")

    def read(self, name: str) -> bytes:
        return self.files[name]


class EpubBook:
    """The EPUB's package: metadata, spine and documents."""

    def __init__(self, data: bytes) -> None:
        self.archive = Package(data)
        container = ElementTree.fromstring(self.archive.read("META-INF/container.xml"))
        rootfile = container.find(".//" + CONTAINER + "rootfile")
        self.opf_path = rootfile.get("full-path")
        self.base = posixpath.dirname(self.opf_path)
        opf = ElementTree.fromstring(self.archive.read(self.opf_path))
        metadata = opf.find(OPF + "metadata")
        # A title broken across lines (CCEL's) is one line.
        self.title = re.sub(r"\s+", " ", metadata.findtext(DC + "title") or "").strip()
        self.creators = [creator.text.strip() for creator in metadata.findall(DC + "creator") if creator.text]
        self.language = (metadata.findtext(DC + "language") or "").strip()
        manifest = {}
        for item in opf.find(OPF + "manifest"):
            manifest[item.get("id")] = (item.get("href"), item.get("media-type"), item.get("properties") or "")
        self.spine = []
        for itemref in opf.find(OPF + "spine"):
            href, media, properties = manifest[itemref.get("idref")]
            if media == "application/xhtml+xml" and "nav" not in properties.split():
                self.spine.append(posixpath.normpath(posixpath.join(self.base, href)))

    def document(self, path: str) -> ElementTree.Element:
        return ElementTree.fromstring(self.archive.read(path))


def body_of(document: ElementTree.Element) -> ElementTree.Element:
    return document.find(XHTML + "body")


def document_kinds(body: ElementTree.Element) -> list:
    kinds = list(epub_types(body))
    for child in body:
        kinds += epub_types(child)
    return kinds


def collect_notes(epub: EpubBook) -> tuple:
    """Every endnote by its document and id, and the documents that hold them."""
    notes = {}
    note_documents = set()
    for path in epub.spine:
        body = body_of(epub.document(path))
        kinds = document_kinds(body)
        if not any(kind in NOTE_TYPES for kind in kinds):
            continue
        note_documents.add(path)
        for item in body.iter(XHTML + "li"):
            if item.get("id"):
                notes[(path, item.get("id"))] = item
        for item in body.iter(XHTML + "aside"):
            if item.get("id"):
                notes[(path, item.get("id"))] = item
    # Notes kept in the chapter that calls them (Wikisource's exports: an <ol> of <li
    # epub:type="footnote"> at the chapter's end).
    for path in epub.spine:
        if path in note_documents:
            continue
        for item in body_of(epub.document(path)).iter():
            if "footnote" in epub_types(item) and item.get("id"):
                notes[(path, item.get("id"))] = item
    return notes, note_documents


def footnote_list(element: ElementTree.Element) -> bool:
    """A list of footnotes standing in the chapter (Wikisource's references): set where
    each is called, not again where the list stands."""
    items = [child for child in element if local(child.tag) == "li"]
    return bool(items) and all("footnote" in epub_types(item) for item in items)


def heading_text(element: ElementTree.Element) -> str:
    text = "".join(element.itertext())
    for character in INVISIBLE:
        text = text.replace(character, "")
    return re.sub(r"\s+", " ", text).strip()


class Walker:
    """Turns one spine document's body into records."""

    def __init__(self, writer: Writer, epub: EpubBook, path: str, notes: dict) -> None:
        self.writer = writer
        self.book = writer.book
        self.epub = epub
        self.path = path
        self.notes = notes
        self.titled = False
        self.in_note = False

    # ---- blocks ----------------------------------------------------------------------

    def blocks(self, element: ElementTree.Element, quote: bool = False) -> None:
        for child in element:
            self.block(child, quote)

    def block(self, element: ElementTree.Element, quote: bool) -> None:
        tag = local(element.tag)
        kinds = epub_types(element)
        # Project Gutenberg's own header and licence, and the lines between them and the
        # book, are the distributor's, not the book's.
        if boilerplate(element):
            self.book.dropped["Project Gutenberg's header, licence and separators"] += 1
            return
        if tag in ("h1", "h2", "h3", "h4", "h5", "h6") or (tag == "hgroup"):
            text = heading_text(element)
            if not text:
                return
            level = "0" if not self.titled else ("1" if tag in ("h1", "h2", "h3") else "2")
            self.titled = True
            self.book.add("S", level, text)
        elif tag == "header":
            self.blocks(element, quote)
        elif tag == "p":
            self.writer.paragraph("quote" if quote else "p")
            self.inlines(element)
            self.writer.trim()
        elif tag == "blockquote" or tag == "div" and "z3998:poem" in kinds:
            if any(kind in ("z3998:poem", "z3998:verse", "z3998:song", "z3998:hymn") for kind in kinds):
                self.poem(element)
            else:
                self.blocks(element, True)
        elif tag == "hr":
            self.book.add("P", "blank")
            self.book.dropped["<hr> (a break between sections, set as space)"] += 1
        elif tag in ("ol", "ul") and footnote_list(element):
            self.book.head_counts["footnotes set where they are called, not in the chapter's list"] += 1
        elif tag in ("ol", "ul"):
            number = 0
            for item in element:
                if local(item.tag) != "li":
                    continue
                number += 1
                self.writer.paragraph("list1")
                if tag == "ol":
                    self.writer.text("%d. " % number)
                if any(local(child.tag) in ("p", "div", "blockquote") for child in item):
                    for child in item:
                        if local(child.tag) == "p":
                            self.inlines(child)
                            self.writer.text(" ")
                        else:
                            self.block(child, quote)
                else:
                    self.inlines(item)
                self.writer.trim()
        elif tag == "table":
            for row in element.iter(XHTML + "tr"):
                cells = [heading_text(cell) for cell in row if local(cell.tag) in ("td", "th")]
                if any(cells):
                    self.writer.paragraph("flush")
                    self.writer.text(" ".join(cell for cell in cells if cell))
            self.book.dropped["tables (each row set as a line)"] += 1
        elif tag in ("img", "svg", "figure") and not any(local(child.tag) == "figcaption" for child in element):
            self.book.dropped["pictures"] += 1
        elif tag == "figure":
            self.book.dropped["pictures"] += 1
            for child in element:
                if local(child.tag) == "figcaption":
                    self.writer.paragraph("centre")
                    self.inlines(child)
                    self.writer.trim()
        elif tag == "pre":
            for line in "".join(element.itertext()).split("\n"):
                if line.strip():
                    self.writer.paragraph("flush")
                    self.writer.text(line.strip())
        elif tag in ("nav", "aside") and any(kind in NOTE_TYPES or kind == "footnote" for kind in kinds):
            pass
        elif tag in ("section", "article", "div", "main", "footer", "dl", "dd", "dt", "address", "center"):
            self.blocks(element, quote)
        elif tag == "br":
            pass  # spacing between blocks: no text to carry
        elif tag in ("span", "a", "em", "i", "b", "strong", "abbr", "cite", "small", "dfn", "var", "q", "u", "sup",
                     "sub", "tt", "code", "font", "big", "ins", "mark", "label", "time", "s"):
            # Inline content standing in a block's place: a paragraph of its own.
            self.writer.paragraph("p")
            self.inline(element)
            self.writer.trim()
        else:
            self.book.dropped["<%s> (not carried)" % tag] += 1
        if element.tail and element.tail.strip():
            self.writer.paragraph("p")
            self.writer.text(element.tail)
            self.writer.trim()

    def poem(self, element: ElementTree.Element) -> None:
        """A poem: each <p> a stanza, each line a <span> or a run between <br/>s; a span
        with class i1, i2... is indented."""
        stanzas = [child for child in element if local(child.tag) == "p"] or [element]
        for number, stanza in enumerate(stanzas):
            if number > 0:
                self.book.add("P", "blank")
            line_open = False
            if stanza.text and stanza.text.strip():
                self.writer.paragraph("q1")
                self.writer.text(stanza.text)
                line_open = True
            for child in stanza:
                tag = local(child.tag)
                if tag == "br":
                    if line_open:
                        self.writer.trim()
                    line_open = False
                elif tag == "span" and not line_open:
                    classes = (child.get("class") or "").split()
                    indent = 0
                    for name in classes:
                        if re.fullmatch(r"i\d+", name):
                            indent = int(name[1:])
                    self.writer.paragraph("q2" if indent > 0 else "q1")
                    self.inline(child, tail=False)
                    line_open = True
                else:
                    if not line_open:
                        self.writer.paragraph("q1")
                        line_open = True
                    self.inline(child, tail=False)
                if child.tail and child.tail.strip():
                    if not line_open:
                        self.writer.paragraph("q1")
                        line_open = True
                    self.writer.text(child.tail)
            if line_open:
                self.writer.trim()

    # ---- inline ----------------------------------------------------------------------

    def inlines(self, element: ElementTree.Element) -> None:
        if element.text:
            self.writer.text(element.text)
        for child in element:
            self.inline(child)

    def inline(self, element: ElementTree.Element, tail: bool = True) -> None:
        tag = local(element.tag)
        kinds = epub_types(element)
        classes = (element.get("class") or "").split()
        style = None
        if tag in ("em", "i", "cite", "var", "dfn"):
            style = "italic"
        elif tag in ("strong", "b"):
            style = "bold"
        if tag == "a" and ("noteref" in kinds or element.get("role") == "doc-noteref"):
            self.note(element)
        elif tag == "a" and ("backlink" in kinds or element.get("role") == "doc-backlink"):
            pass
        elif tag == "span" and "mw-cite-backlink" in classes:
            pass  # Wikisource's "↑" back to the call
        elif tag == "br" and self.in_note:
            # A footnote holds no paragraphs or lines of its own: the break is a space.
            self.book.head_counts["line breaks inside a footnote read as a space"] += 1
            self.writer.text(" ")
        elif tag == "br":
            self.writer.trim()
            self.writer.paragraph("flush")
        elif tag == "span" and ("pagenum" in classes or "tei-pb" in classes or "pagebreak" in kinds):
            self.book.dropped["page numbers of the printed edition"] += 1
        elif tag in ("img", "svg"):
            self.book.dropped["pictures"] += 1
        else:
            if style:
                self.book.add("Y", style, "1")
            self.inlines(element)
            if style:
                self.book.add("Y", style, "0")
        if tail and element.tail:
            self.writer.text(element.tail)

    def note(self, anchor: ElementTree.Element) -> None:
        href = anchor.get("href") or ""
        target_path, _, target_id = href.partition("#")
        path = posixpath.normpath(posixpath.join(posixpath.dirname(self.path), target_path)) if target_path else self.path
        note = self.notes.get((path, target_id))
        if note is None:
            self.writer.text(heading_text(anchor))
            self.book.dropped["note references without their note"] += 1
            return
        label = heading_text(anchor).strip("[]") or "*"
        if self.in_note:
            # A note on a note (an editor's comment on the author's note, as in Gibbon):
            # the reader sets no note inside a note, so it stands in brackets in its own.
            self.book.dropped["notes on notes (each set in brackets inside its note)"] += 1
            self.writer.text(" [")
            first = True
            for child in note:
                if local(child.tag) in ("p", "div", "blockquote"):
                    if not first:
                        self.writer.text(" ")
                    self.inlines(child)
                    first = False
            if first:
                self.inlines(note)
            self.writer.text("]")
            return
        self.book.add("N", label)
        self.in_note = True
        first = True
        for child in note:
            if local(child.tag) in ("p", "div", "blockquote"):
                if not first:
                    self.writer.text(" ")
                self.inlines(child)
                first = False
        if first:
            self.inlines(note)
        self.writer.trim()
        self.in_note = False
        self.book.add("E")


def boilerplate(element: ElementTree.Element) -> bool:
    """Project Gutenberg's own header, licence and "*** START/END OF ***" lines."""
    return "pg-boilerplate" in (element.get("class") or "").split() or (element.get("id") or "") in (
        "pg-header", "pg-footer", "pg-start-separator", "pg-end-separator")


def first_heading(element: ElementTree.Element) -> str:
    """The text of the first heading in a document, outside Gutenberg's boilerplate."""
    if boilerplate(element):
        return ""
    if local(element.tag) in ("h1", "h2", "h3", "h4", "h5", "h6"):
        return heading_text(element)
    for child in element:
        text = first_heading(child)
        if text:
            return text
    return ""


def import_epub(data: bytes, book: typing.Any, division_id: str) -> None:
    """Appends the EPUB's records to `book` (an import_books.Book) as one division."""
    epub = EpubBook(data)
    writer = Writer(book)
    title = epub.title or division_id
    book.head.append(("title", title))
    if epub.creators:
        book.head.append(("author", "; ".join(epub.creators)))
    if epub.language:
        book.head.append(("language", epub.language))
    book.add("B", division_id, title, title)
    notes, note_documents = collect_notes(epub)
    for path in epub.spine:
        body = body_of(epub.document(path))
        kinds = document_kinds(body)
        if path in note_documents:
            book.dropped["endnote documents (their notes are set at the foot of the page)"] += 1
            continue
        if any(kind in SKIPPED_TYPES for kind in kinds):
            book.dropped["the publisher's pages (title page, imprint, colophon, licence)"] += 1
            continue
        if path.endswith(".about.html") and "Christian Classics Ethereal Library" in "".join(body.itertext()):
            # CCEL's own title page and its notice on its edition; the text is the book's.
            book.dropped["the publisher's pages (title page, imprint, colophon, licence)"] += 1
            continue
        # The chapter is named by its first heading.
        book.add("C", first_heading(body))
        walker = Walker(writer, epub, path, notes)
        walker.blocks(body)
