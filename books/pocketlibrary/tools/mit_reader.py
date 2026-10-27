"""Reads the Internet Classics Archive's plain texts (classics.mit.edu, `<work>.mb.txt`)
into the library's plain form (import_books.py): the Oxford translations of Aristotle's
works on nature.

Each file is one work, a division of the book. Its head names the work, its author and
its translator ("Translated by ..."); a line of hyphens ends the head and separates the
books. "BOOK I" opens a chapter (a work without books is one chapter), "Part 1" is a
section heading, and paragraphs are separated by blank lines, their lines joined by a
space. "THE END" and everything after the last line of hyphens (the Archive's notice on
its presentation) are left out, counted.
"""

import html
import re
import typing

RULE = re.compile(r"^-{20,}\s*$")
BOOK = re.compile(r"^BOOK [IVXLC]+\s*$")
PART = re.compile(r"^Part \d+\s*$")


def import_mit(documents: typing.List[bytes], book: typing.Any) -> None:
    """Appends each document as a division."""
    for data in documents:
        text = data.decode("utf-8").replace("\r\n", "\n")
        if "<pre>" in text:
            # Some single-book files are served inside an old saved page, the text intact
            # in its <pre>.
            text = html.unescape(text[text.index("<pre>") + 5:text.rindex("</pre>")])
            book.dropped["a saved page around the text"] += 1
        lines = text.split("\n")
        rules = [n for n, line in enumerate(lines) if RULE.match(line)]
        if not rules:
            raise ValueError("an MIT text without its foot rule")
        # The head ends at its rule, or (a work in one book) at the first heading.
        start = next((n for n, line in enumerate(lines) if PART.match(line.strip()) or BOOK.match(line.strip())),
                     rules[0])
        end_of_head = rules[0] if len(rules) > 1 and rules[0] < start else start
        head = [line.strip() for line in lines[:end_of_head] if line.strip()]
        body = lines[end_of_head + (1 if end_of_head == rules[0] else 0):rules[-1]]
        book.dropped["the Archive's head and its notice on its presentation"] += 1
        title = head[3] if len(head) > 3 else "Untitled"
        for line in head:
            if line.startswith("Translated by "):
                book.head.append(("translator", line[len("Translated by "):]))
        book.add("B", re.sub(r"[^A-Za-z0-9]+", "-", title).strip("-")[:40], title, title)
        chaptered = False
        paragraph: typing.List[str] = []

        def flush() -> None:
            if paragraph:
                book.add("P", "p")
                book.text(" ".join(paragraph))
                paragraph.clear()

        for raw in body:
            line = raw.strip()
            if RULE.match(raw):
                flush()
            elif BOOK.match(line):
                flush()
                book.add("C", line.title())
                chaptered = True
            elif PART.match(line):
                flush()
                if not chaptered:
                    book.add("C", "")
                    chaptered = True
                book.add("S", "1", line)
            elif line == "THE END":
                flush()
                book.dropped["\"THE END\""] += 1
            elif not line:
                flush()
            else:
                if not chaptered:
                    book.add("C", "")
                    chaptered = True
                paragraph.append(re.sub(r"\s+", " ", line))
        flush()
    book.head.append(("works", len(documents)))
