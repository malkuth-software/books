"""Reads the lexicons of the Reference shelf into the library's plain form (import_books.py).

Two sources, both pinned in sources.lock.json:

- STEPBible's brief lexicons, TBESG (Greek) and TBESH (Hebrew), CC BY 4.0: one entry a
  line, TAB-separated, its definition in a small HTML (<b>, <i>, <br>, <ref=...>, and
  "__1." for a numbered sense).
- Abbott-Smith's Manual Greek Lexicon of the New Testament (1922, public domain), in the
  TEI of the translatable-exegetical-tools project.

The library's faces set Greek (polytonic) but no Hebrew, so a Hebrew word is given by
its transliteration (SBL general-purpose style, the Pocket Bible's choice, L007 B); every
such change is counted in the book's head, as the other importers count what they drop.
"""

import html
import re
import typing
import unicodedata
import xml.etree.ElementTree as ElementTree

# ---- Hebrew in Latin letters ---------------------------------------------------------

CONSONANTS = {
    "א": "ʾ", "ב": "v", "ג": "g", "ד": "d", "ה": "h", "ו": "v", "ז": "z", "ח": "ḥ", "ט": "ṭ", "י": "y",
    "כ": "kh", "ך": "kh", "ל": "l", "מ": "m", "ם": "m", "נ": "n", "ן": "n", "ס": "s", "ע": "ʿ", "פ": "f",
    "ף": "f", "צ": "ts", "ץ": "ts", "ק": "q", "ר": "r", "ש": "sh", "ת": "t",
}
# A dagesh hardens the letters that soften (begadkefat) in this style.
HARD = {"ב": "b", "כ": "k", "ך": "k", "פ": "p", "ף": "p"}
VOWELS = {
    "ְ": "e", "ֱ": "e", "ֲ": "a", "ֳ": "o", "ִ": "i", "ֵ": "e", "ֶ": "e",
    "ַ": "a", "ָ": "a", "ֹ": "o", "ֺ": "o", "ֻ": "u", "ׇ": "o",
}
DAGESH, SHIN_DOT, SIN_DOT = "ּ", "ׁ", "ׂ"
CANTILLATION = re.compile("[֑-ֽֿ֯׀׃-׆]")


def hebrew_in_latin(word: str) -> str:
    """A rough transliteration of a (pointed or unpointed) Hebrew word: consonants and
    vowels as written, a silent shewa and a final silent aleph or he dropped, a vowel
    letter (holem or shureq vav, hireq yod) read as its vowel."""
    text = CANTILLATION.sub("", unicodedata.normalize("NFD", word))
    out = []
    letters = []
    for char in text:
        if "א" <= char <= "ת":
            letters.append([char, ""])
        elif letters:
            letters[-1][1] += char
    for index, (letter, marks) in enumerate(letters):
        last = index == len(letters) - 1
        if letter == "ו" and marks in ("ֹ", DAGESH) and out:
            out.append("o" if marks == "ֹ" else "u")
            continue
        if letter == "ו" and marks == DAGESH and not out:
            out.append("u")
            continue
        if letter == "י" and not marks and out and out[-1] in ("i", "e"):
            continue
        # Aleph and ayin are not written in this style; a final he after a vowel is (-ah).
        if letter in "אע":
            sound = ""
        elif letter == "ש":
            sound = "s" if SIN_DOT in marks else "sh"
        elif DAGESH in marks and letter in HARD:
            sound = HARD[letter]
        else:
            sound = CONSONANTS[letter]
        out.append(sound)
        for mark in marks:
            vowel = VOWELS.get(mark)
            if vowel is None:
                continue
            # A shewa at a word's end, or after a short vowel, is silent.
            if mark == "ְ" and (last or (len(out) > 1 and out[-2] in ("a", "e", "i", "o", "u"))):
                continue
            out.append(vowel)
    return "".join(out)


def hebrew_runs(text: str, book: typing.Any) -> str:
    """Hebrew words in running text, each replaced by its transliteration in italic marks
    the caller sets; counted in the book's head."""
    def replace(match: re.Match) -> str:
        book.dropped["Hebrew words in Hebrew letters (each kept as its transliteration)"] += 1
        return "\u0001" + hebrew_in_latin(match.group(0)) + "\u0002"
    return re.sub(r"[֑-״יִ-ﭏ]+(?:[־ ][֑-״יִ-ﭏ]+)*", replace, text)


def emit_text(text: str, book: typing.Any) -> None:
    """Text with transliterated Hebrew in it (marked by hebrew_runs): those parts italic."""
    for part in re.split("(\u0001[^\u0002]*\u0002)", text):
        if part.startswith("\u0001"):
            book.add("Y", "italic", "1")
            book.text(part[1:-1])
            book.add("Y", "italic", "0")
        elif part:
            book.text(part)


def squeeze_entry(book: typing.Any) -> None:
    """The source's layout leaves runs of spaces in an entry; they read as one, and an
    entry's lines begin without one."""
    index = len(book.records) - 1
    while index >= 0 and book.records[index][0] != "K":
        record = book.records[index]
        if record[0] == "T":
            text = re.sub(r" {2,}", " ", record[1])
            if book.records[index - 1][0] == "P":
                text = text.lstrip(" ")
            book.records[index] = ("T", text)
        index -= 1


# ---- STEPBible's brief lexicons --------------------------------------------------------

STEP_TAG = re.compile(r"<(/?)\s*([a-zA-Z]+)\s*(=?'[^']*'|/)?\s*/?>", re.I)


def step_inline(text: str, book: typing.Any) -> None:
    """A TBES definition: <b>, <i>, <br> or <lb> (a new line of the entry), <re> (a note on
    synonyms, on its own line), wrappers whose text stands (<ref='...'> a reference as
    printed, <greek>, <author>, <date>, <a>, <note>, <def>, <corr>), and "__" before a
    numbered sense (dropped: the number stays)."""
    text = text.replace("__", "")
    position = 0
    for match in STEP_TAG.finditer(text):
        emit_text(hebrew_runs(html.unescape(text[position:match.start()]), book), book)
        closing, tag = match.group(1) == "/", match.group(2).lower()
        if tag == "b":
            book.add("Y", "bold", "0" if closing else "1")
        elif tag == "i":
            book.add("Y", "italic", "0" if closing else "1")
        elif tag in ("br", "lb") or (tag == "re" and not closing):
            # A line of the entry; <re> opens its note on synonyms.
            book.add("P", "entry")
        elif tag in ("ref", "re", "greek", "author", "date", "a", "note", "def", "corr", "olbsupport"):
            # Wrappers around text that is kept as it stands: a reference as printed, a
            # Greek word, a cited author or date, an editor's note or correction.
            pass
        else:
            raise ValueError("unknown markup <%s> in a STEPBible definition" % tag)
        position = match.end()
    emit_text(hebrew_runs(html.unescape(text[position:]), book), book)


def import_tbes(data: bytes, book: typing.Any, language: str) -> None:
    """One entry a line: EStrong, the disambiguated id, its Strong's link, the lemma, its
    transliteration, morphology, a gloss and the definition. Lines before the first
    entry are STEPBible's preface and licence, kept in the head's source record."""
    title = "Greek" if language == "G" else "Hebrew"
    book.add("B", title, "Brief Lexicon of Extended Strong's: " + title, title)
    count = 0
    for line in data.decode("utf-8-sig").splitlines():
        fields = line.split("\t")
        if len(fields) < 8 or not re.fullmatch(language + r"\d{4,5}[A-Za-z]?", fields[0].strip()):
            continue
        estrong, lemma, sound, morphology, gloss, definition = (fields[1].split(" ")[0].strip(), fields[3].strip(),
                                                                 fields[4].strip(), fields[5].strip(),
                                                                 fields[6].strip(), fields[7].strip())
        book.add("K", estrong)
        book.add("P", "entry")
        book.add("Y", "bold", "1")
        book.text(estrong + ".")
        book.add("Y", "bold", "0")
        book.text(" ")
        if language == "G":
            book.text(lemma)
            book.text(", ")
        else:
            book.dropped["Hebrew headwords in Hebrew letters (each given by STEPBible's transliteration)"] += 1
        book.add("Y", "italic", "1")
        # STEPBible marks a Hebrew transliteration's syllables with dots: a.ha.vah.
        book.text(sound.replace(".", "") if language == "H" else sound)
        book.add("Y", "italic", "0")
        book.text(" (" + morphology + "): " + gloss + ".")
        if definition:
            book.add("P", "entry")
            step_inline(definition, book)
        squeeze_entry(book)
        count += 1
    book.head.append(("entries", count))


# ---- Abbott-Smith ------------------------------------------------------------------------

TEI = "{http://www.crosswire.org/2013/TEIOSIS/namespace}"
QUIET = ("note", "pb", "lb")


def tei_text(element: ElementTree.Element, book: typing.Any) -> None:
    """An entry's TEI: Greek kept, Hebrew transliterated, emphasis as italic, the head
    word bold, tables read as lines; occurrence counts and page breaks dropped."""
    tag = element.tag.replace(TEI, "")
    if tag in QUIET:
        book.dropped["<%s> (an editor's count or the printed page's break)" % tag] += 1
        return
    styled = {"orth": "bold", "emph": "italic", "hi": "italic", "tns": "italic", "mood": "italic"}.get(tag)
    hebrew = tag == "foreign" and element.get("{http://www.w3.org/XML/1998/namespace}lang") == "heb"
    if styled:
        book.add("Y", styled, "1")
    if tag == "row":
        book.add("P", "entry")
    if hebrew:
        book.dropped["Hebrew words in Hebrew letters (each kept as its transliteration)"] += 1
        book.add("Y", "italic", "1")
        book.text(hebrew_in_latin("".join(element.itertext())))
        book.add("Y", "italic", "0")
    else:
        if element.text:
            emit_text(hebrew_runs(re.sub(r"\s+", " ", element.text), book), book)
        for child in element:
            tei_text(child, book)
            if child.tail:
                emit_text(hebrew_runs(re.sub(r"\s+", " ", child.tail), book), book)
    if tag == "cell":
        book.text(" ")
    if styled:
        book.add("Y", styled, "0")


def import_abbott_smith(data: bytes, book: typing.Any) -> None:
    root = ElementTree.fromstring(data)
    book.add("B", "Lexicon", "A Manual Greek Lexicon of the New Testament", "Abbott-Smith")
    count = 0
    for entry in root.iter(TEI + "entry"):
        key = (entry.get("n") or "").split("|")
        book.add("K", key[-1] if len(key) > 1 and key[-1] else key[0])
        book.add("P", "entry")
        for child in entry:
            tei_text(child, book)
            if child.tail:
                emit_text(hebrew_runs(re.sub(r"\s+", " ", child.tail), book), book)
        squeeze_entry(book)
        count += 1
    book.head.append(("entries", count))
