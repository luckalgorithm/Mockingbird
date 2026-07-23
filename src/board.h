#pragma once

#include <array>
#include <cassert>
#include <optional>
#include <string>
#include <string_view>

#include "types.h"

namespace Mockingbird {

// Convert between an internal square and human-readable algebraic notation
// such as "d1" or "k14." Invalid/cut-out squares have no notation.
[[nodiscard]] std::string square_name(Square square);
[[nodiscard]] std::optional<Square> parse_square(std::string_view name);

// Board owns the direct square-to-piece lookup table.
//
// The array covers the entire 16x16 mailbox so a Square can be used directly
// as an index. Public operations accept only the 160 playable squares; debug
// assertions catch accidental access to padding or cut-out corners.
//
// This class deliberately stores only piece placement. Side to move, castling
// rights, en passant state, hashes, and move history belong to the future
// Position class.
class Board {
  public:
    // Value-initializing the array fills every entry with NO_PIECE.
    constexpr Board() noexcept = default;

    // Return the piece occupying a playable square, or NO_PIECE when empty.
    [[nodiscard]] constexpr Piece piece_on(Square square) const noexcept {
        assert(is_ok(square));
        return squares_[std::size_t(square)];
    }

    // A readable convenience wrapper around piece_on().
    [[nodiscard]] constexpr bool empty(Square square) const noexcept {
        return piece_on(square) == NO_PIECE;
    }

    // Place a real piece on an empty, playable square.
    //
    // Captures are intentionally not implicit: callers must remove the
    // captured piece first. That keeps future piece counts and hashes explicit.
    constexpr void put_piece(Piece piece, Square square) noexcept {
        assert(is_ok(piece));
        assert(is_ok(square));
        assert(empty(square));
        squares_[std::size_t(square)] = piece;
    }

    // Remove and return the piece on square. Returning it will be useful when
    // undoing a move restores a captured piece.
    constexpr Piece remove_piece(Square square) noexcept {
        assert(is_ok(square));
        assert(!empty(square));

        const Piece removed = piece_on(square);
        squares_[std::size_t(square)] = NO_PIECE;
        return removed;
    }

    // Relocate one piece without capturing. The destination must be empty.
    constexpr void move_piece(Square from, Square to) noexcept {
        assert(is_ok(from));
        assert(is_ok(to));
        assert(!empty(from));
        assert(empty(to));

        squares_[std::size_t(to)] = squares_[std::size_t(from)];
        squares_[std::size_t(from)] = NO_PIECE;
    }

    // Restore the board to an entirely empty state.
    constexpr void clear() noexcept {
        squares_.fill(NO_PIECE);
    }

  private:
    // Direct indexing is intentional: no coordinate conversion or search is
    // needed to answer "which piece is on this square?"
    std::array<Piece, SQUARE_NB> squares_{};
};

}  // namespace Mockingbird
