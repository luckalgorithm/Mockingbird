#pragma once

#include <atomic>
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
    // The caller retains ownership of this flag for the complete search.
    // A true value stops search at the next node-entry budget check.
    const std::atomic_bool* external_stop = nullptr;
};

enum class IterativeStop : std::uint8_t {
    DEPTH_LIMIT,
    TERMINAL_POSITION,
    NODE_LIMIT,
    TIME_LIMIT,
    EXTERNAL_STOP,
    INVALID_LIMITS,
    INVALID_INPUT,
};

struct CompletedIteration {
    SearchResult result;
    int depth = 0;
    // Attempts includes the initial search and every aspiration re-search at
    // this depth.
    std::uint32_t attempts = 0;

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

inline constexpr std::int64_t
  INITIAL_ASPIRATION_HALF_WIDTH =
    PAWN_VALUE / 2;
inline constexpr std::int64_t
  FULL_ASPIRATION_HALF_WIDTH =
    std::int64_t{2} * INFINITE_SCORE;

struct AspirationWindow {
    Score alpha = -INFINITE_SCORE;
    Score beta = INFINITE_SCORE;

    [[nodiscard]] constexpr bool
    is_full() const noexcept {
        return alpha == -INFINITE_SCORE
            && beta == INFINITE_SCORE;
    }

    // Alpha-beta scores equal to a boundary retain only an upper or lower
    // bound. An exact score lies strictly between both boundaries.
    [[nodiscard]] constexpr bool
    contains_exact(Score score) const noexcept {
        return score > alpha && score < beta;
    }

    [[nodiscard]] friend constexpr bool operator==(
      const AspirationWindow&,
      const AspirationWindow&) noexcept = default;
};

inline constexpr AspirationWindow FULL_ASPIRATION_WINDOW{};

[[nodiscard]] constexpr AspirationWindow
make_aspiration_window(
  Score center,
  std::int64_t half_width) noexcept {
    assert(center > -INFINITE_SCORE);
    assert(center < INFINITE_SCORE);
    assert(half_width > 0);
    assert(
      half_width
      <= FULL_ASPIRATION_HALF_WIDTH);

    constexpr std::int64_t minimum =
      -std::int64_t{INFINITE_SCORE};
    constexpr std::int64_t maximum =
      std::int64_t{INFINITE_SCORE};
    const std::int64_t lower =
      std::int64_t{center} - half_width;
    const std::int64_t upper =
      std::int64_t{center} + half_width;
    const std::int64_t clamped_lower =
      lower < minimum ? minimum : lower;
    const std::int64_t clamped_upper =
      upper > maximum ? maximum : upper;

    const AspirationWindow window{
      static_cast<Score>(clamped_lower),
      static_cast<Score>(clamped_upper),
    };
    assert(window.alpha < window.beta);
    return window;
}

// A completed failed search supplies a bound on the exact score. The next
// half-width both doubles and extends at least one score unit beyond that
// bound.
[[nodiscard]] constexpr std::int64_t
widen_aspiration_half_width(
  Score center,
  Score bound,
  std::int64_t half_width) noexcept {
    assert(center > -INFINITE_SCORE);
    assert(center < INFINITE_SCORE);
    assert(bound >= -INFINITE_SCORE);
    assert(bound <= INFINITE_SCORE);
    assert(half_width > 0);
    assert(
      half_width
      < FULL_ASPIRATION_HALF_WIDTH);

    const std::int64_t center_value = center;
    const std::int64_t bound_value = bound;
    const std::int64_t distance =
      bound_value >= center_value
        ? bound_value - center_value
        : center_value - bound_value;
    const std::int64_t required =
      distance + 1;
    const std::int64_t doubled =
      half_width
          >= FULL_ASPIRATION_HALF_WIDTH / 2
        ? FULL_ASPIRATION_HALF_WIDTH
        : half_width * 2;
    const std::int64_t widened =
      doubled > required ? doubled : required;

    return widened
             < FULL_ASPIRATION_HALF_WIDTH
      ? widened
      : FULL_ASPIRATION_HALF_WIDTH;
}

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

        case SearchStopReason::EXTERNAL_STOP:
            return IterativeStop::EXTERNAL_STOP;
    }

    assert(false);
    return IterativeStop::INVALID_INPUT;
}

}  // namespace IterationDetail

// Searches consecutive depths and retains only the deepest completed
// iteration. Node and time limits are cumulative across the complete call.
// One transposition table is shared by every iteration. The previous
// completed root move is ordered first in the next iteration when it remains
// legal. After depth one, each iteration starts with an aspiration window
// around the previous exact score. Failed searches widen that window and are
// included in the iteration's node and attempt counts.
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
namespace IterationDetail {

[[nodiscard]] inline IterativeResult iterative_search_impl(
  Position& position,
  const PositionHistory& history,
  const IterativeLimits& limits,
  TranspositionTable& table) {
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
      limits.node_limit,
      deadline,
      limits.external_stop};
    table.new_search();
    SearchDetail::LimitedSearchState state{
      std::move(budget),
      &table};
    PositionHistory search_history{history};
    Move previous_best = Move::none();
    Score previous_score = DRAW_SCORE;

    for (int depth = 1;
         depth <= limits.max_depth;
         ++depth) {
        const std::uint64_t iteration_start_nodes =
          state.nodes;
        std::int64_t half_width =
          depth == 1
            ? IterationDetail::FULL_ASPIRATION_HALF_WIDTH
            : IterationDetail::INITIAL_ASPIRATION_HALF_WIDTH;
        IterationDetail::AspirationWindow window =
          depth == 1
            ? IterationDetail::FULL_ASPIRATION_WINDOW
            : IterationDetail::make_aspiration_window(
                  previous_score,
                  half_width);
        Move attempt_preferred = previous_best;
        SearchDetail::NodeResult completed;
        std::uint32_t attempts = 0;

        while (true) {
            ++attempts;
            const auto iteration =
              SearchDetail::alpha_beta(
                position,
                search_history,
                depth,
                0,
                window.alpha,
                window.beta,
                state,
                attempt_preferred);

            assert(
              search_history.current_key()
              == position.key());

            if (!iteration) {
                result.total_nodes = state.nodes;
                return finish(
                  IterationDetail::iterative_stop(
                    iteration.error()));
            }

            if (!iteration->best_move.is_board_move()
                || window.is_full()
                || window.contains_exact(
                     iteration->score)) {
                completed = *iteration;
                break;
            }

            attempt_preferred =
              iteration->best_move;

            half_width =
              IterationDetail::widen_aspiration_half_width(
                  previous_score,
                  iteration->score,
                  half_width);
            window =
              IterationDetail::make_aspiration_window(
                  previous_score,
                  half_width);
        }

        assert(attempts > 0);
        assert(completed.score > -INFINITE_SCORE);
        assert(completed.score < INFINITE_SCORE);
        result.last_completed =
          CompletedIteration{
            SearchResult{
              completed.best_move,
              completed.score,
              state.nodes
                - iteration_start_nodes,
            },
            depth,
            attempts,
          };
        result.total_nodes = state.nodes;

        if (!completed.best_move.is_board_move()) {
            return finish(
              IterativeStop::TERMINAL_POSITION);
        }

        previous_best = completed.best_move;
        previous_score = completed.score;
    }

    return finish(IterativeStop::DEPTH_LIMIT);
}

}  // namespace IterationDetail

// Reuses table across root searches. Entries remain qualified by their
// position key and repetition-history context.
[[nodiscard]] inline IterativeResult iterative_search(
  Position& position,
  const PositionHistory& history,
  const IterativeLimits& limits,
  TranspositionTable& table) {
    return IterationDetail::iterative_search_impl(
      position, history, limits, table);
}

// Uses a new default-sized transposition table for this call.
[[nodiscard]] inline IterativeResult iterative_search(
  Position& position,
  const PositionHistory& history,
  const IterativeLimits& limits) {
    TranspositionTable table;
    return IterationDetail::iterative_search_impl(
      position, history, limits, table);
}

static_assert(
  IterationDetail::INITIAL_ASPIRATION_HALF_WIDTH
  > 0);
static_assert(
  IterationDetail::make_aspiration_window(
    DRAW_SCORE,
    IterationDetail::INITIAL_ASPIRATION_HALF_WIDTH)
  == IterationDetail::AspirationWindow{
       -PAWN_VALUE / 2,
       PAWN_VALUE / 2,
     });
static_assert(
  IterationDetail::make_aspiration_window(
    MATE_SCORE,
    IterationDetail::FULL_ASPIRATION_HALF_WIDTH)
    .is_full());
static_assert(
  IterationDetail::widen_aspiration_half_width(
    DRAW_SCORE,
    PAWN_VALUE / 2,
    IterationDetail::INITIAL_ASPIRATION_HALF_WIDTH)
  == PAWN_VALUE);
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
