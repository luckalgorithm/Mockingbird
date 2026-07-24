#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "types.h"

namespace Mockingbird {

// Bitboard stores 256 bits as four 64-bit limbs.

// A mailbox Square maps directly to the bit with the same numeric index.
// Limb 0 contains bits 0..63 and limb 3 contains bits 192..255.
class Bitboard {
  public:
    static constexpr std::size_t LIMB_NB = 4;
    static constexpr unsigned BITS_PER_LIMB = 64;
    static constexpr unsigned BIT_NB = LIMB_NB * BITS_PER_LIMB;

    constexpr Bitboard() noexcept = default;

    [[nodiscard]] static constexpr Bitboard from_square(Square square) noexcept {
        Bitboard bitboard;
        bitboard.set(square);
        return bitboard;
    }

    // Precondition: square is in the mailbox index range 0..255.
    constexpr void set(Square square) noexcept {
        assert(is_mailbox_index(square));
        limbs_[limb_index(square)] |= square_mask(square);
    }

    // Precondition: square is in the mailbox index range 0..255.
    constexpr void clear(Square square) noexcept {
        assert(is_mailbox_index(square));
        limbs_[limb_index(square)] &= ~square_mask(square);
    }

    // Sets all 256 bits to zero.
    constexpr void clear() noexcept {
        limbs_.fill(0);
    }

    // Precondition: square is in the mailbox index range 0..255.
    [[nodiscard]] constexpr bool test(Square square) const noexcept {
        assert(is_mailbox_index(square));
        return (limbs_[limb_index(square)] & square_mask(square)) != 0;
    }

    [[nodiscard]] constexpr bool empty() const noexcept {
        return (limbs_[0] | limbs_[1] | limbs_[2] | limbs_[3]) == 0;
    }

    [[nodiscard]] constexpr explicit operator bool() const noexcept {
        return !empty();
    }

    [[nodiscard]] constexpr int popcount() const noexcept {
        return std::popcount(limbs_[0]) + std::popcount(limbs_[1])
             + std::popcount(limbs_[2]) + std::popcount(limbs_[3]);
    }

    // Precondition: the bitboard is not empty.
    [[nodiscard]] constexpr Square lsb() const noexcept {
        assert(!empty());

        for (std::size_t index = 0; index < LIMB_NB; ++index) {
            if (limbs_[index] != 0) {
                const int bit = std::countr_zero(limbs_[index]);
                return Square(int(index * BITS_PER_LIMB) + bit);
            }
        }

        return SQ_NONE;
    }

    // Precondition: the bitboard is not empty.
    [[nodiscard]] constexpr Square msb() const noexcept {
        assert(!empty());

        for (std::size_t index = LIMB_NB; index-- > 0;) {
            if (limbs_[index] != 0) {
                const int bit =
                  static_cast<int>(BITS_PER_LIMB) - 1 - std::countl_zero(limbs_[index]);
                return Square(int(index * BITS_PER_LIMB) + bit);
            }
        }

        return SQ_NONE;
    }

    // Clears and returns the least-significant set bit.
    // Precondition: the bitboard is not empty.
    constexpr Square pop_lsb() noexcept {
        const Square square = lsb();
        const std::size_t index = limb_index(square);
        limbs_[index] &= limbs_[index] - 1;
        return square;
    }

    constexpr Bitboard& operator&=(const Bitboard& other) noexcept {
        for (std::size_t index = 0; index < LIMB_NB; ++index)
            limbs_[index] &= other.limbs_[index];
        return *this;
    }

    constexpr Bitboard& operator|=(const Bitboard& other) noexcept {
        for (std::size_t index = 0; index < LIMB_NB; ++index)
            limbs_[index] |= other.limbs_[index];
        return *this;
    }

    constexpr Bitboard& operator^=(const Bitboard& other) noexcept {
        for (std::size_t index = 0; index < LIMB_NB; ++index)
            limbs_[index] ^= other.limbs_[index];
        return *this;
    }

    constexpr Bitboard& operator<<=(unsigned shift) noexcept {
        if (shift >= BIT_NB) {
            clear();
            return *this;
        }

        const std::size_t limb_shift = shift / BITS_PER_LIMB;
        const unsigned bit_shift = shift % BITS_PER_LIMB;
        std::array<std::uint64_t, LIMB_NB> shifted{};

        for (std::size_t destination = limb_shift; destination < LIMB_NB; ++destination) {
            const std::size_t source = destination - limb_shift;
            shifted[destination] = limbs_[source] << bit_shift;

            if (bit_shift != 0 && source > 0)
                shifted[destination] |= limbs_[source - 1] >> (BITS_PER_LIMB - bit_shift);
        }

        limbs_ = shifted;
        return *this;
    }

    constexpr Bitboard& operator>>=(unsigned shift) noexcept {
        if (shift >= BIT_NB) {
            clear();
            return *this;
        }

        const std::size_t limb_shift = shift / BITS_PER_LIMB;
        const unsigned bit_shift = shift % BITS_PER_LIMB;
        std::array<std::uint64_t, LIMB_NB> shifted{};

        for (std::size_t destination = 0; destination + limb_shift < LIMB_NB; ++destination) {
            const std::size_t source = destination + limb_shift;
            shifted[destination] = limbs_[source] >> bit_shift;

            if (bit_shift != 0 && source + 1 < LIMB_NB)
                shifted[destination] |= limbs_[source + 1] << (BITS_PER_LIMB - bit_shift);
        }

        limbs_ = shifted;
        return *this;
    }

    [[nodiscard]] friend constexpr Bitboard operator&(
      Bitboard left, const Bitboard& right) noexcept {
        left &= right;
        return left;
    }

    [[nodiscard]] friend constexpr Bitboard operator|(
      Bitboard left, const Bitboard& right) noexcept {
        left |= right;
        return left;
    }

    [[nodiscard]] friend constexpr Bitboard operator^(
      Bitboard left, const Bitboard& right) noexcept {
        left ^= right;
        return left;
    }

    [[nodiscard]] friend constexpr Bitboard operator~(Bitboard bitboard) noexcept {
        for (std::uint64_t& limb : bitboard.limbs_)
            limb = ~limb;
        return bitboard;
    }

    [[nodiscard]] friend constexpr Bitboard operator<<(Bitboard bitboard, unsigned shift) noexcept {
        bitboard <<= shift;
        return bitboard;
    }

    [[nodiscard]] friend constexpr Bitboard operator>>(Bitboard bitboard, unsigned shift) noexcept {
        bitboard >>= shift;
        return bitboard;
    }

    [[nodiscard]] friend constexpr bool operator==(
      const Bitboard&, const Bitboard&) noexcept = default;

  private:
    [[nodiscard]] static constexpr bool is_mailbox_index(Square square) noexcept {
        return static_cast<unsigned>(square) < BIT_NB;
    }

    [[nodiscard]] static constexpr std::size_t limb_index(Square square) noexcept {
        return static_cast<unsigned>(square) / BITS_PER_LIMB;
    }

    [[nodiscard]] static constexpr std::uint64_t square_mask(Square square) noexcept {
        return std::uint64_t{1} << (static_cast<unsigned>(square) % BITS_PER_LIMB);
    }

    std::array<std::uint64_t, LIMB_NB> limbs_{};
};

// PLAYABLE_SQUARES contains one set bit for each playable board square.
inline constexpr Bitboard PLAYABLE_SQUARES = [] {
    Bitboard bitboard;

    for (int index = 0; index < SQUARE_NB; ++index) {
        const Square square = Square(index);
        if (is_ok(square))
            bitboard.set(square);
    }

    return bitboard;
}();

static_assert(Bitboard::BIT_NB == SQUARE_NB);
static_assert(PLAYABLE_SQUARES.popcount() == PLAYABLE_SQUARE_NB);

}  // namespace Mockingbird
