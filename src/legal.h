#pragma once

#include <cassert>
#include <cstddef>

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

[[nodiscard]] constexpr bool contains_generated_move(
  const MoveList& moves,
  Move expected) noexcept {
    for (const Move move : moves) {
        if (move == expected)
            return true;
    }

    return false;
}

// Generates only the piece class containing move and tests whether its exact
// encoding is pseudo-legal in position.
[[nodiscard]] constexpr bool is_generated_pseudo_legal_move(
  const Position& position,
  Move move) noexcept {
    if (!move.is_board_move())
        return false;

    const Square from = move.from();
    const Square to = move.to();
    if (!is_ok(from) || !is_ok(to) || from == to
        || position.empty(from)) {
        return false;
    }

    const Piece moving_piece = position.piece_on(from);
    if (color_of(moving_piece) != position.side_to_move())
        return false;

    MoveList candidates;
    switch (type_of(moving_piece)) {
        case PAWN:
            generate_pawn_moves(position, candidates);
            break;
        case KNIGHT:
            generate_knight_moves(position, candidates);
            break;
        case BISHOP:
            generate_bishop_moves(position, candidates);
            break;
        case ROOK:
            generate_rook_moves(position, candidates);
            break;
        case QUEEN:
            generate_queen_moves(position, candidates);
            break;
        case KING:
            generate_king_moves(position, candidates);
            generate_castling_moves(position, candidates);
            break;
        case NO_PIECE_TYPE:
        case PIECE_TYPE_NB:
            return false;
    }

    return contains_generated_move(candidates, move);
}

// A cached move is accepted only when it belongs to the current pseudo-legal
// move set and satisfies the supplied king-safety context.
// Preconditions: the position contains exactly one king belonging to each
// color, context was created from position, and position has not changed.
[[nodiscard]] constexpr bool is_cached_move_legal(
  Position& position,
  Move move,
  const LegalMoveContext& context) noexcept {
    if (!is_generated_pseudo_legal_move(position, move))
        return false;

    return is_legal_move_with_context(
      position, move, context);
}

// This boundary overload validates the king layout before constructing the
// context required by the hot-path overload.
[[nodiscard]] constexpr bool is_cached_move_legal(
  Position& position,
  Move move) noexcept {
    if (!has_exactly_one_king_per_color(position)
        || !is_generated_pseudo_legal_move(position, move)) {
        return false;
    }

    const LegalMoveContext context =
      make_legal_move_context(position);
    return is_legal_move_with_context(
      position, move, context);
}

template<typename Generator>
[[nodiscard]] constexpr Move generated_class_first_legal_move(
  Position& position,
  MoveList& candidates,
  const LegalMoveContext& context,
  Generator generator) noexcept {
    candidates.clear();
    generator(position, candidates);

    for (const Move move : candidates) {
        if (is_legal_move_with_context(position, move, context))
            return move;
    }

    return Move::none();
}

// Returns the first legal move in generator order using a context already
// established for this complete position.
// Preconditions: the position contains exactly one king belonging to each
// color, context was created from position, and position has not changed.
[[nodiscard]] constexpr Move first_legal_move_with_context(
  Position& position,
  const LegalMoveContext& context) noexcept {
    MoveList candidates;
    Move move = Move::none();

    move = generated_class_first_legal_move(
      position,
      candidates,
      context,
      generate_pawn_moves);
    if (move.is_board_move())
        return move;

    move = generated_class_first_legal_move(
      position,
      candidates,
      context,
      generate_knight_moves);
    if (move.is_board_move())
        return move;

    move = generated_class_first_legal_move(
      position,
      candidates,
      context,
      generate_bishop_moves);
    if (move.is_board_move())
        return move;

    move = generated_class_first_legal_move(
      position,
      candidates,
      context,
      generate_rook_moves);
    if (move.is_board_move())
        return move;

    move = generated_class_first_legal_move(
      position,
      candidates,
      context,
      generate_queen_moves);
    if (move.is_board_move())
        return move;

    move = generated_class_first_legal_move(
      position,
      candidates,
      context,
      generate_king_moves);
    if (move.is_board_move())
        return move;

    return generated_class_first_legal_move(
      position,
      candidates,
      context,
      generate_castling_moves);
}

// Preconditions match first_legal_move_with_context().
[[nodiscard]] constexpr bool has_legal_move_with_context(
  Position& position,
  const LegalMoveContext& context) noexcept {
    return first_legal_move_with_context(
      position, context)
      .is_board_move();
}

// Empty output lists are filtered in place. A nonempty output list uses
// separate temporary storage because its documented remaining capacity only
// needs to hold the legal suffix, not every pseudo-legal candidate.
template<typename Generator>
constexpr void append_generated_legal_moves(
  Position& position,
  MoveList& moves,
  const LegalMoveContext& context,
  Generator generator) noexcept {
    if (!moves.empty()) {
        MoveList candidates;
        generator(position, candidates);

        for (const Move move : candidates) {
            if (is_legal_move_with_context(
                  position, move, context)) {
                moves.push_back(move);
            }
        }
        return;
    }

    generator(position, moves);

    std::size_t legal_end = 0;
    const std::size_t generated_end = moves.size();
    for (std::size_t index = 0;
         index < generated_end;
         ++index) {
        const Move move = moves[index];
        if (is_legal_move_with_context(
              position, move, context)) {
            moves[legal_end++] = move;
        }
    }

    moves.truncate(legal_end);
}

// Appends every legal move using an already established context.
// Preconditions: the position contains exactly one king belonging to each
// color, context describes the unchanged position, and moves has enough
// remaining capacity for the appended moves.
constexpr void generate_legal_moves_with_context(
  Position& position,
  MoveList& moves,
  const LegalMoveContext& context) noexcept {
    append_generated_legal_moves(
      position, moves, context, generate_moves);
}

// Appends legal captures and promotions using an already established context.
// Preconditions match generate_legal_moves_with_context().
constexpr void generate_legal_tactical_moves_with_context(
  Position& position,
  MoveList& moves,
  const LegalMoveContext& context) noexcept {
    append_generated_legal_moves(
      position,
      moves,
      context,
      generate_tactical_moves);
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

// Returns the first legal move in generator order. A position with a missing
// or duplicate king of any color returns Move::none(). The position is
// restored after every tested candidate.
[[nodiscard]] constexpr Move first_legal_move(
  Position& position) noexcept {
    if (!Detail::has_exactly_one_king_per_color(position))
        return Move::none();

    const Detail::LegalMoveContext context =
      Detail::make_legal_move_context(position);
    return Detail::first_legal_move_with_context(
      position, context);
}

// Returns after the first legal move is found. A position with a missing or
// duplicate king of any color has no legal move. The position is restored
// after every tested candidate.
[[nodiscard]] constexpr bool has_legal_move(
  Position& position) noexcept {
    return first_legal_move(position).is_board_move();
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
    Detail::generate_legal_moves_with_context(
      position, moves, context);
}

// Appends legal captures and promotions without constructing quiet
// pseudo-legal moves. The relative order matches the tactical subsequence of
// generate_legal_moves(). A position with a missing or duplicate king of any
// color appends no moves.
// Precondition: moves has enough remaining capacity for the appended moves.
constexpr void generate_legal_tactical_moves(
  Position& position, MoveList& moves) noexcept {
    if (!Detail::has_exactly_one_king_per_color(position))
        return;

    const Detail::LegalMoveContext context =
      Detail::make_legal_move_context(position);
    Detail::generate_legal_tactical_moves_with_context(
      position,
      moves,
      context);
}

}  // namespace Mockingbird
