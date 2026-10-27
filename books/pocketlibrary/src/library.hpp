#pragma once
// The library as the reader sees it: the books on the shelf, in shelf order, with how
// each is bound; each book read from the library's parts the first time it is opened and
// kept while the game lives; and the reader's place in every book. No pixels, no
// window: the scenes draw from this.
#include "book.hpp"
#include "container.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pl {

struct Binding {
    std::uint8_t red = 70, green = 64, blue = 58;  // the cloth or leather of the spine
    bool leather = false;
};

struct ShelfBook {
    std::size_t index = 0;   // in the library's parts (LibraryFile::books)
    std::string id;
    std::string title;       // gilt on the spine
    std::string subtitle;    // smaller, below it
    std::string category;    // the shelf it stands on: "Scripture", "Fiction"...
    Binding binding;
    double heft = 1;         // how thick the volume is, 0.5 to 2.5: grows with its text
    // Where the text comes from and on what terms, for the book's About card: the
    // edition, its licence ("Public domain", "CC BY 4.0", "CC BY-SA 4.0"...), the
    // licence's address and the credit the licence asks for (empty when it asks none).
    std::string edition;
    std::string licence = "Public domain";
    std::string licence_url;
    std::string credit;
    [[nodiscard]] bool share_alike() const {
        return licence.find("-SA") != std::string::npos;
    }
};

// A search of the shelf: true when every word of `query` is found in the book's title or
// its subtitle (the author), ignoring case and accents ("balzac", "Goriot", "tolstoi"
// does not find "Tolstoy"). An empty query finds every book.
[[nodiscard]] bool book_matches(const ShelfBook& book, std::string_view query);

// The shelf index: the books in shelf order, each a "book<TAB>id" line, its shelf
// record's lines (key<TAB>value: title, subtitle, category, binding, leather, edition,
// licence, licence_url, credit, block) and a blank line. Kept as text beside the parts,
// so a book is reshelved, renamed or relicensed without coding anything.
struct IndexedBook {
    std::string id;
    std::string record;
};
[[nodiscard]] std::vector<IndexedBook> read_shelf_index(std::string_view index);

class Library {
  public:
    // Takes the shelf index and the parts' bytes, the base first. False, with an empty
    // shelf, when the base does not open. A damaged block, or one coded against another
    // base, is left out with its books; an indexed book no part holds is not shelved.
    [[nodiscard]] bool open(std::string_view index, std::vector<std::vector<std::uint8_t>> parts);
    // The parts that did not open (0 for the base), for the record.
    [[nodiscard]] const std::vector<std::size_t>& refused() const {
        return refused_;
    }
    [[nodiscard]] const std::vector<ShelfBook>& shelf() const {
        return shelf_;
    }
    // Decodes what every book needs (the file's tables and base) ahead of the first
    // book, so it may be done on a worker while the shelf is shown; work() does it
    // itself otherwise. Not to be called while another call is running.
    void prepare();
    // The book, decoded and read the first time it is asked for; nullptr when its text
    // is damaged. The pointer stays valid for the life of the library.
    [[nodiscard]] const Work* work(std::string_view id);
    // The BSB's verses in order (for the concordance's references), or nullptr when the
    // library has no BSB.
    [[nodiscard]] const Versification* bible_places();

  private:
    LibraryFile file_;
    std::vector<ShelfBook> shelf_;
    std::vector<std::unique_ptr<Work>> works_;  // one per shelf book, empty until read
    std::vector<bool> failed_;
    std::unique_ptr<Versification> places_;
    std::vector<std::size_t> refused_;
};

// Where the reader is in a book: its section and the left page of the open spread.
struct Place {
    std::string book;
    std::size_t section = 0;
    std::size_t page = 0;
};

struct Bookmarks {
    std::string open;  // the book open when the game was left; empty for the shelf
    int step = 1;      // the type size: 0 small, 1 medium, 2 large
    int face = 0;      // the typeface: 0 as each book is set, 1 Literata, 2 EB Garamond
    int paper = 0;     // the paper, from India (0)
    int texture = 50;  // the paper's markings, in percent: 50 as the paper is made
    int gutter = 50;   // the shadow into the binding, in percent: 50 as made
    std::vector<Place> places;
};
[[nodiscard]] std::string encode_bookmarks(const Bookmarks& marks);
// False, leaving `marks` unchanged, when the text is not a bookmarks file.
[[nodiscard]] bool decode_bookmarks(std::string_view text, Bookmarks& marks);
[[nodiscard]] Place place_in(const Bookmarks& marks, std::string_view book);
void set_place(Bookmarks& marks, const Place& place);

}  // namespace pl
