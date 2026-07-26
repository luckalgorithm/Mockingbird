#pragma once

#include <cassert>
#include <cstddef>
#include <vector>

#include "zobrist.h"

namespace Mockingbird {

// PositionHistory stores one key for every visited position, including the
// initial position. Repetition queries report key occurrences and do not
// determine a game result.
class PositionHistory {
  public:
    static constexpr std::size_t INITIAL_RESERVE = 1024;

    constexpr explicit PositionHistory(
      PositionKey initial_key) {
        keys_.reserve(INITIAL_RESERVE);
        keys_.push_back(initial_key);
    }

    constexpr PositionHistory(
      const PositionHistory& other) {
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

    // Discards the recorded line and establishes a new initial position.
    constexpr void reset(PositionKey initial_key) {
        if (keys_.capacity() < INITIAL_RESERVE)
            keys_.reserve(INITIAL_RESERVE);

        keys_.clear();
        keys_.push_back(initial_key);
    }

    constexpr void push(PositionKey key) {
        assert(!keys_.empty());
        keys_.push_back(key);
    }

    // Preconditions: the history contains a child of the initial position,
    // and expected_current is the key being removed.
    constexpr void pop(
      PositionKey expected_current) noexcept {
        assert(keys_.size() > 1);
        assert(keys_.back() == expected_current);
        static_cast<void>(expected_current);
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
};

}  // namespace Mockingbird
