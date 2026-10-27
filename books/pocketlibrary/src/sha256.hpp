#pragma once
// Incremental SHA-256 (FIPS 180-4), from the Pocket Bible's core (src/core/sha256.*,
// stage 2), for the library container's checks. No other use.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace pl {

inline constexpr std::size_t sha256_digest_bytes = 32U;
inline constexpr std::size_t sha256_block_bytes = 64U;
inline constexpr std::size_t sha256_hex_characters = 64U;

using Sha256Digest = std::array<std::uint8_t, sha256_digest_bytes>;

// Incremental SHA-256 as specified by FIPS 180-4.
// update() may be called any number of times. finish() pads, returns the
// digest and leaves the object reset, ready for a new message.
class Sha256 final {
  public:
    Sha256() noexcept;
    void reset() noexcept;
    void update(std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] Sha256Digest finish() noexcept;

  private:
    // Consumes exactly sha256_block_bytes starting at block.
    void compress(const std::uint8_t* block) noexcept;

    std::array<std::uint32_t, 8> state_{};
    // Bytes received but not yet compressed; live length is pending_count_.
    std::array<std::uint8_t, sha256_block_bytes> pending_{};
    std::size_t pending_count_{0U};
    std::uint64_t message_bytes_{0U};
};

[[nodiscard]] Sha256Digest sha256(std::span<const std::uint8_t> bytes) noexcept;

// Sixty-four lowercase hexadecimal characters.
[[nodiscard]] std::string sha256_hex(const Sha256Digest& digest);

// Accepts exactly sixty-four lowercase hexadecimal characters. On failure the
// destination is unchanged.
[[nodiscard]] bool parse_sha256_hex(std::string_view text, Sha256Digest& destination) noexcept;

}  // namespace pl
