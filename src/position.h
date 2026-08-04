#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "bitboard.h"
#include "board.h"
#include "evalstate.h"
#include "zobrist.h"

namespace Mockingbird {

class Move;
class Position;
class UndoState;

// transition.h defines the reversible move functions after castling geometry is
// available.
constexpr void do_move(
  Position& position, Move move, UndoState& undo) noexcept;
constexpr void undo_move(
  Position& position, Move move, const UndoState& undo) noexcept;

// Position owns the canonical rule state, its derived occupancy bitboards,
// and a cached key. Mutations update every representation before returning.
class Position {
  public:
    // A default position is empty, has Red to move, and has no castling rights
    // or en-passant targets.
    constexpr Position() noexcept
        : key_(
            Zobrist::side(RED)
            ^ Zobrist::castling(0)) {
        en_passant_squares_.fill(SQ_NONE);
    }

    [[nodiscard]] constexpr const Board& board() const noexcept {
        return board_;
    }

    // Precondition: square is playable.
    [[nodiscard]] constexpr Piece piece_on(Square square) const noexcept {
        return board_.piece_on(square);
    }

    // Precondition: square is playable.
    [[nodiscard]] constexpr bool empty(Square square) const noexcept {
        return board_.empty(square);
    }

    [[nodiscard]] constexpr Color side_to_move() const noexcept {
        return side_to_move_;
    }

    [[nodiscard]] constexpr PositionKey key() const noexcept {
        return key_;
    }

    [[nodiscard]] constexpr const StaticEvaluationState&
    static_evaluation_state() const noexcept {
        return static_evaluation_state_;
    }

    // Reconstructs the board-local evaluation terms from the mailbox.
    [[nodiscard]] constexpr StaticEvaluationState
    recompute_static_evaluation_state() const noexcept {
        StaticEvaluationState result;

        for (int square_index = 0;
             square_index < SQUARE_NB;
             ++square_index) {
            const Square square = Square(square_index);
            if (!is_ok(square))
                continue;

            const Piece piece = piece_on(square);
            if (piece != NO_PIECE)
                result.add_piece(piece, square);
        }

        return result;
    }

    // Reconstructs the key from the position's canonical state.
    [[nodiscard]] constexpr PositionKey recompute_key() const noexcept {
        PositionKey result =
          Zobrist::side(side_to_move_)
          ^ Zobrist::castling(castling_rights_);

        for (int square_index = 0;
             square_index < SQUARE_NB;
             ++square_index) {
            const Square square = Square(square_index);
            if (!is_ok(square))
                continue;

            const Piece piece = piece_on(square);
            if (piece != NO_PIECE)
                result ^= Zobrist::piece(piece, square);
        }

        for (int color_index = 0;
             color_index < COLOR_NB;
             ++color_index) {
            const Color color = Color(color_index);
            result ^= Zobrist::en_passant(
              color,
              en_passant_squares_[std::size_t(color)]);
        }

        return result;
    }

    // Precondition: color is valid.
    constexpr void set_side_to_move(Color color) noexcept {
        assert(is_ok(color));
        if (side_to_move_ == color)
            return;

        key_ ^= Zobrist::side(side_to_move_)
              ^ Zobrist::side(color);
        side_to_move_ = color;
    }

    // A stored right records move history only. Piece locations, path
    // occupancy, and attacked squares are not evaluated.
    // Preconditions: color and side are valid.
    [[nodiscard]] constexpr bool has_castling_right(
      Color color, CastlingSide side) const noexcept {
        assert(is_ok(color));
        assert(is_ok(side));
        return (castling_rights_
                & castling_right_mask(color, side))
            != 0;
    }

    // Preconditions: color and side are valid.
    constexpr void set_castling_right(
      Color color, CastlingSide side) noexcept {
        assert(is_ok(color));
        assert(is_ok(side));
        replace_castling_rights(
          static_cast<std::uint8_t>(
            castling_rights_
            | castling_right_mask(color, side)));
    }

    // Preconditions: color and side are valid.
    constexpr void clear_castling_right(
      Color color, CastlingSide side) noexcept {
        assert(is_ok(color));
        assert(is_ok(side));
        replace_castling_rights(
          static_cast<std::uint8_t>(
            castling_rights_
            & static_cast<std::uint8_t>(
              ~castling_right_mask(color, side))));
    }

    // Clears both rights for one color.
    // Precondition: color is valid.
    constexpr void clear_castling_rights(Color color) noexcept {
        assert(is_ok(color));
        const std::uint8_t color_rights =
          static_cast<std::uint8_t>(
            castling_right_mask(
              color, CastlingSide::KING_SIDE)
            | castling_right_mask(
              color, CastlingSide::QUEEN_SIDE));
        replace_castling_rights(
          static_cast<std::uint8_t>(
            castling_rights_
            & static_cast<std::uint8_t>(
              ~color_rights)));
    }

    constexpr void clear_castling_rights() noexcept {
        replace_castling_rights(0);
    }

    // The indexed color is the owner of the pawn that created the target.
    // SQ_NONE marks a color with no active target.
    // Precondition: color is valid.
    [[nodiscard]] constexpr Square en_passant_square(Color color) const noexcept {
        assert(is_ok(color));
        return en_passant_squares_[std::size_t(color)];
    }

    // Stores the skipped square from a two-square pawn move.
    // Preconditions: color is valid and square is playable.
    constexpr void set_en_passant_square(Color color, Square square) noexcept {
        assert(is_ok(color));
        assert(is_ok(square));
        replace_en_passant_square(color, square);
    }

    // Precondition: color is valid.
    constexpr void clear_en_passant_square(Color color) noexcept {
        assert(is_ok(color));
        replace_en_passant_square(color, SQ_NONE);
    }

    constexpr void clear_en_passant_squares() noexcept {
        std::array<Square, COLOR_NB> empty_targets;
        empty_targets.fill(SQ_NONE);
        replace_en_passant_squares(empty_targets);
    }

    [[nodiscard]] constexpr const Bitboard& occupied() const noexcept {
        return occupied_;
    }

    // Precondition: color is valid.
    [[nodiscard]] constexpr const Bitboard& pieces(Color color) const noexcept {
        assert(is_ok(color));
        return by_color_[std::size_t(color)];
    }

    // Precondition: team is valid.
    [[nodiscard]] constexpr Bitboard pieces(Team team) const noexcept {
        assert(is_ok(team));

        return team == RED_YELLOW ? pieces(RED) | pieces(YELLOW)
                                  : pieces(BLUE) | pieces(GREEN);
    }

    // Precondition: piece_type is valid.
    [[nodiscard]] constexpr const Bitboard& pieces(PieceType piece_type) const noexcept {
        assert(is_ok(piece_type));
        return by_type_[std::size_t(piece_type)];
    }

    // Preconditions: color and piece_type are valid.
    [[nodiscard]] constexpr Bitboard pieces(
      Color color, PieceType piece_type) const noexcept {
        return pieces(color) & pieces(piece_type);
    }

    // Preconditions: piece is valid, square is playable, and square is empty.
    constexpr void put_piece(Piece piece, Square square) noexcept {
        board_.put_piece(piece, square);

        occupied_.set(square);
        by_color_[std::size_t(color_of(piece))].set(square);
        by_type_[std::size_t(type_of(piece))].set(square);
        key_ ^= Zobrist::piece(piece, square);
        static_evaluation_state_.add_piece(piece, square);
    }

    // Preconditions: square is playable and contains a piece.
    // Returns the removed piece.
    constexpr Piece remove_piece(Square square) noexcept {
        const Piece removed = board_.remove_piece(square);

        occupied_.clear(square);
        by_color_[std::size_t(color_of(removed))].clear(square);
        by_type_[std::size_t(type_of(removed))].clear(square);
        key_ ^= Zobrist::piece(removed, square);
        static_evaluation_state_.remove_piece(removed, square);

        return removed;
    }

    // Relocates the source piece without checking whether the move is legal.
    // A piece on the destination square is removed and returned.
    // Preconditions: from and to are distinct playable squares, and from
    // contains a piece.
    constexpr Piece move_piece(Square from, Square to) noexcept {
        assert(is_ok(from));
        assert(is_ok(to));
        assert(from != to);
        assert(!empty(from));

        const Piece moving = piece_on(from);
        const Piece captured = empty(to) ? NO_PIECE : remove_piece(to);

        board_.move_piece(from, to);

        occupied_.clear(from);
        occupied_.set(to);

        Bitboard& color_pieces = by_color_[std::size_t(color_of(moving))];
        color_pieces.clear(from);
        color_pieces.set(to);

        Bitboard& type_pieces = by_type_[std::size_t(type_of(moving))];
        type_pieces.clear(from);
        type_pieces.set(to);

        key_ ^= Zobrist::piece(moving, from)
              ^ Zobrist::piece(moving, to);
        static_evaluation_state_.relocate_piece(
          moving, from, to);

        return captured;
    }

    // Removes all pieces, clears all rule state, and restores Red as the side
    // to move.
    constexpr void clear() noexcept {
        board_.clear();
        occupied_.clear();

        for (Bitboard& bitboard : by_color_)
            bitboard.clear();

        for (Bitboard& bitboard : by_type_)
            bitboard.clear();

        castling_rights_ = 0;
        en_passant_squares_.fill(SQ_NONE);
        side_to_move_ = RED;
        key_ =
          Zobrist::side(RED)
          ^ Zobrist::castling(0);
        static_evaluation_state_ = {};
    }

  private:
    friend constexpr void do_move(
      Position& position, Move move, UndoState& undo) noexcept;
    friend constexpr void undo_move(
      Position& position, Move move, const UndoState& undo) noexcept;

    // Bits 2*c and 2*c+1 store the kingside and queenside rights for Color c.
    [[nodiscard]] static constexpr std::uint8_t castling_right_mask(
      Color color, CastlingSide side) noexcept {
        const std::size_t bit =
          std::size_t(color) * CASTLING_SIDE_NB
          + static_cast<std::size_t>(std::to_underlying(side));
        return static_cast<std::uint8_t>(1U << bit);
    }

    constexpr void replace_castling_rights(
      std::uint8_t rights) noexcept {
        if (castling_rights_ == rights)
            return;

        key_ ^= Zobrist::castling(castling_rights_)
              ^ Zobrist::castling(rights);
        castling_rights_ = rights;
    }

    // SQ_NONE represents an absent target.
    // Preconditions: color is valid and square is playable or SQ_NONE.
    constexpr void replace_en_passant_square(
      Color color,
      Square square) noexcept {
        assert(is_ok(color));
        assert(square == SQ_NONE || is_ok(square));

        Square& current =
          en_passant_squares_[std::size_t(color)];
        if (current == square)
            return;

        key_ ^= Zobrist::en_passant(color, current)
              ^ Zobrist::en_passant(color, square);
        current = square;
    }

    constexpr void replace_en_passant_squares(
      const std::array<Square, COLOR_NB>& squares) noexcept {
        for (int color_index = 0;
             color_index < COLOR_NB;
             ++color_index) {
            const Color color = Color(color_index);
            replace_en_passant_square(
              color, squares[std::size_t(color)]);
        }
    }

    Board board_;
    Bitboard occupied_;
    std::array<Bitboard, COLOR_NB> by_color_{};
    std::array<Bitboard, PIECE_TYPE_NB> by_type_{};
    std::array<Square, COLOR_NB> en_passant_squares_{};
    PositionKey key_;
    StaticEvaluationState static_evaluation_state_;
    std::uint8_t castling_rights_ = 0;
    Color side_to_move_ = RED;
};

static_assert(Position{}.side_to_move() == RED);
static_assert(Position{}.occupied().empty());
static_assert(!Position{}.has_castling_right(
  RED, CastlingSide::KING_SIDE));
static_assert(!Position{}.has_castling_right(
  RED, CastlingSide::QUEEN_SIDE));
static_assert(Position{}.en_passant_square(RED) == SQ_NONE);
static_assert(Position{}.en_passant_square(BLUE) == SQ_NONE);
static_assert(Position{}.en_passant_square(YELLOW) == SQ_NONE);
static_assert(Position{}.en_passant_square(GREEN) == SQ_NONE);
static_assert(Position{}.key() == Position{}.recompute_key());
static_assert(
  Position{}.static_evaluation_state()
  == Position{}.recompute_static_evaluation_state());

}  // namespace Mockingbird
