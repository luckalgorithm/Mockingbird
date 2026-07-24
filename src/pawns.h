#pragma once

#include <array>
#include <cassert>
#include <cstddef>

#include "bitboard.h"

namespace Mockingbird {

namespace Detail {

[[nodiscard]] constexpr bool is_pawn_start_coordinate(
  Color color, Square square) noexcept {
    return color == RED    ? rank_of(square) == RANK_2
         : color == BLUE   ? file_of(square) == FILE_B
         : color == YELLOW ? rank_of(square) == RANK_13
                           : file_of(square) == FILE_M;
}

[[nodiscard]] constexpr bool is_pawn_promotion_coordinate(
  Color color, Square square) noexcept {
    return color == RED    ? rank_of(square) == RANK_11
         : color == BLUE   ? file_of(square) == FILE_K
         : color == YELLOW ? rank_of(square) == RANK_4
                           : file_of(square) == FILE_D;
}

template<bool Promotion>
[[nodiscard]] consteval std::array<Bitboard, COLOR_NB> make_pawn_zone_masks() {
    std::array<Bitboard, COLOR_NB> masks{};

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);

        for (int square_index = 0; square_index < SQUARE_NB; ++square_index) {
            const Square square = Square(square_index);
            if (!is_ok(square))
                continue;

            const bool in_zone = Promotion
              ? is_pawn_promotion_coordinate(color, square)
              : is_pawn_start_coordinate(color, square);
            if (in_zone)
                masks[std::size_t(color)].set(square);
        }
    }

    return masks;
}

template<bool DoublePush>
[[nodiscard]] consteval std::array<std::array<Square, SQUARE_NB>, COLOR_NB>
make_pawn_push_destinations() {
    std::array<std::array<Square, SQUARE_NB>, COLOR_NB> destinations{};

    for (auto& color_destinations : destinations)
        color_destinations.fill(SQ_NONE);

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);
        const Direction direction = pawn_push(color);

        for (int source_index = 0; source_index < SQUARE_NB; ++source_index) {
            const Square source = Square(source_index);
            if (!is_ok(source))
                continue;
            if (DoublePush && !is_pawn_start_coordinate(color, source))
                continue;

            const Square first = source + direction;
            if (!is_ok(first))
                continue;

            if constexpr (DoublePush) {
                const Square second = first + direction;
                if (is_ok(second))
                    destinations[std::size_t(color)][std::size_t(source)] = second;
            } else {
                destinations[std::size_t(color)][std::size_t(source)] = first;
            }
        }
    }

    return destinations;
}

}  // namespace Detail

// Starting zones contain the pawn squares eligible for a two-square first move.
inline constexpr auto PAWN_START_SQUARES =
  Detail::make_pawn_zone_masks<false>();

// Promotion zones contain the destination squares where a pawn must promote.
inline constexpr auto PAWN_PROMOTION_SQUARES =
  Detail::make_pawn_zone_masks<true>();

// Entries contain the geometric destination without considering occupancy.
// SQ_NONE marks a source that has no playable destination.
inline constexpr auto PAWN_PUSH_DESTINATIONS =
  Detail::make_pawn_push_destinations<false>();

// Entries are defined only for sources in PAWN_START_SQUARES. Occupancy of the
// intermediate and destination squares is not considered.
inline constexpr auto PAWN_DOUBLE_PUSH_DESTINATIONS =
  Detail::make_pawn_push_destinations<true>();

// Preconditions: color is valid and square is in the mailbox range 0..255.
[[nodiscard]] constexpr bool is_pawn_start_square(
  Color color, Square square) noexcept {
    assert(is_ok(color));
    assert(static_cast<unsigned>(square) < SQUARE_NB);
    return PAWN_START_SQUARES[std::size_t(color)].test(square);
}

// Preconditions: color is valid and square is in the mailbox range 0..255.
[[nodiscard]] constexpr bool is_pawn_promotion_square(
  Color color, Square square) noexcept {
    assert(is_ok(color));
    assert(static_cast<unsigned>(square) < SQUARE_NB);
    return PAWN_PROMOTION_SQUARES[std::size_t(color)].test(square);
}

// Preconditions: color is valid and source is in the mailbox range 0..255.
[[nodiscard]] constexpr Square pawn_push_destination(
  Color color, Square source) noexcept {
    assert(is_ok(color));
    assert(static_cast<unsigned>(source) < SQUARE_NB);
    return PAWN_PUSH_DESTINATIONS[std::size_t(color)][std::size_t(source)];
}

// Preconditions: color is valid and source is in the mailbox range 0..255.
[[nodiscard]] constexpr Square pawn_double_push_destination(
  Color color, Square source) noexcept {
    assert(is_ok(color));
    assert(static_cast<unsigned>(source) < SQUARE_NB);
    return PAWN_DOUBLE_PUSH_DESTINATIONS[std::size_t(color)][std::size_t(source)];
}

static_assert(PAWN_START_SQUARES[RED].popcount() == 8);
static_assert(PAWN_START_SQUARES[BLUE].popcount() == 8);
static_assert(PAWN_START_SQUARES[YELLOW].popcount() == 8);
static_assert(PAWN_START_SQUARES[GREEN].popcount() == 8);
static_assert(PAWN_PROMOTION_SQUARES[RED].popcount() == 14);
static_assert(PAWN_PROMOTION_SQUARES[BLUE].popcount() == 14);
static_assert(PAWN_PROMOTION_SQUARES[YELLOW].popcount() == 14);
static_assert(PAWN_PROMOTION_SQUARES[GREEN].popcount() == 14);

}  // namespace Mockingbird
