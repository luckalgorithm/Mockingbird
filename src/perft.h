#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "legal.h"
#include "transition.h"

namespace Mockingbird {

// PerftEntry associates one legal root move with its descendant leaf count.
struct PerftEntry {
    Move move = Move::none();
    std::uint64_t nodes = 0;

    [[nodiscard]] friend constexpr bool operator==(
      const PerftEntry&, const PerftEntry&) noexcept = default;
};

// PerftList stores root-move counts inline and performs no dynamic allocation.
class PerftList {
  public:
    using value_type = PerftEntry;
    using iterator = PerftEntry*;
    using const_iterator = const PerftEntry*;

    static constexpr std::size_t CAPACITY = MoveList::CAPACITY;

    constexpr PerftList() noexcept = default;

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

    // Preconditions: entry.move is a board move and the list is not full.
    constexpr void push_back(PerftEntry entry) noexcept {
        assert(is_ok(entry.move));
        assert(!full());
        entries_[size_++] = entry;
    }

    constexpr void clear() noexcept {
        size_ = 0;
    }

    // Precondition: index is less than size().
    [[nodiscard]] constexpr PerftEntry& operator[](
      std::size_t index) noexcept {
        assert(index < size_);
        return entries_[index];
    }

    // Precondition: index is less than size().
    [[nodiscard]] constexpr const PerftEntry& operator[](
      std::size_t index) const noexcept {
        assert(index < size_);
        return entries_[index];
    }

    [[nodiscard]] constexpr iterator begin() noexcept {
        return entries_.data();
    }

    [[nodiscard]] constexpr const_iterator begin() const noexcept {
        return entries_.data();
    }

    [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
        return entries_.data();
    }

    [[nodiscard]] constexpr iterator end() noexcept {
        return entries_.data() + size_;
    }

    [[nodiscard]] constexpr const_iterator end() const noexcept {
        return entries_.data() + size_;
    }

    [[nodiscard]] constexpr const_iterator cend() const noexcept {
        return entries_.data() + size_;
    }

  private:
    std::array<PerftEntry, CAPACITY> entries_{};
    std::size_t size_ = 0;
};

// Counts leaf nodes at exactly depth plies and restores position before
// returning.
// Precondition: depth is nonnegative.
[[nodiscard]] constexpr std::uint64_t perft(
  Position& position, int depth) noexcept {
    assert(depth >= 0);

    if (depth <= 0)
        return 1;

    MoveList moves;
    generate_legal_moves(position, moves);

    std::uint64_t nodes = 0;
    for (const Move move : moves) {
        UndoState undo;
        do_move(position, move, undo);
        nodes += perft(position, depth - 1);
        undo_move(position, move, undo);
    }

    return nodes;
}

// Returns one descendant count for each legal root move in generation order.
// The position is restored after every root move.
// At depth zero, there is no root move and the returned list is empty.
// Precondition: depth is nonnegative.
[[nodiscard]] constexpr PerftList perft_divide(
  Position& position, int depth) noexcept {
    assert(depth >= 0);

    if (depth <= 0)
        return {};

    MoveList moves;
    generate_legal_moves(position, moves);

    PerftList result;
    for (const Move move : moves) {
        UndoState undo;
        do_move(position, move, undo);
        const std::uint64_t nodes = perft(position, depth - 1);
        undo_move(position, move, undo);
        result.push_back({move, nodes});
    }

    return result;
}

static_assert(PerftList::capacity() == MoveList::capacity());
static_assert(PerftList{}.empty());

}  // namespace Mockingbird
