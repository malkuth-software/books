#include "lz_coder.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

namespace pl {
namespace {

// Probabilities are 11-bit, starting at one half; each bit moves one a 32nd of the way.
constexpr int probability_bits = 11;
constexpr std::uint16_t probability_half = 1 << (probability_bits - 1);
constexpr int move_bits = 5;
constexpr std::uint32_t top = 1U << 24;

constexpr int state_count = 12;
// A literal is coded in the context of the whole byte before it. (Adding the top bits
// of the byte before that made the books smaller by 0.2% and the base larger by 0.3%.)
constexpr int literal_contexts = 256;

std::size_t literal_context(std::span<const std::uint8_t> text, std::size_t position) {
    return position > 0 ? text[position - 1] : 0;
}
constexpr int minimum_length = 2;
constexpr int maximum_length = minimum_length + 8 + 8 + 256 - 1;  // 273
constexpr int length_states = 4;
constexpr int slot_bits = 6;
// Slots from here code their lower bits directly, as no model predicts them: in this
// text a far distance's low bits are as good as random (modelling the lowest four, as
// LZMA does, made the code larger and slower).
constexpr int end_tree_slot = 8;
constexpr int full_distances = 1 << (end_tree_slot >> 1);  // 16
// A distance's middle bits, which no model predicts, are coded so many at a time: one
// division in the decoder instead of a step a bit.
constexpr int direct_chunk = 16;
// Room past the end of the decoder's output, so short matches are copied in whole
// words that may run over.
constexpr std::size_t copy_slack = 16;

int after_literal(int state) {
    return state < 4 ? 0 : (state < 10 ? state - 3 : state - 6);
}
int after_match(int state) {
    return state < 7 ? 7 : 10;
}
int after_rep(int state) {
    return state < 7 ? 8 : 11;
}
int after_short_rep(int state) {
    return state < 7 ? 9 : 11;
}

struct LengthModel {
    std::uint16_t choice = probability_half;
    std::uint16_t choice2 = probability_half;
    std::array<std::uint16_t, 8> low{};
    std::array<std::uint16_t, 8> middle{};
    std::array<std::uint16_t, 256> high{};
    LengthModel() {
        low.fill(probability_half);
        middle.fill(probability_half);
        high.fill(probability_half);
    }
};

// Every probability of the coder; the encoder and decoder each hold one and move it in
// the same steps.
struct Model {
    std::vector<std::uint16_t> literal;  // literal_contexts * 0x100
    std::array<std::uint16_t, state_count> is_match{};
    std::array<std::uint16_t, state_count> is_rep{};
    std::array<std::uint16_t, state_count> is_rep_g0{};
    std::array<std::uint16_t, state_count> is_rep_g1{};
    std::array<std::uint16_t, state_count> is_rep_g2{};
    std::array<std::uint16_t, state_count> is_rep0_long{};
    std::array<std::array<std::uint16_t, 1 << slot_bits>, length_states> slot{};
    std::array<std::uint16_t, full_distances - end_tree_slot + 1> special{};
    LengthModel length;
    LengthModel rep_length;
    Model() : literal(static_cast<std::size_t>(literal_contexts) * 0x100, probability_half) {
        is_match.fill(probability_half);
        is_rep.fill(probability_half);
        is_rep_g0.fill(probability_half);
        is_rep_g1.fill(probability_half);
        is_rep_g2.fill(probability_half);
        is_rep0_long.fill(probability_half);
        for (std::array<std::uint16_t, 1 << slot_bits>& row : slot)
            row.fill(probability_half);
        special.fill(probability_half);
    }
};

int slot_of(std::uint32_t distance) {
    if (distance < 4)
        return static_cast<int>(distance);
    int highest = 31;
    while ((distance >> highest) == 0)
        --highest;
    return highest * 2 + static_cast<int>((distance >> (highest - 1)) & 1U);
}

// ---- Encoding ------------------------------------------------------------------------

class RangeEncoder {
  public:
    explicit RangeEncoder(std::vector<std::uint8_t>& out) : out_(out) {}

    void bit(std::uint16_t& probability, int value) {
        const std::uint32_t bound = (range_ >> probability_bits) * probability;
        if (value == 0) {
            range_ = bound;
            probability = static_cast<std::uint16_t>(probability + (((1 << probability_bits) - probability) >> move_bits));
        } else {
            low_ += bound;
            range_ -= bound;
            probability = static_cast<std::uint16_t>(probability - (probability >> move_bits));
        }
        while (range_ < top) {
            range_ <<= 8;
            shift_low();
        }
    }

    void direct(std::uint32_t value, int count) {
        while (count > 0) {
            const int take = std::min(count, direct_chunk);
            count -= take;
            range_ >>= take;
            low_ += static_cast<std::uint64_t>((value >> count) & ((1U << take) - 1U)) * range_;
            while (range_ < top) {
                range_ <<= 8;
                shift_low();
            }
        }
    }

    void tree(std::uint16_t* probabilities, int bits, std::uint32_t value) {
        std::uint32_t node = 1;
        for (int i = bits - 1; i >= 0; --i) {
            const int b = static_cast<int>((value >> i) & 1U);
            bit(probabilities[node], b);
            node = (node << 1) | static_cast<std::uint32_t>(b);
        }
    }

    void reverse_tree(std::uint16_t* probabilities, int bits, std::uint32_t value) {
        std::uint32_t node = 1;
        for (int i = 0; i < bits; ++i) {
            const int b = static_cast<int>(value & 1U);
            value >>= 1;
            bit(probabilities[node], b);
            node = (node << 1) | static_cast<std::uint32_t>(b);
        }
    }

    void finish() {
        for (int i = 0; i < 5; ++i)
            shift_low();
    }

  private:
    void shift_low() {
        if (static_cast<std::uint32_t>(low_) < 0xFF000000U || (low_ >> 32) != 0) {
            const std::uint8_t carry = static_cast<std::uint8_t>(low_ >> 32);
            std::uint8_t pending = cache_;
            while (true) {
                out_.push_back(static_cast<std::uint8_t>(pending + carry));
                pending = 0xFF;
                --cache_size_;
                if (cache_size_ == 0)
                    break;
            }
            cache_ = static_cast<std::uint8_t>((low_ >> 24) & 0xFFU);
        }
        ++cache_size_;
        low_ = (low_ & 0x00FFFFFFU) << 8;
    }

    std::vector<std::uint8_t>& out_;
    std::uint64_t low_ = 0;
    std::uint32_t range_ = 0xFFFFFFFFU;
    std::uint8_t cache_ = 0;
    std::uint64_t cache_size_ = 1;
};

void encode_length(RangeEncoder& coder, LengthModel& model, int length) {
    const int value = length - minimum_length;
    if (value < 8) {
        coder.bit(model.choice, 0);
        coder.tree(model.low.data(), 3, static_cast<std::uint32_t>(value));
    } else if (value < 16) {
        coder.bit(model.choice, 1);
        coder.bit(model.choice2, 0);
        coder.tree(model.middle.data(), 3, static_cast<std::uint32_t>(value - 8));
    } else {
        coder.bit(model.choice, 1);
        coder.bit(model.choice2, 1);
        coder.tree(model.high.data(), 8, static_cast<std::uint32_t>(value - 16));
    }
}

void encode_distance(RangeEncoder& coder, Model& model, std::uint32_t distance, int length) {
    const int length_state = std::min(length - minimum_length, length_states - 1);
    const int slot = slot_of(distance);
    coder.tree(model.slot[static_cast<std::size_t>(length_state)].data(), slot_bits, static_cast<std::uint32_t>(slot));
    if (slot < 4)
        return;
    const int footer = (slot >> 1) - 1;
    const std::uint32_t base = (2U | (static_cast<std::uint32_t>(slot) & 1U)) << footer;
    const std::uint32_t reduced = distance - base;
    if (slot < end_tree_slot) {
        coder.reverse_tree(model.special.data() + base - static_cast<std::uint32_t>(slot) - 1, footer, reduced);
    } else {
        coder.direct(reduced, footer);
    }
}

void encode_literal(RangeEncoder& coder, Model& model, std::size_t context, std::uint8_t byte) {
    std::uint16_t* probabilities = model.literal.data() + context * 0x100;
    std::uint32_t symbol = 1;
    for (int i = 7; i >= 0; --i) {
        const int b = (byte >> i) & 1;
        coder.bit(probabilities[symbol], b);
        symbol = (symbol << 1) | static_cast<std::uint32_t>(b);
    }
}

// ---- Prices --------------------------------------------------------------------------
//
// What a bit costs to code, in sixteenths of a bit, from its probability: the integer
// method of LZMA's encoder, so the same text is parsed the same way on every platform.

constexpr int price_shift = 4;

struct PriceTable {
    std::array<std::uint32_t, ((1 << probability_bits) >> price_shift)> values{};
    PriceTable() {
        for (std::size_t i = 0; i < values.size(); ++i) {
            std::uint32_t w = (static_cast<std::uint32_t>(i) << price_shift) + (1U << (price_shift - 1));
            std::uint32_t bits = 0;
            for (int j = 0; j < 4; ++j) {
                w = w * w;
                bits <<= 1;
                while (w >= (1U << 16)) {
                    w >>= 1;
                    ++bits;
                }
            }
            values[i] = (static_cast<std::uint32_t>(probability_bits) << 4) - 15 - bits;
        }
    }
};

const PriceTable& price_table() {
    static const PriceTable table;
    return table;
}

std::uint32_t bit_price(std::uint16_t probability, int bit) {
    const std::uint32_t p = bit != 0 ? (1U << probability_bits) - probability : probability;
    return price_table().values[p >> price_shift];
}

std::uint32_t tree_price(const std::uint16_t* probabilities, int bits, std::uint32_t value) {
    std::uint32_t price = 0;
    std::uint32_t node = 1;
    for (int i = bits - 1; i >= 0; --i) {
        const int b = static_cast<int>((value >> i) & 1U);
        price += bit_price(probabilities[node], b);
        node = (node << 1) | static_cast<std::uint32_t>(b);
    }
    return price;
}

std::uint32_t reverse_tree_price(const std::uint16_t* probabilities, int bits, std::uint32_t value) {
    std::uint32_t price = 0;
    std::uint32_t node = 1;
    for (int i = 0; i < bits; ++i) {
        const int b = static_cast<int>(value & 1U);
        value >>= 1;
        price += bit_price(probabilities[node], b);
        node = (node << 1) | static_cast<std::uint32_t>(b);
    }
    return price;
}

std::uint32_t literal_price(const Model& model, std::size_t context, std::uint8_t byte) {
    const std::uint16_t* probabilities = model.literal.data() + context * 0x100;
    std::uint32_t price = 0;
    std::uint32_t symbol = 1;
    for (int i = 7; i >= 0; --i) {
        const int b = (byte >> i) & 1;
        price += bit_price(probabilities[symbol], b);
        symbol = (symbol << 1) | static_cast<std::uint32_t>(b);
    }
    return price;
}

// The prices that stay fixed while one stretch is parsed: lengths and distances.
struct Prices {
    std::array<std::uint32_t, maximum_length + 1> length{};
    std::array<std::uint32_t, maximum_length + 1> rep_length{};
    std::array<std::array<std::uint32_t, 1 << slot_bits>, length_states> slot{};
    std::array<std::array<std::uint32_t, full_distances>, length_states> near{};

    static std::uint32_t length_price(const LengthModel& model, int length) {
        const int value = length - minimum_length;
        if (value < 8)
            return bit_price(model.choice, 0) + tree_price(model.low.data(), 3, static_cast<std::uint32_t>(value));
        if (value < 16)
            return bit_price(model.choice, 1) + bit_price(model.choice2, 0) +
                   tree_price(model.middle.data(), 3, static_cast<std::uint32_t>(value - 8));
        return bit_price(model.choice, 1) + bit_price(model.choice2, 1) +
               tree_price(model.high.data(), 8, static_cast<std::uint32_t>(value - 16));
    }

    void update(const Model& model) {
        for (int value = minimum_length; value <= maximum_length; ++value) {
            length[static_cast<std::size_t>(value)] = length_price(model.length, value);
            rep_length[static_cast<std::size_t>(value)] = length_price(model.rep_length, value);
        }
        for (std::size_t state = 0; state < static_cast<std::size_t>(length_states); ++state) {
            for (std::uint32_t s = 0; s < (1U << slot_bits); ++s) {
                std::uint32_t price = tree_price(model.slot[state].data(), slot_bits, s);
                if (s >= static_cast<std::uint32_t>(end_tree_slot))
                    price += ((s >> 1) - 1) << 4;
                slot[state][s] = price;
            }
            for (std::uint32_t distance = 0; distance < static_cast<std::uint32_t>(full_distances); ++distance) {
                const int s = slot_of(distance);
                std::uint32_t price = slot[state][static_cast<std::size_t>(s)];
                if (s >= 4) {
                    const int footer = (s >> 1) - 1;
                    const std::uint32_t base = (2U | (static_cast<std::uint32_t>(s) & 1U)) << footer;
                    price += reverse_tree_price(model.special.data() + base - static_cast<std::uint32_t>(s) - 1, footer,
                                                distance - base);
                }
                near[state][distance] = price;
            }
        }
    }

    std::uint32_t distance_price(std::uint32_t distance, int length) const {
        const std::size_t state = static_cast<std::size_t>(std::min(length - minimum_length, length_states - 1));
        if (distance < static_cast<std::uint32_t>(full_distances))
            return near[state][distance];
        return slot[state][static_cast<std::size_t>(slot_of(distance))];
    }
};

int length_at(std::span<const std::uint8_t> text, std::size_t position, std::uint32_t distance) {
    if (static_cast<std::size_t>(distance) + 1 > position)
        return 0;
    const std::size_t from = position - distance - 1;
    const int limit = static_cast<int>(std::min<std::size_t>(maximum_length, text.size() - position));
    int length = 0;
    while (length < limit && text[from + static_cast<std::size_t>(length)] == text[position + static_cast<std::size_t>(length)])
        ++length;
    return length;
}

// ---- The parse ---------------------------------------------------------------------------
//
// Each stretch of the text is parsed as a whole: every way of reaching each position by a
// literal, a short repeat, a repeat of one of the last four distances or a new match is
// priced, and the cheapest path is coded (in the manner of LZMA's optimal parser).

enum class Step : std::uint8_t { literal, short_rep, rep, match };

struct Choice {
    Step step = Step::literal;
    int length = 1;
    std::uint32_t distance = 0;  // a match's distance, or a repeat's index
};

struct Node {
    std::uint32_t price = 0xFFFFFFFFU;
    int from = -1;
    Choice choice;
    int state = 0;
    std::array<std::uint32_t, 4> reps{};
};

constexpr int parse_span = 2048;

struct Candidate {
    int length = 0;
    std::uint32_t distance = 0;
};

// How far `a` and `b` agree from `length` on, up to `limit`: eight bytes at a time.
int common_length(const std::uint8_t* a, const std::uint8_t* b, int length, int limit) {
    while (length + 8 <= limit) {
        std::uint64_t x = 0, y = 0;
        std::memcpy(&x, a + length, 8);
        std::memcpy(&y, b + length, 8);
        const std::uint64_t differ = x ^ y;
        if (differ != 0) {
            // The first differing byte, in memory order.
            if constexpr (std::endian::native == std::endian::little)
                return length + std::countr_zero(differ) / 8;
            else
                return length + std::countl_zero(differ) / 8;
        }
        length += 8;
    }
    while (length < limit && a[length] == b[length])
        ++length;
    return length;
}

// The binary-tree match finder (after LZMA's bt4): every position is threaded into a
// tree, per hash of its first three bytes, of the earlier positions sorted by the text
// that follows them. One walk down the tree both finds the matches of every length at a
// position, shortest to longest, and inserts the position.
struct TreeFinder {
    static constexpr int hash_bits = 22;
    static constexpr std::int32_t none = -1;
    std::span<const std::uint8_t> text;
    std::vector<std::int32_t> head;
    // Per position, side by side so one cache line holds both: the subtree of smaller
    // suffixes (2 * position) and of greater ones (2 * position + 1).
    std::vector<std::int32_t> children;
    int depth = 0;

    TreeFinder(std::span<const std::uint8_t> source, int chosen_depth)
        : text(source), head(static_cast<std::size_t>(1) << hash_bits, none), children(2 * source.size(), none),
          depth(chosen_depth) {}

    // Continues `prefix`, a finder over the first part of `source`, without walking it again.
    TreeFinder(std::span<const std::uint8_t> source, const TreeFinder& prefix)
        : text(source), head(prefix.head), children(prefix.children), depth(prefix.depth) {
        children.resize(2 * source.size(), none);
    }

    std::uint32_t hash_at(std::size_t position) const {
        const std::uint32_t value = static_cast<std::uint32_t>(text[position]) |
                                    (static_cast<std::uint32_t>(text[position + 1]) << 8) |
                                    (static_cast<std::uint32_t>(text[position + 2]) << 16);
        return (value * 2654435761U) >> (32 - hash_bits);
    }

    // Inserts `position`, and, when `out` is given, lists the matches found on the way.
    void walk(std::size_t position, std::vector<Candidate>* out) {
        if (out != nullptr)
            (*out).clear();
        if (position + 3 > text.size())
            return;
        const int limit = static_cast<int>(std::min<std::size_t>(maximum_length, text.size() - position));
        const std::uint32_t h = hash_at(position);
        std::int32_t candidate = head[h];
        head[h] = static_cast<std::int32_t>(position);
#if defined(__GNUC__) || defined(__clang__)
        // The next position's tree, which the next walk starts from, is fetched while this
        // one is walked.
        if (position + 4 <= text.size())
            __builtin_prefetch(&head[hash_at(position + 1)]);
        if (candidate != none) {
            __builtin_prefetch(&children[2 * static_cast<std::size_t>(candidate)]);
            __builtin_prefetch(text.data() + candidate);
        }
#endif
        std::int32_t* smaller = &children[2 * position];      // where the next smaller suffix hangs
        std::int32_t* greater = &children[2 * position + 1];  // where the next greater suffix hangs
        int smaller_length = 0, greater_length = 0;
        int best = 1;
        int steps = depth;
        const std::uint8_t* here = text.data() + position;
        while (true) {
            if (candidate == none || steps == 0) {
                *smaller = none;
                *greater = none;
                return;
            }
            --steps;
            const std::size_t at = static_cast<std::size_t>(candidate);
            std::int32_t* const node = &children[2 * at];
            const std::uint8_t* there = text.data() + at;
#if defined(__GNUC__) || defined(__clang__)
            // Either child is the next step: both are fetched while this one is compared.
            for (int side = 0; side < 2; ++side) {
                if (node[side] != none) {
                    __builtin_prefetch(&children[2 * static_cast<std::size_t>(node[side])]);
                    __builtin_prefetch(text.data() + node[side]);
                }
            }
#endif
            const int length = common_length(there, here, std::min(smaller_length, greater_length), limit);
            if (length > best) {
                best = length;
                if (out != nullptr && length >= minimum_length)
                    (*out).push_back(Candidate{length, static_cast<std::uint32_t>(position - at - 1)});
                if (length == limit) {
                    // The candidate's whole subtrees take its place.
                    *smaller = node[0];
                    *greater = node[1];
                    return;
                }
            }
            if (there[length] < here[length]) {
                *smaller = candidate;
                smaller = &node[1];
                candidate = *smaller;
                smaller_length = length;
            } else {
                *greater = candidate;
                greater = &node[0];
                candidate = *greater;
                greater_length = length;
            }
        }
    }
};

void emit(RangeEncoder& coder, Model& model, std::span<const std::uint8_t> text, std::size_t position,
          const Choice& choice, int& state, std::array<std::uint32_t, 4>& reps) {
    const std::size_t s = static_cast<std::size_t>(state);
    if (choice.step == Step::literal) {
        coder.bit(model.is_match[s], 0);
        encode_literal(coder, model, literal_context(text, position), text[position]);
        state = after_literal(state);
        return;
    }
    coder.bit(model.is_match[s], 1);
    if (choice.step == Step::match) {
        coder.bit(model.is_rep[s], 0);
        encode_length(coder, model.length, choice.length);
        encode_distance(coder, model, choice.distance, choice.length);
        reps[3] = reps[2];
        reps[2] = reps[1];
        reps[1] = reps[0];
        reps[0] = choice.distance;
        state = after_match(state);
        return;
    }
    coder.bit(model.is_rep[s], 1);
    if (choice.step == Step::short_rep) {
        coder.bit(model.is_rep_g0[s], 0);
        coder.bit(model.is_rep0_long[s], 0);
        state = after_short_rep(state);
        return;
    }
    const int index = static_cast<int>(choice.distance);
    if (index == 0) {
        coder.bit(model.is_rep_g0[s], 0);
        coder.bit(model.is_rep0_long[s], 1);
    } else {
        coder.bit(model.is_rep_g0[s], 1);
        if (index == 1) {
            coder.bit(model.is_rep_g1[s], 0);
        } else {
            coder.bit(model.is_rep_g1[s], 1);
            coder.bit(model.is_rep_g2[s], index == 3 ? 1 : 0);
        }
        const std::uint32_t chosen = reps[static_cast<std::size_t>(index)];
        for (int r = index; r > 0; --r)
            reps[static_cast<std::size_t>(r)] = reps[static_cast<std::size_t>(r - 1)];
        reps[0] = chosen;
    }
    encode_length(coder, model.rep_length, choice.length);
    state = after_rep(state);
}

std::uint32_t rep_price(const Model& model, int state, int index) {
    const std::size_t s = static_cast<std::size_t>(state);
    std::uint32_t price = bit_price(model.is_match[s], 1) + bit_price(model.is_rep[s], 1);
    if (index == 0)
        return price + bit_price(model.is_rep_g0[s], 0) + bit_price(model.is_rep0_long[s], 1);
    price += bit_price(model.is_rep_g0[s], 1);
    if (index == 1)
        return price + bit_price(model.is_rep_g1[s], 0);
    return price + bit_price(model.is_rep_g1[s], 1) + bit_price(model.is_rep_g2[s], index == 3 ? 1 : 0);
}

void relax(std::vector<Node>& nodes, int from, int to, std::uint32_t price, const Choice& choice, int state,
           const std::array<std::uint32_t, 4>& reps) {
    Node& node = nodes[static_cast<std::size_t>(to)];
    if (price >= node.price)
        return;
    node.price = price;
    node.from = from;
    node.choice = choice;
    node.state = state;
    node.reps = reps;
}

}  // namespace

struct LzBase::State {
    Model model;
    std::vector<std::uint8_t> text;
    // The encoder's match tree after the base (a base that was decoded has none).
    std::unique_ptr<TreeFinder> finder;
};

namespace {

// Codes text[start..] with `model`, every byte before `start` being history the matches
// may reach.
void encode_from(std::span<const std::uint8_t> text, std::size_t start, Model& model, std::vector<std::uint8_t>& out,
                 int effort, const TreeFinder* prefix, std::unique_ptr<TreeFinder>* keep) {
    RangeEncoder coder(out);
    std::unique_ptr<TreeFinder> owned =
        prefix != nullptr ? std::make_unique<TreeFinder>(text, *prefix) : std::make_unique<TreeFinder>(text, effort);
    TreeFinder& finder = *owned;
    if (prefix == nullptr)
        for (std::size_t i = 0; i < start; ++i)
            finder.walk(i, nullptr);
    Prices prices;
    std::array<std::uint32_t, 4> reps{};
    int state = 0;
    std::size_t position = start;
    std::vector<Node> nodes;
    std::vector<Candidate> candidates;
    std::vector<int> path;
    while (position < text.size()) {
        const int span = static_cast<int>(std::min<std::size_t>(parse_span, text.size() - position));
        prices.update(model);
        nodes.assign(static_cast<std::size_t>(span) + 1, Node{});
        nodes[0].price = 0;
        nodes[0].state = state;
        nodes[0].reps = reps;
        for (int at = 0; at < span; ++at) {
            const std::size_t here = position + static_cast<std::size_t>(at);
            finder.walk(here, &candidates);
            const Node node = nodes[static_cast<std::size_t>(at)];
            if (node.price == 0xFFFFFFFFU)
                continue;
            const std::size_t s = static_cast<std::size_t>(node.state);
            const int room = span - at;
            // A literal.
            {
                const std::uint32_t price = node.price + bit_price(model.is_match[s], 0) +
                                            literal_price(model, literal_context(text, here), text[here]);
                relax(nodes, at, at + 1, price, Choice{Step::literal, 1, 0}, after_literal(node.state), node.reps);
            }
            // The byte at the last distance, once.
            if (here > node.reps[0] && text[here] == text[here - node.reps[0] - 1]) {
                const std::uint32_t price = node.price + bit_price(model.is_match[s], 1) +
                                            bit_price(model.is_rep[s], 1) + bit_price(model.is_rep_g0[s], 0) +
                                            bit_price(model.is_rep0_long[s], 0);
                relax(nodes, at, at + 1, price, Choice{Step::short_rep, 1, 0}, after_short_rep(node.state), node.reps);
            }
            // Repeats of the last four distances.
            for (int r = 0; r < 4; ++r) {
                const int length = std::min(length_at(text, here, node.reps[static_cast<std::size_t>(r)]), room);
                if (length < minimum_length)
                    continue;
                std::array<std::uint32_t, 4> rotated = node.reps;
                const std::uint32_t chosen = rotated[static_cast<std::size_t>(r)];
                for (int k = r; k > 0; --k)
                    rotated[static_cast<std::size_t>(k)] = rotated[static_cast<std::size_t>(k - 1)];
                rotated[0] = chosen;
                const std::uint32_t base = node.price + rep_price(model, node.state, r);
                for (int l = minimum_length; l <= length; ++l)
                    relax(nodes, at, at + l, base + prices.rep_length[static_cast<std::size_t>(l)],
                          Choice{Step::rep, l, static_cast<std::uint32_t>(r)}, after_rep(node.state), rotated);
            }
            // New matches.
            const std::uint32_t match_base = node.price + bit_price(model.is_match[s], 1) + bit_price(model.is_rep[s], 0);
            int shorter = minimum_length;
            for (const Candidate& candidate : candidates) {
                const int longest = std::min(candidate.length, room);
                if (longest < shorter)
                    continue;
                std::array<std::uint32_t, 4> shifted{candidate.distance, node.reps[0], node.reps[1], node.reps[2]};
                // The distance's price depends on the length only through its first few values.
                std::array<std::uint32_t, length_states> distance{};
                for (int k = 0; k < length_states; ++k)
                    distance[static_cast<std::size_t>(k)] =
                        match_base + prices.distance_price(candidate.distance, minimum_length + k);
                for (int l = shorter; l <= longest; ++l)
                    relax(nodes, at, at + l,
                          distance[static_cast<std::size_t>(std::min(l - minimum_length, length_states - 1))] +
                              prices.length[static_cast<std::size_t>(l)],
                          Choice{Step::match, l, candidate.distance}, after_match(node.state), shifted);
                shorter = longest + 1;
            }
        }
        // The cheapest path to the stretch's end, coded in order.
        path.clear();
        for (int at = span; at > 0; at = nodes[static_cast<std::size_t>(at)].from)
            path.push_back(at);
        std::size_t cursor = position;
        for (std::size_t i = path.size(); i > 0; --i) {
            const Node& node = nodes[static_cast<std::size_t>(path[i - 1])];
            emit(coder, model, text, cursor, node.choice, state, reps);
            cursor += static_cast<std::size_t>(node.choice.length);
        }
        position += static_cast<std::size_t>(span);
    }
    coder.finish();
    if (keep != nullptr)
        *keep = std::move(owned);
}

}  // namespace

void lz_encode(std::span<const std::uint8_t> text, std::vector<std::uint8_t>& out, int effort) {
    Model model;
    encode_from(text, 0, model, out, effort, nullptr, nullptr);
}

void lz_encode_base(std::span<const std::uint8_t> text, std::vector<std::uint8_t>& out, int effort, LzBase& base) {
    std::shared_ptr<LzBase::State> made = std::make_shared<LzBase::State>();
    (*made).text.assign(text.begin(), text.end());
    encode_from((*made).text, 0, (*made).model, out, effort, nullptr, &(*made).finder);
    base.state_ = made;
}

void lz_encode(std::span<const std::uint8_t> text, std::vector<std::uint8_t>& out, int effort, const LzBase& base) {
    if (base.state() == nullptr) {
        lz_encode(text, out, effort);
        return;
    }
    const LzBase::State& kept = *base.state();
    std::vector<std::uint8_t> all(kept.text);
    all.insert(all.end(), text.begin(), text.end());
    Model model = kept.model;
    encode_from(all, kept.text.size(), model, out, effort, kept.finder.get(), nullptr);
}

LzBase::LzBase() = default;
LzBase::~LzBase() = default;
LzBase::LzBase(const LzBase& other) = default;
LzBase& LzBase::operator=(const LzBase& other) = default;

std::size_t LzBase::size() const {
    return state_ ? (*state_).text.size() : 0;
}

std::span<const std::uint8_t> LzBase::text() const {
    if (!state_)
        return {};
    return (*state_).text;
}

// ---- Decoding ------------------------------------------------------------------------

namespace {

class RangeDecoder {
  public:
    explicit RangeDecoder(std::span<const std::uint8_t> code) : code_(code) {
        for (int i = 0; i < 5; ++i)
            value_ = (value_ << 8) | next();
    }

    // Written without a branch on the bit: on text the bits are hard to predict, and
    // selects cost less than mispredicted jumps.
    int bit(std::uint16_t& probability) {
        const std::uint32_t p = probability;
        const std::uint32_t bound = (range_ >> probability_bits) * p;
        const bool one = value_ >= bound;
        range_ = one ? range_ - bound : bound;
        value_ = one ? value_ - bound : value_;
        const std::uint32_t up = p + (((1U << probability_bits) - p) >> move_bits);
        const std::uint32_t down = p - (p >> move_bits);
        probability = static_cast<std::uint16_t>(one ? down : up);
        if (range_ < top) {
            range_ <<= 8;
            value_ = (value_ << 8) | next();
        }
        return one ? 1 : 0;
    }

    std::uint32_t direct(int count) {
        std::uint32_t result = 0;
        while (count > 0) {
            const int take = std::min(count, direct_chunk);
            count -= take;
            range_ >>= take;
            // Never more than the chunk holds, whatever a damaged code says.
            const std::uint32_t chunk = std::min(value_ / range_, (1U << take) - 1U);
            value_ -= chunk * range_;
            result = (result << take) | chunk;
            while (range_ < top) {
                range_ <<= 8;
                value_ = (value_ << 8) | next();
            }
        }
        return result;
    }

    std::uint32_t tree(std::uint16_t* probabilities, int bits) {
        std::uint32_t node = 1;
        for (int i = 0; i < bits; ++i)
            node = (node << 1) | static_cast<std::uint32_t>(bit(probabilities[node]));
        return node - (1U << bits);
    }

    std::uint32_t reverse_tree(std::uint16_t* probabilities, int bits) {
        std::uint32_t node = 1;
        std::uint32_t result = 0;
        for (int i = 0; i < bits; ++i) {
            const int b = bit(probabilities[node]);
            node = (node << 1) | static_cast<std::uint32_t>(b);
            result |= static_cast<std::uint32_t>(b) << i;
        }
        return result;
    }

    [[nodiscard]] bool overrun() const {
        return position_ > code_.size() + 4;
    }

  private:
    std::uint32_t next() {
        const std::uint32_t byte = position_ < code_.size() ? code_[position_] : 0U;
        ++position_;
        return byte;
    }

    std::span<const std::uint8_t> code_;
    std::size_t position_ = 0;
    std::uint32_t range_ = 0xFFFFFFFFU;
    std::uint32_t value_ = 0;
};

int decode_length(RangeDecoder& coder, LengthModel& model) {
    if (coder.bit(model.choice) == 0)
        return minimum_length + static_cast<int>(coder.tree(model.low.data(), 3));
    if (coder.bit(model.choice2) == 0)
        return minimum_length + 8 + static_cast<int>(coder.tree(model.middle.data(), 3));
    return minimum_length + 16 + static_cast<int>(coder.tree(model.high.data(), 8));
}

std::uint32_t decode_distance(RangeDecoder& coder, Model& model, int length) {
    const int length_state = std::min(length - minimum_length, length_states - 1);
    const int slot = static_cast<int>(coder.tree(model.slot[static_cast<std::size_t>(length_state)].data(), slot_bits));
    if (slot < 4)
        return static_cast<std::uint32_t>(slot);
    const int footer = (slot >> 1) - 1;
    const std::uint32_t base = (2U | (static_cast<std::uint32_t>(slot) & 1U)) << footer;
    if (slot < end_tree_slot)
        return base + coder.reverse_tree(model.special.data() + base - static_cast<std::uint32_t>(slot) - 1, footer);
    return base + coder.direct(footer);
}

}  // namespace

namespace {

// The byte `back` bytes before `position`, which may lie in the history before the text.
std::uint8_t byte_before(const std::uint8_t* data, std::size_t position, std::size_t back,
                         std::span<const std::uint8_t> history) {
    return back <= position ? data[position - back] : history[history.size() - (back - position)];
}

// Decodes `length` bytes into `out` (replaced), continuing `model`. `history` is the text
// before it (the base's text, or nothing), which matches may reach back into; it is read
// where it lies, never copied. The checks for reaching into the history are the checks
// for a bad distance the decoder makes anyway, so a book without one pays nothing.
bool decode_from(std::span<const std::uint8_t> code, std::size_t length, std::vector<std::uint8_t>& out,
                 Model& model, std::span<const std::uint8_t> history) {
    // The output is sized once and written in place; matches that do not overlap
    // themselves are copied as blocks.
    out.clear();
    out.resize(length + copy_slack);
    std::uint8_t* const data = out.data();
    const std::size_t reach = history.size();
    std::size_t position = 0;
    RangeDecoder coder(code);
    std::array<std::uint32_t, 4> reps{};
    int state = 0;
    while (position < length) {
        const std::size_t s = static_cast<std::size_t>(state);
        if (coder.bit(model.is_match[s]) == 0) {
            const std::uint8_t previous = position > 0 ? data[position - 1] : (reach > 0 ? history[reach - 1] : 0);
            std::uint16_t* probabilities = model.literal.data() + static_cast<std::size_t>(previous) * 0x100;
            std::uint32_t symbol = 1;
            for (int i = 0; i < 8; ++i)
                symbol = (symbol << 1) | static_cast<std::uint32_t>(coder.bit(probabilities[symbol]));
            data[position] = static_cast<std::uint8_t>(symbol & 0xFFU);
            ++position;
            state = after_literal(state);
            continue;
        }
        int run = 0;
        if (coder.bit(model.is_rep[s]) != 0) {
            if (coder.bit(model.is_rep_g0[s]) == 0) {
                if (coder.bit(model.is_rep0_long[s]) == 0) {
                    if (position + reach <= reps[0]) {
                        out.clear();
                        return false;
                    }
                    data[position] = byte_before(data, position, static_cast<std::size_t>(reps[0]) + 1, history);
                    ++position;
                    state = after_short_rep(state);
                    continue;
                }
            } else {
                int index = 1;
                if (coder.bit(model.is_rep_g1[s]) != 0)
                    index = coder.bit(model.is_rep_g2[s]) != 0 ? 3 : 2;
                const std::uint32_t chosen = reps[static_cast<std::size_t>(index)];
                for (int r = index; r > 0; --r)
                    reps[static_cast<std::size_t>(r)] = reps[static_cast<std::size_t>(r - 1)];
                reps[0] = chosen;
            }
            run = decode_length(coder, model.rep_length);
            state = after_rep(state);
        } else {
            run = decode_length(coder, model.length);
            const std::uint32_t distance = decode_distance(coder, model, run);
            reps[3] = reps[2];
            reps[2] = reps[1];
            reps[1] = reps[0];
            reps[0] = distance;
            state = after_match(state);
        }
        const std::size_t count = static_cast<std::size_t>(run);
        const std::size_t back = static_cast<std::size_t>(reps[0]) + 1;
        if (position + count > length) {
            out.clear();
            return false;
        }
        std::uint8_t* to = data + position;
        if (back > position) {
            // From the history: what lies there, then on into the text.
            if (back > position + reach) {
                out.clear();
                return false;
            }
            const std::size_t before = std::min(count, back - position);
            std::memcpy(to, history.data() + (reach - (back - position)), before);
            for (std::size_t i = before; i < count; ++i)
                to[i] = to[i - back];
        } else if (back >= 8 && count <= 32) {
            // Whole words, running over into what is still to be written (or the slack).
            for (std::size_t i = 0; i < count; i += 8)
                std::memcpy(to + i, to + i - back, 8);
        } else if (back >= count) {
            std::memcpy(to, to - back, count);
        } else {
            for (std::size_t i = 0; i < count; ++i)
                to[i] = to[i - back];
        }
        position += count;
        if (coder.overrun()) {
            out.clear();
            return false;
        }
    }
    if (coder.overrun()) {
        out.clear();
        return false;
    }
    out.resize(length);
    return true;
}

}  // namespace

bool lz_decode(std::span<const std::uint8_t> code, std::size_t length, std::vector<std::uint8_t>& out) {
    Model model;
    return decode_from(code, length, out, model, {});
}

bool lz_decode_base(std::span<const std::uint8_t> code, std::size_t length, LzBase& base) {
    std::shared_ptr<LzBase::State> made = std::make_shared<LzBase::State>();
    if (!decode_from(code, length, (*made).text, (*made).model, {})) {
        base = LzBase{};
        return false;
    }
    base.state_ = made;
    return true;
}

bool lz_decode(std::span<const std::uint8_t> code, std::size_t length, std::vector<std::uint8_t>& out,
               const LzBase& base) {
    if (base.state() == nullptr)
        return lz_decode(code, length, out);
    const LzBase::State& kept = *base.state();
    Model model = kept.model;
    return decode_from(code, length, out, model, kept.text);
}

}  // namespace pl
