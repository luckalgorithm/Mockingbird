#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "evaluate.h"
#include "movelist.h"

namespace Mockingbird {

using MoveOrderScore = std::uint32_t;

namespace OrderingDetail {

inline constexpr MoveOrderScore QUIET_SCORE = 0;
inline constexpr MoveOrderScore CAPTURE_BASE = 1;
using WideMoveOrderScore = std::uint64_t;

// An en-passant move can capture a queen on its destination and a pawn on the
// vulnerable pawn square.
inline constexpr WideMoveOrderScore
  MAX_CAPTURED_MATERIAL_WIDE =
    static_cast<WideMoveOrderScore>(
      MAX_PIECE_VALUE)
    + static_cast<WideMoveOrderScore>(
        PAWN_VALUE);

// Kings follow queens in least-valuable-attacker ordering. Their HCE material
// value remains zero.
inline constexpr WideMoveOrderScore
  KING_ATTACKER_COST_WIDE =
    static_cast<WideMoveOrderScore>(
      MAX_PIECE_VALUE)
    + 1;
inline constexpr WideMoveOrderScore ATTACKER_STRIDE_WIDE =
  KING_ATTACKER_COST_WIDE + 1;
inline constexpr WideMoveOrderScore MAX_CAPTURE_SCORE_WIDE =
  static_cast<WideMoveOrderScore>(CAPTURE_BASE)
  + MAX_CAPTURED_MATERIAL_WIDE
      * ATTACKER_STRIDE_WIDE
  + KING_ATTACKER_COST_WIDE;
inline constexpr WideMoveOrderScore PROMOTION_BASE_WIDE =
  MAX_CAPTURE_SCORE_WIDE + 1;
inline constexpr WideMoveOrderScore PROMOTION_STRIDE_WIDE =
  MAX_CAPTURED_MATERIAL_WIDE + 1;
inline constexpr WideMoveOrderScore MAX_PROMOTION_SCORE_WIDE =
  PROMOTION_BASE_WIDE
  + static_cast<WideMoveOrderScore>(QUEEN_VALUE)
      * PROMOTION_STRIDE_WIDE
  + MAX_CAPTURED_MATERIAL_WIDE;
inline constexpr WideMoveOrderScore KING_CAPTURE_SCORE_WIDE =
  MAX_PROMOTION_SCORE_WIDE + 1;

static_assert(
  KING_CAPTURE_SCORE_WIDE
  < std::numeric_limits<MoveOrderScore>::max());

inline constexpr MoveOrderScore MAX_CAPTURED_MATERIAL =
  static_cast<MoveOrderScore>(
    MAX_CAPTURED_MATERIAL_WIDE);
inline constexpr MoveOrderScore KING_ATTACKER_COST =
  static_cast<MoveOrderScore>(
    KING_ATTACKER_COST_WIDE);
inline constexpr MoveOrderScore ATTACKER_STRIDE =
  static_cast<MoveOrderScore>(
    ATTACKER_STRIDE_WIDE);
inline constexpr MoveOrderScore MAX_CAPTURE_SCORE =
  static_cast<MoveOrderScore>(
    MAX_CAPTURE_SCORE_WIDE);
inline constexpr MoveOrderScore PROMOTION_BASE =
  static_cast<MoveOrderScore>(
    PROMOTION_BASE_WIDE);
inline constexpr MoveOrderScore PROMOTION_STRIDE =
  static_cast<MoveOrderScore>(
    PROMOTION_STRIDE_WIDE);
inline constexpr MoveOrderScore MAX_PROMOTION_SCORE =
  static_cast<MoveOrderScore>(
    MAX_PROMOTION_SCORE_WIDE);
inline constexpr MoveOrderScore KING_CAPTURE_SCORE =
  static_cast<MoveOrderScore>(
    KING_CAPTURE_SCORE_WIDE);

// Precondition: piece_type is a real piece type.
[[nodiscard]] constexpr MoveOrderScore attacker_cost(
  PieceType piece_type) noexcept {
    assert(is_ok(piece_type));

    if (piece_type == KING)
        return KING_ATTACKER_COST;

    return static_cast<MoveOrderScore>(
      piece_value(piece_type));
}

// Returns the non-king material removed by move. destination is the piece on
// move.to(), or NO_PIECE when that square is empty. An en-passant pawn is
// included in addition to destination.
[[nodiscard]] constexpr MoveOrderScore captured_material(
  Piece destination,
  Move move) noexcept {
    assert(is_ok(move));
    assert(
      destination == NO_PIECE
      || is_ok(destination));

    MoveOrderScore material = 0;
    if (destination != NO_PIECE) {
        material += static_cast<MoveOrderScore>(
          piece_value(
            type_of(destination)));
    }

    if (move.type() == MoveType::EN_PASSANT) {
        material +=
          static_cast<MoveOrderScore>(PAWN_VALUE);
    }

    return material;
}

}  // namespace OrderingDetail

// Returns whether move removes at least one opposing piece.
// Precondition: move was generated for position.
[[nodiscard]] constexpr bool is_capture_move(
  const Position& position,
  Move move) noexcept {
    assert(is_ok(move));
    return move.type() == MoveType::EN_PASSANT
        || !position.empty(move.to());
}

// Captures and promotions are the tactical moves used by material ordering.
// Precondition: move was generated for position.
[[nodiscard]] constexpr bool is_tactical_move(
  const Position& position,
  Move move) noexcept {
    return is_capture_move(position, move)
        || move.is_promotion();
}

// Higher scores are searched first. Equal scores retain generation order.
// Precondition: move was generated for the unchanged position.
[[nodiscard]] constexpr MoveOrderScore move_order_score(
  const Position& position,
  Move move) noexcept {
    assert(is_ok(move));
    assert(!position.empty(move.from()));

    const Piece attacker = position.piece_on(move.from());
    assert(color_of(attacker) == position.side_to_move());
    assert(
      move.type() != MoveType::EN_PASSANT
      || type_of(attacker) == PAWN);
    assert(
      !move.is_promotion()
      || type_of(attacker) == PAWN);

    const Piece victim =
      position.empty(move.to())
        ? NO_PIECE
        : position.piece_on(move.to());

    if (victim != NO_PIECE) {
        assert(
          team_of(color_of(victim))
          != team_of(position.side_to_move()));

        if (type_of(victim) == KING)
            return OrderingDetail::KING_CAPTURE_SCORE;
    }

    const MoveOrderScore captured =
      OrderingDetail::captured_material(
        victim, move);
    assert(
      captured
      <= OrderingDetail::MAX_CAPTURED_MATERIAL);

    if (move.is_promotion()) {
        return OrderingDetail::PROMOTION_BASE
          + static_cast<MoveOrderScore>(
              piece_value(move.promotion_type()))
              * OrderingDetail::PROMOTION_STRIDE
          + captured;
    }

    if (captured != 0) {
        const MoveOrderScore attacker_cost =
          OrderingDetail::attacker_cost(
            type_of(attacker));

        return OrderingDetail::CAPTURE_BASE
          + captured * OrderingDetail::ATTACKER_STRIDE
          + (OrderingDetail::KING_ATTACKER_COST
             - attacker_cost);
    }

    return OrderingDetail::QUIET_SCORE;
}

// MoveOrderingBuffer stores merge-sort output between passes. One buffer can
// be reused after each completed order_moves() call.
class MoveOrderingBuffer {
  public:
    constexpr MoveOrderingBuffer() noexcept = default;

    // Precondition: index is less than MoveList::capacity().
    [[nodiscard]] constexpr Move& operator[](
      std::size_t index) noexcept {
        assert(index < moves_.size());
        return moves_[index];
    }

    // Precondition: index is less than MoveList::capacity().
    [[nodiscard]] constexpr const Move& operator[](
      std::size_t index) const noexcept {
        assert(index < moves_.size());
        return moves_[index];
    }

  private:
    std::array<Move, MoveList::CAPACITY> moves_{};
};

namespace OrderingDetail {

[[nodiscard]] constexpr Move source_move(
  const MoveList& moves,
  const MoveOrderingBuffer& buffer,
  bool source_is_move_list,
  std::size_t index) noexcept {
    return source_is_move_list
        ? moves[index]
        : buffer[index];
}

constexpr void write_move(
  MoveList& moves,
  MoveOrderingBuffer& buffer,
  bool destination_is_move_list,
  std::size_t index,
  Move move) noexcept {
    if (destination_is_move_list)
        moves[index] = move;
    else
        buffer[index] = move;
}

// Merges adjacent sorted ranges of width elements. Equal scores are taken
// from the left range first, preserving their existing order.
constexpr void merge_pass(
  const Position& position,
  MoveList& moves,
  MoveOrderingBuffer& buffer,
  std::size_t width,
  bool source_is_move_list) noexcept {
    const std::size_t size = moves.size();
    const std::size_t block_width = 2 * width;

    for (std::size_t begin = 0;
         begin < size;
         begin += block_width) {
        const std::size_t middle =
          begin + width < size
            ? begin + width
            : size;
        const std::size_t end =
          begin + block_width < size
            ? begin + block_width
            : size;

        std::size_t left = begin;
        std::size_t right = middle;
        std::size_t output = begin;

        while (left < middle && right < end) {
            const Move left_move =
              source_move(
                moves,
                buffer,
                source_is_move_list,
                left);
            const Move right_move =
              source_move(
                moves,
                buffer,
                source_is_move_list,
                right);
            const bool take_right =
              move_order_score(position, right_move)
              > move_order_score(position, left_move);

            write_move(
              moves,
              buffer,
              !source_is_move_list,
              output,
              take_right ? right_move : left_move);

            if (take_right)
                ++right;
            else
                ++left;
            ++output;
        }

        while (left < middle) {
            write_move(
              moves,
              buffer,
              !source_is_move_list,
              output++,
              source_move(
                moves,
                buffer,
                source_is_move_list,
                left++));
        }

        while (right < end) {
            write_move(
              moves,
              buffer,
              !source_is_move_list,
              output++,
              source_move(
                moves,
                buffer,
                source_is_move_list,
                right++));
        }
    }
}

}  // namespace OrderingDetail

// Orders moves by descending material-order score using stable bottom-up merge
// sort. The position is unchanged and no dynamic allocation is performed.
// Preconditions:
// - every entry in moves was generated for position;
// - buffer is not in use by another order_moves() call.
constexpr void order_moves(
  const Position& position,
  MoveList& moves,
  MoveOrderingBuffer& buffer) noexcept {
    if (moves.size() < 2)
        return;

    bool source_is_move_list = true;
    std::size_t width = 1;

    while (width < moves.size()) {
        OrderingDetail::merge_pass(
          position,
          moves,
          buffer,
          width,
          source_is_move_list);
        source_is_move_list = !source_is_move_list;

        if (width > moves.size() / 2)
            width = moves.size();
        else
            width *= 2;
    }

    if (!source_is_move_list) {
        for (std::size_t index = 0;
             index < moves.size();
             ++index)
            moves[index] = buffer[index];
    }
}

// This overload owns its temporary merge-sort buffer.
constexpr void order_moves(
  const Position& position,
  MoveList& moves) noexcept {
    MoveOrderingBuffer buffer;
    order_moves(position, moves, buffer);
}

static_assert(OrderingDetail::QUIET_SCORE
              < OrderingDetail::CAPTURE_BASE);
static_assert(OrderingDetail::MAX_CAPTURE_SCORE
              < OrderingDetail::PROMOTION_BASE);
static_assert(OrderingDetail::MAX_CAPTURED_MATERIAL
              < OrderingDetail::PROMOTION_STRIDE);
static_assert(OrderingDetail::MAX_PROMOTION_SCORE
              < OrderingDetail::KING_CAPTURE_SCORE);
static_assert(
  OrderingDetail::KING_CAPTURE_SCORE
  < std::numeric_limits<MoveOrderScore>::max());
static_assert(
  OrderingDetail::attacker_cost(PAWN)
  < OrderingDetail::attacker_cost(KNIGHT));
static_assert(
  OrderingDetail::attacker_cost(QUEEN)
  < OrderingDetail::attacker_cost(KING));

}  // namespace Mockingbird
