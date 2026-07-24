#pragma once

#include <cassert>
#include <cstdint>
#include <utility>

#include "types.h"

namespace Mockingbird {

enum class MoveType : std::uint8_t {
    NORMAL,
    PROMOTION,
    CASTLING,
    EN_PASSANT,
    COUNT
};

[[nodiscard]] constexpr bool is_ok(MoveType move_type) noexcept {
    return std::to_underlying(move_type) < std::to_underlying(MoveType::COUNT);
}

// Move stores all board-move fields in the low 21 bits of a 32-bit value:
//
//     bits  0..7: destination square
//     bits 8..15: source square
//     bits 16..18: promotion PieceType
//     bits 19..20: MoveType
//
// Bit 31 distinguishes the null-move token from both board moves and Move::none().
class Move {
  public:
    constexpr Move() noexcept = default;

    [[nodiscard]] static constexpr Move none() noexcept {
        return Move{};
    }

    [[nodiscard]] static constexpr Move null() noexcept {
        return Move(NULL_VALUE);
    }

    // Preconditions: from and to are distinct playable squares.
    [[nodiscard]] static constexpr Move normal(Square from, Square to) noexcept {
        return make(from, to, MoveType::NORMAL, NO_PIECE_TYPE);
    }

    // Preconditions: from and to are distinct playable squares, and promotion
    // is KNIGHT, BISHOP, ROOK, or QUEEN.
    [[nodiscard]] static constexpr Move promotion(
      Square from, Square to, PieceType promotion) noexcept {
        assert(is_promotion_piece(promotion));
        return make(from, to, MoveType::PROMOTION, promotion);
    }

    // Preconditions: from and to are distinct playable squares.
    [[nodiscard]] static constexpr Move castling(Square from, Square to) noexcept {
        return make(from, to, MoveType::CASTLING, NO_PIECE_TYPE);
    }

    // Preconditions: from and to are distinct playable squares.
    [[nodiscard]] static constexpr Move en_passant(Square from, Square to) noexcept {
        return make(from, to, MoveType::EN_PASSANT, NO_PIECE_TYPE);
    }

    [[nodiscard]] constexpr bool is_none() const noexcept {
        return value_ == NONE_VALUE;
    }

    [[nodiscard]] constexpr bool is_null() const noexcept {
        return value_ == NULL_VALUE;
    }

    [[nodiscard]] constexpr bool is_board_move() const noexcept {
        return !is_none() && !is_null();
    }

    // Precondition: this is a board move.
    [[nodiscard]] constexpr Square from() const noexcept {
        assert(is_board_move());
        return Square((value_ >> FROM_SHIFT) & SQUARE_MASK);
    }

    // Precondition: this is a board move.
    [[nodiscard]] constexpr Square to() const noexcept {
        assert(is_board_move());
        return Square((value_ >> TO_SHIFT) & SQUARE_MASK);
    }

    // Precondition: this is a board move.
    [[nodiscard]] constexpr MoveType type() const noexcept {
        assert(is_board_move());
        return MoveType((value_ >> TYPE_SHIFT) & TYPE_MASK);
    }

    // Preconditions: this is a promotion move.
    [[nodiscard]] constexpr PieceType promotion_type() const noexcept {
        assert(is_board_move());
        assert(type() == MoveType::PROMOTION);
        return PieceType((value_ >> PROMOTION_SHIFT) & PROMOTION_MASK);
    }

    [[nodiscard]] constexpr std::uint32_t raw() const noexcept {
        return value_;
    }

    [[nodiscard]] friend constexpr bool operator==(Move, Move) noexcept = default;

  private:
    static constexpr unsigned TO_SHIFT = 0;
    static constexpr unsigned FROM_SHIFT = 8;
    static constexpr unsigned PROMOTION_SHIFT = 16;
    static constexpr unsigned TYPE_SHIFT = 19;

    static constexpr std::uint32_t SQUARE_MASK = 0xFF;
    static constexpr std::uint32_t PROMOTION_MASK = 0x7;
    static constexpr std::uint32_t TYPE_MASK = 0x3;

    static constexpr std::uint32_t NONE_VALUE = 0;
    static constexpr std::uint32_t NULL_VALUE = std::uint32_t{1} << 31;

    constexpr explicit Move(std::uint32_t value) noexcept : value_(value) {}

    [[nodiscard]] static constexpr bool is_promotion_piece(
      PieceType piece_type) noexcept {
        return piece_type >= KNIGHT && piece_type <= QUEEN;
    }

    [[nodiscard]] static constexpr Move make(
      Square from, Square to, MoveType move_type, PieceType promotion) noexcept {
        assert(is_ok(from));
        assert(is_ok(to));
        assert(from != to);
        assert(is_ok(move_type));

        const auto from_value = static_cast<std::uint32_t>(from);
        const auto to_value = static_cast<std::uint32_t>(to);
        const auto type_value = static_cast<std::uint32_t>(std::to_underlying(move_type));
        const auto promotion_value = static_cast<std::uint32_t>(promotion);

        return Move((to_value << TO_SHIFT) | (from_value << FROM_SHIFT)
                    | (promotion_value << PROMOTION_SHIFT)
                    | (type_value << TYPE_SHIFT));
    }

    std::uint32_t value_ = NONE_VALUE;
};

[[nodiscard]] constexpr bool is_ok(Move move) noexcept {
    return move.is_board_move();
}

static_assert(sizeof(Move) == sizeof(std::uint32_t));
static_assert(Move::none().is_none());
static_assert(Move::null().is_null());
static_assert(Move::none() != Move::null());

}  // namespace Mockingbird
