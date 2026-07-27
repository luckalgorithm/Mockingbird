#pragma once

#include <cassert>
#include <cstdint>
#include <limits>

#include "evaluate.h"
#include "result.h"
#include "transition.h"

namespace Mockingbird {

inline constexpr Score DRAW_SCORE = 0;
inline constexpr int MAX_SEARCH_DEPTH = 256;

// The score ranges leave space between material evaluation, mate scores, and
// the search-window bounds.
inline constexpr Score MATE_SCORE = 1'000'000;
inline constexpr Score INFINITE_SCORE = 2'000'000;

// SearchResult::nodes counts every entered node, including the root and
// horizon nodes.
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

struct SearchState {
    std::uint64_t nodes = 0;
};

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
        do_move(position_, move_, undo_);
        child_key_ = position_.key();

        try {
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
    assert(ply <= MAX_SEARCH_DEPTH);

    const auto winner = result.winning_team();
    if (!winner)
        return DRAW_SCORE;

    const Score win_score =
      MATE_SCORE - static_cast<Score>(ply);
    return *winner == perspective
        ? win_score
        : -win_score;
}

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

    if (depth == 0)
        return {evaluate(position), Move::none()};

    Score best_score = -INFINITE_SCORE;
    Move best_move = Move::none();

    // Legal moves retain generation order. Strict score comparison preserves
    // the first generated move when two moves have equal scores.
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

// Returns the fixed-depth negamax result. Terminal positions end a line before
// the horizon, and alpha-beta bounds can prune lines that cannot affect it.
// The position is restored before return, and history is read-only.
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

static_assert(MAX_SEARCH_DEPTH > 0);
static_assert(
  MATE_SCORE - MAX_SEARCH_DEPTH
  > MAX_MATERIAL_SCORE);
static_assert(MATE_SCORE < INFINITE_SCORE);
static_assert(
  INFINITE_SCORE
  < std::numeric_limits<Score>::max());
static_assert(
  -INFINITE_SCORE
  > std::numeric_limits<Score>::lowest());
static_assert(!SearchResult{}.has_move());

}  // namespace Mockingbird
