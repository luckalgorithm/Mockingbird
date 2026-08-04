#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "exchange.h"
#include "movelist.h"

namespace Mockingbird {

using MoveOrderScore = std::uint32_t;
using HistoryScore = std::int32_t;

namespace OrderingDetail {

inline constexpr MoveOrderScore QUIET_SCORE = 0;
inline constexpr MoveOrderScore CAPTURE_BASE = 1;
using WideMoveOrderScore = std::uint64_t;

// Higher composite keys are searched first. Quiet history occupies the low
// range, while every tactical material score occupies the disjoint high range.
using MoveOrderKey = std::uint32_t;

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

// QuietHistory entries are separated by player, piece type, and destination
// square. Unused NO_PIECE_TYPE entries remain zero.
class QuietHistory {
  public:
    static constexpr HistoryScore LIMIT = 16'384;
    static constexpr HistoryScore MAX_DEPTH_BONUS = 2'048;
    static constexpr HistoryScore DEPTH_BONUS_SCALE = 32;

    constexpr QuietHistory() noexcept = default;

    // Preconditions:
    // - piece is a real piece;
    // - destination is a playable square.
    [[nodiscard]] constexpr HistoryScore score(
      Piece piece,
      Square destination) const noexcept {
        assert(is_ok(piece));
        assert(is_ok(destination));

        return entries_[color_index(piece)]
                       [type_index(piece)]
                       [square_index(destination)];
    }

    // Applies a bounded gravity update to one entry. Positive bonuses raise
    // the entry and negative bonuses lower it.
    // Preconditions:
    // - piece is a real piece;
    // - destination is a playable square.
    constexpr void update(
      Piece piece,
      Square destination,
      HistoryScore bonus) noexcept {
        assert(is_ok(piece));
        assert(is_ok(destination));

        const std::int64_t bounded_bonus =
          bonus < -LIMIT
            ? -static_cast<std::int64_t>(LIMIT)
            : bonus > LIMIT
                ? static_cast<std::int64_t>(LIMIT)
                : static_cast<std::int64_t>(bonus);
        const std::int64_t magnitude =
          bounded_bonus < 0
            ? -bounded_bonus
            : bounded_bonus;

        Storage& entry =
          entries_[color_index(piece)]
                  [type_index(piece)]
                  [square_index(destination)];
        const std::int64_t current = entry;
        std::int64_t next =
          current + bounded_bonus
          - current * magnitude / LIMIT;

        if (next > LIMIT)
            next = LIMIT;
        else if (next < -LIMIT)
            next = -LIMIT;

        assert(
          next >= std::numeric_limits<Storage>::lowest()
          && next
               <= std::numeric_limits<Storage>::max());
        entry = static_cast<Storage>(next);
    }

    // The depth bonus grows quadratically through depth eight and remains
    // MAX_DEPTH_BONUS at greater depths.
    // Precondition: depth is positive.
    [[nodiscard]] static constexpr HistoryScore
    depth_bonus(int depth) noexcept {
        assert(depth > 0);

        constexpr int SATURATION_DEPTH = 8;
        const std::int64_t bounded_depth =
          depth < SATURATION_DEPTH
            ? depth
            : SATURATION_DEPTH;
        return static_cast<HistoryScore>(
          DEPTH_BONUS_SCALE
          * bounded_depth
          * bounded_depth);
    }

    // Precondition: depth is positive.
    constexpr void reward(
      Piece piece,
      Square destination,
      int depth) noexcept {
        update(
          piece,
          destination,
          depth_bonus(depth));
    }

    // Precondition: depth is positive.
    constexpr void penalize(
      Piece piece,
      Square destination,
      int depth) noexcept {
        update(
          piece,
          destination,
          -depth_bonus(depth));
    }

    constexpr void clear() noexcept {
        entries_ = {};
    }

  private:
    using Storage = std::int16_t;
    using SquareEntries =
      std::array<Storage, SQUARE_NB>;
    using TypeEntries =
      std::array<SquareEntries, PIECE_TYPE_NB>;
    using ColorEntries =
      std::array<TypeEntries, COLOR_NB>;

    [[nodiscard]] static constexpr std::size_t
    color_index(Piece piece) noexcept {
        return static_cast<std::size_t>(
          color_of(piece));
    }

    [[nodiscard]] static constexpr std::size_t
    type_index(Piece piece) noexcept {
        return static_cast<std::size_t>(
          type_of(piece));
    }

    [[nodiscard]] static constexpr std::size_t
    square_index(Square square) noexcept {
        return static_cast<std::size_t>(square);
    }

    ColorEntries entries_{};
};

using KillerPriority = std::uint8_t;

// KillerMoves stores two quiet beta-cutoff moves for one main-search ply.
// The primary slot contains the most recently recorded distinct move.
class KillerMoves {
  public:
    static constexpr std::size_t SLOT_NB = 2;
    static constexpr KillerPriority PRIMARY_PRIORITY = 2;
    static constexpr KillerPriority SECONDARY_PRIORITY = 1;

    constexpr KillerMoves() noexcept = default;

    [[nodiscard]] constexpr Move primary() const noexcept {
        return moves_[0];
    }

    [[nodiscard]] constexpr Move secondary() const noexcept {
        return moves_[1];
    }

    [[nodiscard]] constexpr KillerPriority priority(
      Move move) const noexcept {
        if (!move.is_board_move())
            return 0;
        if (move == primary())
            return PRIMARY_PRIORITY;
        if (move == secondary())
            return SECONDARY_PRIORITY;
        return 0;
    }

    // Recording a new move shifts the previous primary move to the secondary
    // slot. Recording the primary move leaves both slots unchanged.
    // Precondition: move is a quiet beta-cutoff move.
    constexpr void record(Move move) noexcept {
        assert(is_ok(move));

        if (move == primary())
            return;

        moves_[1] = moves_[0];
        moves_[0] = move;
    }

    constexpr void clear() noexcept {
        moves_ = {};
    }

  private:
    std::array<Move, SLOT_NB> moves_{};
};

// MoveOrderingBuffer stores merge-sort output and precomputed ordering keys
// between passes. One buffer can be reused after each completed order_moves()
// call.
class MoveOrderingBuffer {
  public:
    constexpr MoveOrderingBuffer() noexcept = default;

    // Selects inline storage for common lists and sizes overflow storage only
    // when the caller supplies a larger list.
    constexpr void prepare(std::size_t size) {
        assert(size <= MoveList::CAPACITY);
        size_ = size;
        if (size <= MoveList::INLINE_CAPACITY) {
            overflow_.clear();
            move_list_key_overflow_.clear();
            buffer_key_overflow_.clear();
            spilled_ = false;
            return;
        }

        overflow_.resize(size);
        move_list_key_overflow_.resize(size);
        buffer_key_overflow_.resize(size);
        spilled_ = true;
    }

    // Precondition: prepare() was called and index is less than its size.
    [[nodiscard]] constexpr Move& operator[](
      std::size_t index) noexcept {
        assert(index < size_);
        return spilled_
          ? overflow_[index]
          : inline_moves_[index];
    }

    // Precondition: prepare() was called and index is less than its size.
    [[nodiscard]] constexpr const Move& operator[](
      std::size_t index) const noexcept {
        assert(index < size_);
        return spilled_
          ? overflow_[index]
          : inline_moves_[index];
    }

    // Keys aligned with the caller's MoveList are kept separately from keys
    // aligned with this buffer while merge passes alternate their source.
    [[nodiscard]] constexpr OrderingDetail::MoveOrderKey&
    move_list_key(std::size_t index) noexcept {
        assert(index < size_);
        return spilled_
          ? move_list_key_overflow_[index]
          : move_list_keys_[index];
    }

    [[nodiscard]] constexpr const OrderingDetail::MoveOrderKey&
    move_list_key(std::size_t index) const noexcept {
        assert(index < size_);
        return spilled_
          ? move_list_key_overflow_[index]
          : move_list_keys_[index];
    }

    [[nodiscard]] constexpr OrderingDetail::MoveOrderKey&
    buffer_key(std::size_t index) noexcept {
        assert(index < size_);
        return spilled_
          ? buffer_key_overflow_[index]
          : buffer_keys_[index];
    }

    [[nodiscard]] constexpr const OrderingDetail::MoveOrderKey&
    buffer_key(std::size_t index) const noexcept {
        assert(index < size_);
        return spilled_
          ? buffer_key_overflow_[index]
          : buffer_keys_[index];
    }

  private:
    std::array<Move, MoveList::INLINE_CAPACITY> inline_moves_{};
    std::array<
      OrderingDetail::MoveOrderKey,
      MoveList::INLINE_CAPACITY>
      move_list_keys_{};
    std::array<
      OrderingDetail::MoveOrderKey,
      MoveList::INLINE_CAPACITY>
      buffer_keys_{};
    std::vector<Move> overflow_;
    std::vector<OrderingDetail::MoveOrderKey>
      move_list_key_overflow_;
    std::vector<OrderingDetail::MoveOrderKey>
      buffer_key_overflow_;
    std::size_t size_ = 0;
    bool spilled_ = false;
};

// Describes the proven-losing suffix created by zero-threshold exchange
// ordering. The preferred-move index records the stable rotation applied to
// the final list.
class OrderingExchangeBands {
  public:
    constexpr void reset() noexcept {
        size_ = 0;
        losing_begin_ = 0;
        promoted_index_ = 0;
        classified_ = false;
    }

    constexpr void set(
      std::size_t size,
      std::size_t losing_begin,
      std::size_t promoted_index) noexcept {
        assert(losing_begin <= size);
        assert(promoted_index <= size);
        size_ = size;
        losing_begin_ = losing_begin;
        promoted_index_ = promoted_index;
        classified_ = true;
    }

    // Precondition: final_index addresses the ordered list described by the
    // most recent set() call.
    [[nodiscard]] constexpr bool proven_below_zero(
      std::size_t final_index) const noexcept {
        assert(!classified_ || final_index < size_);
        if (!classified_)
            return false;

        std::size_t original_index = final_index;
        if (promoted_index_ < size_) {
            if (final_index == 0)
                original_index = promoted_index_;
            else if (final_index <= promoted_index_)
                original_index = final_index - 1;
        }

        return original_index >= losing_begin_;
    }

  private:
    std::size_t size_ = 0;
    std::size_t losing_begin_ = 0;
    std::size_t promoted_index_ = 0;
    bool classified_ = false;
};

namespace OrderingDetail {

inline constexpr MoveOrderKey TACTICAL_KEY_BASE =
  static_cast<MoveOrderKey>(2 * QuietHistory::LIMIT + 1);

static_assert(
  static_cast<WideMoveOrderScore>(TACTICAL_KEY_BASE)
      + KING_CAPTURE_SCORE_WIDE
    <= std::numeric_limits<MoveOrderKey>::max());

[[nodiscard]] constexpr MoveOrderKey make_move_order_key(
  const Position& position,
  Move move,
  const QuietHistory* history) noexcept {
    const MoveOrderScore material =
      move_order_score(position, move);
    if (material != QUIET_SCORE)
        return TACTICAL_KEY_BASE + material;

    const HistoryScore quiet_history = history
      ? history->score(
          position.piece_on(move.from()),
          move.to())
      : HistoryScore{0};
    assert(quiet_history >= -QuietHistory::LIMIT);
    assert(quiet_history <= QuietHistory::LIMIT);
    return static_cast<MoveOrderKey>(
      quiet_history + QuietHistory::LIMIT);
}

[[nodiscard]] constexpr bool key_precedes(
  const MoveOrderKey& candidate,
  const MoveOrderKey& current) noexcept {
    return candidate > current;
}

[[nodiscard]] constexpr bool contains_move(
  const MoveList& moves,
  Move expected) noexcept {
    for (const Move move : moves) {
        if (move == expected)
            return true;
    }

    return false;
}

[[nodiscard]] constexpr Move source_move(
  const MoveList& moves,
  const MoveOrderingBuffer& buffer,
  bool source_is_move_list,
  std::size_t index) noexcept {
    return source_is_move_list
        ? moves[index]
        : buffer[index];
}

[[nodiscard]] constexpr const MoveOrderKey& source_key(
  const MoveOrderingBuffer& buffer,
  bool source_is_move_list,
  std::size_t index) noexcept {
    return source_is_move_list
        ? buffer.move_list_key(index)
        : buffer.buffer_key(index);
}

constexpr void write_entry(
  MoveList& moves,
  MoveOrderingBuffer& buffer,
  bool destination_is_move_list,
  std::size_t index,
  Move move,
  MoveOrderKey key) noexcept {
    if (destination_is_move_list) {
        moves[index] = move;
        buffer.move_list_key(index) = key;
    } else {
        buffer[index] = move;
        buffer.buffer_key(index) = key;
    }
}

// Merges adjacent sorted ranges of width elements. Equal scores are taken
// from the left range first, preserving their existing order.
constexpr void merge_pass(
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
            const MoveOrderKey left_key =
              source_key(
                buffer,
                source_is_move_list,
                left);
            const MoveOrderKey right_key =
              source_key(
                buffer,
                source_is_move_list,
                right);
            const bool take_right =
              key_precedes(right_key, left_key);

            write_entry(
              moves,
              buffer,
              !source_is_move_list,
              output,
              take_right ? right_move : left_move,
              take_right ? right_key : left_key);

            if (take_right)
                ++right;
            else
                ++left;
            ++output;
        }

        while (left < middle) {
            write_entry(
              moves,
              buffer,
              !source_is_move_list,
              output++,
              source_move(
                moves,
                buffer,
                source_is_move_list,
                left),
              source_key(
                buffer,
                source_is_move_list,
                left));
            ++left;
        }

        while (right < end) {
            write_entry(
              moves,
              buffer,
              !source_is_move_list,
              output++,
              source_move(
                moves,
                buffer,
                source_is_move_list,
                right),
              source_key(
                buffer,
                source_is_move_list,
                right));
            ++right;
        }
    }
}

// Moves one matching entry to begin and preserves the relative order of all
// other entries. Searches beginning at begin cannot move an earlier entry.
[[nodiscard]] constexpr std::size_t promote_move(
  MoveList& moves,
  std::size_t begin,
  Move promoted) noexcept {
    if (!promoted.is_board_move()
        || begin >= moves.size()) {
        return moves.size();
    }

    std::size_t promoted_index = begin;
    while (promoted_index < moves.size()
           && moves[promoted_index] != promoted)
        ++promoted_index;

    if (promoted_index == moves.size())
        return moves.size();

    for (std::size_t index = promoted_index;
         index > begin;
         --index)
        moves[index] = moves[index - 1];

    moves[begin] = promoted;
    return promoted_index;
}

// Places present killer moves at the front of the range and preserves the
// relative order of every other move. Buffer stores the non-killer entries
// while the range is rewritten.
constexpr void prioritize_killers(
  MoveList& moves,
  MoveOrderingBuffer& buffer,
  std::size_t begin,
  std::size_t end,
  const KillerMoves& killers) noexcept {
    assert(begin <= end);
    assert(end <= moves.size());

    if (begin >= end)
        return;

    const Move primary = killers.primary();
    const Move secondary = killers.secondary();
    bool found_primary = false;
    bool found_secondary = false;
    std::size_t other_count = 0;

    for (std::size_t index = begin;
         index < end;
         ++index) {
        const Move move = moves[index];
        if (!found_primary && move == primary) {
            found_primary = true;
            continue;
        }
        if (!found_secondary && move == secondary) {
            found_secondary = true;
            continue;
        }

        buffer[other_count++] = move;
    }

    if (!found_primary && !found_secondary)
        return;

    std::size_t output = begin;
    if (found_primary)
        moves[output++] = primary;
    if (found_secondary)
        moves[output++] = secondary;

    for (std::size_t index = 0;
         index < other_count;
         ++index)
        moves[output++] = buffer[index];

    assert(output == end);
}

struct MoveBands {
    std::size_t quiet_begin = 0;
    std::size_t quiet_end = 0;
};

// Moves non-losing or unclassified tactical entries before quiet entries and
// proven losing tactical entries after them. A single bounded exchange budget
// is shared across the list. Relative order within all three ranges is
// preserved. The input has already been sorted with every tactical entry
// before every quiet entry.
[[nodiscard]] constexpr MoveBands partition_exchange_bands(
  const Position& position,
  MoveList& moves,
  MoveOrderingBuffer& buffer) noexcept {
    std::size_t tactical_end = 0;
    while (tactical_end < moves.size()
           && is_tactical_move(
                position, moves[tactical_end])) {
        ++tactical_end;
    }

    std::size_t non_losing_count = 0;
    std::size_t losing_count = 0;
    std::size_t remaining_exchange_nodes =
      MAX_ORDERING_EXCHANGE_NODES;
    for (std::size_t index = 0;
         index < tactical_end;
        ++index) {
        const Move move = moves[index];
        if (bounded_static_exchange_is_not_proven_below(
              position,
              move,
              0,
              remaining_exchange_nodes)) {
            moves[non_losing_count++] = move;
        } else {
            buffer[losing_count++] = move;
        }
    }

    const std::size_t quiet_begin =
      non_losing_count;
    std::size_t output = quiet_begin;
    for (std::size_t index = tactical_end;
         index < moves.size();
         ++index) {
        moves[output++] = moves[index];
    }
    const std::size_t quiet_end = output;

    for (std::size_t index = 0;
         index < losing_count;
         ++index) {
        moves[output++] = buffer[index];
    }

    assert(output == moves.size());
    return {quiet_begin, quiet_end};
}

constexpr void order_moves_impl(
  const Position& position,
  MoveList& moves,
  MoveOrderingBuffer& buffer,
  const QuietHistory* history,
  const KillerMoves* killers,
  Move preferred,
  OrderingExchangeBands* exchange_bands) noexcept {
    if (exchange_bands)
        exchange_bands->reset();

    buffer.prepare(moves.size());
    MoveBands bands{
      0,
      moves.size(),
    };

    if (moves.size() >= 2) {
        for (std::size_t index = 0;
             index < moves.size();
             ++index) {
            buffer.move_list_key(index) =
              make_move_order_key(
                position, moves[index], history);
        }

        bool source_is_move_list = true;
        std::size_t width = 1;

        while (width < moves.size()) {
            OrderingDetail::merge_pass(
              moves,
              buffer,
              width,
              source_is_move_list);
            source_is_move_list =
              !source_is_move_list;

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

        bands = partition_exchange_bands(
          position, moves, buffer);
    }

    if (killers
        && (killers->primary().is_board_move()
            || killers->secondary().is_board_move())) {
        prioritize_killers(
          moves,
          buffer,
          bands.quiet_begin,
          bands.quiet_end,
          *killers);
    }

    const std::size_t promoted_index =
      promote_move(moves, 0, preferred);
    if (exchange_bands && moves.size() >= 2) {
        exchange_bands->set(
          moves.size(),
          bands.quiet_end,
          promoted_index);
    }
}

}  // namespace OrderingDetail

// Orders non-losing tactical moves by descending material score, followed by
// primary and secondary killer moves, then other quiet moves by descending
// history score, and finally losing tactical moves. Equal scores retain their
// existing order. A preferred move contained in the list is placed first
// after sorting. The position is unchanged. Lists no larger than
// MoveList::INLINE_CAPACITY use no dynamic allocation.
// Preconditions:
// - every entry in moves is legal and was generated for position;
// - position contains exactly one king of each color;
// - buffer is not in use by another order_moves() call.
constexpr void order_moves(
  const Position& position,
  MoveList& moves,
  MoveOrderingBuffer& buffer,
  const QuietHistory& history,
  const KillerMoves& killers,
  Move preferred) noexcept {
    OrderingDetail::order_moves_impl(
      position,
      moves,
      buffer,
      &history,
      &killers,
      preferred,
      nullptr);
}

// This overload also exposes the proven-losing exchange band to selective
// search at the same node.
constexpr void order_moves(
  const Position& position,
  MoveList& moves,
  MoveOrderingBuffer& buffer,
  const QuietHistory& history,
  const KillerMoves& killers,
  Move preferred,
  OrderingExchangeBands& exchange_bands) noexcept {
    OrderingDetail::order_moves_impl(
      position,
      moves,
      buffer,
      &history,
      &killers,
      preferred,
      &exchange_bands);
}

// This overload applies quiet history without killer moves. Its position and
// move-list preconditions are identical to the overload above.
constexpr void order_moves(
  const Position& position,
  MoveList& moves,
  MoveOrderingBuffer& buffer,
  const QuietHistory& history,
  Move preferred = Move::none()) noexcept {
    OrderingDetail::order_moves_impl(
      position,
      moves,
      buffer,
      &history,
      nullptr,
      preferred,
      nullptr);
}

// This overload exposes the proven-losing exchange band to selective search
// at the same node.
constexpr void order_moves(
  const Position& position,
  MoveList& moves,
  MoveOrderingBuffer& buffer,
  const QuietHistory& history,
  Move preferred,
  OrderingExchangeBands& exchange_bands) noexcept {
    OrderingDetail::order_moves_impl(
      position,
      moves,
      buffer,
      &history,
      nullptr,
      preferred,
      &exchange_bands);
}

// This overload preserves stable generation order among quiet moves. Its
// position and move-list preconditions are identical to the overload above.
constexpr void order_moves(
  const Position& position,
  MoveList& moves,
  MoveOrderingBuffer& buffer,
  Move preferred = Move::none()) noexcept {
    OrderingDetail::order_moves_impl(
      position,
      moves,
      buffer,
      nullptr,
      nullptr,
      preferred,
      nullptr);
}

// This overload owns its temporary merge-sort buffer. Its position and
// move-list preconditions are identical to the overload above.
constexpr void order_moves(
  const Position& position,
  MoveList& moves,
  const QuietHistory& history,
  const KillerMoves& killers,
  Move preferred) noexcept {
    MoveOrderingBuffer buffer;
    order_moves(
      position,
      moves,
      buffer,
      history,
      killers,
      preferred);
}

// This overload owns its temporary merge-sort buffer. Its position and
// move-list preconditions are identical to the overload above.
constexpr void order_moves(
  const Position& position,
  MoveList& moves,
  const QuietHistory& history,
  Move preferred = Move::none()) noexcept {
    MoveOrderingBuffer buffer;
    order_moves(
      position,
      moves,
      buffer,
      history,
      preferred);
}

// This overload owns its temporary merge-sort buffer. Its position and
// move-list preconditions are identical to the overload above.
constexpr void order_moves(
  const Position& position,
  MoveList& moves,
  Move preferred = Move::none()) noexcept {
    MoveOrderingBuffer buffer;
    order_moves(
      position, moves, buffer, preferred);
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
static_assert(QuietHistory::LIMIT > 0);
static_assert(
  QuietHistory::LIMIT
  <= std::numeric_limits<std::int16_t>::max());
static_assert(QuietHistory::MAX_DEPTH_BONUS > 0);
static_assert(
  QuietHistory::MAX_DEPTH_BONUS
  < QuietHistory::LIMIT);
static_assert(
  QuietHistory::depth_bonus(1)
  == QuietHistory::DEPTH_BONUS_SCALE);
static_assert(
  QuietHistory::depth_bonus(8)
  == QuietHistory::MAX_DEPTH_BONUS);
static_assert(
  QuietHistory::depth_bonus(256)
  == QuietHistory::MAX_DEPTH_BONUS);
static_assert(KillerMoves{}.primary().is_none());
static_assert(KillerMoves{}.secondary().is_none());
static_assert(KillerMoves::SLOT_NB == 2);
static_assert(
  KillerMoves::PRIMARY_PRIORITY
  > KillerMoves::SECONDARY_PRIORITY);
static_assert(KillerMoves::SECONDARY_PRIORITY > 0);
static_assert(
  KillerMoves{}.priority(Move::none()) == 0);
static_assert(
  OrderingDetail::attacker_cost(PAWN)
  < OrderingDetail::attacker_cost(KNIGHT));
static_assert(
  OrderingDetail::attacker_cost(QUEEN)
  < OrderingDetail::attacker_cost(KING));

}  // namespace Mockingbird
