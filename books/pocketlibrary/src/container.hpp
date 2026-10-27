#pragma once
// The library in parts: one base and any number of blocks of books, each a read-only
// file, all coded against the base. Little-endian throughout.
//
// The base (base-<first 16 hex digits of its SHA-256>.plb):
//
//   "PLBA", version (u32, 4), coder (u32, 2 = packed, then lz_coder), the coder's
//   effort (u32), count of the books inside the base (u32)
//   the packing tables (text_pack.hpp): length (u32), code offset (u32), code length
//             (u32), SHA-256 of the saved tables (32 bytes)
//   the base: length (u32), code offset (u32), code length (u32), SHA-256 (32 bytes)
//   per book inside the base: id length (u8), id, text length (u32), packed length
//             (u32), offset of its packed text in the base (u32), SHA-256 of the text
//   the codes: the tables', then the base's
//
// A block (block-<number>-<first 16 hex digits of its SHA-256>.plb):
//
//   "PLBK", version (u32, 4), coder (u32, 2), the SHA-256 of the whole base file it
//   was coded against (32 bytes), book count (u32)
//   per book: id length (u8), id, text length (u32), packed length (u32), offset of its
//             code in this file (u32), code length (u32), SHA-256 of the text
//   the codes
//
// The tables and the base are a generation: learned and chosen once, from every book
// of the library at the time, then frozen. A block of new books is packed with the
// frozen tables and coded after the frozen base, so adding books makes one new block
// and leaves every other part as it was. A block made against another base is refused
// when it is added, never read as garbled text.
//
// Every book is packed with the tables (text_pack.hpp) and the packed bytes coded by
// lz_coder. The base is some books' packed texts end to end, coded as one: a book inside
// it is a slice of it, and every other book is coded after it (LzBase), so it may refer
// back to anything the base says and starts with a model already trained. The tables
// and the base are decoded once, the first time a book is read; each other book is
// decoded alone when it is opened. Each book is checked by the SHA-256 of its text.
//
// Nothing about a book's place on the shelf is here: the shelf index (library.hpp)
// orders, names and binds the books, so they can be reshelved without coding anything.
// A book's text is its normalized records (book.hpp reads them).
#include "lz_coder.hpp"
#include "sha256.hpp"
#include "text_pack.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace pl {

struct ShelvedBook {
    std::string id;
    std::uint32_t length = 0;         // of the text
    std::uint32_t packed_length = 0;
    int block = -1;                   // the block holding its code (in the order added); -1 inside the base
    std::uint32_t offset = 0;         // of the code in its block, or of the packed text in the base
    std::uint32_t code_length = 0;    // 0 for a book inside the base
    Sha256Digest digest{};
    [[nodiscard]] bool in_base() const {
        return block < 0;
    }
};

// Where one of the shared parts (the tables, the base) lies in the base file.
struct SharedPart {
    std::uint32_t length = 0;
    std::uint32_t offset = 0;
    std::uint32_t code_length = 0;
    Sha256Digest digest{};
};

class LibraryFile {
  public:
    LibraryFile();
    ~LibraryFile();
    // Takes the base file's bytes, forgetting any earlier base and blocks. False,
    // holding nothing, when its header or table is damaged or a code lies outside it.
    // Decodes nothing.
    [[nodiscard]] bool open(std::vector<std::uint8_t> base);
    // Adds a block's books. False, adding nothing, when the block is damaged, was coded
    // against another base, or holds a book already held.
    [[nodiscard]] bool add_block(std::vector<std::uint8_t> block);
    [[nodiscard]] const std::vector<ShelvedBook>& books() const {
        return books_;
    }
    // The book with this id, or books().size() when none.
    [[nodiscard]] std::size_t find(std::string_view id) const;
    [[nodiscard]] const SharedPart& base() const {
        return base_part_;
    }
    // The SHA-256 of the whole base file: what each block names.
    [[nodiscard]] const Sha256Digest& generation() const {
        return generation_;
    }
    [[nodiscard]] int effort() const {
        return effort_;
    }
    // Decodes the tables and the base, if that is not done. False when either is
    // damaged; no book can then be read. read() does this itself when it must.
    [[nodiscard]] bool prepare();
    // The text of book `index`. False, with `out` cleared, when its code is damaged or
    // its text does not match its SHA-256.
    [[nodiscard]] bool read(std::size_t index, std::string& out);
    // Once prepared: the frozen tables and the base's packed text and code, for coding
    // a block after them.
    [[nodiscard]] const TextPacker* packer() const {
        return packer_.get();
    }
    [[nodiscard]] std::span<const std::uint8_t> base_text() const {
        return base_.text();
    }
    [[nodiscard]] std::span<const std::uint8_t> base_code() const {
        return code_of(base_part_);
    }

  private:
    std::vector<std::uint8_t> base_bytes_;
    std::vector<std::vector<std::uint8_t>> blocks_;
    std::vector<ShelvedBook> books_;
    SharedPart tables_part_;
    SharedPart base_part_;
    Sha256Digest generation_{};
    int effort_ = 0;
    enum class Prepared : std::uint8_t { no, yes, damaged };
    Prepared prepared_ = Prepared::no;
    std::unique_ptr<TextPacker> packer_;
    LzBase base_;
    [[nodiscard]] std::span<const std::uint8_t> code_of(const SharedPart& part) const;
};

struct BookText {
    std::string id;
    std::string text;
    int block = 1;  // the block that codes it, when it is not inside the base
};

// Codes made by an earlier build, so a build codes again only what changed. A code is
// kept under a digest of everything it depends on.
class CodeCache {
  public:
    virtual ~CodeCache() = default;
    [[nodiscard]] virtual bool find(const Sha256Digest& key, std::vector<std::uint8_t>& code) = 0;
    virtual void keep(const Sha256Digest& key, const std::vector<std::uint8_t>& code) = 0;
};

struct LibraryPlan {
    // The books placed first in the base, by id, in this order. Ids not among the books
    // are ignored.
    std::vector<std::string> base;
    // Then books spread evenly through the shelf, a stride apart, while the base holds
    // under this many packed bytes: a cross-section of the library's language.
    std::size_t base_bytes = 0;
    int effort = 48;               // the coder's match search (lz_coder.hpp)
    CodeCache* cache = nullptr;
};

struct NumberedBlock {
    int number = 0;
    std::vector<std::uint8_t> bytes;
};

struct Generation {
    std::vector<std::uint8_t> base;
    // In increasing number; a block whose books all went into the base is left out.
    std::vector<NumberedBlock> blocks;
};

// A new generation for `books`, in shelf order: tables learned from all of them, the
// base chosen by `plan`, and every other book coded into the block it names.
// Deterministic: the same texts and plan give the same bytes on every platform. False
// when a text cannot be packed.
[[nodiscard]] bool write_generation(const std::vector<BookText>& books, const LibraryPlan& plan, Generation& out);

// A block of `books` coded with the frozen tables and after the frozen base of `base`,
// a base file, at the effort it was made with. False when the base does not open or
// decode, a text cannot be packed, or a book is already inside the base.
[[nodiscard]] bool write_block(std::span<const std::uint8_t> base, const std::vector<BookText>& books,
                               CodeCache* cache, std::vector<std::uint8_t>& out);

}  // namespace pl
