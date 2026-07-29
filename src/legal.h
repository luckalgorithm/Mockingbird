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

struct LegalMoveContext {
    Bitboard slider_blockers;
    PositionKey position_key = 0;
    Color moving_color = COLOR_NB;
    bool checked = false;
};

// The caller has already established that every color has exactly one king.
[[nodiscard]] constexpr LegalMoveContext make_legal_move_context(
  const Position& position) noexcept {
    const Color moving_color = position.side_to_move();
    const bool checked = in_check(position, moving_color);

    return {
      .slider_blockers =
        checked
          ? Bitboard{}
          : slider_blockers_to_king(position, moving_color),
      .position_key = position.key(),
      .moving_color = moving_color,
      .checked = checked,
    };
}

// Returns true when move captures a king belonging to the opposing team.
// Precondition: move is generated pseudo-legal.
[[nodiscard]] constexpr bool captures_opposing_king(
  const Position& position,
  Move move,
  Color moving_color) noexcept {
    if (position.empty(move.to()))
        return false;

    const Piece destination_piece =
      position.piece_on(move.to());
    return type_of(destination_piece) == KING
        && team_of(color_of(destination_piece))
             != team_of(moving_color);
}

// The position is restored before this function returns.
// Preconditions: move is generated pseudo-legal, and the position contains
// exactly one king belonging to each color.
[[nodiscard]] constexpr bool is_legal_move_by_transition(
  Position& position, Move move) noexcept {
    assert(is_ok(move));

    const Color moving_color = position.side_to_move();
    const bool captures_opposing_king =
      Detail::captures_opposing_king(
        position, move, moving_color);

    UndoState undo;
    do_move(position, move, undo);

    // Capturing an opposing king ends the game before mover king safety is
    // evaluated. Other moves must leave the moving color's king unattacked.
    const bool legal =
      captures_opposing_king || !in_check(position, moving_color);

    undo_move(position, move, undo);
    return legal;
}

// Generated castling has already passed its king-path attack tests. Other
// moves are accepted here only when clearing the source cannot expose the
// moving color's king. Checked positions, king moves, en-passant moves, and
// slider blockers require the transition-based test.
// Preconditions: move is generated pseudo-legal, context was created from
// position, and position has not changed since context was created.
[[nodiscard]] constexpr bool can_accept_without_transition(
  const Position& position,
  Move move,
  const LegalMoveContext& context) noexcept {
    assert(context.position_key == position.key());
    assert(context.moving_color == position.side_to_move());

    if (context.checked
        || move.type() == MoveType::EN_PASSANT)
        return false;

    if (move.type() == MoveType::CASTLING)
        return true;

    const Piece moving_piece =
      position.piece_on(move.from());
    return type_of(moving_piece) != KING
        && !context.slider_blockers.test(move.from());
}

// Preconditions: move is generated pseudo-legal, the position contains
// exactly one king belonging to each color, and context describes position.
[[nodiscard]] constexpr bool is_legal_move_with_context(
  Position& position,
  Move move,
  const LegalMoveContext& context) noexcept {
    assert(is_ok(move));
    assert(context.position_key == position.key());
    assert(context.moving_color == position.side_to_move());

    const Color moving_color = position.side_to_move();
    if (captures_opposing_king(
          position, move, moving_color)
        || can_accept_without_transition(
          position, move, context))
        return true;

    return is_legal_move_by_transition(position, move);
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

    const Detail::LegalMoveContext context =
      Detail::make_legal_move_context(position);
    return Detail::is_legal_move_with_context(
      position, move, context);
}

// Returns after the first legal move is found. A position with a missing or
// duplicate king of any color has no legal move. The position is restored
// after every tested candidate.
[[nodiscard]] constexpr bool has_legal_move(
  Position& position) noexcept {
    if (!Detail::has_exactly_one_king_per_color(position))
        return false;

    const Detail::LegalMoveContext context =
      Detail::make_legal_move_context(position);
    MoveList pseudo_moves;
    generate_moves(position, pseudo_moves);

    for (const Move move : pseudo_moves) {
        if (Detail::is_legal_move_with_context(
              position, move, context))
            return true;
    }

    return false;
}

// Appends legal moves in the order produced by generate_moves(). A position
// with a missing or duplicate king of any color appends no moves. The position
// is restored after every candidate.
// Precondition: moves has enough remaining capacity for the appended moves.
constexpr void generate_legal_moves(
  Position& position, MoveList& moves) noexcept {
    if (!Detail::has_exactly_one_king_per_color(position))
        return;

    const Detail::LegalMoveContext context =
      Detail::make_legal_move_context(position);
    MoveList pseudo_moves;
    generate_moves(position, pseudo_moves);

    for (const Move move : pseudo_moves) {
        if (Detail::is_legal_move_with_context(
              position, move, context))
            moves.push_back(move);
    }
}

}  // namespace Mockingbird
