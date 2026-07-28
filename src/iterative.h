#pragma once

#include <cassert>
#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>

#include "search.h"

namespace Mockingbird {

struct IterativeLimits {
    // Iterations search depths 1 through max_depth.
    int max_depth = 1;
    std::optional<std::uint64_t> node_limit;
    std::optional<SearchDuration> time_limit;
};

enum class IterativeStop : std::uint8_t {
    DEPTH_LIMIT,
    TERMINAL_POSITION,
    NODE_LIMIT,
    TIME_LIMIT,
    INVALID_LIMITS,
    INVALID_INPUT,
};

struct CompletedIteration {
    SearchResult result;
    int depth = 0;

    [[nodiscard]] friend constexpr bool operator==(
      const CompletedIteration&,
      const CompletedIteration&) noexcept = default;
};

struct IterativeResult {
    // last_completed excludes every partially searched iteration.
    std::optional<CompletedIteration> last_completed;

    // total_nodes includes completed iterations and the final partial
    // iteration, when one was interrupted.
    std::uint64_t total_nodes = 0;
    SearchDuration elapsed{};
    IterativeStop stop = IterativeStop::INVALID_INPUT;

    [[nodiscard]] constexpr bool
    has_completed_iteration() const noexcept {
        return last_completed.has_value();
    }

    [[nodiscard]] constexpr bool has_move() const noexcept {
        return last_completed
            && last_completed->result.has_move();
    }
};

namespace IterationDetail {

[[nodiscard]] constexpr bool valid_limits(
  const IterativeLimits& limits) noexcept {
    return limits.max_depth >= 1
        && limits.max_depth <= MAX_SEARCH_DEPTH
        && (!limits.time_limit
            || *limits.time_limit
                 >= SearchDuration::zero());
}

[[nodiscard]] constexpr IterativeStop
iterative_stop(SearchStopReason reason) noexcept {
    switch (reason) {
        case SearchStopReason::NODE_LIMIT:
            return IterativeStop::NODE_LIMIT;

        case SearchStopReason::TIME_LIMIT:
            return IterativeStop::TIME_LIMIT;
    }

    assert(false);
    return IterativeStop::INVALID_INPUT;
}

}  // namespace IterationDetail

// Searches consecutive depths and retains only the deepest completed
// iteration. Node and time limits are cumulative across the complete call.
// One transposition table is shared by every iteration. The previous
// completed root move is ordered first in the next iteration when it remains
// legal.
// Time is checked when a node is entered, so one active node can finish after
// the requested duration.
//
// A zero node or time limit is valid and prevents the first node from being
// entered. When both limits stop the same node, the node limit has precedence.
//
// Preconditions:
// - max_depth is in the inclusive range 1..MAX_SEARCH_DEPTH;
// - time_limit is absent or non-negative;
// - history.current_key() equals position.key();
// - the position has a result-valid king layout.
[[nodiscard]] inline IterativeResult iterative_search(
  Position& position,
  const PositionHistory& history,
  const IterativeLimits& limits) {
    const SearchClock::time_point start =
      SearchClock::now();
    IterativeResult result;

    const auto finish =
      [&](IterativeStop stop) {
          result.stop = stop;
          result.elapsed =
            SearchClock::now() - start;
          return result;
      };

    const bool limits_are_valid =
      IterationDetail::valid_limits(limits);
    assert(limits_are_valid);
    if (!limits_are_valid)
        return finish(
          IterativeStop::INVALID_LIMITS);

    const bool matching_history =
      history.current_key() == position.key();
    assert(matching_history);
    if (!matching_history)
        return finish(
          IterativeStop::INVALID_INPUT);

    const bool valid_king_layout =
      Detail::king_layout(position)
      != Detail::KingLayout::INVALID;
    assert(valid_king_layout);
    if (!valid_king_layout)
        return finish(
          IterativeStop::INVALID_INPUT);

    std::optional<SearchClock::time_point> deadline;
    if (limits.time_limit) {
        deadline = SearchDetail::make_deadline(
          start, *limits.time_limit);
    }

    SearchDetail::SearchBudget budget{
      limits.node_limit, deadline};
    TranspositionTable table;
    table.new_search();
    SearchDetail::LimitedSearchState state{
      std::move(budget),
      &table};
    PositionHistory search_history{history};
    Move previous_best = Move::none();

    for (int depth = 1;
         depth <= limits.max_depth;
         ++depth) {
        const std::uint64_t iteration_start_nodes =
          state.nodes;
        const auto iteration =
          SearchDetail::alpha_beta(
            position,
            search_history,
            depth,
            0,
            -INFINITE_SCORE,
            INFINITE_SCORE,
            state,
            previous_best);

        assert(
          search_history.current_key()
          == position.key());

        if (!iteration) {
            result.total_nodes = state.nodes;
            return finish(
              IterationDetail::iterative_stop(
                iteration.error()));
        }

        result.last_completed =
          CompletedIteration{
            SearchResult{
              iteration->best_move,
              iteration->score,
              state.nodes
                - iteration_start_nodes,
            },
            depth,
          };
        result.total_nodes = state.nodes;

        if (!iteration->best_move.is_board_move()) {
            return finish(
              IterativeStop::TERMINAL_POSITION);
        }

        previous_best = iteration->best_move;
    }

    return finish(IterativeStop::DEPTH_LIMIT);
}

static_assert(
  IterationDetail::valid_limits(
    IterativeLimits{}));
static_assert(
  !IterationDetail::valid_limits(
    IterativeLimits{
      .max_depth = 0,
      .node_limit = std::nullopt,
      .time_limit = std::nullopt,
    }));
static_assert(
  !IterativeResult{}.has_completed_iteration());
static_assert(!IterativeResult{}.has_move());

}  // namespace Mockingbird
