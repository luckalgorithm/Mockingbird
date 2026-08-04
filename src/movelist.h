#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <vector>

#include "move.h"

namespace Mockingbird {

// This bound applies when one color's material derives from one king, one
// queen, two rooks, two bishops, two knights, and eight pawns. Treating every
// pawn as a promoted queen and using empty-board attack maxima gives
// 9*45 + 2*26 + 2*19 + 2*8 + 8 + 2 = 521 moves.
inline constexpr std::size_t STANDARD_INVENTORY_MOVE_UPPER_BOUND = 521;

// A queen has the largest empty-board destination count of any piece. This
// geometric bound also covers promotion multiplicity, en passant, and the two
// additional castling moves for an arbitrary 160-square inventory.
inline constexpr std::size_t ABSOLUTE_MOVE_UPPER_BOUND =
  static_cast<std::size_t>(PLAYABLE_SQUARE_NB) * 45 + 2;

// MoveList keeps common search nodes inline while limiting every recursive
// stack frame. High-mobility and arbitrary constructed positions spill into
// contiguous dynamic storage rather than writing past the inline array.
class MoveList {
  public:
    using value_type = Move;
    using iterator = Move*;
    using const_iterator = const Move*;

    static constexpr std::size_t INLINE_CAPACITY = 96;
    static constexpr std::size_t CAPACITY = 7216;

    constexpr MoveList() noexcept = default;

    [[nodiscard]] constexpr bool empty() const noexcept {
        return size() == 0;
    }

    [[nodiscard]] constexpr bool full() const noexcept {
        return size() == CAPACITY;
    }

    [[nodiscard]] constexpr std::size_t size() const noexcept {
        return spilled_
          ? overflow_.size()
          : inline_size_;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept {
        return CAPACITY;
    }

    // Precondition: move is a board move. The absolute geometric capacity is
    // never reached by a valid board position.
    constexpr void push_back(Move move) {
        assert(is_ok(move));
        assert(!full());
        if (full())
            return;

        if (!spilled_
            && inline_size_ < INLINE_CAPACITY) {
            inline_moves_[inline_size_++] = move;
            return;
        }

        if (!spilled_) {
            overflow_.clear();
            overflow_.reserve(INLINE_CAPACITY * 2);
            overflow_.insert(
              overflow_.end(),
              inline_moves_.begin(),
              inline_moves_.begin()
                + static_cast<std::ptrdiff_t>(
                    inline_size_));
            overflow_.push_back(move);
            spilled_ = true;
            return;
        }

        overflow_.push_back(move);
    }

    constexpr void clear() noexcept {
        overflow_.clear();
        inline_size_ = 0;
        spilled_ = false;
    }

    // Removes every move at or after new_size while preserving the remaining
    // move order and the current storage representation.
    // Precondition: new_size is not greater than size().
    constexpr void truncate(std::size_t new_size) noexcept {
        assert(new_size <= size());

        if (spilled_) {
            overflow_.resize(new_size);
            return;
        }

        inline_size_ = new_size;
    }

    // Precondition: index is less than size().
    [[nodiscard]] constexpr Move& operator[](std::size_t index) noexcept {
        assert(index < size());
        return spilled_
          ? overflow_[index]
          : inline_moves_[index];
    }

    // Precondition: index is less than size().
    [[nodiscard]] constexpr const Move& operator[](std::size_t index) const noexcept {
        assert(index < size());
        return spilled_
          ? overflow_[index]
          : inline_moves_[index];
    }

    [[nodiscard]] constexpr iterator begin() noexcept {
        return spilled_
          ? overflow_.data()
          : inline_moves_.data();
    }

    [[nodiscard]] constexpr const_iterator begin() const noexcept {
        return spilled_
          ? overflow_.data()
          : inline_moves_.data();
    }

    [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
        return begin();
    }

    [[nodiscard]] constexpr iterator end() noexcept {
        return begin()
             + static_cast<std::ptrdiff_t>(size());
    }

    [[nodiscard]] constexpr const_iterator end() const noexcept {
        return begin()
             + static_cast<std::ptrdiff_t>(size());
    }

    [[nodiscard]] constexpr const_iterator cend() const noexcept {
        return end();
    }

  private:
    std::array<Move, INLINE_CAPACITY> inline_moves_{};
    std::vector<Move> overflow_;
    std::size_t inline_size_ = 0;
    bool spilled_ = false;
};

static_assert(MoveList::INLINE_CAPACITY == 96);
static_assert(MoveList::capacity() == 7216);
static_assert(
  MoveList::INLINE_CAPACITY
  < STANDARD_INVENTORY_MOVE_UPPER_BOUND);
static_assert(
  MoveList::capacity()
  >= ABSOLUTE_MOVE_UPPER_BOUND);
static_assert(MoveList{}.empty());

}  // namespace Mockingbird
