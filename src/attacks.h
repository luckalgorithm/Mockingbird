#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "bitboard.h"

namespace Mockingbird {

// RayDirection identifies the eight straight and diagonal directions used by
// sliding pieces.
enum class RayDirection : std::uint8_t {
    NORTH,
    NORTH_EAST,
    EAST,
    SOUTH_EAST,
    SOUTH,
    SOUTH_WEST,
    WEST,
    NORTH_WEST,
    COUNT
};

inline constexpr std::size_t RAY_DIRECTION_NB =
  static_cast<std::size_t>(std::to_underlying(RayDirection::COUNT));

[[nodiscard]] constexpr bool is_ok(RayDirection direction) noexcept {
    return std::to_underlying(direction) < RAY_DIRECTION_NB;
}

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

// Pawn capture offsets are indexed by Color.
inline constexpr std::array<std::array<int, 2>, COLOR_NB> PAWN_OFFSETS = {{
  {NORTH_WEST, NORTH_EAST},
  {NORTH_EAST, SOUTH_EAST},
  {SOUTH_EAST, SOUTH_WEST},
  {SOUTH_WEST, NORTH_WEST},
}};

// Mailbox offsets use the same order as RayDirection.
inline constexpr std::array<Direction, RAY_DIRECTION_NB> RAY_OFFSETS = {
  NORTH,
  NORTH_EAST,
  EAST,
  SOUTH_EAST,
  SOUTH,
  SOUTH_WEST,
  WEST,
  NORTH_WEST,
};

inline constexpr std::array<RayDirection, 4> ROOK_DIRECTIONS = {
  RayDirection::NORTH,
  RayDirection::EAST,
  RayDirection::SOUTH,
  RayDirection::WEST,
};

inline constexpr std::array<RayDirection, 4> BISHOP_DIRECTIONS = {
  RayDirection::NORTH_EAST,
  RayDirection::SOUTH_EAST,
  RayDirection::SOUTH_WEST,
  RayDirection::NORTH_WEST,
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

[[nodiscard]] consteval std::array<Bitboard, SQUARE_NB> make_ray_table(Direction offset) {
    std::array<Bitboard, SQUARE_NB> table{};

    for (int source_index = 0; source_index < SQUARE_NB; ++source_index) {
        const Square source = Square(source_index);
        if (!is_ok(source))
            continue;

        for (int destination_index = source_index + int(offset);
             is_ok(Square(destination_index));
             destination_index += int(offset))
            table[std::size_t(source)].set(Square(destination_index));
    }

    return table;
}

[[nodiscard]] consteval std::array<std::array<Bitboard, SQUARE_NB>, RAY_DIRECTION_NB>
make_ray_tables() {
    std::array<std::array<Bitboard, SQUARE_NB>, RAY_DIRECTION_NB> tables{};

    for (std::size_t index = 0; index < RAY_DIRECTION_NB; ++index)
        tables[index] = make_ray_table(RAY_OFFSETS[index]);

    return tables;
}

}  // namespace Detail

// Each table entry contains the playable destinations for one mailbox index.
// Entries for padding and cut-out corner indices are empty.
inline constexpr auto KING_ATTACKS = Detail::make_attack_table(Detail::KING_OFFSETS);
inline constexpr auto KNIGHT_ATTACKS = Detail::make_attack_table(Detail::KNIGHT_OFFSETS);
inline constexpr std::array<std::array<Bitboard, SQUARE_NB>, COLOR_NB> PAWN_ATTACKS = {
  Detail::make_attack_table(Detail::PAWN_OFFSETS[RED]),
  Detail::make_attack_table(Detail::PAWN_OFFSETS[BLUE]),
  Detail::make_attack_table(Detail::PAWN_OFFSETS[YELLOW]),
  Detail::make_attack_table(Detail::PAWN_OFFSETS[GREEN]),
};

// A ray contains every playable square from its source to the first board edge
// in one direction. The source square is not part of its ray.
inline constexpr auto RAY_ATTACKS = Detail::make_ray_tables();

namespace Detail {

template<RayDirection Direction>
[[nodiscard]] constexpr Bitboard blocked_horizontal_ray_attacks(
  Square square, const Bitboard& occupied) noexcept {
    static_assert(
      Direction == RayDirection::EAST
      || Direction == RayDirection::WEST);
    constexpr std::size_t direction_index = std::to_underlying(Direction);
    const Bitboard& ray = RAY_ATTACKS[direction_index][std::size_t(square)];
    const std::size_t limb_index =
      static_cast<unsigned>(square) / Bitboard::BITS_PER_LIMB;
    // Four complete 16-square mailbox ranks fit in each 64-bit limb, so a
    // horizontal ray never crosses a limb boundary.
    const std::uint64_t blockers =
      ray.intersection_limb(occupied, limb_index);

    if (blockers == 0)
        return ray;

    int bit;
    if constexpr (Direction == RayDirection::EAST) {
        bit = std::countr_zero(blockers);
    } else {
        bit = static_cast<int>(Bitboard::BITS_PER_LIMB) - 1
            - std::countl_zero(blockers);
    }
    const Square first_blocker =
      Square(int(limb_index * Bitboard::BITS_PER_LIMB) + bit);
    // The blocker tail excludes the blocker itself, so XOR retains every
    // square through the first occupied square.
    return ray ^ RAY_ATTACKS[direction_index][std::size_t(first_blocker)];
}

template<RayDirection Direction>
[[nodiscard]] constexpr Bitboard blocked_multilimb_ray_attacks(
  Square square, const Bitboard& occupied) noexcept {
    static_assert(
      Direction != RayDirection::EAST
      && Direction != RayDirection::WEST);
    constexpr std::size_t direction_index = std::to_underlying(Direction);
    const Bitboard& ray = RAY_ATTACKS[direction_index][std::size_t(square)];
    Square first_blocker;
    if constexpr (int(RAY_OFFSETS[direction_index]) > 0)
        first_blocker = ray.lsb_intersection(occupied);
    else
        first_blocker = ray.msb_intersection(occupied);

    if (first_blocker == SQ_NONE)
        return ray;

    return ray ^ RAY_ATTACKS[direction_index][std::size_t(first_blocker)];
}

}  // namespace Detail

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

// Preconditions: color is valid and square is in the mailbox index range
// 0..255.
[[nodiscard]] constexpr const Bitboard& pawn_attacks(Color color, Square square) noexcept {
    assert(is_ok(color));
    assert(static_cast<unsigned>(square) < SQUARE_NB);
    return PAWN_ATTACKS[std::size_t(color)][std::size_t(square)];
}

// Returns the union of all destinations attacked from the source set by the
// pawn geometry for color. Mailbox padding and cut-out corners are removed
// after shifting. Every set source square must be playable.
// Precondition: color is valid.
[[nodiscard]] constexpr Bitboard pawn_attacks(
  Color color, const Bitboard& sources) noexcept {
    assert(is_ok(color));

    Bitboard attacks;
    switch (color) {
        case RED:
            attacks = (sources << 15) | (sources << 17);
            break;
        case BLUE:
            attacks = (sources << 17) | (sources >> 15);
            break;
        case YELLOW:
            attacks = (sources >> 15) | (sources >> 17);
            break;
        case GREEN:
            attacks = (sources >> 17) | (sources << 15);
            break;
        case COLOR_NB:
            return {};
    }

    return attacks & PLAYABLE_SQUARES;
}

// Preconditions: direction is valid and square is in the mailbox index range
// 0..255.
[[nodiscard]] constexpr const Bitboard& ray_attacks(
  RayDirection direction, Square square) noexcept {
    assert(is_ok(direction));
    assert(static_cast<unsigned>(square) < SQUARE_NB);
    return RAY_ATTACKS[std::to_underlying(direction)][std::size_t(square)];
}

// Returns the squares from the source through the first occupied square in the
// selected direction. Squares beyond the first occupied square are excluded.
// Preconditions: direction is valid and square is in the mailbox index range
// 0..255.
[[nodiscard]] constexpr Bitboard ray_attacks(
  RayDirection direction, Square square, const Bitboard& occupied) noexcept {
    const Bitboard ray = ray_attacks(direction, square);
    const Bitboard blockers = ray & occupied;

    if (blockers.empty())
        return ray;

    const Direction offset = Detail::RAY_OFFSETS[std::to_underlying(direction)];
    const Square first_blocker = int(offset) > 0 ? blockers.lsb() : blockers.msb();
    // The blocker tail is a subset of the source ray, so XOR removes it while
    // retaining the first occupied square.
    return ray ^ ray_attacks(direction, first_blocker);
}

// Occupied squares stop a ray but remain in the returned attack set.
// Precondition: square is in the mailbox index range 0..255.
[[nodiscard]] constexpr Bitboard rook_attacks(
    Square square, const Bitboard& occupied = {}) noexcept {
    assert(static_cast<unsigned>(square) < SQUARE_NB);
    return Detail::blocked_multilimb_ray_attacks<RayDirection::NORTH>(
             square, occupied)
         | Detail::blocked_horizontal_ray_attacks<RayDirection::EAST>(
             square, occupied)
         | Detail::blocked_multilimb_ray_attacks<RayDirection::SOUTH>(
             square, occupied)
         | Detail::blocked_horizontal_ray_attacks<RayDirection::WEST>(
             square, occupied);
}

// Occupied squares stop a ray but remain in the returned attack set.
// Precondition: square is in the mailbox index range 0..255.
[[nodiscard]] constexpr Bitboard bishop_attacks(
  Square square, const Bitboard& occupied = {}) noexcept {
    assert(static_cast<unsigned>(square) < SQUARE_NB);
    return Detail::blocked_multilimb_ray_attacks<RayDirection::NORTH_EAST>(
             square, occupied)
         | Detail::blocked_multilimb_ray_attacks<RayDirection::SOUTH_EAST>(
             square, occupied)
         | Detail::blocked_multilimb_ray_attacks<RayDirection::SOUTH_WEST>(
             square, occupied)
         | Detail::blocked_multilimb_ray_attacks<RayDirection::NORTH_WEST>(
             square, occupied);
}

// Occupied squares stop a ray but remain in the returned attack set.
// Precondition: square is in the mailbox index range 0..255.
[[nodiscard]] constexpr Bitboard queen_attacks(
  Square square, const Bitboard& occupied = {}) noexcept {
    return rook_attacks(square, occupied) | bishop_attacks(square, occupied);
}

static_assert(king_attacks(make_square(FILE_H, RANK_8)).popcount() == 8);
static_assert(knight_attacks(make_square(FILE_H, RANK_8)).popcount() == 8);
static_assert(pawn_attacks(RED, make_square(FILE_H, RANK_8)).popcount() == 2);
static_assert(pawn_attacks(BLUE, make_square(FILE_H, RANK_8)).popcount() == 2);
static_assert(pawn_attacks(YELLOW, make_square(FILE_H, RANK_8)).popcount() == 2);
static_assert(pawn_attacks(GREEN, make_square(FILE_H, RANK_8)).popcount() == 2);
static_assert(queen_attacks(make_square(FILE_H, RANK_8))
              == (rook_attacks(make_square(FILE_H, RANK_8))
                  | bishop_attacks(make_square(FILE_H, RANK_8))));

}  // namespace Mockingbird
