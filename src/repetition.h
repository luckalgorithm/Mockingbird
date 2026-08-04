#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "zobrist.h"

namespace Mockingbird {

// HistoryContext fingerprints the multiset of position keys in the active
// reversible segment. The length provides an exact check on its size.
struct HistoryContext {
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    std::size_t length = 0;

    [[nodiscard]] friend constexpr bool operator==(
      const HistoryContext&,
      const HistoryContext&) noexcept = default;
};

namespace RepetitionDetail {

[[nodiscard]] constexpr std::uint64_t mix(
  std::uint64_t value) noexcept {
    value = (value ^ (value >> 30))
          * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27))
          * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

[[nodiscard]] constexpr std::uint64_t first_component(
  PositionKey key) noexcept {
    return mix(key ^ 0x243F6A8885A308D3ULL);
}

[[nodiscard]] constexpr std::uint64_t second_component(
  PositionKey key) noexcept {
    return mix(key ^ 0x13198A2E03707344ULL);
}

[[nodiscard]] constexpr HistoryContext make_context(
  PositionKey key) noexcept {
    return {
      first_component(key),
      second_component(key),
      1,
    };
}

constexpr void add(
  HistoryContext& context,
  PositionKey key) noexcept {
    context.first += first_component(key);
    context.second += second_component(key);
    ++context.length;
}

constexpr void remove(
  HistoryContext& context,
  PositionKey key) noexcept {
    assert(context.length > 1);
    context.first -= first_component(key);
    context.second -= second_component(key);
    --context.length;
}

}  // namespace RepetitionDetail

// PositionHistory stores one key for every visited position, including the
// initial position. Repetition queries report occurrences since the latest
// irreversible boundary and do not determine a game result.
class PositionHistory {
  public:
    static constexpr std::size_t INITIAL_RESERVE = 1024;

    constexpr explicit PositionHistory(
      PositionKey initial_key)
        : context_(
            RepetitionDetail::make_context(
              initial_key)) {
        keys_.reserve(INITIAL_RESERVE);
        occurrence_counts_.reserve(INITIAL_RESERVE);
        keys_.push_back(initial_key);
        occurrence_counts_.push_back(1);
    }

    constexpr PositionHistory(
      const PositionHistory& other)
        : boundary_frames_(
            other.boundary_frames_),
          repeated_position_count_(
            other.repeated_position_count_),
          segment_start_(other.segment_start_),
          context_(other.context_) {
        assert(!other.keys_.empty());

        const std::size_t reserve_size =
          other.capacity() < INITIAL_RESERVE
              ? INITIAL_RESERVE
              : other.capacity();
        keys_.reserve(reserve_size);
        occurrence_counts_.reserve(reserve_size);
        keys_.insert(
          keys_.end(),
          other.keys_.begin(),
          other.keys_.end());
        occurrence_counts_.insert(
          occurrence_counts_.end(),
          other.occurrence_counts_.begin(),
          other.occurrence_counts_.end());
    }

    constexpr PositionHistory& operator=(
      const PositionHistory& other) {
        assert(!other.keys_.empty());

        if (this == &other)
            return *this;

        PositionHistory replacement{other};
        keys_.swap(replacement.keys_);
        occurrence_counts_.swap(
          replacement.occurrence_counts_);
        boundary_frames_.swap(
          replacement.boundary_frames_);
        std::swap(
          repeated_position_count_,
          replacement.repeated_position_count_);
        std::swap(
          segment_start_,
          replacement.segment_start_);
        std::swap(context_, replacement.context_);
        return *this;
    }

    // A moved-from source supports destruction, reset, and assignment as a
    // destination. Reset or assignment establishes a new active history.
    constexpr PositionHistory(PositionHistory&&) noexcept = default;
    constexpr PositionHistory& operator=(
      PositionHistory&&) noexcept = default;

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return keys_.size();
    }

    [[nodiscard]] constexpr std::size_t capacity() const noexcept {
        return keys_.capacity();
    }

    [[nodiscard]] constexpr PositionKey current_key() const noexcept {
        assert(!keys_.empty());
        return keys_.back();
    }

    [[nodiscard]] constexpr const HistoryContext&
    context() const noexcept {
        assert(!keys_.empty());
        return context_;
    }

    // Discards the recorded line and establishes a new initial position.
    constexpr void reset(PositionKey initial_key) {
        reserve_entries(INITIAL_RESERVE);

        keys_.clear();
        occurrence_counts_.clear();
        keys_.push_back(initial_key);
        occurrence_counts_.push_back(1);
        boundary_frames_.clear();

        repeated_position_count_ = 0;
        segment_start_ = 0;
        context_ =
          RepetitionDetail::make_context(initial_key);
    }

    constexpr void push(PositionKey key) {
        push_impl(key, false);
    }

    // Starts a repetition segment at key. Positions before the boundary stay
    // on the traversal stack for exact pop restoration but do not participate
    // in repetition counts or the transposition history context.
    constexpr void push_irreversible(PositionKey key) {
        push_impl(key, true);
    }

    // Preconditions: the history contains a child of the initial position,
    // and expected_current is the key being removed.
    constexpr void pop(
      PositionKey expected_current) noexcept {
        assert(!keys_.empty());
        assert(keys_.size() > 1);
        assert(keys_.back() == expected_current);
        static_cast<void>(expected_current);

        const bool removes_boundary =
          !boundary_frames_.empty()
          && boundary_frames_.back().key_index
               == keys_.size() - 1;
        const std::size_t occurrence_count =
          occurrence_counts_.back();
        keys_.pop_back();
        occurrence_counts_.pop_back();

        if (removes_boundary) {
            const BoundaryFrame frame =
              boundary_frames_.back();
            boundary_frames_.pop_back();
            context_ = frame.previous_context;
            repeated_position_count_ =
              frame.previous_repeated_position_count;
            segment_start_ =
              frame.previous_segment_start;
        } else {
            if (occurrence_count == 2) {
                assert(repeated_position_count_ > 0);
                --repeated_position_count_;
            }

            RepetitionDetail::remove(
              context_, expected_current);
        }
    }

    [[nodiscard]] constexpr std::size_t count(
      PositionKey key) const noexcept {
        assert(!keys_.empty());
        if (key == keys_.back())
            return occurrence_counts_.back();

        return active_count(key);
    }

    [[nodiscard]] constexpr std::size_t current_count() const noexcept {
        assert(!occurrence_counts_.empty());
        return occurrence_counts_.back();
    }

    [[nodiscard]] constexpr bool is_twofold() const noexcept {
        return current_count() >= 2;
    }

    [[nodiscard]] constexpr bool is_threefold() const noexcept {
        return current_count() >= 3;
    }

    // Reports whether any position key occurs more than once in the active
    // reversible segment, including repetitions before the current position.
    [[nodiscard]] constexpr bool has_repeated_position() const noexcept {
        assert(!keys_.empty());
        return repeated_position_count_ != 0;
    }

  private:
    struct BoundaryFrame {
        std::size_t key_index = 0;
        HistoryContext previous_context{};
        std::size_t previous_repeated_position_count = 0;
        std::size_t previous_segment_start = 0;
    };

    constexpr void reserve_entries(std::size_t capacity) {
        if (keys_.capacity() >= capacity
            && occurrence_counts_.capacity() >= capacity) {
            return;
        }

        std::size_t reserve_size =
          keys_.capacity() > occurrence_counts_.capacity()
            ? keys_.capacity()
            : occurrence_counts_.capacity();
        if (reserve_size < INITIAL_RESERVE)
            reserve_size = INITIAL_RESERVE;
        while (reserve_size < capacity)
            reserve_size *= 2;

        if (keys_.capacity() < reserve_size)
            keys_.reserve(reserve_size);
        if (occurrence_counts_.capacity() < reserve_size)
            occurrence_counts_.reserve(reserve_size);
    }

    [[nodiscard]] constexpr std::size_t active_count(
      PositionKey key) const noexcept {
        assert(segment_start_ < keys_.size());

        std::size_t occurrences = 0;
        for (std::size_t index = keys_.size();
             index > segment_start_;
             --index) {
            if (keys_[index - 1] == key)
                ++occurrences;
        }

        return occurrences;
    }

    constexpr void push_impl(
      PositionKey key,
      bool irreversible) {
        assert(!keys_.empty());

        const std::size_t occurrence_count =
          irreversible ? 1 : active_count(key) + 1;
        reserve_entries(keys_.size() + 1);

        if (irreversible) {
            boundary_frames_.push_back({
              keys_.size(),
              context_,
              repeated_position_count_,
              segment_start_,
            });
        }

        try {
            keys_.push_back(key);
            try {
                occurrence_counts_.push_back(
                  occurrence_count);
            } catch (...) {
                keys_.pop_back();
                throw;
            }
        } catch (...) {
            if (irreversible)
                boundary_frames_.pop_back();
            throw;
        }

        if (irreversible) {
            segment_start_ = keys_.size() - 1;
            repeated_position_count_ = 0;
            context_ =
              RepetitionDetail::make_context(key);
        } else {
            if (occurrence_count == 2)
                ++repeated_position_count_;

            RepetitionDetail::add(context_, key);
        }
    }

    std::vector<PositionKey> keys_;
    // Each value is the key's occurrence count at the corresponding point in
    // the active reversible segment.
    std::vector<std::size_t> occurrence_counts_;
    // Only active irreversible boundaries require restoration records.
    std::vector<BoundaryFrame> boundary_frames_;
    std::size_t repeated_position_count_ = 0;
    std::size_t segment_start_ = 0;
    HistoryContext context_;
};

static_assert(
  RepetitionDetail::first_component(0)
  != RepetitionDetail::second_component(0));

}  // namespace Mockingbird
