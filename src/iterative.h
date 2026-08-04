#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <optional>
#include <utility>

#include "search.h"

namespace Mockingbird {

struct IterativeLimits {
    // Iterations search depths 1 through max_depth.
    int max_depth = 1;
    std::optional<std::uint64_t> node_limit;
    // time_limit is the hard node-entry deadline. soft_time_limit is checked
    // only after a completed iteration, so the last trustworthy result is
    // retained before another iteration is started.
    std::optional<SearchDuration> time_limit;
    std::optional<SearchDuration> soft_time_limit;
    // A shared control supplies dormant deadlines that another thread can
    // activate while search is running. The caller retains ownership for the
    // complete search.
    const SearchDetail::SearchTimeControl* time_control = nullptr;
    // The caller retains ownership of this flag for the complete search.
    // A true value stops search at the next sampled control check. Sampling
    // can admit at most CONTROL_CHECK_INTERVAL - 1 additional nodes.
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
    // selective_depth is the greatest entered ply across every aspiration
    // attempt that contributed to this completed iteration.
    int selective_depth = 0;
    // Attempts includes the initial search and every aspiration re-search at
    // this depth.
    std::uint32_t attempts = 0;

    [[nodiscard]] friend constexpr bool operator==(
      const CompletedIteration&,
      const CompletedIteration&) noexcept = default;
};

// A completion observer runs once after each exact iterative-deepening result.
// The node count is cumulative across all completed depths and aspiration
// attempts. The duration is measured from the beginning of the root search.
template<typename Observer>
concept CompletionObserver =
  std::invocable<
    Observer&,
    const CompletedIteration&,
    std::uint64_t,
    SearchDuration>;

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

// A failed search proves one boundary of the next window. Retaining that
// boundary and expanding only toward the unknown score avoids discarding the
// useful bound supplied by the completed attempt.
[[nodiscard]] constexpr AspirationWindow
widen_aspiration_window(
  AspirationWindow failed_window,
  Score bound,
  std::int64_t width) noexcept {
    assert(failed_window.alpha < failed_window.beta);
    assert(!failed_window.contains_exact(bound));
    assert(width > 0);
    assert(width < FULL_ASPIRATION_HALF_WIDTH);

    constexpr std::int64_t minimum =
      -std::int64_t{INFINITE_SCORE};
    constexpr std::int64_t maximum =
      std::int64_t{INFINITE_SCORE};

    if (bound <= failed_window.alpha) {
        const std::int64_t upper = failed_window.alpha;
        const std::int64_t lower = upper - width;
        return {
          static_cast<Score>(
            lower < minimum ? minimum : lower),
          failed_window.alpha,
        };
    }

    assert(bound >= failed_window.beta);
    const std::int64_t lower = failed_window.beta;
    const std::int64_t upper = lower + width;
    return {
      failed_window.beta,
      static_cast<Score>(
        upper > maximum ? maximum : upper),
    };
}

[[nodiscard]] constexpr bool valid_limits(
  const IterativeLimits& limits) noexcept {
    return limits.max_depth >= 1
        && limits.max_depth <= MAX_SEARCH_DEPTH
        && (!limits.time_limit
            || *limits.time_limit
                 >= SearchDuration::zero())
        && (!limits.soft_time_limit
            || *limits.soft_time_limit
                 >= SearchDuration::zero())
        && (!limits.time_limit
            || !limits.soft_time_limit
            || *limits.soft_time_limit
                 <= *limits.time_limit);
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
// The hard time limit is checked when a node is entered, so one active node
// can finish after that duration. The optional soft limit is checked between
// completed iterations.
//
// A zero node or time limit is valid and prevents the first node from being
// entered. When both limits stop the same node, the node limit has precedence.
//
// Preconditions:
// - max_depth is in the inclusive range 1..MAX_SEARCH_DEPTH;
// - both time limits are absent or non-negative;
// - soft_time_limit does not exceed time_limit when both are present;
// - history.current_key() equals position.key();
// - the position has a result-valid king layout.
namespace IterationDetail {

template<CompletionObserver Observer>
[[nodiscard]] inline IterativeResult iterative_search_impl(
  Position& position,
  const PositionHistory& history,
  const IterativeLimits& limits,
  TranspositionTable& table,
  Observer&& observer) {
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
      limits.external_stop,
      limits.time_control};
    // Every completed depth and aspiration attempt belongs to one root-search
    // generation, so safe scores can be shared throughout this call.
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
        state.reset_selective_depth();
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
              half_width
                  == IterationDetail::FULL_ASPIRATION_HALF_WIDTH
                ? IterationDetail::FULL_ASPIRATION_WINDOW
                : IterationDetail::widen_aspiration_window(
                    window,
                    iteration->score,
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
            state.selective_depth(),
            attempts,
          };
        result.total_nodes = state.nodes;

        const SearchClock::time_point completed_at =
          SearchClock::now();
        observer(
          *result.last_completed,
          result.total_nodes,
          completed_at - start);

        if (!completed.best_move.is_board_move()) {
            return finish(
              IterativeStop::TERMINAL_POSITION);
        }

        // A soft deadline never interrupts a node. It prevents a deeper
        // iteration only after the current exact root result is available.
        const SearchClock::time_point now =
          SearchClock::now();
        const bool static_soft_limit_reached =
          limits.soft_time_limit
          && now - start
               >= *limits.soft_time_limit;
        const bool activated_soft_limit_reached =
          limits.time_control
          && limits.time_control
               ->soft_limit_reached(now);
        if (depth < limits.max_depth
            && (static_soft_limit_reached
                || activated_soft_limit_reached)) {
            return finish(
              IterativeStop::TIME_LIMIT);
        }

        previous_best = completed.best_move;
        previous_score = completed.score;
    }

    return finish(IterativeStop::DEPTH_LIMIT);
}

}  // namespace IterationDetail

// Reuses the table across root searches. Each call advances one generation;
// current-generation scores are position-keyed, while stale scores require
// their stored repetition-history tag.
[[nodiscard]] inline IterativeResult iterative_search(
  Position& position,
  const PositionHistory& history,
  const IterativeLimits& limits,
  TranspositionTable& table) {
    const auto ignore_completion =
      [](const CompletedIteration&,
         std::uint64_t,
         SearchDuration) noexcept {};
    return IterationDetail::iterative_search_impl(
      position,
      history,
      limits,
      table,
      ignore_completion);
}

// Reports each completed depth without placing protocol concerns in the
// search core. The observer is called on the searching thread.
template<CompletionObserver Observer>
[[nodiscard]] inline IterativeResult iterative_search(
  Position& position,
  const PositionHistory& history,
  const IterativeLimits& limits,
  TranspositionTable& table,
  Observer&& observer) {
    return IterationDetail::iterative_search_impl(
      position,
      history,
      limits,
      table,
      std::forward<Observer>(observer));
}

// Uses a new default-sized transposition table for this call.
[[nodiscard]] inline IterativeResult iterative_search(
  Position& position,
  const PositionHistory& history,
  const IterativeLimits& limits) {
    TranspositionTable table;
    return iterative_search(
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
      .soft_time_limit = std::nullopt,
    }));
static_assert(
  !IterationDetail::valid_limits(
    IterativeLimits{
      .max_depth = 1,
      .node_limit = std::nullopt,
      .time_limit = SearchDuration{1},
      .soft_time_limit = SearchDuration{2},
    }));
static_assert(
  !IterativeResult{}.has_completed_iteration());
static_assert(!IterativeResult{}.has_move());

}  // namespace Mockingbird
