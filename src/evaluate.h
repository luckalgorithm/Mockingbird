#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "position.h"

namespace Mockingbird {

using Score = std::int32_t;

// One pawn is represented by 100 score units. Kings have no material value
// because terminal results are scored separately from static evaluation.
inline constexpr Score PAWN_VALUE = 100;
inline constexpr Score KNIGHT_VALUE = 320;
inline constexpr Score BISHOP_VALUE = 330;
inline constexpr Score ROOK_VALUE = 500;
inline constexpr Score QUEEN_VALUE = 900;
inline constexpr Score KING_VALUE = 0;

inline constexpr std::array<Score, PIECE_TYPE_NB> PIECE_VALUES = {
  0,
  PAWN_VALUE,
  KNIGHT_VALUE,
  BISHOP_VALUE,
  ROOK_VALUE,
  QUEEN_VALUE,
  KING_VALUE,
};

inline constexpr Score MAX_PIECE_VALUE = [] {
    Score maximum = 0;

    for (const Score value : PIECE_VALUES) {
        if (value > maximum)
            maximum = value;
    }

    return maximum;
}();

inline constexpr Score MAX_MATERIAL_SCORE =
  static_cast<Score>(PLAYABLE_SQUARE_NB)
  * MAX_PIECE_VALUE;

// Precondition: piece_type is a real piece type.
[[nodiscard]] constexpr Score piece_value(
  PieceType piece_type) noexcept {
    assert(is_ok(piece_type));
    return PIECE_VALUES[std::size_t(piece_type)];
}

// Returns friendly material minus opposing material for perspective.
// Precondition: perspective is a valid team.
[[nodiscard]] constexpr Score material_balance(
  const Position& position,
  Team perspective) noexcept {
    assert(is_ok(perspective));

    const Team opponent =
      perspective == RED_YELLOW
        ? BLUE_GREEN
        : RED_YELLOW;
    const Bitboard friendly_pieces =
      position.pieces(perspective);
    const Bitboard opposing_pieces =
      position.pieces(opponent);

    Score score = 0;
    for (int type_index = PAWN;
         type_index <= QUEEN;
         ++type_index) {
        const PieceType piece_type =
          PieceType(type_index);
        const Bitboard& pieces_of_type =
          position.pieces(piece_type);
        const int friendly_count =
          (friendly_pieces & pieces_of_type).popcount();
        const int opposing_count =
          (opposing_pieces & pieces_of_type).popcount();
        const Score count_difference =
          static_cast<Score>(
            friendly_count - opposing_count);

        score +=
          count_difference * piece_value(piece_type);
    }

    return score;
}

// The score is positive when the side-to-move team has more material and
// negative when the opposing team has more material.
[[nodiscard]] constexpr Score evaluate(
  const Position& position) noexcept {
    return material_balance(
      position, team_of(position.side_to_move()));
}

static_assert(std::numeric_limits<Score>::is_signed);
static_assert(sizeof(Score) >= 4);
static_assert(MAX_PIECE_VALUE == QUEEN_VALUE);
static_assert(MAX_MATERIAL_SCORE == 144000);
static_assert(
  MAX_MATERIAL_SCORE
  < std::numeric_limits<Score>::max());
static_assert(piece_value(KING) == 0);
static_assert(evaluate(Position{}) == 0);

}  // namespace Mockingbird
