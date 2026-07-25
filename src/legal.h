#pragma once

#include <cassert>

#include "checks.h"
#include "movegen.h"
#include "transition.h"

namespace Mockingbird {

namespace Detail {

// A complete active position contains one king belonging to each color.
[[nodiscard]] constexpr bool has_exactly_one_king_per_color(
  const Position& position) noexcept {
    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        if (position.pieces(Color(color_index), KING).popcount() != 1)
            return false;
    }

    return true;
}

// The position is restored before this function returns.
// Preconditions: move is generated pseudo-legal, and the position contains
// exactly one king belonging to each color.
[[nodiscard]] constexpr bool is_legal_move_with_complete_king_set(
  Position& position, Move move) noexcept {
    assert(is_ok(move));

    const Color moving_color = position.side_to_move();
    const Piece destination_piece =
      position.empty(move.to()) ? NO_PIECE : position.piece_on(move.to());
    const bool captures_opposing_king =
      destination_piece != NO_PIECE
      && type_of(destination_piece) == KING
      && team_of(color_of(destination_piece)) != team_of(moving_color);

    UndoState undo;
    do_move(position, move, undo);

    // Capturing an opposing king ends the game before mover king safety is
    // evaluated. Other moves must leave the moving color's king unattacked.
    const bool legal =
      captures_opposing_king || !in_check(position, moving_color);

    undo_move(position, move, undo);
    return legal;
}

}  // namespace Detail

// Tests a generated pseudo-legal move and restores the position before return.
// A position with a missing or duplicate king of any color is rejected.
// Precondition: move was generated from position by generate_moves().
[[nodiscard]] constexpr bool is_legal_move(
  Position& position, Move move) noexcept {
    assert(is_ok(move));

    if (!Detail::has_exactly_one_king_per_color(position))
        return false;

    return Detail::is_legal_move_with_complete_king_set(position, move);
}

// Appends legal moves in the order produced by generate_moves(). A position
// with a missing or duplicate king of any color appends no moves. The position
// is restored after every candidate.
// Precondition: moves has enough remaining capacity for the appended moves.
constexpr void generate_legal_moves(
  Position& position, MoveList& moves) noexcept {
    if (!Detail::has_exactly_one_king_per_color(position))
        return;

    MoveList pseudo_moves;
    generate_moves(position, pseudo_moves);

    for (const Move move : pseudo_moves) {
        if (Detail::is_legal_move_with_complete_king_set(position, move))
            moves.push_back(move);
    }
}

}  // namespace Mockingbird
