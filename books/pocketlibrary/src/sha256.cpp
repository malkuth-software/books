#include "sha256.hpp"

#include <bit>

#if defined(__ARM_FEATURE_SHA2)
#include <arm_neon.h>
#endif

namespace pl {
namespace {

constexpr std::array<std::uint32_t, 64> round_constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

constexpr std::array<std::uint32_t, 8> initial_state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                                     0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

constexpr char hex_digits[] = "0123456789abcdef";

// Returns 0..15 for a lowercase hexadecimal digit and -1 otherwise.
int hex_value(char character) noexcept {
    int value = -1;
    if (character >= '0' && character <= '9') {
        value = character - '0';
    } else if (character >= 'a' && character <= 'f') {
        value = character - 'a' + 10;
    }
    return value;
}

} // namespace

Sha256::Sha256() noexcept {
    reset();
}

void Sha256::reset() noexcept {
    state_ = initial_state;
    pending_.fill(0U);
    pending_count_ = 0U;
    message_bytes_ = 0U;
}

void Sha256::compress(const std::uint8_t* block) noexcept {
#if defined(__ARM_FEATURE_SHA2)
    // The processor's SHA-256 instructions: four rounds an instruction pair, about
    // fifteen times the arithmetic below. The same digest.
    uint32x4_t first_half = vld1q_u32(state_.data());
    uint32x4_t second_half = vld1q_u32(state_.data() + 4);
    const uint32x4_t first_saved = first_half;
    const uint32x4_t second_saved = second_half;
    std::array<uint32x4_t, 4> words{};
    for (std::size_t index = 0U; index < 4U; ++index)
        words[index] = vreinterpretq_u32_u8(vrev32q_u8(vld1q_u8(block + index * 16U)));
    for (std::size_t group = 0U; group < 16U; ++group) {
        const uint32x4_t scheduled = vaddq_u32(words[group & 3U], vld1q_u32(round_constants.data() + group * 4U));
        if (group < 12U)
            words[group & 3U] = vsha256su1q_u32(vsha256su0q_u32(words[group & 3U], words[(group + 1U) & 3U]),
                                                words[(group + 2U) & 3U], words[(group + 3U) & 3U]);
        const uint32x4_t before = first_half;
        first_half = vsha256hq_u32(first_half, second_half, scheduled);
        second_half = vsha256h2q_u32(second_half, before, scheduled);
    }
    vst1q_u32(state_.data(), vaddq_u32(first_half, first_saved));
    vst1q_u32(state_.data() + 4, vaddq_u32(second_half, second_saved));
#else
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t index = 0U; index < 16U; ++index) {
        const std::size_t offset = index * 4U;
        schedule[index] = (static_cast<std::uint32_t>(block[offset]) << 24U) |
                          (static_cast<std::uint32_t>(block[offset + 1U]) << 16U) |
                          (static_cast<std::uint32_t>(block[offset + 2U]) << 8U) |
                          static_cast<std::uint32_t>(block[offset + 3U]);
    }
    for (std::size_t index = 16U; index < 64U; ++index) {
        const std::uint32_t early = schedule[index - 15U];
        const std::uint32_t late = schedule[index - 2U];
        const std::uint32_t small_sigma_0 = std::rotr(early, 7) ^ std::rotr(early, 18) ^ (early >> 3U);
        const std::uint32_t small_sigma_1 = std::rotr(late, 17) ^ std::rotr(late, 19) ^ (late >> 10U);
        schedule[index] = schedule[index - 16U] + small_sigma_0 + schedule[index - 7U] + small_sigma_1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t index = 0U; index < 64U; ++index) {
        const std::uint32_t big_sigma_1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const std::uint32_t choice = (e & f) ^ (~e & g);
        const std::uint32_t first = h + big_sigma_1 + choice + round_constants[index] + schedule[index];
        const std::uint32_t big_sigma_0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t second = big_sigma_0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
#endif
}

void Sha256::update(std::span<const std::uint8_t> bytes) noexcept {
    const std::size_t count = bytes.size();
    if (count == 0U) {
        return;
    }
    message_bytes_ += static_cast<std::uint64_t>(count);
    std::size_t consumed = 0U;

    // Complete a partly filled block first.
    if (pending_count_ > 0U) {
        const std::size_t room = sha256_block_bytes - pending_count_;
        const std::size_t taken = count < room ? count : room;
        for (std::size_t index = 0U; index < taken; ++index) {
            pending_[pending_count_ + index] = bytes[index];
        }
        pending_count_ += taken;
        consumed = taken;
        if (pending_count_ < sha256_block_bytes) {
            return;
        }
        compress(pending_.data());
        pending_count_ = 0U;
    }

    // Whole blocks are compressed in place, without copying.
    while (count - consumed >= sha256_block_bytes) {
        compress(bytes.data() + consumed);
        consumed += sha256_block_bytes;
    }

    const std::size_t remaining = count - consumed;
    for (std::size_t index = 0U; index < remaining; ++index) {
        pending_[index] = bytes[consumed + index];
    }
    pending_count_ = remaining;
}

Sha256Digest Sha256::finish() noexcept {
    const std::uint64_t message_bits = message_bytes_ * 8U;

    // Padding: one 0x80 byte, zeros to 56 modulo 64, then the bit length as a
    // big-endian 64-bit integer.
    pending_[pending_count_] = 0x80U;
    ++pending_count_;
    if (pending_count_ > sha256_block_bytes - 8U) {
        for (std::size_t index = pending_count_; index < sha256_block_bytes; ++index) {
            pending_[index] = 0U;
        }
        compress(pending_.data());
        pending_count_ = 0U;
    }
    for (std::size_t index = pending_count_; index < sha256_block_bytes - 8U; ++index) {
        pending_[index] = 0U;
    }
    for (std::size_t index = 0U; index < 8U; ++index) {
        const unsigned int shift = static_cast<unsigned int>((7U - index) * 8U);
        pending_[sha256_block_bytes - 8U + index] = static_cast<std::uint8_t>(message_bits >> shift);
    }
    compress(pending_.data());

    Sha256Digest digest{};
    for (std::size_t word = 0U; word < 8U; ++word) {
        digest[word * 4U] = static_cast<std::uint8_t>(state_[word] >> 24U);
        digest[word * 4U + 1U] = static_cast<std::uint8_t>(state_[word] >> 16U);
        digest[word * 4U + 2U] = static_cast<std::uint8_t>(state_[word] >> 8U);
        digest[word * 4U + 3U] = static_cast<std::uint8_t>(state_[word]);
    }
    reset();
    return digest;
}

Sha256Digest sha256(std::span<const std::uint8_t> bytes) noexcept {
    Sha256 hasher;
    hasher.update(bytes);
    const Sha256Digest digest = hasher.finish();
    return digest;
}

std::string sha256_hex(const Sha256Digest& digest) {
    std::string text(sha256_hex_characters, '0');
    for (std::size_t index = 0U; index < sha256_digest_bytes; ++index) {
        const std::uint8_t value = digest[index];
        text[index * 2U] = hex_digits[value >> 4U];
        text[index * 2U + 1U] = hex_digits[value & 0x0fU];
    }
    return text;
}

bool parse_sha256_hex(std::string_view text, Sha256Digest& destination) noexcept {
    if (text.size() != sha256_hex_characters) {
        return false;
    }
    Sha256Digest parsed{};
    for (std::size_t index = 0U; index < sha256_digest_bytes; ++index) {
        const int high = hex_value(text[index * 2U]);
        const int low = hex_value(text[index * 2U + 1U]);
        if (high < 0 || low < 0) {
            return false;
        }
        parsed[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    destination = parsed;
    return true;
}

}  // namespace pl
