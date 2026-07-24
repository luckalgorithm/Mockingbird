#pragma once

#include "attacks.h"
#include "movelist.h"
#include "position.h"

namespace Mockingbird {

// Appends pseudo-legal knight moves for the side to move. Squares occupied by
// either member of that side's team are excluded. Check and pin constraints are
// not evaluated.
// Precondition: moves has enough remaining capacity for the generated moves.
constexpr void generate_knight_moves(
  const Position& position, MoveList& moves) noexcept {
    const Color us = position.side_to_move();
    const Bitboard friendly = position.pieces(team_of(us));
    Bitboard knights = position.pieces(us, KNIGHT);

    while (knights) {
        const Square from = knights.pop_lsb();
        Bitboard destinations = knight_attacks(from) & ~friendly;

        while (destinations)
            moves.push_back(Move::normal(from, destinations.pop_lsb()));
    }
}

}  // namespace Mockingbird
