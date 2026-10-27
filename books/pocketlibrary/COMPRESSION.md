# How the library is stored: the study

Every book ships with the application, in parts: a base and blocks in `assets/books/`
(`src/container.hpp`; see Reproducing below). The owner's budget for the whole library is about 128 MB. This is
the record of how the file came to be made the way it is, with the numbers that chose
each part, the losers included. Numbers are from the M4 unless marked; this Mac's
timings move by twice under load and were used only for sizes.

## The pipeline

1. **Normalized records** (`book.hpp`): what the importers write and the reader reads.
2. **Packing** (`text_pack.hpp`): tables learned once from the whole library turn the
   records into denser bytes. Each record becomes a control byte. A verse or chapter
   that counts on from the one before becomes one byte. Eight common characters outside
   ASCII get one byte each. 73,815 words become one-, two- or three-byte codes, each
   carrying the space after it. A capitalized or all-capital word is a flag byte plus its
   lowercase code. Exact for any records, including words the tables never saw.
3. **The base**: some books' packed texts end to end, coded as one stream. These are the
   Bible, the Tanakh, then a cross-section of the shelf (every so many books) to 16 MB.
4. **The coder** (`lz_coder.hpp`): LZ77 over the whole window with an optimal parse,
   coded bit by bit with adaptive probabilities by a range coder, in the manner of LZMA.
   Every book outside the base continues the base's trained model and may refer back
   into the base's text.
5. **Checks**: each book's text by its SHA-256, and the tables and the base by theirs.

Reading:
- The tables and the base are decoded once, on a GUI.Forms worker while the shelf is
  shown.
- A book inside the base is a slice of it.
- Any other book is decoded alone, reading back into the base where it lies, never
  copying it.

## The whole library

344 books, 224,794,551 bytes of normalized text. That is our own 8 and the 336 trial
imports of batch 1 (`.build/library/trial`).

| | bytes |
|---|---|
| packed | 92,961,498 (41.4%) |
| the base, packed | 15,999,082 |
| the base, coded | 8,565,161 |
| the other books, coded | 40,803,608 |
| **the file** | **49,656,183 (22.09% of the text)** |

Reading, M4:

| step | time or speed |
|---|---|
| prepared (once, on a worker) | 224 ms: base decoded 205, tables 11, base checked 7 |
| books inside the base | 340 MB/s of text (unpack and check) |
| other books | 116 MB/s of text (decode, unpack, check) |
| SHA-256 | 2.4 GB/s (Arm instructions; 138 MB/s portable, on this Mac) |
| a novel of 700 KB of text | about 6 ms |

Building it takes 44 s on the M4. A code cache keyed by the coder's format, the effort
and the base's digest re-codes only what changed. A change to any book changes the
learned tables, though, and so every packed text.

The shipped file, our own 8 books:
- v2 (each book coded alone): 4,243,176 bytes.
- v3: 3,685,235 bytes. All 8 books fit in the base, so after the one prepare each
  opens without decoding.

## The base's size

Measured over the whole library, with the base filled by stride before the second
filling pass:

| base, packed | file | prepared in |
|---|---|---|
| 3,374,202 (the Bibles only) | 52,141,067 | 46 ms |
| 6,493,948 | 50,475,895 | 98 ms |
| 15,902,658 | 49,471,183 | 236 ms |
| 21,281,289 | 49,249,278 | 310 ms |
| 31,995,901 | 49,148,463 | 444 ms |

16 MB is the knee. Beyond it each megabyte saves about 20 KB and costs about 13 ms at
every start.

## The coder, step by step

The sample: `.build/library/study/mini-p`, 11 books (5,512,700 packed bytes), coded
after an 8 MB base (`base-8m.bin`) at effort 48, using `tools/base_lab.cpp`. Decode
times are the best of five.

| change | base coded | books coded | base decoded | books decoded |
|---|---|---|---|---|
| version 3 as first written | 3,356,329 | 2,466,897 | 104 ms | 68 MB/s |
| raw distance bits sixteen at a time (one division); short matches copied by words | 3,356,338 | 2,466,908 | 88 ms | 81 MB/s |
| align bits 6 (LZMA has 4) | 3,357,432 | 2,467,445 | 95 ms | 76 MB/s |
| align bits 5 | 3,357,175 | 2,467,342 | 88 ms | 79 MB/s |
| align bits 3 | 3,356,958 | 2,467,669 | 86 ms | 82 MB/s |
| align bits 2 | 3,356,289 | 2,466,412 | 78–84 ms | 84–89 MB/s |
| align bits 1 | 3,355,148 | 2,465,482 | 78 ms | 91 MB/s |
| **align bits 0** | 3,354,756 | 2,465,567 | 79 ms | 91 MB/s |
| modelled distance slots to 4 | 3,353,555 | 2,465,224 | 72 ms | 96 MB/s |
| … to 6 | 3,354,379 | 2,464,823 | 73 ms | 95 MB/s |
| **… to 8** | 3,354,271 | 2,464,984 | 73 ms | 95 MB/s |
| … to 10 | 3,353,661 | 2,465,769 | 74 ms | 95 MB/s |
| … to 12 | 3,355,109 | 2,466,127 | 73 ms | 96 MB/s |
| **no matched literals** | **3,349,822** | **2,463,818** | **72 ms** | **97 MB/s** |
| literal context plus 1 bit of the byte before | 3,358,660 | 2,459,300 | 70 ms | 96 MB/s |
| … plus 2 bits | 3,367,293 | 2,466,909 | 68 ms | 98 MB/s |
| a counted literal loop | same | same | 72 ms | 96–97 MB/s |

In this packed text a far distance's low bits are as good as random, so modelling them
only costs. After a match, LZMA's matched literal bets that the next byte differs from
the byte the match would have given. With word codes that bet does not pay. The literal
context stays the whole previous byte; earlier, fewer bits of it were worse too.

Where the decoder's time goes, over the books outside the base:
- 291 million modelled bits;
- 101 million raw bits (11.2 a match, before they were taken sixteen at a time);
- 15.9 million literals and 9.0 million matches, averaging 6.8 bytes;
- 260 thousand repeats.

What is left is the serial chain of binary decisions.

Encoding, the same lab run (5 decodes included, about 0.65 s):

| change | time |
|---|---|
| version 3 as first written | 5.68 s |
| a tree node's two children in one cache line; the next walk's hash head prefetched | 5.30 s |
| matches compared eight bytes at a time | 5.10 s |
| both children of the node being compared prefetched | 4.74–4.94 s (A/B: 4.78–5.07 without) |
| a candidate's distance price computed once, not per length | no change measured |

The search effort (tree nodes per byte) hardly matters. Effort 24 is 0.2% larger than
48; efforts 96 and 192 are within 0.05% of it. The time is the same, because it goes
to cache misses, not steps. The default stays 48.

## Earlier steps (before this record was begun)

On the 11-book sample (10.75 MB of normalized text), each book coded alone:

| | coded |
|---|---|
| greedy coder, hash chains at depth 64 | 3,301,923 (decode 33 MB/s) |
| xz -9e | 3,003,880 |
| records packed | 3,271,909 |
| … and characters one byte each | 3,254,444 |
| … and words | 2,864,874 |
| … words with 32 two-byte and 1 three-byte prefix | 2,766,839 |
| … hash chains at depth 4096 | 2.675 M (xz -9e over the same packed text: 2.670 M) |
| … the binary tree at depth 32 | 2,640,126 (encode 2.5 s, against 15.7 s for chains at 1024) |
| … a space implied after every word code | 2,559,743 |
| … one-letter words, a word's worth `count × (2 × length − 1)` | 2,545,332 |
| … 8 one-byte characters, 32 two-byte prefixes, 1 three-byte | 2,538,928 |

Other settings measured then:
- Range coder probabilities moving a 16th or a 64th of the way: both worse than a 32nd.
- Parse spans of 512 to 8192: flat.
- A branchless bit decode: 31 → 36 MB/s.

Rejected:
- **A context-mixing coder** (`text_coder.hpp`, kept as a reference): 2.43 MB on the
  packed sample, about 9% under the LZ coder, but it decodes at 0.17 MB/s. A large book
  would take most of a minute to open.
- **Mixing two literal models inside the LZ coder**: 0.3% smaller, and decoding fell to
  4 MB/s.
- **A primer, for zstd**: 5.6% smaller. The base does the same work, better, in our own
  coder.

## Not done, and why

- **Literals as two 4-bit symbols, slots as one 64-way symbol** (multi-symbol adaptive
  coding, as LZNA does). This could make decoding perhaps 15–30% faster. It means a new
  coder and new pricing. Books already open in milliseconds, and the base is decoded off
  the main thread, so it waits until something needs it.
- **Reduced-offset LZ** (distances as an index among the earlier places sharing a
  context). This would shrink the raw distance bits, a third of the code, at the cost of
  a table update per decoded byte. The file is at 39% of its budget, so it is not
  needed now.
- **The tables learned anew with every library change**. Any change to any book changes
  every packed text, so a build re-codes everything (44 s on the M4). Fixed tables,
  learned once and extended, would let the cache serve unchanged books. Worth doing when
  books are added in batches.

## Reproducing

The library ships in parts (`src/container.hpp`, format 4): a base and numbered blocks.
The base holds the packing tables and the base text, learned and chosen once from every
book of the library then (a generation) and frozen; each block holds books coded against
that base. Adding a batch of books makes one new block (`tools/add_batch.py` gives them
the next block number), so nothing already published is coded, uploaded or downloaded
again. A block made against another base is refused when it is opened. The shelf index
(`assets/books/shelf.txt`, from the catalogue: `import_books.py --shelf`) orders, names
and binds the books, and says which block holds each one; reshelving codes nothing.

The parts are not kept in Git: `tools/publish_library.py` publishes each to the
repository's `library-data` release, named by its content, pins it in
`pinned_assets.json` and lists it in `assets/books/parts.txt`; builds fetch and check
the pinned parts (`tools/pinned_assets.py`, run by the asset preparation) and ship them
inside the application. Commit the pins and the list, never a part.

The first generation (2026-10-18), 472 books, 433,189,357 bytes of text:

| part | books | bytes |
|---|---|---|
| base (tables and base text, prepared in 297 ms on this Mac) | 43 inside it | 7,616,891 |
| block 1, the first shelves | 313 more | 45,018,877 |
| block 2, the reference shelves | 116 more | 33,083,464 |
| **all three** | **472** | **85,719,232 (19.79% of the text)** |

Every book read back byte for byte. Block 2 coded again alone against the published base
is the same file, byte for byte.

Block 3 (2026-10-19), the first coded after the generation was frozen: 253 books (the
Greek and Roman authors, the Nicene and Post-Nicene Fathers, the Jerusalem Talmud, the
Mishneh Torah and the owner's named works), 294,212,184 bytes of text in 60,145,047
(20.44%, against 19.79% for the generation: the frozen tables cost about half a point).
The library is then 725 books in 145,864,279 bytes.

The importers learned more for block 3: Wikisource's chapter footnotes, CCEL's title page,
Perseus' TEI (`perseus_reader.py`), Sefaria's links, and Hebrew given in Latin letters and
Japanese, Arabic and Syriac characters left out wherever they stand (the faces have
none). Blocks 1 and 2 were made before that; a block is never re-made unless its own books
change, and then it is re-imported with the importers as they stand.

Block 4 (2026-10-19): 21 books of the Greeks and Romans that had no clean source for block
3, from Perseus' TEI (Diogenes Laertius, Demosthenes complete, Heath's Euclid,
Apollodorus, Claudian, Ausonius, Phaedrus, Columella, the second volume of Hippocrates
and others) and the Internet Classics Archive's Oxford Aristotle (`mit_reader.py`: the
Physics and the works on nature), 11,680,270 bytes of text in 2,307,186 (19.75%). The
library is then 746 books in 148,171,465 bytes. The one-file library it replaces (format 3) was
85,802,857 bytes; the shelf records it carried now live in the shelf index.

```sh
# a new generation: tables, base and every block (from the shelf index's block numbers)
make_library --cache <dir> [--base-mb 16] [--effort 48] generation <out dir> <normalized dir>
# one block, against the published base (a new batch, or a fixed book's block)
make_library --cache <dir> block assets/books/base-<hash>.plb <number> <out dir> <normalized dir>
# publish and pin them (then commit pinned_assets.json and assets/books/parts.txt)
python3 tools/publish_library.py <out dir> [part ...]
# the shelf index alone, after editing the catalogue
python3 tools/import_books.py --shelf assets/books/shelf.txt
# the coder alone, after a base
base_lab <folder of packed books> <base file> <effort>
# the packer alone
pack_lab <out folder> <folder>...
```

The tools build with the library's sources alone (`tools/*.cpp` plus `src/book.cpp`, `library.cpp`,
`container.cpp`, `lz_coder.cpp`, `text_pack.cpp` and `sha256.cpp`), with no GUI.Forms.
