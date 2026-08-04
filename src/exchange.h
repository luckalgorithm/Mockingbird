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
  MAX_ORDERING_EXCHANGE_NODES = 64;

// The fast threshold routine classifies supported exchanges without changing
// the position. UNKNOWN preserves moves whose special capture rules are not
// represented by its occupancy-only model.
enum class FastExchangeResult : std::uint8_t {
    BELOW,
    AT_LEAST,
    UNKNOWN,
};

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

// Returns the value of the piece occupying move.to() after move. Promotion
// replaces the moving pawn before the first opposing reply.
[[nodiscard]] constexpr Score post_move_piece_value(
  const Position& position,
  Move move) noexcept {
    assert(is_ok(move));
    assert(move.type() != MoveType::CASTLING);
    assert(!position.empty(move.from()));

    return move.is_promotion()
        ? piece_value(move.promotion_type())
        : piece_value(
            type_of(position.piece_on(move.from())));
}

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

// Returns an upper bound on the material available in the first opposing
// capture on move.to(). The moved piece is always available. An en-passant
// reply can remove one additional pawn, and a promotion reply can add the
// difference between a queen and a pawn. Active targets that the move itself
// expires are deliberately retained in the bound.
[[nodiscard]] constexpr Score maximum_first_reply_gain(
  const Position& position,
  Move move) noexcept {
    assert(is_ok(move));
    assert(move.type() != MoveType::CASTLING);

    Score maximum = post_move_piece_value(
      position, move);
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        if (position.en_passant_square(color)
            == move.to()) {
            maximum += PAWN_VALUE;
            break;
        }
    }

    const Team moving_team =
      team_of(position.side_to_move());
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        if (team_of(color) != moving_team
            && is_pawn_promotion_square(
                 color, move.to())) {
            maximum += QUEEN_VALUE - PAWN_VALUE;
            break;
        }
    }

    return maximum;
}

// The opposing team may decline every capture, so its optimal reply is
// non-negative and cannot exceed the immediate gain of its first capture.
// This proves a threshold whenever move's direct gain minus that first-reply
// upper bound already reaches threshold.
[[nodiscard]] constexpr bool immediate_gain_guarantees(
  const Position& position,
  Move move,
  Score threshold) noexcept {
    assert(is_ok(move));
    assert(move.type() != MoveType::CASTLING);

    const ImmediateGain gain =
      immediate_gain(position, move);
    if (gain.captures_king)
        return threshold <= EXCHANGE_KING_SCORE;

    const std::int64_t guaranteed_floor =
      static_cast<std::int64_t>(gain.material)
      - maximum_first_reply_gain(position, move);
    return guaranteed_floor >= threshold;
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
    Position branch = position;
    int branch_turn_offset = 0;

    // The next color and the color two turns after it belong to the opposing
    // team. The intervening colors belong to the target occupant's team.
    // Every candidate move is undone before the next candidate, so one branch
    // can advance cumulatively to each eligible color.
    for (int turn_offset = 0;
         turn_offset < COLOR_NB;
         turn_offset += 2) {
        while (branch_turn_offset < turn_offset) {
            pass_local_turn(branch);
            ++branch_turn_offset;
        }

        if (team_of(branch.side_to_move())
            != capturing_team) {
            continue;
        }

        MoveList pseudo_moves;
        generate_tactical_moves(branch, pseudo_moves);

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

[[nodiscard]] constexpr bool is_not_proven_below(
  ThresholdResult result) noexcept {
    return result != ThresholdResult::BELOW;
}

struct FastAttacker {
    Square source = SQ_NONE;
    PieceType piece_type = NO_PIECE_TYPE;
    Color color = COLOR_NB;
    bool promotes = false;
};

// Returns live attackers under a local exchange occupancy. The position's
// piece masks retain removed source squares, so the final intersection is
// required after each recapture.
[[nodiscard]] constexpr Bitboard fast_attackers_to(
  const Position& position,
  Square target,
  const Bitboard& occupied) noexcept {
    return attackers_to(position, target, occupied)
         & occupied;
}

// Tests king safety after source captures on target. target is absent from the
// exchange occupancy because its changing occupant is not represented in the
// position's immutable piece masks. It is restored as a ray blocker while the
// resulting king position is tested, then excluded from the checker set.
[[nodiscard]] constexpr bool fast_attacker_is_legal(
  const Position& position,
  Square source,
  Square target,
  PieceType piece_type,
  Color color,
  const Bitboard& occupied) noexcept {
    assert(is_ok(source));
    assert(is_ok(target));
    assert(is_ok(piece_type));
    assert(is_ok(color));
    assert(occupied.test(source));

    Bitboard resulting_occupied = occupied;
    resulting_occupied.clear(source);
    const Team opponents = opposing_team(team_of(color));

    if (piece_type == KING) {
        return !(fast_attackers_to(
                    position,
                    target,
                    resulting_occupied)
                 & position.pieces(opponents));
    }

    const Bitboard kings =
      position.pieces(color, KING) & occupied;
    if (kings.popcount() != 1)
        return false;

    Bitboard blocking_occupancy = resulting_occupied;
    blocking_occupancy.set(target);
    const Bitboard checkers =
      attackers_to(
        position,
        kings.lsb(),
        blocking_occupancy)
      & resulting_occupied
      & position.pieces(opponents);
    return checkers.empty();
}

// Finds the least valuable legal non-king attacker belonging to capturing_team,
// with kings considered after every material piece. A king on target ends the
// exchange before the capturing piece's king safety matters.
[[nodiscard]] constexpr FastAttacker find_fast_attacker(
  const Position& position,
  Square target,
  Team capturing_team,
  PieceType target_piece_type,
  const Bitboard& occupied,
  const Bitboard& attackers) noexcept {
    assert(is_ok(target));
    assert(is_ok(capturing_team));
    assert(is_ok(target_piece_type));

    const Bitboard team_attackers =
      attackers & position.pieces(capturing_team);
    for (int type_index = PAWN;
         type_index <= KING;
         ++type_index) {
        const PieceType piece_type =
          PieceType(type_index);
        Bitboard candidates =
          team_attackers & position.pieces(piece_type);
        FastAttacker selected;

        while (candidates) {
            const Square source = candidates.pop_lsb();
            const Piece piece = position.piece_on(source);
            assert(type_of(piece) == piece_type);
            const Color color = color_of(piece);

            if (target_piece_type != KING
                && !fast_attacker_is_legal(
                     position,
                     source,
                     target,
                     piece_type,
                     color,
                     occupied)) {
                continue;
            }

            if (selected.source == SQ_NONE) {
                selected.source = source;
                selected.piece_type = piece_type;
                selected.color = color;
            }
            if (piece_type == PAWN
                && is_pawn_promotion_square(
                     color, target)) {
                selected.promotes = true;
            }
        }

        if (selected.source != SQ_NONE)
            return selected;
    }

    return {};
}

[[nodiscard]] constexpr bool has_en_passant_target(
  const Position& position,
  Square target) noexcept {
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        if (position.en_passant_square(
              Color(color_index)) == target) {
            return true;
        }
    }

    return false;
}

// Pawn attack geometry is independent of occupancy between source and target.
// This detects every live pawn that would promote while recapturing on target.
[[nodiscard]] constexpr bool has_promotion_attacker(
  const Position& position,
  Square target,
  const Bitboard& occupied) noexcept {
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        if (!is_pawn_promotion_square(
              color, target)) {
            continue;
        }

        const Color reverse =
          next_color(next_color(color));
        if (pawn_attacks(reverse, target)
            & position.pieces(color, PAWN)
            & occupied) {
            return true;
        }
    }

    return false;
}

}  // namespace ExchangeDetail

// Compares a normal move's local exchange value with threshold using a single
// occupancy bitboard and target-centric attacker queries. Each team may use
// the least valuable legal attacker belonging to either teammate. The routine
// does not copy Position, generate a MoveList, or perform move transitions.
//
// Promotions, en-passant moves, and exchanges on an active en-passant target
// return UNKNOWN because their material and expiry rules require state beyond
// the occupancy-only exchange model. A pawn recapture that reaches its
// promotion zone also returns UNKNOWN.
//
// Preconditions:
// - move is a legal move generated for position;
// - position contains exactly one king of each color.
[[nodiscard]] constexpr FastExchangeResult
fast_static_exchange_at_least(
  const Position& position,
  Move move,
  Score threshold = 0) noexcept {
    assert(is_ok(move));
    assert(!position.empty(move.from()));

    if (threshold <= -EXCHANGE_KING_SCORE)
        return FastExchangeResult::AT_LEAST;
    if (threshold > EXCHANGE_KING_SCORE)
        return FastExchangeResult::BELOW;

    const Piece moving_piece =
      position.piece_on(move.from());
    const Piece captured_piece =
      position.empty(move.to())
        ? NO_PIECE
        : position.piece_on(move.to());
    if (captured_piece != NO_PIECE
        && type_of(captured_piece) == KING) {
        return FastExchangeResult::AT_LEAST;
    }

    if (move.type() != MoveType::NORMAL
        || ExchangeDetail::has_en_passant_target(
             position, move.to())
        || (type_of(moving_piece) == PAWN
            && is_pawn_promotion_square(
                 color_of(moving_piece),
                 move.to()))) {
        return FastExchangeResult::UNKNOWN;
    }

    Bitboard occupied = position.occupied();
    occupied.clear(move.from());
    occupied.clear(move.to());
    if (ExchangeDetail::has_promotion_attacker(
          position, move.to(), occupied)) {
        return FastExchangeResult::UNKNOWN;
    }

    const Score captured_value =
      captured_piece == NO_PIECE
        ? Score{0}
        : piece_value(type_of(captured_piece));
    Score swap = captured_value - threshold;
    if (swap < 0)
        return FastExchangeResult::BELOW;

    swap = piece_value(type_of(moving_piece))
         - swap;
    if (swap <= 0)
        return FastExchangeResult::AT_LEAST;

    Team occupant_team =
      team_of(color_of(moving_piece));
    PieceType occupant_piece_type =
      type_of(moving_piece);
    bool at_least = true;
    Bitboard attackers =
      ExchangeDetail::fast_attackers_to(
        position, move.to(), occupied);
    const Bitboard diagonal_sliders =
      position.pieces(BISHOP)
      | position.pieces(QUEEN);
    const Bitboard orthogonal_sliders =
      position.pieces(ROOK)
      | position.pieces(QUEEN);

    while (true) {
        attackers &= occupied;
        const Team capturing_team =
          ExchangeDetail::opposing_team(
            occupant_team);
        const ExchangeDetail::FastAttacker attacker =
          ExchangeDetail::find_fast_attacker(
            position,
            move.to(),
            capturing_team,
            occupant_piece_type,
            occupied,
            attackers);
        if (attacker.source == SQ_NONE)
            break;
        if (attacker.promotes)
            return FastExchangeResult::UNKNOWN;

        at_least = !at_least;
        swap = piece_value(attacker.piece_type)
             - swap;
        if (swap < static_cast<Score>(at_least))
            break;

        occupied.clear(attacker.source);
        // Removing a pawn or slider can reveal a compatible slider behind its
        // source. Fixed-distance attackers do not create target-ray x-rays.
        if (attacker.piece_type == PAWN
            || attacker.piece_type == BISHOP
            || attacker.piece_type == QUEEN) {
            attackers |=
              bishop_attacks(move.to(), occupied)
              & diagonal_sliders;
        }
        if (attacker.piece_type == ROOK
            || attacker.piece_type == QUEEN) {
            attackers |=
              rook_attacks(move.to(), occupied)
              & orthogonal_sliders;
        }
        occupant_team = capturing_team;
        occupant_piece_type = attacker.piece_type;

        if (attacker.piece_type == KING)
            break;
    }

    return at_least
        ? FastExchangeResult::AT_LEAST
        : FastExchangeResult::BELOW;
}

// UNKNOWN passes through the fast filter so unsupported special exchanges are
// never classified as losing by this routine alone.
[[nodiscard]] constexpr bool
fast_static_exchange_is_not_proven_below(
  const Position& position,
  Move move,
  Score threshold = 0) noexcept {
    return fast_static_exchange_at_least(
             position, move, threshold)
        != FastExchangeResult::BELOW;
}

// Returns the local non-king material swing from the moving team's
// perspective. Positive values favor the mover and negative values favor the
// opposing team. The exchange model does not simulate moves on compressed
// turns that could answer checks, block lines, or change occupancy. Its result
// can order moves, but does not alone prove that a full-position continuation
// is unprofitable.
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

// Returns false only when exchange analysis classifies move below threshold.
// Supported normal moves use the occupancy-only classifier. Other moves fall
// back to the full-state traversal, whose reply-node budget is shared with the
// caller. An exhausted fallback traversal leaves the move unclassified.
// Preconditions are identical to static_exchange_evaluation().
[[nodiscard]] constexpr bool
bounded_static_exchange_is_not_proven_below(
  const Position& position,
  Move move,
  Score threshold,
  std::size_t& remaining_reply_nodes) noexcept {
    const FastExchangeResult fast_result =
      fast_static_exchange_at_least(
        position, move, threshold);
    if (fast_result != FastExchangeResult::UNKNOWN) {
        return fast_result
            == FastExchangeResult::AT_LEAST;
    }

    if (ExchangeDetail::immediate_gain_guarantees(
          position, move, threshold)) {
        return true;
    }

    const ExchangeDetail::ThresholdResult result =
      ExchangeDetail::move_at_least(
        position,
        move,
        threshold,
        &remaining_reply_nodes);
    return ExchangeDetail::is_not_proven_below(
      result);
}

// Creates an independent reply-node budget and returns false only when that
// search proves that move's local exchange value is below threshold.
// The traversal expands at most max_reply_nodes recursive reply nodes.
// Preconditions are identical to static_exchange_evaluation().
[[nodiscard]] constexpr bool
static_exchange_is_not_proven_below(
  const Position& position,
  Move move,
  Score threshold,
  std::size_t max_reply_nodes) noexcept {
    assert(max_reply_nodes > 0);

    std::size_t remaining_nodes =
      max_reply_nodes;
    return bounded_static_exchange_is_not_proven_below(
      position,
      move,
      threshold,
      remaining_nodes);
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
    return static_exchange_is_not_proven_below(
      position,
      move,
      0,
      MAX_ORDERING_EXCHANGE_NODES);
}

static_assert(
  EXCHANGE_KING_SCORE
  > 2 * MAX_MATERIAL_SCORE);
static_assert(
  EXCHANGE_KING_SCORE_WIDE
    + ExchangeDetail::MAX_EXCHANGE_IMMEDIATE_GAIN
  < std::numeric_limits<Score>::max());
static_assert(MAX_ORDERING_EXCHANGE_NODES > 0);
static_assert(PAWN_VALUE < KNIGHT_VALUE);
static_assert(KNIGHT_VALUE < BISHOP_VALUE);
static_assert(BISHOP_VALUE < ROOK_VALUE);
static_assert(ROOK_VALUE < QUEEN_VALUE);
static_assert(
  !ExchangeDetail::is_not_proven_below(
    ExchangeDetail::ThresholdResult::BELOW));
static_assert(
  ExchangeDetail::is_not_proven_below(
    ExchangeDetail::ThresholdResult::AT_LEAST));
static_assert(
  ExchangeDetail::is_not_proven_below(
    ExchangeDetail::ThresholdResult::UNKNOWN));

}  // namespace Mockingbird
