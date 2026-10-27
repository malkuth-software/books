// The library without pixels: the record reader, the coder, the library file and its
// checks, the BSB's versification and the bookmarks. No window, no fonts.
#include "book.hpp"
#include "container.hpp"
#include "library.hpp"
#include "lz_coder.hpp"
#include "text_pack.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

std::span<const std::uint8_t> bytes_of(const std::string& text) {
    return std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

// Two books of a small Bible, in the normalized form import_books.py writes.
const std::string sample_bible =
    "H\tsource\ttest\n"
    "B\tGEN\tGenesis\tGenesis\tGen\n"
    "M\t2\tGenesis\n"
    "C\t1\n"
    "S\t1\tThe Creation\n"
    "P\tp\n"
    "V\t1\n"
    "T\tIn the beginning God created the heavens and the earth.\n"
    "V\t2\n"
    "T\tNow the earth was formless and void.\n"
    "N\t1:2\n"
    "T\tOr \n"
    "Y\titalic\t1\n"
    "T\twaste\n"
    "Y\titalic\t0\n"
    "E\n"
    "C\t2\n"
    "P\tq1\n"
    "V\t1\n"
    "T\tThus the heavens were completed.\n"
    "B\tMRK\tThe Gospel According to Mark\tMark\tMark\n"
    "C\t1\n"
    "P\tp\n"
    "V\t1\n"
    "Y\twj\t1\n"
    "T\t\xe2\x80\x9c" "Follow Me,\xe2\x80\x9d\n"
    "Y\twj\t0\n"
    "T\the said.\n";

void test_records() {
    pl::Work work;
    check(pl::read_work("bsb", sample_bible, work), "the sample Bible reads");
    check(work.divisions.size() == 2, "two divisions");
    check(work.divisions[0].abbreviation == "Gen" && work.divisions[1].short_title == "Mark", "titles and abbreviations");
    const pl::Chapter& first = work.divisions[0].chapters[0];
    check(first.label == "1" && first.blocks.size() == 2, "a heading and a paragraph in Genesis 1");
    check(first.notes.size() == 1 && first.notes[0].label == "1:2", "the footnote is kept with its reference");
    check(first.notes[0].inlines.size() == 2 && (first.notes[0].inlines[1].flags & pl::span_italic) != 0,
          "the footnote's italic span");
    const pl::Block& paragraph = first.blocks[1];
    int verses = 0, calls = 0;
    for (const pl::Inline& item : paragraph.inlines) {
        verses += item.kind == pl::InlineKind::verse ? 1 : 0;
        calls += item.kind == pl::InlineKind::note_call ? 1 : 0;
    }
    check(verses == 2 && calls == 1, "two verses and one footnote call");
    const pl::Block& words = work.divisions[1].chapters[0].blocks[0];
    check(words.inlines.size() == 3 && (words.inlines[1].flags & pl::span_red) != 0 &&
              (words.inlines[2].flags & pl::span_red) == 0,
          "the words of Christ open and close");
    pl::Work broken;
    check(!pl::read_work("x", "Z\tnothing\n", broken) && broken.divisions.empty(), "an unknown record is refused");
    check(!pl::read_work("x", "E\n", broken), "a footnote closed without opening is refused");

    pl::Versification places;
    pl::versify(work, places);
    check(places.places.size() == 4, "four verses");
    check(places.places[2].chapter == 1 && places.places[2].verse == "1", "the third verse is Genesis 2:1");
    check(places.places[3].division == 1, "the fourth is in Mark");
}

void test_coder() {
    std::string text;
    for (int n = 0; n < 4000; ++n)
        text += "And there was evening, and there was morning: day " + std::to_string(n % 37) + ".\n";
    std::vector<std::uint8_t> code;
    pl::lz_encode(bytes_of(text), code);
    std::vector<std::uint8_t> back;
    check(pl::lz_decode(code, text.size(), back), "the code decodes");
    check(std::string(back.begin(), back.end()) == text, "every byte comes back");
    check(code.size() * 20 < text.size(), "a repetitive text codes to under a twentieth");
    std::vector<std::uint8_t> nothing;
    std::vector<std::uint8_t> empty_code;
    pl::lz_encode(nothing, empty_code);
    check(pl::lz_decode(empty_code, 0, back) && back.empty(), "an empty text");
    check(!pl::lz_decode(std::span<const std::uint8_t>(code.data(), code.size() / 3), text.size(), back) ||
              std::string(back.begin(), back.end()) != text,
          "a code cut short does not give the text back");
}

void test_digest() {
    // Known answers (FIPS 180-4's examples), whichever way the processor computes them.
    const std::string one = "abc";
    const std::string two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    check(pl::sha256_hex(pl::sha256(bytes_of(one))) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "the SHA-256 of one block");
    check(pl::sha256_hex(pl::sha256(bytes_of(two))) ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "the SHA-256 of two blocks");
    const std::string million(1000000, 'a');
    check(pl::sha256_hex(pl::sha256(bytes_of(million))) ==
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "the SHA-256 of a million letters");
}

void test_base() {
    // A book coded after a base it repeats is a small fraction of the book coded alone,
    // and decodes to the same bytes reading back into the base.
    std::string base_text;
    for (int n = 0; n < 200; ++n)
        base_text += "In the beginning was the Word " + std::to_string(n * 7919 % 1000) + ". ";
    const std::string book_text = "A passage: " + base_text.substr(1000, 2000);
    std::vector<std::uint8_t> base_code, alone, after;
    pl::LzBase base;
    pl::lz_encode_base(bytes_of(base_text), base_code, 48, base);
    pl::lz_encode(bytes_of(book_text), alone);
    pl::lz_encode(bytes_of(book_text), after, 48, base);
    check(after.size() * 4 < alone.size(), "a book after a base that says the same is coded smaller");
    pl::LzBase decoded;
    check(pl::lz_decode_base(base_code, base_text.size(), decoded) && decoded.size() == base_text.size(),
          "the base decodes");
    std::vector<std::uint8_t> back;
    check(pl::lz_decode(after, book_text.size(), back, decoded) && std::string(back.begin(), back.end()) == book_text,
          "the book after the base comes back");
    check(!pl::lz_decode(after, book_text.size(), back) || std::string(back.begin(), back.end()) != book_text,
          "and does not come back without it");
}

void test_packing() {
    const std::string notes = "B\tN\tNotes\tNotes\nC\t\nP\tp\nT\tA line, and THE Lord's word \u00e2\u0080\u0094 \u00c3\u00a9t\u00c3\u00a9.\n";
    std::vector<std::string_view> books{sample_bible, notes};
    const pl::PackTables tables = pl::learn_tables(books);
    pl::PackTables loaded;
    check(pl::load_tables(pl::save_tables(tables), loaded), "the tables load back");
    check(!pl::load_tables("nonsense", loaded), "damaged tables are refused");
    const pl::TextPacker packer(tables);
    for (const std::string_view book : books) {
        std::string packed, back;
        check(packer.pack(book, packed) && packer.unpack(packed, back) && back == book, "a book packs and comes back");
        check(packed.size() < book.size(), "and packs smaller");
    }
    const std::string unseen = "B\tX\tX\tX\nC\t\nP\tp\nT\tZebra QUAGGA okapi \u00e2\u0084\u00a2 never seen.\n";
    std::string packed, back;
    check(packer.pack(unseen, packed) && packer.unpack(packed, back) && back == unseen,
          "words and characters the tables never saw come back");
}

void test_library_file() {
    std::vector<pl::BookText> books(4);
    books[0].id = "bsb";
    books[0].text = sample_bible;
    books[1].id = "notes";
    books[1].text = "B\tN\tNotes\tNotes\nC\t\nP\tp\nT\tA line.\n";
    books[2].id = "more";
    books[2].text = "B\tM\tMore\tMore\nC\t\nP\tp\nT\tIn the beginning God created the heavens and the earth.\n";
    books[3].id = "later";
    books[3].block = 2;
    books[3].text = "B\tL\tLater\tLater\nC\t\nP\tp\nT\tAnd God said, Let there be light.\n";
    pl::LibraryPlan plan;
    plan.base = {"bsb"};
    pl::Generation generation;
    check(pl::write_generation(books, plan, generation), "the generation is written");
    pl::Generation again;
    check(pl::write_generation(books, plan, again) && generation.base == again.base &&
              again.blocks.size() == generation.blocks.size() && generation.blocks[1].bytes == again.blocks[1].bytes,
          "the same books give the same parts");
    check(generation.blocks.size() == 2 && generation.blocks[0].number == 1 && generation.blocks[1].number == 2,
          "a block for each number the books name");
    const std::string index = "book\tbsb\ntitle\tHOLY BIBLE\nsubtitle\tBerean Standard Bible\ncategory\tScripture\n"
                              "binding\t1b1715\nleather\t1\nblock\t1\n\nbook\tlater\ntitle\tLater\n\n"
                              "book\tnotes\n\nbook\tmore\n\nbook\tmissing\ntitle\tNot in any part\n\n";
    pl::Library library;
    std::vector<std::vector<std::uint8_t>> parts{generation.base, generation.blocks[0].bytes,
                                                 generation.blocks[1].bytes};
    check(library.open(index, parts) && library.refused().empty(), "the library opens");
    check(library.shelf().size() == 4 && library.shelf()[0].title == "HOLY BIBLE", "the shelf, from the index");
    check(library.shelf()[1].id == "later" && library.shelf()[2].id == "notes",
          "in the index's order, whatever the parts' order");
    check(library.shelf()[0].category == "Scripture" && library.shelf()[0].binding.leather &&
              library.shelf()[0].binding.red == 0x1b,
          "the category and binding");
    check(library.shelf()[2].title == "notes", "a book without a shelf record is lettered with its id");
    const pl::Work* bible = library.work("bsb");
    check(bible != nullptr && (*bible).divisions.size() == 2, "the Bible is read from the base");
    check(library.work("bsb") == bible, "and read once");
    check(library.work("more") != nullptr && library.work("later") != nullptr, "books after the base are read");
    const pl::Versification* places = library.bible_places();
    check(places != nullptr && (*places).places.size() == 4, "the Bible's verses for the concordance");

    // A block made later against the frozen base: tables and base untouched.
    std::vector<pl::BookText> added(1);
    added[0].id = "added";
    added[0].text = "B\tA\tAdded\tAdded\nC\t\nP\tp\nT\tZebra QUAGGA okapi, words the tables never saw.\n";
    std::vector<std::uint8_t> block;
    check(pl::write_block(generation.base, added, nullptr, block), "a later block is written");
    std::vector<std::uint8_t> same;
    check(pl::write_block(generation.base, books, nullptr, same) == false, "a book already in the base is refused");
    pl::LibraryFile file;
    check(file.open(generation.base) && file.add_block(generation.blocks[0].bytes) && file.add_block(block),
          "the later block joins the base it was made for");
    std::string text;
    check(file.read(file.find("added"), text) && text == added[0].text, "and its book reads");
    check(!file.add_block(block), "a block's books are not taken twice");
    // The same block coded again from the generation's own codes is the same block.
    std::vector<pl::BookText> second(books.begin() + 1, books.begin() + 3);
    std::vector<std::uint8_t> remade;
    check(pl::write_block(generation.base, second, nullptr, remade) && remade == generation.blocks[0].bytes,
          "a block coded alone equals the same block of its generation");

    // Another generation's block is refused, not read as garbled text.
    std::vector<pl::BookText> others = books;
    others[0].text += "V\t3\nT\tA verse the first generation never had.\n";
    pl::Generation other;
    check(pl::write_generation(others, plan, other), "another generation is written");
    check(file.open(generation.base) && !file.add_block(other.blocks[0].bytes) && file.books().size() == 1,
          "a block from another base is refused");
    check(library.open(index, {generation.base, other.blocks[0].bytes, generation.blocks[1].bytes}) &&
              library.refused().size() == 1 && library.refused()[0] == 1 && library.shelf().size() == 2,
          "the library opens without the refused block's books");

    // A damaged byte inside a book's code is caught by the code or by the text's SHA-256.
    std::vector<std::uint8_t> damaged = generation.blocks[0].bytes;
    check(file.open(generation.base) && file.add_block(generation.blocks[0].bytes), "the parts open");
    const pl::ShelvedBook third = file.books()[file.find("more")];
    damaged[third.offset + third.code_length / 2] ^= 0x10U;
    check(file.open(generation.base) && file.add_block(damaged), "the damaged block's table still opens");
    check(!file.read(file.find("more"), text) && text.empty(), "the damaged book is refused");
    check(file.read(file.find("notes"), text) && text == books[1].text, "an undamaged book still reads");
    check(file.read(file.find("bsb"), text) && text == books[0].text, "and so does the base's book");
    // A damaged base: nothing reads.
    damaged = generation.base;
    damaged[file.base().offset + file.base().code_length / 2] ^= 0x10U;
    check(file.open(damaged) && !file.prepare() && !file.read(0, text), "a damaged base is refused");
    std::vector<std::uint8_t> short_file(generation.base.begin(), generation.base.begin() + 20);
    check(!file.open(short_file), "a base cut short is refused");
}

void test_search() {
    pl::ShelfBook book;
    book.title = "Father Goriot";
    book.subtitle = "Honoré de Balzac";
    check(pl::book_matches(book, ""), "an empty search finds every book");
    check(pl::book_matches(book, "balzac"), "a book is found by its author");
    check(pl::book_matches(book, "GORIOT"), "and by its title, in any case");
    check(pl::book_matches(book, "honore gor"), "every word, in any order, part words too");
    check(pl::book_matches(book, "Honoré"), "accents match themselves");
    check(!pl::book_matches(book, "balzac dickens"), "every word must be found");
    pl::ShelfBook other;
    other.title = "Émile";
    other.subtitle = "Flann O\u2019Brien";
    check(pl::book_matches(other, "emile"), "an accented letter is found by its plain one");
    check(pl::book_matches(other, "o'brien") && pl::book_matches(other, "obrien"), "apostrophes join their word");
}

void test_bookmarks() {
    pl::Bookmarks marks;
    marks.open = "avot";
    marks.step = 2;
    marks.face = 2;
    marks.paper = 9;
    marks.texture = 20;
    marks.gutter = 75;
    pl::Place place;
    place.book = "bsb";
    place.section = 40;
    place.page = 6;
    pl::set_place(marks, place);
    place.page = 8;
    pl::set_place(marks, place);
    check(marks.places.size() == 1, "a book has one place");
    pl::Bookmarks read;
    check(pl::decode_bookmarks(pl::encode_bookmarks(marks), read), "the bookmarks read back");
    check(read.open == "avot" && read.step == 2, "the open book and the type size");
    check(read.face == 2 && read.paper == 9 && read.texture == 20 && read.gutter == 75, "the page's look");
    pl::Bookmarks older;
    check(pl::decode_bookmarks("pocket-library-bookmarks 1\nopen \ntype 0\n", older) && older.face == 0 &&
              older.paper == 0 && older.texture == 50 && older.gutter == 50,
          "a file from before the look was chosen reads as the first look");
    const pl::Place back = pl::place_in(read, "bsb");
    check(back.section == 40 && back.page == 8, "the place in the Bible");
    check(pl::place_in(read, "jps1917").section == 0, "a book never opened opens at its start");
    check(!pl::decode_bookmarks("something else", read), "another file is refused");
}

}  // namespace

int main() {
    test_records();
    test_coder();
    test_digest();
    test_base();
    test_packing();
    test_library_file();
    test_search();
    test_bookmarks();
    if (failures == 0)
        std::printf("pocketlibrary library tests passed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
