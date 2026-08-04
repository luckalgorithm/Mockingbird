#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <utility>

#include "budget.h"
#include "evaluate.h"
#include "ordering.h"
#include "result.h"
#include "transposition.h"
#include "transition.h"

namespace Mockingbird {

inline constexpr Score DRAW_SCORE = 0;
inline constexpr int MAX_SEARCH_DEPTH = 256;
inline constexpr int MAX_QUIESCENCE_PLY = 16;
inline constexpr int MAX_QUIESCENCE_CHECK_PLY = 24;
inline constexpr std::size_t
  MAX_QUIESCENCE_EXCHANGE_NODES = 64;
inline constexpr Score QUIESCENCE_DELTA_MARGIN =
  3 * PAWN_VALUE;
inline constexpr std::size_t
  QUIESCENCE_LATE_CAPTURE_LIMIT = 2;
inline constexpr int MAX_SEARCH_PLY =
  MAX_SEARCH_DEPTH + MAX_QUIESCENCE_CHECK_PLY;

// The score ranges leave space between material evaluation, mate scores, and
// the search-window bounds.
inline constexpr Score MATE_SCORE = 1'000'000;
inline constexpr Score INFINITE_SCORE = 2'000'000;

namespace SearchDetail {

inline constexpr Score TABLE_MATE_THRESHOLD =
  MATE_SCORE - MAX_SEARCH_PLY;

// Mate scores are stored relative to the current node and reconstructed for
// the probing node's distance from the root.
[[nodiscard]] constexpr Score score_to_table(
  Score score,
  int ply) noexcept {
    assert(ply >= 0);
    assert(ply <= MAX_SEARCH_PLY);

    if (score >= TABLE_MATE_THRESHOLD)
        return score + static_cast<Score>(ply);
    if (score <= -TABLE_MATE_THRESHOLD)
        return score - static_cast<Score>(ply);
    return score;
}

[[nodiscard]] constexpr Score score_from_table(
  Score score,
  int ply) noexcept {
    assert(ply >= 0);
    assert(ply <= MAX_SEARCH_PLY);

    if (score >= TABLE_MATE_THRESHOLD)
        return score - static_cast<Score>(ply);
    if (score <= -TABLE_MATE_THRESHOLD)
        return score + static_cast<Score>(ply);
    return score;
}

[[nodiscard]] constexpr TranspositionBound
classify_bound(
  Score score,
  Score original_alpha,
  Score original_beta) noexcept {
    assert(original_alpha < original_beta);

    if (score <= original_alpha)
        return TranspositionBound::UPPER;
    if (score >= original_beta)
        return TranspositionBound::LOWER;
    return TranspositionBound::EXACT;
}

[[nodiscard]] constexpr bool transposition_cutoff(
  int stored_depth,
  int requested_depth,
  TranspositionBound bound,
  Score score,
  Score alpha,
  Score beta) noexcept {
    assert(stored_depth >= 0);
    assert(requested_depth >= 0);
    assert(bound != TranspositionBound::NONE);
    assert(alpha < beta);

    if (stored_depth < requested_depth)
        return false;

    return bound == TranspositionBound::EXACT
        || (bound == TranspositionBound::LOWER
            && score >= beta)
        || (bound == TranspositionBound::UPPER
            && score <= alpha);
}

// A beta cutoff depends on the current child's final accepted result, not on
// earlier fail-low siblings. A completed exact or upper-bound node depends on
// every final child result. Repetition in the node's own history always
// remains relevant. Speculative reduced and scout attempts must be excluded
// from both child arguments when a later search replaces them.
[[nodiscard]] constexpr bool
repetition_sensitive_after_final_child(
  bool own_node_sensitive,
  bool earlier_final_children_sensitive,
  bool current_final_child_sensitive,
  bool beta_cutoff) noexcept {
    return own_node_sensitive
        || current_final_child_sensitive
        || (!beta_cutoff
            && earlier_final_children_sensitive);
}

template<typename Budget>
class BasicSearchState {
  public:
    constexpr BasicSearchState() noexcept = default;

    constexpr explicit BasicSearchState(
      Budget budget,
      TranspositionTable* table = nullptr) noexcept
        : budget_(std::move(budget)),
          table_(table) {}

    // selective_depth is reset at the beginning of every root iteration.
    // Updating it before the budget check avoids a second success branch in
    // the normal node-entry path. An interrupted iteration is never
    // published, so only successful entries contribute to reported depths.
    [[nodiscard]] NodeEntry enter_node(int ply) noexcept {
        assert(ply >= 0);
        assert(ply <= MAX_SEARCH_PLY);
        if (ply > selective_depth_)
            selective_depth_ = ply;
        return budget_.enter_node(nodes);
    }

    constexpr void reset_selective_depth() noexcept {
        selective_depth_ = 0;
    }

    [[nodiscard]] constexpr int
    selective_depth() const noexcept {
        return selective_depth_;
    }

    [[nodiscard]] constexpr const Budget&
    budget() const noexcept {
        return budget_;
    }

    [[nodiscard]] constexpr TranspositionTable*
    table() const noexcept {
        return table_;
    }

    // Main-search nodes with positive depth use ply values from zero through
    // MAX_SEARCH_DEPTH - 1.
    // Precondition: ply is in that inclusive range.
    [[nodiscard]] constexpr KillerMoves& killer_moves(
      int ply) noexcept {
        assert(ply >= 0);
        assert(ply < MAX_SEARCH_DEPTH);
        return killer_moves_[
          static_cast<std::size_t>(ply)];
    }

    // Main-search nodes with positive depth use ply values from zero through
    // MAX_SEARCH_DEPTH - 1.
    // Precondition: ply is in that inclusive range.
    [[nodiscard]] constexpr const KillerMoves&
    killer_moves(int ply) const noexcept {
        assert(ply >= 0);
        assert(ply < MAX_SEARCH_DEPTH);
        return killer_moves_[
          static_cast<std::size_t>(ply)];
    }

    std::uint64_t nodes = 0;
    MoveOrderingBuffer ordering_buffer;
    QuietHistory quiet_history;

  private:
    std::array<
      KillerMoves,
      static_cast<std::size_t>(MAX_SEARCH_DEPTH)>
      killer_moves_{};
    int selective_depth_ = 0;
    Budget budget_;
    TranspositionTable* table_ = nullptr;
};

using SearchState =
  BasicSearchState<UnlimitedBudget>;
using LimitedSearchState =
  BasicSearchState<SearchBudget>;

// A constructed ChildState owns one applied move and its history entry.
// Destruction removes the entry before undoing the move.
class ChildState {
  public:
    ChildState(
      Position& position,
      PositionHistory& history,
      Move move)
        : position_(position),
          history_(history),
          move_(move) {
        const bool irreversible =
          is_repetition_irreversible(
            position_, move_);
        do_move(position_, move_, undo_);
        child_key_ = position_.key();

        try {
            if (irreversible)
                history_.push_irreversible(child_key_);
            else
                history_.push(child_key_);
        } catch (...) {
            undo_move(position_, move_, undo_);
            throw;
        }
    }

    ChildState(const ChildState&) = delete;
    ChildState& operator=(const ChildState&) = delete;
    ChildState(ChildState&&) = delete;
    ChildState& operator=(ChildState&&) = delete;

    ~ChildState() noexcept {
        history_.pop(child_key_);
        undo_move(position_, move_, undo_);
    }

  private:
    Position& position_;
    PositionHistory& history_;
    Move move_;
    UndoState undo_;
    PositionKey child_key_ = 0;
};

// Returns a terminal score from the team-to-move perspective.
[[nodiscard]] constexpr Score terminal_score(
  const PositionResult& result,
  Team perspective,
  int ply) noexcept {
    assert(result.is_terminal());
    assert(is_ok(perspective));
    assert(ply >= 0);
    assert(ply <= MAX_SEARCH_PLY);

    const auto winner = result.winning_team();
    if (!winner)
        return DRAW_SCORE;

    const Score win_score =
      MATE_SCORE - static_cast<Score>(ply);
    return *winner == perspective
        ? win_score
        : -win_score;
}

struct QuiescenceResult {
    Score score = DRAW_SCORE;
    bool repetition_sensitive = false;
    Move best_move = Move::none();
    bool stand_pat = false;
};

// Returns whether either king on team is currently attacked. Active positions
// contain exactly one king of each color.
[[nodiscard]] constexpr bool
quiescence_team_has_checked_king(
  const Position& position,
  Team team) noexcept {
    assert(is_ok(team));

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        if (team_of(color) == team
            && in_check(position, color)) {
            return true;
        }
    }

    return false;
}

// Checked nodes, promotions, and opposing-king captures remain in the
// quiescence tree regardless of their local material exchange.
[[nodiscard]] constexpr bool
is_quiescence_exchange_candidate(
  const Position& position,
  Move move,
  bool checked) noexcept {
    assert(is_ok(move));

    return !checked
        && is_capture_move(position, move)
        && !move.is_promotion()
        && !Detail::captures_opposing_king(
             position,
             move,
             position.side_to_move());
}

// Returns false when the bounded exchange search reaches its node limit.
// UNKNOWN therefore keeps the move in the quiescence tree.
[[nodiscard]] constexpr bool
quiescence_exchange_is_proven_below(
  const Position& position,
  Move move,
  Score threshold,
  std::size_t& remaining_exchange_nodes) noexcept {
    return ExchangeDetail::move_at_least(
             position,
             move,
             threshold,
             &remaining_exchange_nodes)
        == ExchangeDetail::ThresholdResult::BELOW;
}

[[nodiscard]] constexpr bool
quiescence_exchange_is_proven_losing(
  const Position& position,
  Move move,
  std::size_t& remaining_exchange_nodes) noexcept {
    return quiescence_exchange_is_proven_below(
      position,
      move,
      0,
      remaining_exchange_nodes);
}

// Delta pruning applies only to ordinary captures. Promotions, en-passant
// captures, and opposing-king captures retain their existing search behavior.
[[nodiscard]] constexpr bool
is_quiescence_delta_candidate(
  const Position& position,
  Move move,
  bool checked) noexcept {
    assert(is_ok(move));

    return move.type() == MoveType::NORMAL
        && is_quiescence_exchange_candidate(
             position, move, checked);
}

// The immediate material gain is an upper bound on an ordinary capture's
// complete exchange value because the opposing team may decline every reply.
// Equality is retained so a capture that can exactly reach its threshold is
// still searched.
[[nodiscard]] constexpr bool
quiescence_immediate_gain_is_below(
  const Position& position,
  Move move,
  Score threshold) noexcept {
    assert(is_ok(move));
    assert(
      is_quiescence_delta_candidate(
        position, move, false));

    if (threshold <= 0)
        return false;

    const ExchangeDetail::ImmediateGain gain =
      ExchangeDetail::immediate_gain(
        position, move);
    assert(!gain.captures_king);
    return gain.material < threshold;
}

// Late-capture pruning is limited to ordinary captures whose tactical result
// does not require special-move or king-capture handling. A capture on the
// preceding move's destination is a recapture and remains searchable. An
// unknown preceding destination disables the heuristic at the current node.
[[nodiscard]] constexpr bool
is_late_quiescence_capture_candidate(
  const Position& position,
  Move move,
  bool checked,
  Square previous_destination,
  std::size_t searched_captures) noexcept {
    assert(is_ok(move));
    assert(
      previous_destination == SQ_NONE
      || is_ok(previous_destination));

    return previous_destination != SQ_NONE
        && searched_captures
             >= QUIESCENCE_LATE_CAPTURE_LIMIT
        && move.type() == MoveType::NORMAL
        && is_quiescence_exchange_candidate(
             position, move, checked)
        && move.to() != previous_destination;
}

// Quiescence uses material ordering without the eager exchange partition used
// by the main search. Exchange thresholds are classified lazily in the move
// loop, so each capture consumes at most one bounded exchange search.
constexpr void order_quiescence_moves(
  const Position& position,
  MoveList& moves,
  MoveOrderingBuffer& buffer,
  const QuietHistory& history,
  Move preferred) noexcept {
    buffer.prepare(moves.size());

    if (moves.size() >= 2) {
        for (std::size_t index = 0;
             index < moves.size();
             ++index) {
            buffer.move_list_key(index) =
              OrderingDetail::make_move_order_key(
                position, moves[index], &history);
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
                 ++index) {
                moves[index] = buffer[index];
            }
        }
    }

    static_cast<void>(
      OrderingDetail::promote_move(
        moves, 0, preferred));
}

// Returns the local exchange gain required to reach alpha after adding the
// fixed delta margin to the parent static score. Threshold zero retains the
// existing losing-exchange test.
[[nodiscard]] constexpr Score
quiescence_exchange_threshold(
  Score parent_static_score,
  Score alpha,
  bool delta_candidate) noexcept {
    assert(
      -MAX_EVALUATION_SCORE
      <= parent_static_score);
    assert(
      parent_static_score
      <= MAX_EVALUATION_SCORE);
    assert(-INFINITE_SCORE <= alpha);
    assert(alpha <= INFINITE_SCORE);

    if (!delta_candidate)
        return 0;

    const std::int64_t threshold =
      static_cast<std::int64_t>(alpha)
      - parent_static_score
      - QUIESCENCE_DELTA_MARGIN;
    if (threshold <= 0)
        return 0;

    assert(
      threshold
      <= std::numeric_limits<Score>::max());
    return static_cast<Score>(threshold);
}

// Checking moves remain searchable even when the local exchange cannot reach
// its threshold. Opposing-king captures are excluded by the exchange
// candidate predicate.
[[nodiscard]] constexpr bool
selective_quiescence_capture_requires_search(
  const Position& position,
  Team moving_team) noexcept {
    assert(is_ok(moving_team));

    const Team opposing_team =
      ExchangeDetail::opposing_team(moving_team);
    return quiescence_team_has_checked_king(
      position, opposing_team);
}

// Searches legal captures and promotions until the position is quiet. Checked
// nodes search every legal evasion and do not use stand-pat evaluation.
// Terminal positions are classified before static evaluation. Tactical lines
// stop at MAX_QUIESCENCE_PLY. Consecutive checked positions may continue to
// MAX_QUIESCENCE_CHECK_PLY so a checked node is not normally scored as quiet.
// previous_destination is the preceding board move's destination; SQ_NONE
// disables context-dependent late-capture pruning at this node.
template<typename State>
[[nodiscard]] inline
std::expected<QuiescenceResult, SearchStopReason>
quiescence_with_repetition(
  Position& position,
  PositionHistory& history,
  int ply,
  int quiescence_ply,
  Score alpha,
  Score beta,
  State& state,
  bool transposition_allowed = true,
  std::optional<Score> known_static_score = std::nullopt,
  Square previous_destination = SQ_NONE) {
    assert(ply >= 0);
    assert(quiescence_ply >= 0);
    assert(
      quiescence_ply
      <= MAX_QUIESCENCE_CHECK_PLY);
    assert(ply >= quiescence_ply);
    assert(ply - quiescence_ply <= MAX_SEARCH_DEPTH);
    assert(ply <= MAX_SEARCH_PLY);
    assert(-INFINITE_SCORE <= alpha);
    assert(alpha < beta);
    assert(beta <= INFINITE_SCORE);
    assert(history.current_key() == position.key());
    assert(
      previous_destination == SQ_NONE
      || is_ok(previous_destination));

    const NodeEntry entry = state.enter_node(ply);
    if (!entry)
        return std::unexpected(entry.error());

    const Score original_alpha = alpha;
    const Score original_beta = beta;
    const bool own_node_repetition_sensitive =
      history.has_repeated_position();
    bool earlier_final_children_sensitive = false;
    bool repetition_sensitive =
      own_node_repetition_sensitive;
    TranspositionTable* const table =
      quiescence_ply == 0
          && transposition_allowed
          && !repetition_sensitive
        ? state.table()
        : nullptr;

    const Detail::KingLayout king_layout =
      Detail::king_layout(position);
    if (king_layout != Detail::KingLayout::COMPLETE) {
        const PositionResult king_result =
          Detail::classify_result_with_facts(
            position,
            history,
            king_layout,
            true,
            false);

        assert(
          king_result.is_terminal()
          || !king_result.is_valid());
        if (!king_result.is_valid()) {
            return QuiescenceResult{
              DRAW_SCORE,
              repetition_sensitive,
            };
        }

        return QuiescenceResult{
          terminal_score(
            king_result,
            team_of(position.side_to_move()),
            ply),
          repetition_sensitive,
        };
    }

    const Detail::LegalMoveContext legal_context =
      Detail::make_legal_move_context(position);
    const bool checked = legal_context.checked;
    MoveList legal_moves;
    if (checked) {
        Detail::generate_legal_moves_with_context(
          position, legal_moves, legal_context);
    } else {
        Detail::generate_legal_tactical_moves_with_context(
          position, legal_moves, legal_context);
    }

    const bool legal_move_exists =
      !legal_moves.empty()
      || (!checked
          && Detail::has_legal_move_with_context(
               position, legal_context));
    const PositionResult position_result =
      Detail::classify_result_with_facts(
        position,
        history,
        king_layout,
        legal_move_exists,
        checked);

    assert(position_result.is_valid());
    if (!position_result.is_valid())
        return QuiescenceResult{
          DRAW_SCORE,
          repetition_sensitive,
        };

    if (position_result.is_terminal()) {
        return QuiescenceResult{
          terminal_score(
            position_result,
            team_of(position.side_to_move()),
            ply),
          repetition_sensitive,
        };
    }

    // The ordinary bound ends quiet tactical lines. A checked node receives a
    // bounded extension so at least one legal evasion is searched unless the
    // separate check limit is reached.
    if ((!checked
         && quiescence_ply >= MAX_QUIESCENCE_PLY)
        || quiescence_ply
             == MAX_QUIESCENCE_CHECK_PLY) {
        return QuiescenceResult{
          evaluate(position),
          repetition_sensitive,
        };
    }

    Move table_move = Move::none();
    std::optional<Score> table_static_score;
    if (table) {
        const TranspositionEntry* const preview =
          table->find(position.key());
        if (preview && preview->has_static_evaluation())
            table_static_score = preview->static_evaluation;
        table_move = table->best_move(
          position.key());
        const TranspositionEntry* const cached =
          table->probe(
            position.key(), history.context());
        if (cached) {
            const bool cached_move_is_legal =
              OrderingDetail::contains_move(
                legal_moves,
                cached->best_move);
            if (cached_move_is_legal)
                table_move = cached->best_move;

            const bool cached_stand_pat_is_valid =
              cached->depth == 0
              && cached->stand_pat
              && cached->best_move.is_none()
              && !checked;
            const bool cached_result_is_valid =
              cached->depth == 0
              && (cached_move_is_legal
                  || cached_stand_pat_is_valid);

            const Score cached_score =
              score_from_table(
                cached->score, ply);
            if (cached_result_is_valid
                && transposition_cutoff(
                  cached->depth,
                  0,
                  cached->bound,
                  cached_score,
                  alpha,
                  beta)) {
                return QuiescenceResult{
                  cached_score,
                  false,
                  cached_move_is_legal
                    ? cached->best_move
                    : Move::none(),
                  cached_stand_pat_is_valid,
                };
            }
        }

        if (!OrderingDetail::contains_move(
              legal_moves, table_move)) {
            table_move = Move::none();
        }

    }

    Score parent_static_score = -INFINITE_SCORE;
    const auto completed_result =
      [&](Score score,
          Move best_move,
          bool stand_pat,
          bool sensitive) {
        const QuiescenceResult result{
          score,
          sensitive,
          best_move,
          stand_pat,
        };
        if (table && !sensitive) {
            table->store_quiescence(
              position.key(),
              history.context(),
              score_to_table(score, ply),
              classify_bound(
                score,
                original_alpha,
                original_beta),
              best_move,
              stand_pat,
              parent_static_score == -INFINITE_SCORE
                ? NO_STATIC_EVALUATION
                : parent_static_score);
        }
        return result;
    };

    Score best_score = -INFINITE_SCORE;
    Move best_move = Move::none();
    bool best_is_stand_pat = false;

    if (!checked) {
        parent_static_score = known_static_score
          ? *known_static_score
          : table_static_score
              ? *table_static_score
              : evaluate(position);
        best_score = parent_static_score;
        best_is_stand_pat = true;
        if (best_score >= beta) {
            return completed_result(
              best_score,
              Move::none(),
              true,
              repetition_sensitive);
        }
        if (best_score > alpha)
            alpha = best_score;
    }

    if (checked) {
        order_moves(
          position,
          legal_moves,
          state.ordering_buffer,
          state.quiet_history,
          table_move);
    } else {
        order_quiescence_moves(
          position,
          legal_moves,
          state.ordering_buffer,
          state.quiet_history,
          table_move);
    }

    std::size_t remaining_exchange_nodes =
      MAX_QUIESCENCE_EXCHANGE_NODES;
    std::size_t searched_captures = 0;

    for (std::size_t move_index = 0;
         move_index < legal_moves.size();
         ++move_index) {
        const Move move = legal_moves[move_index];
        const Color moving_color =
          position.side_to_move();
        const bool capture =
          is_capture_move(position, move);
        const bool exchange_candidate =
          is_quiescence_exchange_candidate(
            position, move, checked);
        const bool delta_candidate =
          is_quiescence_delta_candidate(
            position, move, checked);
        const bool late_capture_candidate =
          is_late_quiescence_capture_candidate(
            position,
            move,
            checked,
            previous_destination,
            searched_captures);
        bool proven_below_exchange_threshold =
          false;
        if (exchange_candidate
            && !late_capture_candidate) {
            const Score exchange_threshold =
              quiescence_exchange_threshold(
                parent_static_score,
                alpha,
                delta_candidate);
            if (delta_candidate
                && quiescence_immediate_gain_is_below(
                     position,
                     move,
                     exchange_threshold)) {
                proven_below_exchange_threshold = true;
            } else {
                proven_below_exchange_threshold =
                  quiescence_exchange_is_proven_below(
                    position,
                    move,
                    exchange_threshold,
                    remaining_exchange_nodes);
            }
        }

        std::expected<
          QuiescenceResult,
          SearchStopReason> child_result{
            QuiescenceResult{}};
        {
            ChildState child{position, history, move};

            if ((proven_below_exchange_threshold
                 || late_capture_candidate)
                && !selective_quiescence_capture_requires_search(
                     position,
                     team_of(moving_color)))
                continue;

            if (capture)
                ++searched_captures;

            child_result =
              quiescence_with_repetition(
                position,
                history,
                ply + 1,
                quiescence_ply + 1,
                -beta,
                -alpha,
                state,
                transposition_allowed,
                std::nullopt,
                move.to());
        }

        if (!child_result)
            return std::unexpected(
              child_result.error());

        const Score candidate_score =
          -child_result->score;

        if (candidate_score > best_score) {
            best_score = candidate_score;
            best_move = move;
            best_is_stand_pat = false;
        }

        if (candidate_score > alpha)
            alpha = candidate_score;

        const bool beta_cutoff = alpha >= beta;
        repetition_sensitive =
          repetition_sensitive_after_final_child(
            own_node_repetition_sensitive,
            earlier_final_children_sensitive,
            child_result->repetition_sensitive,
            beta_cutoff);

        if (beta_cutoff)
            break;

        earlier_final_children_sensitive =
          earlier_final_children_sensitive
          || child_result->repetition_sensitive;
    }

    assert(best_score != -INFINITE_SCORE);
    return completed_result(
      best_score,
      best_move,
      best_is_stand_pat,
      repetition_sensitive);
}

// Preserves the score-only quiescence interface used by direct callers. Main
// search uses quiescence_with_repetition so repetition-derived horizon scores
// cannot be published in the transposition table.
template<typename State>
[[nodiscard]] inline
std::expected<Score, SearchStopReason>
quiescence(
  Position& position,
  PositionHistory& history,
  int ply,
  int quiescence_ply,
  Score alpha,
  Score beta,
  State& state,
  bool transposition_allowed = true) {
    const auto result =
      quiescence_with_repetition(
        position,
        history,
        ply,
        quiescence_ply,
        alpha,
        beta,
        state,
        transposition_allowed);
    if (!result)
        return std::unexpected(result.error());

    return result->score;
}

}  // namespace SearchDetail

static_assert(MAX_SEARCH_DEPTH > 0);
static_assert(MAX_QUIESCENCE_PLY > 0);
static_assert(MAX_QUIESCENCE_EXCHANGE_NODES > 0);
static_assert(QUIESCENCE_DELTA_MARGIN > 0);
static_assert(QUIESCENCE_LATE_CAPTURE_LIMIT > 0);
static_assert(
  MAX_QUIESCENCE_CHECK_PLY
  > MAX_QUIESCENCE_PLY);
static_assert(MAX_SEARCH_PLY > MAX_SEARCH_DEPTH);
static_assert(
  MATE_SCORE - MAX_SEARCH_PLY
  > MAX_EVALUATION_SCORE);
static_assert(MATE_SCORE < INFINITE_SCORE);
static_assert(
  INFINITE_SCORE
  < std::numeric_limits<Score>::max());
static_assert(
  -INFINITE_SCORE
  > std::numeric_limits<Score>::lowest());

}  // namespace Mockingbird
