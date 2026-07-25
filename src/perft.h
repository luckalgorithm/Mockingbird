#pragma once

#include <cassert>
#include <cstdint>

#include "legal.h"
#include "transition.h"

namespace Mockingbird {

// Counts leaf nodes at exactly depth plies and restores position before
// returning.
// Precondition: depth is nonnegative.
[[nodiscard]] constexpr std::uint64_t perft(
  Position& position, int depth) noexcept {
    assert(depth >= 0);

    if (depth == 0)
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

}  // namespace Mockingbird
