#pragma once

#include <array>
#include <cassert>
#include <optional>
#include <string>
#include <string_view>

#include "types.h"

namespace Mockingbird {

// square_name returns lowercase notation such as "d1" or "k14". It returns
// "-" for invalid and cut-out squares.
// parse_square returns no value for malformed or non-playable coordinates.
[[nodiscard]] std::string square_name(Square square);
[[nodiscard]] std::optional<Square> parse_square(std::string_view name);

// Board stores one Piece value for each index in the 16x16 mailbox.
//
// Public operations require playable squares. Assertions enforce the
// documented preconditions in builds where NDEBUG is not defined.
class Board {
  public:
    // A default-constructed Board contains NO_PIECE at every mailbox index.
    constexpr Board() noexcept = default;

    // Precondition: square is playable.
    // Returns NO_PIECE when square is empty.
    [[nodiscard]] constexpr Piece piece_on(Square square) const noexcept {
        assert(is_ok(square));
        return squares_[std::size_t(square)];
    }

    // Precondition: square is playable.
    [[nodiscard]] constexpr bool empty(Square square) const noexcept {
        return piece_on(square) == NO_PIECE;
    }

    // Preconditions: piece is valid, square is playable, and square is empty.
    constexpr void put_piece(Piece piece, Square square) noexcept {
        assert(is_ok(piece));
        assert(is_ok(square));
        assert(empty(square));
        squares_[std::size_t(square)] = piece;
    }

    // Preconditions: square is playable and contains a piece.
    // Returns the removed piece.
    constexpr Piece remove_piece(Square square) noexcept {
        assert(is_ok(square));
        assert(!empty(square));

        const Piece removed = piece_on(square);
        squares_[std::size_t(square)] = NO_PIECE;
        return removed;
    }

    // Preconditions: from and to are playable, from contains a piece, and to
    // is empty.
    constexpr void move_piece(Square from, Square to) noexcept {
        assert(is_ok(from));
        assert(is_ok(to));
        assert(!empty(from));
        assert(empty(to));

        squares_[std::size_t(to)] = squares_[std::size_t(from)];
        squares_[std::size_t(from)] = NO_PIECE;
    }

    // Sets every mailbox entry to NO_PIECE.
    constexpr void clear() noexcept {
        squares_.fill(NO_PIECE);
    }

  private:
    // Indexed by the numeric Square values 0 through 255.
    std::array<Piece, SQUARE_NB> squares_{};
};

}  // namespace Mockingbird
