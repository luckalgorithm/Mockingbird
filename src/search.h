#pragma once

#include <cassert>
#include <cstdint>
#include <expected>

#include "quiescence.h"

namespace Mockingbird {

// SearchResult::nodes counts every entered node, including the root and
// quiescence nodes.
struct SearchResult {
    Move best_move = Move::none();
    Score score = DRAW_SCORE;
    std::uint64_t nodes = 0;

    [[nodiscard]] constexpr bool has_move() const noexcept {
        return best_move.is_board_move();
    }

    [[nodiscard]] friend constexpr bool operator==(
      const SearchResult&,
      const SearchResult&) noexcept = default;
};

namespace SearchDetail {

struct NodeResult {
    Score score = DRAW_SCORE;
    Move best_move = Move::none();
};

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

template<typename State>
[[nodiscard]] inline
std::expected<NodeResult, SearchStopReason>
alpha_beta(
  Position& position,
  PositionHistory& history,
  int depth,
  int ply,
  Score alpha,
  Score beta,
  State& state,
  Move preferred_move = Move::none()) {
    assert(depth >= 0);
    assert(ply >= 0);
    assert(depth + ply <= MAX_SEARCH_DEPTH);
    assert(-INFINITE_SCORE <= alpha);
    assert(alpha < beta);
    assert(beta <= INFINITE_SCORE);
    assert(history.current_key() == position.key());

    if (depth == 0) {
        const auto score =
          quiescence(
            position,
            history,
            ply,
            0,
            alpha,
            beta,
            state);
        if (!score)
            return std::unexpected(
              score.error());

        return NodeResult{
          *score,
          Move::none(),
        };
    }

    const NodeEntry entry = state.enter_node();
    if (!entry)
        return std::unexpected(entry.error());

    const Score original_alpha = alpha;
    const Score original_beta = beta;

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    const PositionResult position_result =
      terminal_result(position, history, legal_moves);

    assert(position_result.is_valid());
    if (!position_result.is_valid())
        return {};

    if (position_result.is_terminal()) {
        return NodeResult{
          terminal_score(
            position_result,
            team_of(position.side_to_move()),
            ply),
          Move::none(),
        };
    }

    TranspositionTable* const table =
      state.table();
    Move table_move = Move::none();

    if (table) {
        const TranspositionEntry* cached =
          table->probe(
            position.key(),
            history.context());
        table_move =
          cached
              && cached->best_move.is_board_move()
            ? cached->best_move
            : table->best_move(position.key());

        if (cached
            && cached->depth == depth
            && OrderingDetail::contains_move(
                 legal_moves,
                 cached->best_move)) {
            const Score cached_score =
              score_from_table(
                cached->score, ply);

            const bool cutoff =
              cached->bound
                  == TranspositionBound::EXACT
              || (cached->bound
                    == TranspositionBound::LOWER
                  && cached_score >= beta)
              || (cached->bound
                    == TranspositionBound::UPPER
                  && cached_score <= alpha);

            if (cutoff) {
                return NodeResult{
                  cached_score,
                  cached->best_move,
                };
            }
        }
    }

    const Move ordering_move =
      OrderingDetail::contains_move(
        legal_moves, preferred_move)
        ? preferred_move
        : table_move;
    order_moves(
      position,
      legal_moves,
      state.ordering_buffer,
      ordering_move);

    Score best_score = -INFINITE_SCORE;
    Move best_move = Move::none();

    // Strict score comparison preserves the first searched move among moves
    // that receive equal search scores.
    for (const Move move : legal_moves) {
        std::expected<NodeResult, SearchStopReason>
          child_result{NodeResult{}};

        {
            ChildState child{position, history, move};
            // Every move advances to the opposing team, so the child score
            // and child window are negated for the parent perspective.
            child_result = alpha_beta(
              position,
              history,
              depth - 1,
              ply + 1,
              -beta,
              -alpha,
              state);
        }

        if (!child_result)
            return std::unexpected(
              child_result.error());

        const Score candidate_score =
          -child_result->score;

        if (candidate_score > best_score) {
            best_score = candidate_score;
            best_move = move;
        }

        if (candidate_score > alpha)
            alpha = candidate_score;

        if (alpha >= beta)
            break;
    }

    assert(is_ok(best_move));

    if (table) {
        table->store(
          position.key(),
          history.context(),
          depth,
          score_to_table(best_score, ply),
          classify_bound(
            best_score,
            original_alpha,
            original_beta),
          best_move);
    }

    return NodeResult{best_score, best_move};
}

[[nodiscard]] inline bool valid_search_input(
  const Position& position,
  const PositionHistory& history,
  int depth) noexcept {
    const bool valid_depth =
      depth >= 0 && depth <= MAX_SEARCH_DEPTH;
    assert(valid_depth);
    if (!valid_depth)
        return false;

    const bool matching_history =
      history.current_key() == position.key();
    assert(matching_history);
    return matching_history;
}

[[nodiscard]] inline SearchResult run_search(
  Position& position,
  const PositionHistory& history,
  int depth,
  TranspositionTable* table) {
    PositionHistory search_history{history};
    SearchState state{
      UnlimitedBudget{},
      table};
    const auto result =
      alpha_beta(
        position,
        search_history,
        depth,
        0,
        -INFINITE_SCORE,
        INFINITE_SCORE,
        state);

    assert(result.has_value());
    if (!result)
        return {};

    return {
      result->best_move,
      result->score,
      state.nodes,
    };
}

}  // namespace SearchDetail

// Returns the fixed-depth negamax result. At the nominal horizon, quiescence
// continues captures, promotions, and every legal check evasion. Terminal
// positions end a line before evaluation, and alpha-beta bounds can prune
// lines that cannot affect the result. The position is restored before
// return, and history is read-only.
// Preconditions:
// - depth is in the inclusive range 0..MAX_SEARCH_DEPTH;
// - history.current_key() equals position.key();
// - the position has a result-valid king layout.
[[nodiscard]] inline SearchResult search(
  Position& position,
  const PositionHistory& history,
  int depth) {
    if (!SearchDetail::valid_search_input(
          position, history, depth)) {
        return {};
    }

    return SearchDetail::run_search(
      position, history, depth, nullptr);
}

// Uses caller-owned table storage and advances its generation once for this
// root search. Table contents remain available to later calls.
[[nodiscard]] inline SearchResult search(
  Position& position,
  const PositionHistory& history,
  int depth,
  TranspositionTable& table) {
    if (!SearchDetail::valid_search_input(
          position, history, depth)) {
        return {};
    }

    table.new_search();

    return SearchDetail::run_search(
      position, history, depth, &table);
}

static_assert(!SearchResult{}.has_move());
static_assert(
  SearchDetail::TABLE_MATE_THRESHOLD
  > MAX_MATERIAL_SCORE);
static_assert(
  MATE_SCORE + MAX_SEARCH_PLY
  < INFINITE_SCORE);

}  // namespace Mockingbird
