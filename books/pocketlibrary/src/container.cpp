#include "container.hpp"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace pl {
namespace {

constexpr std::uint32_t format_version = 4;
constexpr std::uint32_t coder_packed_lz = 2;
constexpr std::size_t part_bytes = 12 + 32;

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFU));
}

bool get_u32(std::span<const std::uint8_t> bytes, std::size_t& at, std::uint32_t& value) {
    if (at + 4 > bytes.size())
        return false;
    value = 0;
    for (int i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(bytes[at + static_cast<std::size_t>(i)]) << (8 * i);
    at += 4;
    return true;
}

bool get_digest(std::span<const std::uint8_t> bytes, std::size_t& at, Sha256Digest& digest) {
    if (at + digest.size() > bytes.size())
        return false;
    std::memcpy(digest.data(), bytes.data() + at, digest.size());
    at += digest.size();
    return true;
}

bool get_part(std::span<const std::uint8_t> bytes, std::size_t& at, SharedPart& part) {
    if (!get_u32(bytes, at, part.length) || !get_u32(bytes, at, part.offset) || !get_u32(bytes, at, part.code_length) ||
        !get_digest(bytes, at, part.digest))
        return false;
    return static_cast<std::uint64_t>(part.offset) + part.code_length <= bytes.size();
}

void put_part(std::vector<std::uint8_t>& out, const SharedPart& part) {
    put_u32(out, part.length);
    put_u32(out, part.offset);
    put_u32(out, part.code_length);
    out.insert(out.end(), part.digest.begin(), part.digest.end());
}

std::span<const std::uint8_t> bytes_of(std::string_view text) {
    const std::span<const std::uint8_t> view(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    return view;
}

// The cache's key for a code: what is coded, how, and after what.
Sha256Digest cache_key(std::string_view kind, int effort, const Sha256Digest& after, std::string_view text) {
    Sha256 hash;
    hash.update(bytes_of(kind));
    const std::string how = " lz format " + std::to_string(lz_format) + " effort " + std::to_string(effort) + " ";
    hash.update(bytes_of(how));
    hash.update(after);
    hash.update(bytes_of(text));
    return hash.finish();
}


// A book's entry in a part's table: the id, then what the caller reads after it.
bool get_id(std::span<const std::uint8_t> bytes, std::size_t& at, std::string& id) {
    if (at >= bytes.size())
        return false;
    const std::size_t length = bytes[at];
    ++at;
    if (at + length > bytes.size())
        return false;
    id.assign(reinterpret_cast<const char*>(bytes.data() + at), length);
    at += length;
    return true;
}

void put_id(std::vector<std::uint8_t>& out, const std::string& id) {
    out.push_back(static_cast<std::uint8_t>(id.size()));
    out.insert(out.end(), id.begin(), id.end());
}

// The books outside the base, coded after it, as a block file.
bool code_block(const TextPacker& packer, const LzBase& base, const Sha256Digest& base_digest,
                const Sha256Digest& generation, int effort, CodeCache* cache, const std::vector<const BookText*>& books,
                std::vector<std::uint8_t>& out) {
    out.clear();
    std::vector<std::string> packed(books.size());
    std::vector<std::vector<std::uint8_t>> codes(books.size());
    for (std::size_t n = 0; n < books.size(); ++n) {
        if (!packer.pack((*books[n]).text, packed[n]))
            return false;
        const Sha256Digest key = cache_key("book", effort, base_digest, packed[n]);
        if (cache != nullptr && (*cache).find(key, codes[n]))
            continue;
        lz_encode(bytes_of(packed[n]), codes[n], effort, base);
        if (cache != nullptr)
            (*cache).keep(key, codes[n]);
    }
    std::size_t offset = 4 + 4 + 4 + 32 + 4;
    for (const BookText* book : books)
        offset += 1 + (*book).id.size() + 4 + 4 + 4 + 4 + 32;
    out.insert(out.end(), {'P', 'L', 'B', 'K'});
    put_u32(out, format_version);
    put_u32(out, coder_packed_lz);
    out.insert(out.end(), generation.begin(), generation.end());
    put_u32(out, static_cast<std::uint32_t>(books.size()));
    for (std::size_t n = 0; n < books.size(); ++n) {
        put_id(out, (*books[n]).id);
        put_u32(out, static_cast<std::uint32_t>((*books[n]).text.size()));
        put_u32(out, static_cast<std::uint32_t>(packed[n].size()));
        put_u32(out, static_cast<std::uint32_t>(offset));
        put_u32(out, static_cast<std::uint32_t>(codes[n].size()));
        const Sha256Digest digest = sha256(bytes_of((*books[n]).text));
        out.insert(out.end(), digest.begin(), digest.end());
        offset += codes[n].size();
    }
    for (const std::vector<std::uint8_t>& code : codes)
        out.insert(out.end(), code.begin(), code.end());
    return true;
}

}  // namespace

LibraryFile::LibraryFile() = default;
LibraryFile::~LibraryFile() = default;

bool LibraryFile::open(std::vector<std::uint8_t> base) {
    base_bytes_.clear();
    blocks_.clear();
    books_.clear();
    packer_.reset();
    base_ = LzBase{};
    prepared_ = Prepared::no;
    const std::span<const std::uint8_t> view(base);
    if (view.size() < 20 || std::memcmp(view.data(), "PLBA", 4) != 0)
        return false;
    std::size_t at = 4;
    std::uint32_t version = 0, coder = 0, effort = 0, count = 0;
    if (!get_u32(view, at, version) || !get_u32(view, at, coder) || !get_u32(view, at, effort) ||
        !get_u32(view, at, count))
        return false;
    if (version != format_version || coder != coder_packed_lz || count > 4000 || effort == 0 || effort > 4096)
        return false;
    SharedPart tables, text;
    if (!get_part(view, at, tables) || !get_part(view, at, text))
        return false;
    std::vector<ShelvedBook> books;
    for (std::uint32_t n = 0; n < count; ++n) {
        ShelvedBook book;
        if (!get_id(view, at, book.id) || !get_u32(view, at, book.length) || !get_u32(view, at, book.packed_length) ||
            !get_u32(view, at, book.offset) || !get_digest(view, at, book.digest))
            return false;
        if (static_cast<std::uint64_t>(book.offset) + book.packed_length > text.length)
            return false;
        books.push_back(book);
    }
    generation_ = sha256(view);
    effort_ = static_cast<int>(effort);
    base_bytes_ = std::move(base);
    books_ = std::move(books);
    tables_part_ = tables;
    base_part_ = text;
    return true;
}

bool LibraryFile::add_block(std::vector<std::uint8_t> block) {
    if (base_bytes_.empty())
        return false;
    const std::span<const std::uint8_t> view(block);
    if (view.size() < 48 || std::memcmp(view.data(), "PLBK", 4) != 0)
        return false;
    std::size_t at = 4;
    std::uint32_t version = 0, coder = 0, count = 0;
    Sha256Digest generation{};
    if (!get_u32(view, at, version) || !get_u32(view, at, coder) || !get_digest(view, at, generation) ||
        !get_u32(view, at, count))
        return false;
    if (version != format_version || coder != coder_packed_lz || generation != generation_ || count > 4000)
        return false;
    const int number = static_cast<int>(blocks_.size());
    std::vector<ShelvedBook> books;
    for (std::uint32_t n = 0; n < count; ++n) {
        ShelvedBook book;
        book.block = number;
        if (!get_id(view, at, book.id) || !get_u32(view, at, book.length) || !get_u32(view, at, book.packed_length) ||
            !get_u32(view, at, book.offset) || !get_u32(view, at, book.code_length) ||
            !get_digest(view, at, book.digest))
            return false;
        if (static_cast<std::uint64_t>(book.offset) + book.code_length > view.size())
            return false;
        if (find(book.id) != books_.size())
            return false;
        for (const ShelvedBook& earlier : books)
            if (earlier.id == book.id)
                return false;
        books.push_back(book);
    }
    blocks_.push_back(std::move(block));
    books_.insert(books_.end(), books.begin(), books.end());
    return true;
}

std::size_t LibraryFile::find(std::string_view id) const {
    for (std::size_t n = 0; n < books_.size(); ++n)
        if (books_[n].id == id)
            return n;
    return books_.size();
}

std::span<const std::uint8_t> LibraryFile::code_of(const SharedPart& part) const {
    if (base_bytes_.empty())
        return {};
    return {base_bytes_.data() + part.offset, part.code_length};
}

bool LibraryFile::prepare() {
    if (prepared_ != Prepared::no)
        return prepared_ == Prepared::yes;
    prepared_ = Prepared::damaged;
    if (base_bytes_.empty())
        return false;
    std::vector<std::uint8_t> saved;
    if (!lz_decode(code_of(tables_part_), tables_part_.length, saved) || sha256(saved) != tables_part_.digest)
        return false;
    const std::string_view saved_view(reinterpret_cast<const char*>(saved.data()), saved.size());
    PackTables tables;
    if (!load_tables(saved_view, tables))
        return false;
    LzBase base;
    if (base_part_.length > 0) {
        if (!lz_decode_base(code_of(base_part_), base_part_.length, base) ||
            sha256(base.text()) != base_part_.digest)
            return false;
    }
    packer_ = std::make_unique<TextPacker>(std::move(tables));
    base_ = base;
    prepared_ = Prepared::yes;
    return true;
}

bool LibraryFile::read(std::size_t index, std::string& out) {
    out.clear();
    if (index >= books_.size() || !prepare())
        return false;
    const ShelvedBook& book = books_[index];
    std::string_view packed;
    std::vector<std::uint8_t> decoded;
    if (book.in_base()) {
        const std::span<const std::uint8_t> base = base_.text();
        packed = std::string_view(reinterpret_cast<const char*>(base.data()) + book.offset, book.packed_length);
    } else {
        const std::vector<std::uint8_t>& block = blocks_[static_cast<std::size_t>(book.block)];
        const std::span<const std::uint8_t> code(block.data() + book.offset, book.code_length);
        if (!lz_decode(code, book.packed_length, decoded, base_))
            return false;
        packed = std::string_view(reinterpret_cast<const char*>(decoded.data()), decoded.size());
    }
    out.reserve(book.length);
    if (!(*packer_).unpack(packed, out) || out.size() != book.length || sha256(bytes_of(out)) != book.digest) {
        out.clear();
        return false;
    }
    return true;
}

bool write_generation(const std::vector<BookText>& books, const LibraryPlan& plan, Generation& out) {
    out = Generation{};
    // The tables, from every book.
    std::vector<std::string_view> texts;
    texts.reserve(books.size());
    for (const BookText& book : books)
        texts.push_back(book.text);
    const PackTables tables = learn_tables(texts);
    const std::string saved = save_tables(tables);
    const TextPacker packer(tables);
    std::vector<std::string> packed(books.size());
    for (std::size_t n = 0; n < books.size(); ++n)
        if (!packer.pack(books[n].text, packed[n]))
            return false;

    // The base: the planned books' packed texts, end to end.
    std::vector<bool> in_base(books.size(), false);
    std::vector<std::uint32_t> base_offset(books.size(), 0);
    std::string base_text;
    for (const std::string& id : plan.base) {
        for (std::size_t n = 0; n < books.size(); ++n) {
            if (books[n].id != id || in_base[n])
                continue;
            in_base[n] = true;
            base_offset[n] = static_cast<std::uint32_t>(base_text.size());
            base_text += packed[n];
        }
    }
    // Then a cross-section: every so many books of the shelf, the stride growing until
    // the chosen books fit.
    std::size_t outside = 0;
    for (std::size_t n = 0; n < books.size(); ++n)
        outside += in_base[n] ? 0 : 1;
    if (plan.base_bytes > base_text.size() && outside > 0) {
        std::size_t left = 0;
        for (std::size_t n = 0; n < books.size(); ++n)
            left += in_base[n] ? 0 : packed[n].size();
        const std::size_t room = plan.base_bytes - base_text.size();
        // The share of the shelf that fits, as one book in `stride`.
        const double share = static_cast<double>(room) / static_cast<double>(std::max<std::size_t>(left, 1));
        const double stride = share >= 1 ? 1.0 : 1.0 / share;
        double next = 0;
        std::size_t seen = 0;
        for (std::size_t n = 0; n < books.size(); ++n) {
            if (in_base[n])
                continue;
            if (static_cast<double>(seen++) < next)
                continue;
            next += stride;
            if (base_text.size() + packed[n].size() > plan.base_bytes)
                continue;
            in_base[n] = true;
            base_offset[n] = static_cast<std::uint32_t>(base_text.size());
            base_text += packed[n];
        }
        // What the stride left of the room, filled in shelf order.
        for (std::size_t n = 0; n < books.size(); ++n) {
            if (in_base[n] || base_text.size() + packed[n].size() > plan.base_bytes)
                continue;
            in_base[n] = true;
            base_offset[n] = static_cast<std::uint32_t>(base_text.size());
            base_text += packed[n];
        }
    }
    const Sha256Digest base_digest = sha256(bytes_of(base_text));
    const Sha256Digest none{};

    // The shared codes, from the cache where it has them. The encoder's base (its model
    // and its match tree) is needed for any book coded after it, so it is made again
    // even when its code was kept.
    std::vector<std::uint8_t> tables_code, base_code;
    const Sha256Digest tables_key = cache_key("tables", plan.effort, none, saved);
    if (plan.cache == nullptr || !(*plan.cache).find(tables_key, tables_code)) {
        lz_encode(bytes_of(saved), tables_code, plan.effort);
        if (plan.cache != nullptr)
            (*plan.cache).keep(tables_key, tables_code);
    }
    LzBase base;
    if (!base_text.empty())
        lz_encode_base(bytes_of(base_text), base_code, plan.effort, base);

    // The base file.
    std::size_t count = 0, offset = 4 + 4 + 4 + 4 + 4 + 2 * part_bytes;
    for (std::size_t n = 0; n < books.size(); ++n) {
        if (!in_base[n])
            continue;
        ++count;
        offset += 1 + books[n].id.size() + 4 + 4 + 4 + 32;
    }
    std::vector<std::uint8_t>& file = out.base;
    file.insert(file.end(), {'P', 'L', 'B', 'A'});
    put_u32(file, format_version);
    put_u32(file, coder_packed_lz);
    put_u32(file, static_cast<std::uint32_t>(plan.effort));
    put_u32(file, static_cast<std::uint32_t>(count));
    const SharedPart tables_part{static_cast<std::uint32_t>(saved.size()), static_cast<std::uint32_t>(offset),
                                 static_cast<std::uint32_t>(tables_code.size()), sha256(bytes_of(saved))};
    offset += tables_code.size();
    const SharedPart base_part{static_cast<std::uint32_t>(base_text.size()), static_cast<std::uint32_t>(offset),
                               static_cast<std::uint32_t>(base_code.size()), base_digest};
    put_part(file, tables_part);
    put_part(file, base_part);
    for (std::size_t n = 0; n < books.size(); ++n) {
        if (!in_base[n])
            continue;
        put_id(file, books[n].id);
        put_u32(file, static_cast<std::uint32_t>(books[n].text.size()));
        put_u32(file, static_cast<std::uint32_t>(packed[n].size()));
        put_u32(file, base_offset[n]);
        const Sha256Digest digest = sha256(bytes_of(books[n].text));
        file.insert(file.end(), digest.begin(), digest.end());
    }
    file.insert(file.end(), tables_code.begin(), tables_code.end());
    file.insert(file.end(), base_code.begin(), base_code.end());
    const Sha256Digest generation = sha256(file);

    // The blocks, in increasing number, each its books in shelf order.
    std::vector<int> numbers;
    for (std::size_t n = 0; n < books.size(); ++n)
        if (!in_base[n] && std::find(numbers.begin(), numbers.end(), books[n].block) == numbers.end())
            numbers.push_back(books[n].block);
    std::sort(numbers.begin(), numbers.end());
    for (const int number : numbers) {
        std::vector<const BookText*> members;
        for (std::size_t n = 0; n < books.size(); ++n)
            if (!in_base[n] && books[n].block == number)
                members.push_back(&books[n]);
        NumberedBlock block;
        block.number = number;
        if (!code_block(packer, base, base_digest, generation, plan.effort, plan.cache, members, block.bytes))
            return false;
        out.blocks.push_back(std::move(block));
    }
    return true;
}

bool write_block(std::span<const std::uint8_t> base, const std::vector<BookText>& books, CodeCache* cache,
                 std::vector<std::uint8_t>& out) {
    out.clear();
    LibraryFile file;
    if (!file.open(std::vector<std::uint8_t>(base.begin(), base.end())) || !file.prepare())
        return false;
    for (const BookText& book : books)
        if (file.find(book.id) != file.books().size())
            return false;
    // The encoder's state after the base, made again from the base's text at the base's
    // effort; it must code the base exactly as the file holds it, or the books coded
    // after it would not decode.
    std::vector<std::uint8_t> code;
    LzBase encoder;
    const std::span<const std::uint8_t> text = file.base_text();
    if (!text.empty()) {
        lz_encode_base(text, code, file.effort(), encoder);
        const std::span<const std::uint8_t> kept = file.base_code();
        if (code.size() != kept.size() || !std::equal(code.begin(), code.end(), kept.begin()))
            return false;
    }
    std::vector<const BookText*> members;
    for (const BookText& book : books)
        members.push_back(&book);
    return code_block(*file.packer(), encoder, file.base().digest, file.generation(), file.effort(), cache, members,
                      out);
}

}  // namespace pl
