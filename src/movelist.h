#pragma once

#include <array>
#include <cassert>
#include <cstddef>

#include "move.h"

namespace Mockingbird {

// MoveList stores moves inline and performs no dynamic allocation.
class MoveList {
  public:
    using value_type = Move;
    using iterator = Move*;
    using const_iterator = const Move*;

    static constexpr std::size_t CAPACITY = 256;

    constexpr MoveList() noexcept = default;

    [[nodiscard]] constexpr bool empty() const noexcept {
        return size_ == 0;
    }

    [[nodiscard]] constexpr bool full() const noexcept {
        return size_ == CAPACITY;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return size_;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return CAPACITY;
    }

    // Preconditions: move is a board move and the list is not full.
    constexpr void push_back(Move move) noexcept {
        assert(is_ok(move));
        assert(!full());
        moves_[size_++] = move;
    }

    constexpr void clear() noexcept {
        size_ = 0;
    }

    // Precondition: index is less than size().
    [[nodiscard]] constexpr Move& operator[](std::size_t index) noexcept {
        assert(index < size_);
        return moves_[index];
    }

    // Precondition: index is less than size().
    [[nodiscard]] constexpr const Move& operator[](std::size_t index) const noexcept {
        assert(index < size_);
        return moves_[index];
    }

    [[nodiscard]] constexpr iterator begin() noexcept {
        return moves_.data();
    }

    [[nodiscard]] constexpr const_iterator begin() const noexcept {
        return moves_.data();
    }

    [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
        return moves_.data();
    }

    [[nodiscard]] constexpr iterator end() noexcept {
        return moves_.data() + size_;
    }

    [[nodiscard]] constexpr const_iterator end() const noexcept {
        return moves_.data() + size_;
    }

    [[nodiscard]] constexpr const_iterator cend() const noexcept {
        return moves_.data() + size_;
    }

  private:
    std::array<Move, CAPACITY> moves_{};
    std::size_t size_ = 0;
};

static_assert(MoveList::capacity() == 256);
static_assert(MoveList{}.empty());

}  // namespace Mockingbird
