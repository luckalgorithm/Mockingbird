#pragma once

#include <atomic>
#include <bit>
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

// Asynchronous controls and clock queries are sampled at node entry. A state
// change published between entries can admit at most
// CONTROL_CHECK_INTERVAL - 1 additional nodes before it is observed.
inline constexpr std::uint64_t CONTROL_CHECK_INTERVAL = 256;
inline constexpr std::uint64_t CONTROL_CHECK_MASK =
  CONTROL_CHECK_INTERVAL - 1;
static_assert(std::has_single_bit(CONTROL_CHECK_INTERVAL));

// Retained for callers that describe only deadline sampling.
inline constexpr std::uint64_t TIME_CHECK_INTERVAL =
  CONTROL_CHECK_INTERVAL;
inline constexpr std::uint64_t TIME_CHECK_MASK =
  CONTROL_CHECK_MASK;

using NodeEntry =
  std::expected<void, SearchStopReason>;

class SearchBudget;

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

// SearchTimeControl publishes hard and soft deadlines exactly once. Until
// activation both limits are dormant. Deadline counts are written before the
// active state is released to search threads.
class SearchTimeControl {
  public:
    SearchTimeControl(
      std::optional<SearchDuration> hard_limit,
      std::optional<SearchDuration> soft_limit) noexcept
        : hard_limit_(hard_limit),
          soft_limit_(soft_limit) {
        assert(
          !hard_limit_
          || *hard_limit_ >= SearchDuration::zero());
        assert(
          !soft_limit_
          || *soft_limit_ >= SearchDuration::zero());
        assert(
          !hard_limit_
          || !soft_limit_
          || *soft_limit_ <= *hard_limit_);
    }

    SearchTimeControl(const SearchTimeControl&) = delete;
    SearchTimeControl& operator=(const SearchTimeControl&) = delete;
    SearchTimeControl(SearchTimeControl&&) = delete;
    SearchTimeControl& operator=(SearchTimeControl&&) = delete;

    // Returns true only for the thread that publishes the deadlines. Later
    // activation attempts leave the original activation time unchanged.
    [[nodiscard]] bool activate(
      SearchClock::time_point start =
        SearchClock::now()) noexcept {
        std::uint8_t expected = INACTIVE;
        if (!state_.compare_exchange_strong(
              expected,
              INITIALIZING,
              std::memory_order_acq_rel,
              std::memory_order_acquire)) {
            return false;
        }

        if (hard_limit_) {
            hard_deadline_.store(
              make_deadline(start, *hard_limit_)
                .time_since_epoch()
                .count(),
              std::memory_order_relaxed);
        }
        if (soft_limit_) {
            soft_deadline_.store(
              make_deadline(start, *soft_limit_)
                .time_since_epoch()
                .count(),
              std::memory_order_relaxed);
        }

        state_.store(
          ACTIVE, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool active() const noexcept {
        return state_.load(
                 std::memory_order_acquire)
            == ACTIVE;
    }

    [[nodiscard]] bool hard_limit_reached(
      SearchClock::time_point now) const noexcept {
        return hard_limit_
            && active()
            && active_hard_limit_reached(now);
    }

    [[nodiscard]] bool soft_limit_reached(
      SearchClock::time_point now) const noexcept {
        return soft_limit_
            && active()
            && now.time_since_epoch().count()
                 >= soft_deadline_.load(
                      std::memory_order_relaxed);
    }

  private:
    friend class SearchBudget;

    using ClockRep = SearchDuration::rep;
    static constexpr std::uint8_t INACTIVE = 0;
    static constexpr std::uint8_t INITIALIZING = 1;
    static constexpr std::uint8_t ACTIVE = 2;

    [[nodiscard]] constexpr bool
    has_hard_limit() const noexcept {
        return hard_limit_.has_value();
    }

    // The active-state acquire that precedes this call makes the published
    // deadline visible to the calling search thread.
    [[nodiscard]] bool active_hard_limit_reached(
      SearchClock::time_point now) const noexcept {
        return hard_limit_
            && now.time_since_epoch().count()
                 >= hard_deadline_.load(
                      std::memory_order_relaxed);
    }

    std::optional<SearchDuration> hard_limit_;
    std::optional<SearchDuration> soft_limit_;
    std::atomic<ClockRep> hard_deadline_{0};
    std::atomic<ClockRep> soft_deadline_{0};
    std::atomic<std::uint8_t> state_{INACTIVE};
};

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

// SearchBudget applies cumulative limits at node entry. Asynchronous controls
// are checked on the first entry and at fixed node intervals. The node limit
// remains exact between those polls and precedes a deadline reached on the
// same entry. At a polling entry, an external stop is checked first.
class SearchBudget {
  public:
    constexpr SearchBudget() noexcept = default;

    constexpr SearchBudget(
      std::optional<std::uint64_t> node_limit,
      std::optional<SearchClock::time_point> deadline,
      const std::atomic_bool* external_stop = nullptr,
      const SearchTimeControl* time_control = nullptr) noexcept
        : node_limit_(node_limit),
          deadline_(deadline),
          external_stop_(external_stop),
          time_control_(time_control) {}

    [[nodiscard]] NodeEntry enter_node(
      std::uint64_t& nodes) noexcept {
        if (stop_reason_)
            return std::unexpected(*stop_reason_);

        const bool poll_controls =
          first_entry_
          || (nodes & CONTROL_CHECK_MASK) == 0;
        first_entry_ = false;

        if (poll_controls
            && external_stop_
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

        if (poll_controls) {
            if (time_control_
                && time_control_->has_hard_limit()
                && !time_control_was_active_) {
                time_control_was_active_ =
                  time_control_->active();
            }

            if (deadline_ || time_control_was_active_) {
                const SearchClock::time_point now =
                  SearchClock::now();
                const bool shared_deadline_reached =
                  time_control_was_active_
                  && time_control_->active_hard_limit_reached(
                       now);
                if ((deadline_ && now >= *deadline_)
                    || shared_deadline_reached) {
                    stop_reason_ =
                      SearchStopReason::TIME_LIMIT;
                    return std::unexpected(*stop_reason_);
                }
            }
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
    const SearchTimeControl* time_control_ = nullptr;
    bool first_entry_ = true;
    bool time_control_was_active_ = false;
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
