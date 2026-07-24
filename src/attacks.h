#pragma once

#include <array>
#include <cassert>
#include <cstddef>

#include "bitboard.h"

namespace Mockingbird {

namespace Detail {

// Offsets are measured in the 16x16 mailbox.
inline constexpr std::array<int, 8> KING_OFFSETS = {
  NORTH,
  NORTH_EAST,
  EAST,
  SOUTH_EAST,
  SOUTH,
  SOUTH_WEST,
  WEST,
  NORTH_WEST,
};

// A knight moves two mailbox cells on one axis and one on the other.
inline constexpr std::array<int, 8> KNIGHT_OFFSETS = {
  2 * NORTH + EAST,
  2 * NORTH + WEST,
  NORTH + 2 * EAST,
  NORTH + 2 * WEST,
  SOUTH + 2 * EAST,
  SOUTH + 2 * WEST,
  2 * SOUTH + EAST,
  2 * SOUTH + WEST,
};

template<std::size_t OffsetCount>
[[nodiscard]] consteval std::array<Bitboard, SQUARE_NB> make_attack_table(
  const std::array<int, OffsetCount>& offsets) {
    std::array<Bitboard, SQUARE_NB> table{};

    for (int source_index = 0; source_index < SQUARE_NB; ++source_index) {
        const Square source = Square(source_index);
        if (!is_ok(source))
            continue;

        for (const int offset : offsets) {
            const Square destination = Square(source_index + offset);
            if (is_ok(destination))
                table[std::size_t(source)].set(destination);
        }
    }

    return table;
}

}  // namespace Detail

// Each table entry contains the playable destinations for one mailbox index.
// Entries for padding and cut-out corner indices are empty.
inline constexpr auto KING_ATTACKS = Detail::make_attack_table(Detail::KING_OFFSETS);
inline constexpr auto KNIGHT_ATTACKS = Detail::make_attack_table(Detail::KNIGHT_OFFSETS);

// Precondition: square is in the mailbox index range 0..255.
[[nodiscard]] constexpr const Bitboard& king_attacks(Square square) noexcept {
    assert(static_cast<unsigned>(square) < SQUARE_NB);
    return KING_ATTACKS[std::size_t(square)];
}

// Precondition: square is in the mailbox index range 0..255.
[[nodiscard]] constexpr const Bitboard& knight_attacks(Square square) noexcept {
    assert(static_cast<unsigned>(square) < SQUARE_NB);
    return KNIGHT_ATTACKS[std::size_t(square)];
}

static_assert(king_attacks(make_square(FILE_H, RANK_8)).popcount() == 8);
static_assert(knight_attacks(make_square(FILE_H, RANK_8)).popcount() == 8);

}  // namespace Mockingbird
