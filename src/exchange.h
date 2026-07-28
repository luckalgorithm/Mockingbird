#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "evaluate.h"
#include "legal.h"

namespace Mockingbird {

// King capture ends the position. This value is greater than every possible
// non-king material swing on a 160-square board.
inline constexpr std::int64_t
  EXCHANGE_KING_SCORE_WIDE =
    std::int64_t{2} * MAX_MATERIAL_SCORE + 1;
static_assert(
  EXCHANGE_KING_SCORE_WIDE
  < std::numeric_limits<Score>::max());
inline constexpr Score EXCHANGE_KING_SCORE =
  static_cast<Score>(EXCHANGE_KING_SCORE_WIDE);

// Every recapture empties its source while replacing the previous target
// occupant. The playable-square count is therefore a conservative upper bound
// on recursive reply plies.
inline constexpr int MAX_EXCHANGE_PLY =
  PLAYABLE_SQUARE_NB;
inline constexpr std::size_t
  MAX_ORDERING_EXCHANGE_NODES = 128;

namespace ExchangeDetail {

enum class ThresholdResult : std::uint8_t {
    BELOW,
    AT_LEAST,
    UNKNOWN,
};

struct ImmediateGain {
    Score material = 0;
    bool captures_king = false;
};

// A move can remove a queen on its destination and an en-passant pawn while
// replacing its moving pawn with a queen.
inline constexpr Score
  MAX_EXCHANGE_IMMEDIATE_GAIN =
    MAX_PIECE_VALUE
    + PAWN_VALUE
    + (QUEEN_VALUE - PAWN_VALUE);

// The two teams are the only valid Team values.
[[nodiscard]] constexpr Team opposing_team(Team team) noexcept {
    assert(is_ok(team));
    return team == RED_YELLOW ? BLUE_GREEN : RED_YELLOW;
}

// Returns the material change produced directly by move, before any
// recapture. Promotion replaces a pawn with the selected promotion piece.
// Preconditions:
// - move was generated for position;
// - move is not castling.
[[nodiscard]] constexpr ImmediateGain immediate_gain(
  const Position& position,
  Move move) noexcept {
    assert(is_ok(move));
    assert(move.type() != MoveType::CASTLING);
    assert(!position.empty(move.from()));

    ImmediateGain gain;
    if (!position.empty(move.to())) {
        const Piece captured =
          position.piece_on(move.to());
        gain.captures_king =
          type_of(captured) == KING;
        gain.material +=
          piece_value(type_of(captured));
    }

    if (move.type() == MoveType::EN_PASSANT)
        gain.material += PAWN_VALUE;

    if (move.is_promotion()) {
        gain.material +=
          piece_value(move.promotion_type())
          - PAWN_VALUE;
    }

    assert(
      gain.captures_king
      || gain.material
           <= MAX_EXCHANGE_IMMEDIATE_GAIN);
    return gain;
}

// A compressed turn changes the active color and expires that color's
// en-passant target. No board move is applied.
constexpr void pass_local_turn(Position& position) noexcept {
    const Color passing_color =
      position.side_to_move();
    position.clear_en_passant_square(
      passing_color);
    position.set_side_to_move(
      next_color(passing_color));
}

// Classifies whether the team opposing the piece on target can obtain at least
// threshold material. A non-null exhausted node counter may produce UNKNOWN.
// Either opposing color may capture before the target occupant's color
// receives another turn. Declining every capture scores zero. Intervening turns
// are represented by pass_local_turn().
[[nodiscard]] constexpr ThresholdResult
reply_at_least(
  const Position& position,
  Square target,
  Score threshold,
  int reply_ply,
  std::size_t* remaining_nodes) noexcept {
    assert(is_ok(target));
    assert(!position.empty(target));
    assert(reply_ply >= 0);
    assert(reply_ply < MAX_EXCHANGE_PLY);

    if (threshold <= 0)
        return ThresholdResult::AT_LEAST;
    if (threshold > EXCHANGE_KING_SCORE)
        return ThresholdResult::BELOW;
    if (remaining_nodes) {
        if (*remaining_nodes == 0)
            return ThresholdResult::UNKNOWN;
        --*remaining_nodes;
    }

    const Piece occupant =
      position.piece_on(target);
    assert(
      position.side_to_move()
      == next_color(color_of(occupant)));
    const Team capturing_team =
      opposing_team(team_of(color_of(occupant)));
    bool unknown = false;

    // The next color and the color two turns after it belong to the opposing
    // team. The intervening colors belong to the target occupant's team.
    for (int turn_offset = 0;
         turn_offset < COLOR_NB;
         turn_offset += 2) {
        Position branch = position;
        for (int skipped_turn = 0;
             skipped_turn < turn_offset;
             ++skipped_turn) {
            pass_local_turn(branch);
        }

        if (team_of(branch.side_to_move())
            != capturing_team) {
            continue;
        }

        MoveList pseudo_moves;
        generate_moves(branch, pseudo_moves);

        for (const Move move : pseudo_moves) {
            if (move.to() != target
                || move.type() == MoveType::CASTLING) {
                continue;
            }

            const ImmediateGain gain =
              immediate_gain(branch, move);
            if (gain.captures_king)
                return ThresholdResult::AT_LEAST;

            // Every recursive reply is non-negative. A move whose immediate
            // gain is below threshold cannot reach threshold.
            if (gain.material < threshold)
                continue;

            const Color moving_color =
              branch.side_to_move();
            UndoState undo;
            do_move(branch, move, undo);
            if (in_check(branch, moving_color)) {
                undo_move(branch, move, undo);
                continue;
            }

            // gain - reply >= threshold exactly when
            // reply < gain - threshold + 1.
            const Score reply_threshold =
              gain.material - threshold + 1;
            const ThresholdResult reply_result =
              reply_at_least(
                branch,
                target,
                reply_threshold,
                reply_ply + 1,
                remaining_nodes);
            undo_move(branch, move, undo);

            if (reply_result
                == ThresholdResult::BELOW) {
                return ThresholdResult::AT_LEAST;
            }
            if (reply_result
                == ThresholdResult::UNKNOWN) {
                unknown = true;
            }
        }
    }

    return unknown
        ? ThresholdResult::UNKNOWN
        : ThresholdResult::BELOW;
}

// Classifies whether move's complete local exchange value is at least
// threshold. A non-null exhausted node counter may produce UNKNOWN.
// Preconditions are identical to static_exchange_evaluation().
[[nodiscard]] constexpr ThresholdResult
move_at_least(
  const Position& position,
  Move move,
  Score threshold,
  std::size_t* remaining_nodes = nullptr) noexcept {
    assert(is_ok(move));
    assert(move.type() != MoveType::CASTLING);

    if (threshold <= -EXCHANGE_KING_SCORE)
        return ThresholdResult::AT_LEAST;
    if (threshold > EXCHANGE_KING_SCORE)
        return ThresholdResult::BELOW;

    const ImmediateGain gain =
      immediate_gain(position, move);
    if (gain.captures_king)
        return ThresholdResult::AT_LEAST;

    // gain - reply >= threshold exactly when reply is no greater than limit.
    const Score limit =
      gain.material - threshold;
    if (limit < 0)
        return ThresholdResult::BELOW;

    Position branch = position;
    UndoState undo;
    do_move(branch, move, undo);
    const ThresholdResult reply_result =
      reply_at_least(
        branch,
        move.to(),
        limit + 1,
        0,
        remaining_nodes);

    if (reply_result == ThresholdResult::AT_LEAST)
        return ThresholdResult::BELOW;
    if (reply_result == ThresholdResult::BELOW)
        return ThresholdResult::AT_LEAST;
    return ThresholdResult::UNKNOWN;
}

[[nodiscard]] constexpr bool is_not_proven_losing(
  ThresholdResult result) noexcept {
    return result != ThresholdResult::BELOW;
}

}  // namespace ExchangeDetail

// Returns the local non-king material swing from the moving team's
// perspective. Positive values favor the mover and negative values favor the
// opposing team. The exchange model does not simulate moves on compressed
// turns that could answer checks, block lines, or change occupancy. Its result
// is suitable for move ordering rather than legality or pruning.
// Preconditions:
// - move is a legal move generated for position;
// - move is not castling;
// - position contains exactly one king of each color.
[[nodiscard]] constexpr Score static_exchange_evaluation(
  const Position& position,
  Move move) noexcept {
    assert(is_ok(move));
    assert(move.type() != MoveType::CASTLING);

    Score lower = -EXCHANGE_KING_SCORE;
    Score upper = EXCHANGE_KING_SCORE;

    // move_at_least() is monotonic in its threshold. Binary search returns the
    // greatest threshold satisfied by the exact exchange value.
    while (lower < upper) {
        const Score middle =
          lower
          + static_cast<Score>(
              (static_cast<std::int64_t>(upper)
               - lower + 1)
              / 2);

        const ExchangeDetail::ThresholdResult
          result =
            ExchangeDetail::move_at_least(
              position, move, middle);
        assert(
          result
          != ExchangeDetail::ThresholdResult::UNKNOWN);

        if (result
            == ExchangeDetail::ThresholdResult::AT_LEAST) {
            lower = middle;
        } else {
            upper = middle - 1;
        }
    }

    return lower;
}

// Evaluates the complete local exchange and compares it with threshold.
// Preconditions are identical to static_exchange_evaluation().
[[nodiscard]] constexpr bool static_exchange_at_least(
  const Position& position,
  Move move,
  Score threshold = 0) noexcept {
    const ExchangeDetail::ThresholdResult result =
      ExchangeDetail::move_at_least(
        position, move, threshold);
    assert(
      result
      != ExchangeDetail::ThresholdResult::UNKNOWN);
    return result
        == ExchangeDetail::ThresholdResult::AT_LEAST;
}

// Returns false only when the bounded zero-threshold search proves that move
// loses material. The traversal expands at most MAX_ORDERING_EXCHANGE_NODES
// recursive reply nodes; an unfinished result remains above the losing
// ordering band.
// Preconditions are identical to static_exchange_evaluation().
[[nodiscard]] constexpr bool
static_exchange_is_not_proven_losing(
  const Position& position,
  Move move) noexcept {
    std::size_t remaining_nodes =
      MAX_ORDERING_EXCHANGE_NODES;
    const ExchangeDetail::ThresholdResult result =
      ExchangeDetail::move_at_least(
        position,
        move,
        0,
        &remaining_nodes);
    return ExchangeDetail::is_not_proven_losing(
      result);
}

static_assert(
  EXCHANGE_KING_SCORE
  > 2 * MAX_MATERIAL_SCORE);
static_assert(
  EXCHANGE_KING_SCORE_WIDE
    + ExchangeDetail::MAX_EXCHANGE_IMMEDIATE_GAIN
  < std::numeric_limits<Score>::max());
static_assert(MAX_ORDERING_EXCHANGE_NODES > 0);
static_assert(
  !ExchangeDetail::is_not_proven_losing(
    ExchangeDetail::ThresholdResult::BELOW));
static_assert(
  ExchangeDetail::is_not_proven_losing(
    ExchangeDetail::ThresholdResult::AT_LEAST));
static_assert(
  ExchangeDetail::is_not_proven_losing(
    ExchangeDetail::ThresholdResult::UNKNOWN));

}  // namespace Mockingbird
