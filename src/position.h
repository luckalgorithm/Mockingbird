#pragma once

#include <array>
#include <cassert>
#include <cstddef>

#include "bitboard.h"
#include "board.h"

namespace Mockingbird {

// Position owns the mailbox and the occupancy bitboards derived from it.
// Piece mutations update every representation before returning.
class Position {
  public:
    // A default position is empty with Red as the side to move.
    constexpr Position() noexcept = default;

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

    // Precondition: color is valid.
    constexpr void set_side_to_move(Color color) noexcept {
        assert(is_ok(color));
        side_to_move_ = color;
    }

    [[nodiscard]] constexpr const Bitboard& occupied() const noexcept {
        return occupied_;
    }

    // Precondition: color is valid.
    [[nodiscard]] constexpr const Bitboard& pieces(Color color) const noexcept {
        assert(is_ok(color));
        return by_color_[std::size_t(color)];
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
    }

    // Preconditions: square is playable and contains a piece.
    // Returns the removed piece.
    constexpr Piece remove_piece(Square square) noexcept {
        const Piece removed = board_.remove_piece(square);

        occupied_.clear(square);
        by_color_[std::size_t(color_of(removed))].clear(square);
        by_type_[std::size_t(type_of(removed))].clear(square);

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

        return captured;
    }

    // Removes all pieces and restores Red as the side to move.
    constexpr void clear() noexcept {
        board_.clear();
        occupied_.clear();

        for (Bitboard& bitboard : by_color_)
            bitboard.clear();

        for (Bitboard& bitboard : by_type_)
            bitboard.clear();

        side_to_move_ = RED;
    }

  private:
    Board board_;
    Bitboard occupied_;
    std::array<Bitboard, COLOR_NB> by_color_{};
    std::array<Bitboard, PIECE_TYPE_NB> by_type_{};
    Color side_to_move_ = RED;
};

static_assert(Position{}.side_to_move() == RED);
static_assert(Position{}.occupied().empty());

}  // namespace Mockingbird
