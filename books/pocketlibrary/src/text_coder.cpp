#include "text_coder.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace pl {
namespace {

// The logistic curve in 4096ths, sampled every 128 steps of its argument from -2048 to
// 2048 and interpolated between (the shape the PAQ family of coders uses).
constexpr std::array<int, 33> squash_points = {1,    2,    3,    6,    10,   16,   27,   45,   73,
                                               120,  194,  310,  488,  747,  1101, 1546, 2047, 2549,
                                               2994, 3348, 3607, 3785, 3901, 3975, 4022, 4050, 4068,
                                               4079, 4085, 4089, 4092, 4093, 4094};

int squash(int d) {
    if (d > 2047)
        return 4095;
    if (d < -2047)
        return 1;
    const int weight = d & 127;
    const int index = (d >> 7) + 16;
    const int p = (squash_points[static_cast<std::size_t>(index)] * (128 - weight) +
                   squash_points[static_cast<std::size_t>(index) + 1] * weight + 64) >>
                  7;
    return p;
}

// stretch(p) is the inverse of squash: ln(p / (1 - p)) in the same units.
struct StretchTable {
    std::array<std::int16_t, 4096> values{};
    StretchTable() {
        int next = 0;
        for (int x = -2047; x <= 2047; ++x) {
            const int p = squash(x);
            for (int i = next; i <= p; ++i)
                values[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(x);
            next = p + 1;
        }
        for (int i = next; i < 4096; ++i)
            values[static_cast<std::size_t>(i)] = 2047;
    }
};

const StretchTable& stretch_table() {
    static const StretchTable table;
    return table;
}

int stretch(int p) {
    return stretch_table().values[static_cast<std::size_t>(p)];
}

std::uint32_t mix_hash(std::uint32_t a, std::uint32_t b) {
    std::uint32_t h = a * 0x9E3779B1U ^ (b + 0x7F4A7C15U) * 0x85EBCA77U;
    h ^= h >> 15;
    h *= 0xC2B2AE3DU;
    h ^= h >> 13;
    return h;
}

// An adaptive probability: 22 bits of probability over 10 bits of count. It learns
// quickly when new and settles as it is used (its rate is 1 / (count + 1.5)), down to
// 1 / (limit + 1.5).
constexpr std::uint32_t counter_limit = 255;
constexpr std::uint32_t counter_start = (1U << 31);

struct RateTable {
    std::array<int, 1024> values{};
    RateTable() {
        for (std::size_t n = 0; n < values.size(); ++n)
            values[n] = static_cast<int>(65536 * 2 / (2 * static_cast<int>(n) + 3));
    }
};

const RateTable& rate_table() {
    static const RateTable table;
    return table;
}

int counter_p(std::uint32_t counter) {
    return static_cast<int>(counter >> 20);
}

void counter_update(std::uint32_t& counter, int bit) {
    const std::uint32_t count = counter & 1023U;
    const std::int64_t p = static_cast<std::int64_t>(counter >> 10);
    const std::int64_t target = bit != 0 ? (1 << 22) - 1 : 0;
    const std::int64_t rate = rate_table().values[count];
    const std::int64_t next = p + (((target - p) * rate) >> 16);
    const std::uint32_t next_count = count < counter_limit ? count + 1 : count;
    counter = (static_cast<std::uint32_t>(next) << 10) | next_count;
}

// Secondary estimation: refines a probability in a small context by where it falls on
// the stretched scale, 33 buckets interpolated.
struct Refiner {
    std::vector<std::uint16_t> table;
    std::size_t index = 0;
    explicit Refiner(std::size_t contexts) : table(contexts * 33) {
        for (std::size_t context = 0; context < contexts; ++context)
            for (std::size_t j = 0; j < 33; ++j)
                table[context * 33 + j] = static_cast<std::uint16_t>(squash((static_cast<int>(j) - 16) * 128) * 16);
    }
    int refine(int p, std::size_t context) {
        const int s = stretch(p) + 2048;  // 0..4095
        const int low = s >> 7;           // 0..31
        const int weight = s & 127;
        const std::size_t base = context * 33 + static_cast<std::size_t>(low);
        index = weight < 64 ? base : base + 1;
        const int refined = (table[base] * (128 - weight) + table[base + 1] * weight) >> 11;
        return std::clamp(refined, 1, 4095);
    }
    void update(int bit) {
        const int target = bit != 0 ? 65535 : 0;
        const int current = table[index];
        table[index] = static_cast<std::uint16_t>(current + ((target - current) >> 6));
    }
};

constexpr int context_count = 7;   // orders 1, 2, 3, 4, 6; the word; the word and the one before
constexpr int input_count = context_count + 3;  // and order 0, the match, a bias
constexpr int match_minimum = 7;

}  // namespace

struct TextModel::State {
    CoderShape shape;
    std::uint32_t table_mask = 0;
    std::vector<std::uint32_t> tables;              // context_count tables, each 1 << table_bits
    std::array<std::uint32_t, 256> order0{};
    std::array<std::uint32_t, context_count> hashes{};
    std::array<std::size_t, context_count> slots{};
    // The match model: every byte seen, and where each run of match_minimum bytes last
    // ended.
    std::vector<std::uint8_t> history;
    std::vector<std::uint32_t> match_table;
    std::uint32_t match_mask = 0;
    std::size_t match_pointer = 0;  // in history, the byte expected next; 0 for none
    int match_length = 0;
    std::array<std::uint32_t, 64 * 2> match_counters{};
    std::size_t match_slot = 0;
    // The mixer: one set of weights for each partial byte.
    std::vector<int> weights;
    std::array<int, input_count> inputs{};
    std::size_t weight_set = 0;
    Refiner refine_order1{65536};
    Refiner refine_order2{65536};
    int mixed = 2048;
    int refined_a = 2048;
    int refined_b = 2048;
    int prediction = 2048;
    // The byte so far, with a leading one bit (1..255), and the bytes before it.
    std::uint32_t partial = 1;
    int bit_position = 0;
    std::uint32_t last_bytes = 0;  // the four bytes before, the latest lowest
    std::uint32_t older_bytes = 0;
    std::uint32_t word = 0;
    std::uint32_t previous_word = 0;

    explicit State(CoderShape chosen)
        : shape(chosen), table_mask((1U << chosen.table_bits) - 1),
          tables(static_cast<std::size_t>(context_count) << chosen.table_bits, counter_start),
          match_table(static_cast<std::size_t>(1) << chosen.match_bits, 0),
          match_mask((1U << chosen.match_bits) - 1), weights(static_cast<std::size_t>(256 * input_count), 0) {
        order0.fill(counter_start);
        match_counters.fill(counter_start);
        for (std::size_t set = 0; set < 256; ++set)
            for (std::size_t input = 0; input < static_cast<std::size_t>(input_count); ++input)
                weights[set * input_count + input] = input < static_cast<std::size_t>(context_count + 2) ? 20000 : 0;
        history.reserve(1 << 20);
        history.push_back(0);  // position 0 means "no match"
        begin_byte();
    }

    void begin_byte() {
        const std::uint32_t b1 = last_bytes & 0xFFU;
        const std::uint32_t b2 = last_bytes & 0xFFFFU;
        const std::uint32_t b3 = last_bytes & 0xFFFFFFU;
        const std::uint32_t b4 = last_bytes;
        const std::uint32_t b6 = mix_hash(last_bytes, older_bytes & 0xFFFFU);
        hashes[0] = mix_hash(b1, 1);
        hashes[1] = mix_hash(b2, 2);
        hashes[2] = mix_hash(b3, 3);
        hashes[3] = mix_hash(b4, 4);
        hashes[4] = mix_hash(b6, 6);
        hashes[5] = mix_hash(word, 7);
        hashes[6] = mix_hash(mix_hash(word, previous_word), 8);
        find_match();
        compute();
    }

    void find_match() {
        const std::size_t size = history.size();
        if (size <= static_cast<std::size_t>(match_minimum))
            return;
        std::uint32_t h = 0;
        for (std::size_t i = size - static_cast<std::size_t>(match_minimum); i < size; ++i)
            h = h * 0x2F0F1D3U + history[i] + 1;
        h = mix_hash(h, 9) & match_mask;
        if (match_length > 0 && match_pointer < size) {
            match_length = std::min(match_length + 1, 63);
        } else {
            match_length = 0;
            const std::uint32_t candidate = match_table[h];
            if (candidate > 0) {
                int length = 0;
                while (length < 63 && static_cast<std::size_t>(length) < candidate &&
                       history[candidate - 1 - static_cast<std::size_t>(length)] ==
                           history[size - 1 - static_cast<std::size_t>(length)])
                    ++length;
                if (length >= match_minimum) {
                    match_length = length;
                    match_pointer = candidate;
                }
            }
        }
        match_table[h] = static_cast<std::uint32_t>(size);
    }

    void compute() {
        for (std::size_t i = 0; i < static_cast<std::size_t>(context_count); ++i) {
            const std::uint32_t h = mix_hash(hashes[i], partial) & table_mask;
            slots[i] = (i << shape.table_bits) + h;
            inputs[i] = stretch(counter_p(tables[slots[i]]));
        }
        inputs[context_count] = stretch(counter_p(order0[partial]));
        int expected_bit = -1;
        if (match_length > 0 && match_pointer < history.size()) {
            const std::uint32_t expected = history[match_pointer] + 256U;
            const int shift = 8 - bit_position;
            if ((expected >> shift) == partial)
                expected_bit = static_cast<int>((expected >> (shift - 1)) & 1U);
            else
                match_length = 0;
        }
        if (expected_bit >= 0) {
            match_slot = static_cast<std::size_t>(match_length) * 2 + static_cast<std::size_t>(expected_bit);
            inputs[context_count + 1] = stretch(counter_p(match_counters[match_slot]));
        } else {
            match_slot = 0;
            inputs[context_count + 1] = 0;
        }
        inputs[context_count + 2] = 256;
        weight_set = static_cast<std::size_t>(partial) * input_count;
        std::int64_t dot = 0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(input_count); ++i)
            dot += static_cast<std::int64_t>(inputs[i]) * weights[weight_set + i];
        const int stretched = static_cast<int>(std::clamp<std::int64_t>(dot >> 16, -2047, 2047));
        mixed = squash(stretched);
        refined_a = refine_order1.refine(mixed, (partial | ((last_bytes & 0xFFU) << 8)) & 0xFFFFU);
        refined_b = refine_order2.refine(mixed, mix_hash(last_bytes & 0xFFFFU, partial) & 0xFFFFU);
        prediction = std::clamp((mixed * 2 + refined_a + refined_b + 2) >> 2, 1, 4095);
    }

    void update(int bit) {
        const int error = ((bit << 12) - mixed) * 6;
        for (std::size_t i = 0; i < static_cast<std::size_t>(input_count); ++i)
            weights[weight_set + i] += (inputs[i] * error + 512) >> 10;
        for (std::size_t i = 0; i < static_cast<std::size_t>(context_count); ++i)
            counter_update(tables[slots[i]], bit);
        counter_update(order0[partial], bit);
        if (match_slot > 0)
            counter_update(match_counters[match_slot], bit);
        refine_order1.update(bit);
        refine_order2.update(bit);
        partial = (partial << 1) | static_cast<std::uint32_t>(bit);
        ++bit_position;
        if (bit_position == 8) {
            const std::uint8_t byte = static_cast<std::uint8_t>(partial & 0xFFU);
            end_byte(byte);
        } else {
            compute();
        }
    }

    void end_byte(std::uint8_t byte) {
        older_bytes = (older_bytes << 8) | (last_bytes >> 24);
        last_bytes = (last_bytes << 8) | byte;
        const bool letter = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || byte >= 128 ||
                            byte == '\'';
        if (letter) {
            const std::uint32_t folded = byte >= 'A' && byte <= 'Z' ? byte + 32U : byte;
            word = mix_hash(word, folded);
        } else if (word != 0) {
            previous_word = word;
            word = 0;
        }
        if (match_length > 0)
            ++match_pointer;
        history.push_back(byte);
        partial = 1;
        bit_position = 0;
        begin_byte();
    }
};

TextModel::TextModel(CoderShape shape) : state_(std::make_unique<State>(shape)) {}

TextModel::TextModel(const TextModel& other) : state_(std::make_unique<State>(*other.state_)) {}

TextModel& TextModel::operator=(const TextModel& other) {
    if (this != &other)
        state_ = std::make_unique<State>(*other.state_);
    return *this;
}

TextModel::~TextModel() = default;

void TextModel::learn(std::span<const std::uint8_t> text) {
    for (const std::uint8_t byte : text)
        for (int i = 7; i >= 0; --i)
            update((byte >> i) & 1);
}

int TextModel::predict() const {
    return (*state_).prediction;
}

void TextModel::update(int bit) {
    (*state_).update(bit);
}

void encode(TextModel& model, std::span<const std::uint8_t> text, std::vector<std::uint8_t>& out) {
    std::uint32_t low = 0;
    std::uint32_t high = 0xFFFFFFFFU;
    for (const std::uint8_t byte : text) {
        for (int i = 7; i >= 0; --i) {
            const int bit = (byte >> i) & 1;
            const std::uint32_t p = static_cast<std::uint32_t>(model.predict());
            const std::uint32_t range = high - low;
            const std::uint32_t middle = low + (range >> 12) * p + (((range & 0xFFFU) * p) >> 12);
            if (bit != 0)
                high = middle;
            else
                low = middle + 1;
            model.update(bit);
            while (((low ^ high) & 0xFF000000U) == 0) {
                out.push_back(static_cast<std::uint8_t>(high >> 24));
                low <<= 8;
                high = (high << 8) | 0xFFU;
            }
        }
    }
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(low >> 24));
        low <<= 8;
    }
}

bool decode(TextModel& model, std::span<const std::uint8_t> code, std::size_t length,
            std::vector<std::uint8_t>& out) {
    out.clear();
    out.reserve(length);
    std::uint32_t low = 0;
    std::uint32_t high = 0xFFFFFFFFU;
    std::uint32_t x = 0;
    std::size_t next = 0;
    for (int i = 0; i < 4; ++i) {
        x = (x << 8) | (next < code.size() ? code[next] : 0U);
        ++next;
    }
    for (std::size_t n = 0; n < length; ++n) {
        std::uint32_t byte = 0;
        for (int i = 0; i < 8; ++i) {
            const std::uint32_t p = static_cast<std::uint32_t>(model.predict());
            const std::uint32_t range = high - low;
            const std::uint32_t middle = low + (range >> 12) * p + (((range & 0xFFFU) * p) >> 12);
            const int bit = x <= middle ? 1 : 0;
            if (bit != 0)
                high = middle;
            else
                low = middle + 1;
            model.update(bit);
            byte = (byte << 1) | static_cast<std::uint32_t>(bit);
            while (((low ^ high) & 0xFF000000U) == 0) {
                low <<= 8;
                high = (high << 8) | 0xFFU;
                if (next > code.size() + 4) {
                    out.clear();
                    return false;
                }
                x = (x << 8) | (next < code.size() ? code[next] : 0U);
                ++next;
            }
        }
        out.push_back(static_cast<std::uint8_t>(byte));
    }
    return true;
}

}  // namespace pl
