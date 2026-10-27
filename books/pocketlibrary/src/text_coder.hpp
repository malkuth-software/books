#pragma once
// The library's text coder: a context-mixing model that predicts each bit of the text
// from the bytes and words before it, driving a binary arithmetic coder. Everything is
// integer arithmetic, so the encoder (a build tool) and the decoder (the game, on every
// platform) make the same predictions bit for bit.
//
// A model can be taught a primer first: text the reader always has, which the coder
// reads but does not store. Every block of a book is then coded from a copy of the
// taught model, so a block can be opened alone and still starts knowing the language
// of the whole library. It does not know about books, pages or records: it codes bytes.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace pl {

// Table sizes as powers of two. Every context table holds 1 << table_bits counters of
// four bytes; the match model indexes 1 << match_bits positions.
struct CoderShape {
    int table_bits = 20;
    int match_bits = 20;
};

class TextModel {
  public:
    explicit TextModel(CoderShape shape);
    TextModel(const TextModel& other);
    TextModel& operator=(const TextModel& other);
    ~TextModel();

    // Teaches the model `text` as if it had been coded, and keeps it as history the
    // match model may point back into.
    void learn(std::span<const std::uint8_t> text);
    // The probability, 0..4095 in 4096ths, that the next bit is a one.
    [[nodiscard]] int predict() const;
    // Moves the model past one bit.
    void update(int bit);

  private:
    struct State;
    std::unique_ptr<State> state_;
};

// Codes `text` with `model`, which is moved past it. Appends to `out`.
void encode(TextModel& model, std::span<const std::uint8_t> text, std::vector<std::uint8_t>& out);
// Decodes `length` bytes of `code` with `model` into `out` (replaced). False, with `out`
// cleared, when the code runs out before `length` bytes.
[[nodiscard]] bool decode(TextModel& model, std::span<const std::uint8_t> code, std::size_t length,
                          std::vector<std::uint8_t>& out);

}  // namespace pl
