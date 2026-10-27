# Pocket Library

The brief. It follows the owner's direction of 2026-10-15, recorded with its
reconciliation in the private planning repository `falseywinchnet/pocket-bible`
(`planning/OWNER_DIRECTION_2026-10-15.md`, `planning/RECONCILIATION_004.md`).

## The game

A bookcase of good books to read, each set as a printed book on India paper. Choose a
book from the shelf and read it a spread at a time; it keeps your place.

## Rule 2

New to the collection: not a game but a reading room, because reading can be just as
fun and is just as useful as playing games. Its decisions are the Pocket Bible's: the
books are set as books (justified, a drop numeral at a chapter, notes at the foot of
the page), not as a scrolling text, on paper you notice only when you look for it.

## A session

The first minute: the shelf, a book taken down, the first spread. A session is as long
as the reader likes. Returning, the reader finds the book they had open, at the spread
they left.

## Replayability

A library is read, not replayed. Every book keeps its own place. The shelves hold 746
books on fourteen planks: Scripture, Rabbinic writings, Church Fathers and Reference (our
own importers, `tools/import_books.py`, `sefaria_reader.py`, `lexicon_reader.py` and
`usfm_reader.py`), Philosophy, History, Society and Science, and the novels, plays,
poems and children's books. Batches 1 (336 books) and 2 (111, non-fiction and the Greek
and Latin classics) were found and checked by workers, pinned in
`tools/sources.lock.json` and catalogued by `tools/add_batch.py`. Batch 3 (253: the
Greek and Roman authors, the Nicene and Post-Nicene Fathers, the Jerusalem Talmud, the
Mishneh Torah, Josephus, the Shinto chronicles and the owner's named works) was chosen
from a list of candidates and catalogued by `tools/add_selection.py`, the books coded as
block 3; Perseus' TEI is read by `perseus_reader.py`. The mathematical books (Euclid,
Newton's Principia and Opticks, Proclus on Euclid) are read as text; their figures wait
for the reader to print them. Block 4 (21) took what had no clean source then, from Perseus and
from the Internet Classics Archive (`mit_reader.py`). Still waiting, for want of a clean
text anyone can fetch: Legge's Texts of Taoism (SBE 39 and 40), Jacobi's Jaina Sutras
(SBE 22 and 45), the Yengishiki, Mead's Thrice-Greatest Hermes and Taylor's Orphic Hymns
(clean only on sacred-texts.com, which refuses scripts), Hegel's Phenomenology,
Philosophy of History and Philosophy of Right, Aristotle's On the Soul (Smith, 1931: US
public domain in 2027), Diodorus, Eckermann, Martial, Propertius, Herodas, Vegetius,
Menander and the Greek mathematicians (scans only), and Faulkner's later books (not yet
public domain).

The books ship in parts (`src/container.hpp`): a base and blocks of books coded against
it, listed in `assets/books/parts.txt` and pinned in `pinned_assets.json`. The parts are
not in Git: `tools/publish_library.py` publishes them to the `library-data` release, and
the asset preparation fetches and checks them for every build. A new batch of books is a
new block (COMPRESSION.md, "Reproducing"). The shelf index, `assets/books/shelf.txt`, is
made from the catalogue (`tools/import_books.py --shelf`) and orders, letters and binds
every book.

Licences: most books are public domain; some are CC BY or CC BY-NC (allowed: the Pocket
Library is free and never sold) and carry their credit on the book's About card. Share-alike
books are allowed too: each stands on its own plank at the end of its category with a
green label low on the spine, and none is merged into another book.

## Passive mode

None is needed: nothing moves unless the reader turns a page.

## Rules

There are none to referee. What must hold: the text of every edition is exactly its
source's, read from a hash-pinned file by a deterministic importer
(`tools/import_books.py`); every correction of a source's own defects is recorded
(`tools/bsb_corrections.tsv`, `tools/avot_corrections.tsv`, and the counted rules in
each book's head); the library file checks every book against its SHA-256 when it is
opened.

## Look, sound and words

The shelf is PlaySuite's, whose shell the Pocket Library shares: its walnut wall and oak plank. The books stand spine out,
bound in leather or cloth and lettered in gold. A book opens to a spread on the paper
engine's India paper (#f0ece0) with a soft shadow into the gutter. The Bible is set in
EB Garamond, every other book in Literata, in warm black ink, the words of Christ in
red. A page turned rustles softly: the leaf's edge drawn over the page beneath, its band
measured from recordings, in ten variations so quick reading never repeats one, crossing
the way the page goes (`audio_src/make_page.py`, chosen by ear). A book taken down and put
back is still to come. No music.

## Controls

Pointer: click a spine to open a book; click the right page to turn forward, the left
to turn back; the wheel turns pages. Keyboard: arrows choose a book on the shelf and
Enter opens it; in a book, arrows, Page Up, Page Down and Space turn pages, C shows
the contents, Escape closes the contents or the book. Capsule: Shelf, Contents,
Previous page, Next page, Help. Settings: the type size.

## Not included

- Search of any kind: the owner removed it (2026-10-15).
- Notes, commentary and study aids beyond what each edition prints itself.
- Hebrew and Greek script: the library sets English only for now; Strong's words are
  given in transliteration.
- Books still under copyright: only what may be copied whole is shelved.
