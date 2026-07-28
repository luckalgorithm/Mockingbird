#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "zobrist.h"

namespace Mockingbird {

// HistoryContext fingerprints the multiset of recorded position keys. The
// length provides an exact check on the number of recorded positions.
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
// initial position. Repetition queries report key occurrences and do not
// determine a game result.
class PositionHistory {
  public:
    static constexpr std::size_t INITIAL_RESERVE = 1024;

    constexpr explicit PositionHistory(
      PositionKey initial_key)
        : context_(
            RepetitionDetail::make_context(
              initial_key)) {
        keys_.reserve(INITIAL_RESERVE);
        keys_.push_back(initial_key);
    }

    constexpr PositionHistory(
      const PositionHistory& other)
        : context_(other.context_) {
        assert(!other.keys_.empty());

        const std::size_t reserve_size =
          other.capacity() < INITIAL_RESERVE
            ? INITIAL_RESERVE
            : other.capacity();
        keys_.reserve(reserve_size);
        keys_.insert(
          keys_.end(),
          other.keys_.begin(),
          other.keys_.end());
    }

    constexpr PositionHistory& operator=(
      const PositionHistory& other) {
        assert(!other.keys_.empty());

        if (this == &other)
            return *this;

        PositionHistory replacement{other};
        keys_.swap(replacement.keys_);
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
        if (keys_.capacity() < INITIAL_RESERVE)
            keys_.reserve(INITIAL_RESERVE);

        keys_.clear();
        keys_.push_back(initial_key);
        context_ =
          RepetitionDetail::make_context(initial_key);
    }

    constexpr void push(PositionKey key) {
        assert(!keys_.empty());
        keys_.push_back(key);
        RepetitionDetail::add(context_, key);
    }

    // Preconditions: the history contains a child of the initial position,
    // and expected_current is the key being removed.
    constexpr void pop(
      PositionKey expected_current) noexcept {
        assert(keys_.size() > 1);
        assert(keys_.back() == expected_current);
        static_cast<void>(expected_current);
        RepetitionDetail::remove(
          context_, keys_.back());
        keys_.pop_back();
    }

    [[nodiscard]] constexpr std::size_t count(
      PositionKey key) const noexcept {
        assert(!keys_.empty());

        std::size_t occurrences = 0;

        for (const PositionKey stored : keys_) {
            if (stored == key)
                ++occurrences;
        }

        return occurrences;
    }

    [[nodiscard]] constexpr std::size_t current_count() const noexcept {
        return count(current_key());
    }

    [[nodiscard]] constexpr bool is_twofold() const noexcept {
        return has_current_count(2);
    }

    [[nodiscard]] constexpr bool is_threefold() const noexcept {
        return has_current_count(3);
    }

  private:
    [[nodiscard]] constexpr bool has_current_count(
      std::size_t required) const noexcept {
        assert(required > 0);

        const PositionKey current = current_key();
        std::size_t occurrences = 0;

        for (auto entry = keys_.rbegin();
             entry != keys_.rend();
             ++entry) {
            if (*entry == current
                && ++occurrences >= required)
                return true;
        }

        return false;
    }

    std::vector<PositionKey> keys_;
    HistoryContext context_;
};

static_assert(
  RepetitionDetail::first_component(0)
  != RepetitionDetail::second_component(0));

}  // namespace Mockingbird
