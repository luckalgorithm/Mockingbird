#pragma once

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>

namespace Mockingbird {

using SearchClock = std::chrono::steady_clock;
using SearchDuration = SearchClock::duration;

enum class SearchStopReason : std::uint8_t {
    NODE_LIMIT,
    TIME_LIMIT,
    EXTERNAL_STOP,
};

namespace SearchDetail {

using NodeEntry =
  std::expected<void, SearchStopReason>;

// Returns an absolute steady-clock deadline. Durations that extend beyond the
// clock's maximum time point produce time_point::max().
[[nodiscard]] constexpr SearchClock::time_point
make_deadline(
  SearchClock::time_point start,
  SearchDuration duration) noexcept {
    assert(duration >= SearchDuration::zero());

    if (start > SearchClock::time_point{}) {
        const SearchDuration remaining =
          SearchClock::time_point::max() - start;
        if (duration >= remaining)
            return SearchClock::time_point::max();
    }

    return start + duration;
}

class UnlimitedBudget {
  public:
    // The node counter cannot represent another entered node at UINT64_MAX.
    [[nodiscard]] constexpr NodeEntry enter_node(
      std::uint64_t& nodes) noexcept {
        if (nodes
            == std::numeric_limits<std::uint64_t>::max()) {
            return std::unexpected(
              SearchStopReason::NODE_LIMIT);
        }

        ++nodes;
        return {};
    }
};

// SearchBudget applies cumulative limits at node entry. An external stop is
// checked first. The node limit precedes the deadline when both are reached.
class SearchBudget {
  public:
    constexpr SearchBudget() noexcept = default;

    constexpr SearchBudget(
      std::optional<std::uint64_t> node_limit,
      std::optional<SearchClock::time_point> deadline,
      const std::atomic_bool* external_stop = nullptr) noexcept
        : node_limit_(node_limit),
          deadline_(deadline),
          external_stop_(external_stop) {}

    [[nodiscard]] NodeEntry enter_node(
      std::uint64_t& nodes) noexcept {
        if (stop_reason_)
            return std::unexpected(*stop_reason_);

        if (external_stop_
            && external_stop_->load(
                 std::memory_order_relaxed)) {
            stop_reason_ =
              SearchStopReason::EXTERNAL_STOP;
            return std::unexpected(*stop_reason_);
        }

        if (nodes
              == std::numeric_limits<std::uint64_t>::max()
            || (node_limit_
                && nodes >= *node_limit_)) {
            stop_reason_ =
              SearchStopReason::NODE_LIMIT;
            return std::unexpected(*stop_reason_);
        }

        if (deadline_
            && SearchClock::now() >= *deadline_) {
            stop_reason_ =
              SearchStopReason::TIME_LIMIT;
            return std::unexpected(*stop_reason_);
        }

        ++nodes;
        return {};
    }

    [[nodiscard]] constexpr
    std::optional<SearchStopReason>
    stop_reason() const noexcept {
        return stop_reason_;
    }

  private:
    std::optional<std::uint64_t> node_limit_;
    std::optional<SearchClock::time_point> deadline_;
    const std::atomic_bool* external_stop_ = nullptr;
    std::optional<SearchStopReason> stop_reason_;
};

static_assert(SearchClock::is_steady);
static_assert(
  make_deadline(
    SearchClock::time_point{
      SearchDuration{1}},
    SearchDuration::max())
  == SearchClock::time_point::max());

}  // namespace SearchDetail

}  // namespace Mockingbird
