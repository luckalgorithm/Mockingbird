#pragma once

#include <cassert>
#include <cstdint>

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

[[nodiscard]] inline NodeResult alpha_beta(
  Position& position,
  PositionHistory& history,
  int depth,
  int ply,
  Score alpha,
  Score beta,
  SearchState& state) {
    assert(depth >= 0);
    assert(ply >= 0);
    assert(depth + ply <= MAX_SEARCH_DEPTH);
    assert(-INFINITE_SCORE <= alpha);
    assert(alpha < beta);
    assert(beta <= INFINITE_SCORE);
    assert(history.current_key() == position.key());

    if (depth == 0) {
        return {
          quiescence(
            position,
            history,
            ply,
            0,
            alpha,
            beta,
            state),
          Move::none(),
        };
    }

    ++state.nodes;

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    const PositionResult position_result =
      terminal_result(position, history, legal_moves);

    assert(position_result.is_valid());
    if (!position_result.is_valid())
        return {};

    if (position_result.is_terminal()) {
        return {
          terminal_score(
            position_result,
            team_of(position.side_to_move()),
            ply),
          Move::none(),
        };
    }

    order_moves(
      position,
      legal_moves,
      state.ordering_buffer);

    Score best_score = -INFINITE_SCORE;
    Move best_move = Move::none();

    // Material ordering is stable. Strict score comparison preserves the
    // first generated move among equal-priority moves with equal search scores.
    for (const Move move : legal_moves) {
        Score candidate_score = DRAW_SCORE;

        {
            ChildState child{position, history, move};
            // Every move advances to the opposing team, so the child score
            // and child window are negated for the parent perspective.
            const NodeResult child_result =
              alpha_beta(
                position,
                history,
                depth - 1,
                ply + 1,
                -beta,
                -alpha,
                state);
            candidate_score = -child_result.score;
        }

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
    return {best_score, best_move};
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
    const bool valid_depth =
      depth >= 0 && depth <= MAX_SEARCH_DEPTH;
    assert(valid_depth);
    if (!valid_depth)
        return {};

    const bool matching_history =
      history.current_key() == position.key();
    assert(matching_history);
    if (!matching_history)
        return {};

    PositionHistory search_history{history};
    SearchDetail::SearchState state;
    const SearchDetail::NodeResult result =
      SearchDetail::alpha_beta(
        position,
        search_history,
        depth,
        0,
        -INFINITE_SCORE,
        INFINITE_SCORE,
        state);

    return {
      result.best_move,
      result.score,
      state.nodes,
    };
}

static_assert(!SearchResult{}.has_move());

}  // namespace Mockingbird
