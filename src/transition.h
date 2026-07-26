#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "castling.h"
#include "move.h"
#include "pawns.h"
#include "position.h"

namespace Mockingbird {

// UndoState snapshots reversible rule state and both pieces that one move can
// capture. A separate state is required for each live ply.
class UndoState {
  public:
    constexpr UndoState() noexcept = default;

  private:
    friend constexpr void do_move(
      Position& position, Move move, UndoState& undo) noexcept;
    friend constexpr void undo_move(
      Position& position, Move move, const UndoState& undo) noexcept;

    std::array<Square, COLOR_NB> en_passant_squares_{};
    Piece captured_on_destination_ = NO_PIECE;
    Piece en_passant_victim_ = NO_PIECE;
    Square en_passant_victim_square_ = SQ_NONE;
    std::uint8_t castling_rights_ = 0;
};

namespace Detail {

[[nodiscard]] constexpr std::uint8_t castling_right_bit(
  Color color, CastlingSide side) noexcept {
    const std::size_t bit =
      std::size_t(color) * CASTLING_SIDE_NB
      + std::to_underlying(side);
    return static_cast<std::uint8_t>(1U << bit);
}

[[nodiscard]] constexpr std::uint8_t color_castling_rights(
  Color color) noexcept {
    return static_cast<std::uint8_t>(
      castling_right_bit(color, CastlingSide::KING_SIDE)
      | castling_right_bit(color, CastlingSide::QUEEN_SIDE));
}

[[nodiscard]] consteval std::array<std::uint8_t, SQUARE_NB>
make_castling_rights_by_square() {
    std::array<std::uint8_t, SQUARE_NB> masks{};

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);

        for (std::size_t side_index = 0;
             side_index < CASTLING_SIDE_NB;
             ++side_index) {
            const CastlingSide side =
              static_cast<CastlingSide>(side_index);
            const CastlingGeometry& geometry =
              castling_geometry(color, side);
            const std::uint8_t right =
              castling_right_bit(color, side);

            masks[std::size_t(geometry.king_source)] =
              static_cast<std::uint8_t>(
                masks[std::size_t(geometry.king_source)] | right);
            masks[std::size_t(geometry.rook_source)] =
              static_cast<std::uint8_t>(
                masks[std::size_t(geometry.rook_source)] | right);
        }
    }

    return masks;
}

inline constexpr auto CASTLING_RIGHTS_BY_SQUARE =
  make_castling_rights_by_square();

[[nodiscard]] constexpr CastlingSide castling_side_for_move(
  Color color, Move move) noexcept {
    for (std::size_t side_index = 0;
         side_index < CASTLING_SIDE_NB;
         ++side_index) {
        const CastlingSide side =
          static_cast<CastlingSide>(side_index);
        const CastlingGeometry& geometry =
          castling_geometry(color, side);

        if (move.from() == geometry.king_source
            && move.to() == geometry.king_destination)
            return side;
    }

    assert(false);
    return CastlingSide::KING_SIDE;
}

struct EnPassantCapture {
    Color owner = COLOR_NB;
    Square victim_square = SQ_NONE;
};

// The owner and victim are resolved from the state before any target expires.
// Exactly one owner must match an en-passant move in a reachable position.
[[nodiscard]] constexpr EnPassantCapture resolve_en_passant_capture(
  const Position& position,
  Move move,
  const std::array<Square, COLOR_NB>& en_passant_squares) noexcept {
    const Color moving_color = position.side_to_move();
    assert(type_of(position.piece_on(move.from())) == PAWN);
    assert(pawn_attacks(moving_color, move.from()).test(move.to()));

    EnPassantCapture capture;

    for (int owner_index = 0; owner_index < COLOR_NB; ++owner_index) {
        const Color owner = Color(owner_index);
        if (team_of(owner) == team_of(moving_color)
            || en_passant_squares[std::size_t(owner)] != move.to())
            continue;

        const Square source = move.to() - pawn_push(owner);
        if (!is_ok(source)
            || pawn_push_destination(owner, source) != move.to())
            continue;

        const Square victim_square =
          pawn_double_push_destination(owner, source);
        if (victim_square == SQ_NONE
            || position.piece_on(victim_square)
                 != make_piece(owner, PAWN))
            continue;

        assert(!is_ok(capture.owner));
        capture.owner = owner;
        capture.victim_square = victim_square;
    }

    assert(is_ok(capture.owner));
    assert(is_ok(capture.victim_square));
    assert(capture.victim_square != move.from());
    assert(capture.victim_square != move.to());
    return capture;
}

constexpr void replace_pawn_with_promotion(
  Position& position, Move move, Color color) noexcept {
    const Piece removed = position.remove_piece(move.to());
    assert(removed == make_piece(color, PAWN));
    static_cast<void>(removed);
    position.put_piece(
      make_piece(color, move.promotion_type()), move.to());
}

constexpr void replace_promotion_with_pawn(
  Position& position, Move move, Color color) noexcept {
    const Piece removed = position.remove_piece(move.to());
    assert(
      removed == make_piece(color, move.promotion_type()));
    static_cast<void>(removed);
    position.put_piece(make_piece(color, PAWN), move.to());
}

}  // namespace Detail

// Applies a generated pseudo-legal move and advances to the next color.
// Castling must satisfy the legality checks used by generate_castling_moves().
// Preconditions: move belongs to the side to move, and undo is not the state
// for another live ply.
constexpr void do_move(
  Position& position, Move move, UndoState& undo) noexcept {
    assert(is_ok(move));

    const Square from = move.from();
    const Square to = move.to();
    assert(!position.empty(from));

    const Color moving_color = position.side_to_move_;
    const Piece moving_piece = position.piece_on(from);
    assert(color_of(moving_piece) == moving_color);

    if (!position.empty(to)) {
        assert(
          team_of(color_of(position.piece_on(to)))
          != team_of(moving_color));
    }

    undo.en_passant_squares_ =
      position.en_passant_squares_;
    undo.castling_rights_ = position.castling_rights_;
    undo.captured_on_destination_ = NO_PIECE;
    undo.en_passant_victim_ = NO_PIECE;
    undo.en_passant_victim_square_ = SQ_NONE;

    Detail::EnPassantCapture en_passant;
    if (move.type() == MoveType::EN_PASSANT) {
        assert(type_of(moving_piece) == PAWN);
        en_passant = Detail::resolve_en_passant_capture(
          position, move, undo.en_passant_squares_);
    }

    CastlingSide castling_side = CastlingSide::KING_SIDE;
    if (move.type() == MoveType::CASTLING) {
        assert(type_of(moving_piece) == KING);
        assert(!move.is_promotion());
        castling_side =
          Detail::castling_side_for_move(moving_color, move);
        assert(is_castling_legal(position, castling_side));
    }

    // A target expires when its owner next moves. It also expires when its
    // vulnerable pawn moves, is captured, or is captured en passant.
    std::array<Square, COLOR_NB> active_en_passant =
      position.en_passant_squares_;
    active_en_passant[std::size_t(moving_color)] =
      SQ_NONE;
    for (int owner_index = 0;
         owner_index < COLOR_NB;
         ++owner_index) {
        const Color owner = Color(owner_index);
        const Square target =
          active_en_passant[std::size_t(owner)];
        if (target == SQ_NONE)
            continue;

        const Square victim_square =
          target + pawn_push(owner);
        if (from == victim_square
            || to == victim_square
            || (move.type() == MoveType::EN_PASSANT
                && to == target))
            active_en_passant[std::size_t(owner)] =
              SQ_NONE;
    }
    position.replace_en_passant_squares(
      active_en_passant);

    switch (move.type()) {
        case MoveType::NORMAL:
            assert(!move.is_promotion());
            undo.captured_on_destination_ =
              position.move_piece(from, to);
            break;

        case MoveType::PROMOTION:
            assert(type_of(moving_piece) == PAWN);
            assert(move.is_promotion());
            assert(
              is_pawn_promotion_square(moving_color, to));
            undo.captured_on_destination_ =
              position.move_piece(from, to);
            Detail::replace_pawn_with_promotion(
              position, move, moving_color);
            break;

        case MoveType::CASTLING: {
            const CastlingGeometry& geometry =
              castling_geometry(moving_color, castling_side);
            assert(position.empty(geometry.king_destination));
            assert(position.empty(geometry.rook_destination));

            const Piece king_capture =
              position.move_piece(
                geometry.king_source,
                geometry.king_destination);
            const Piece rook_capture =
              position.move_piece(
                geometry.rook_source,
                geometry.rook_destination);
            assert(king_capture == NO_PIECE);
            assert(rook_capture == NO_PIECE);
            static_cast<void>(king_capture);
            static_cast<void>(rook_capture);
            break;
        }

        case MoveType::EN_PASSANT:
            assert(
              move.is_promotion()
              == is_pawn_promotion_square(moving_color, to));
            undo.captured_on_destination_ =
              position.move_piece(from, to);
            undo.en_passant_victim_square_ =
              en_passant.victim_square;
            undo.en_passant_victim_ =
              position.remove_piece(en_passant.victim_square);
            assert(
              undo.en_passant_victim_
              == make_piece(en_passant.owner, PAWN));

            if (move.is_promotion()) {
                Detail::replace_pawn_with_promotion(
                  position, move, moving_color);
            }
            break;

        case MoveType::COUNT:
            assert(false);
            break;
    }

    if (move.type() == MoveType::NORMAL
        && type_of(moving_piece) == PAWN
        && pawn_double_push_destination(moving_color, from)
             == to) {
        const Square skipped =
          pawn_push_destination(moving_color, from);
        assert(skipped != SQ_NONE);
        position.set_en_passant_square(
          moving_color, skipped);
    }

    std::uint8_t rights_to_clear =
      static_cast<std::uint8_t>(
        Detail::CASTLING_RIGHTS_BY_SQUARE[std::size_t(from)]
        | Detail::CASTLING_RIGHTS_BY_SQUARE[std::size_t(to)]);

    if (type_of(moving_piece) == KING) {
        rights_to_clear =
          static_cast<std::uint8_t>(
            rights_to_clear
            | Detail::color_castling_rights(moving_color));
    }

    if (undo.captured_on_destination_ != NO_PIECE
        && type_of(undo.captured_on_destination_) == KING) {
        rights_to_clear =
          static_cast<std::uint8_t>(
            rights_to_clear
            | Detail::color_castling_rights(
                color_of(undo.captured_on_destination_)));
    }

    position.replace_castling_rights(
      static_cast<std::uint8_t>(
        position.castling_rights_
        & static_cast<std::uint8_t>(~rights_to_clear)));
    position.set_side_to_move(next_color(moving_color));
}

// Reverses the matching most-recent do_move() call.
// Preconditions: move and undo describe the current position's previous ply.
constexpr void undo_move(
  Position& position,
  Move move,
  const UndoState& undo) noexcept {
    assert(is_ok(move));

    const Square from = move.from();
    const Square to = move.to();
    position.set_side_to_move(
      previous_color(position.side_to_move_));
    const Color moving_color = position.side_to_move_;

    if (move.type() == MoveType::CASTLING) {
        const CastlingSide side =
          Detail::castling_side_for_move(moving_color, move);
        const CastlingGeometry& geometry =
          castling_geometry(moving_color, side);

        assert(position.empty(geometry.rook_source));
        assert(position.empty(geometry.king_source));

        const Piece rook_capture =
          position.move_piece(
            geometry.rook_destination,
            geometry.rook_source);
        const Piece king_capture =
          position.move_piece(
            geometry.king_destination,
            geometry.king_source);
        assert(rook_capture == NO_PIECE);
        assert(king_capture == NO_PIECE);
        static_cast<void>(rook_capture);
        static_cast<void>(king_capture);
    } else {
        if (move.is_promotion()) {
            Detail::replace_promotion_with_pawn(
              position, move, moving_color);
        }

        assert(position.empty(from));
        const Piece source_capture =
          position.move_piece(to, from);
        assert(source_capture == NO_PIECE);
        static_cast<void>(source_capture);

        if (undo.captured_on_destination_ != NO_PIECE) {
            assert(position.empty(to));
            position.put_piece(
              undo.captured_on_destination_, to);
        }

        if (undo.en_passant_victim_ != NO_PIECE) {
            assert(
              is_ok(undo.en_passant_victim_square_));
            assert(
              position.empty(
                undo.en_passant_victim_square_));
            position.put_piece(
              undo.en_passant_victim_,
              undo.en_passant_victim_square_);
        }
    }

    position.replace_en_passant_squares(
      undo.en_passant_squares_);
    position.replace_castling_rights(
      undo.castling_rights_);
}

static_assert(
  Detail::CASTLING_RIGHTS_BY_SQUARE[
    std::size_t(
      castling_geometry(
        RED, CastlingSide::KING_SIDE).king_source)]
  == Detail::color_castling_rights(RED));

}  // namespace Mockingbird
