#pragma once
// The library's byte coder: LZ77 with a window as large as the text, its literals,
// lengths and distances coded bit by bit with adaptive probabilities by a range coder,
// in the manner of LZMA. Decoding is a few table lookups a byte, so a whole book opens
// in milliseconds. Integer arithmetic only: the same bytes give the same code on every
// platform. It knows nothing of books; the container decides what a block holds.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace pl {

// The code's format: raised whenever the same text would be coded differently, so codes
// kept from an earlier build are not taken for this one's.
inline constexpr int lz_format = 3;

// Codes `text`, appending to `out`. `effort` bounds the match search (tree nodes visited
// per byte); more is smaller and slower to encode, never slower to decode.
void lz_encode(std::span<const std::uint8_t> text, std::vector<std::uint8_t>& out, int effort = 48);

// Decodes `length` bytes of `code` into `out` (replaced). False, with `out` cleared,
// when the code is damaged: it runs out, or a distance points before the text.
[[nodiscard]] bool lz_decode(std::span<const std::uint8_t> code, std::size_t length,
                             std::vector<std::uint8_t>& out);

// A shared base: a text every book may refer back to, and the coder's state at its end
// (its probabilities, trained by the base). Books coded after a base are smaller, as
// they need not learn the language again, and can repeat what the base already says.
// The base is itself stored as code; decoding that code rebuilds the same state.
class LzBase {
  public:
    LzBase();
    ~LzBase();
    LzBase(const LzBase& other);
    LzBase& operator=(const LzBase& other);
    [[nodiscard]] std::size_t size() const;  // the base text's length; 0 for none
    [[nodiscard]] std::span<const std::uint8_t> text() const;  // the base text
    struct State;
    [[nodiscard]] const State* state() const {
        return state_.get();
    }

  private:
    friend void lz_encode_base(std::span<const std::uint8_t>, std::vector<std::uint8_t>&, int, LzBase&);
    friend bool lz_decode_base(std::span<const std::uint8_t>, std::size_t, LzBase&);
    std::shared_ptr<const State> state_;
};

// Codes `text` as a base: appends its code to `out` and makes `base` from it.
void lz_encode_base(std::span<const std::uint8_t> text, std::vector<std::uint8_t>& out, int effort, LzBase& base);
// Decodes a base's code of `length` bytes into `base`. False, leaving `base` empty, on damage.
[[nodiscard]] bool lz_decode_base(std::span<const std::uint8_t> code, std::size_t length, LzBase& base);

// As lz_encode and lz_decode, continuing from `base`.
void lz_encode(std::span<const std::uint8_t> text, std::vector<std::uint8_t>& out, int effort, const LzBase& base);
[[nodiscard]] bool lz_decode(std::span<const std::uint8_t> code, std::size_t length, std::vector<std::uint8_t>& out,
                             const LzBase& base);

}  // namespace pl
