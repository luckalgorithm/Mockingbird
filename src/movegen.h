#pragma once

#include <array>

#include "attacks.h"
#include "movelist.h"
#include "pawns.h"
#include "position.h"

namespace Mockingbird {

namespace Detail {

inline constexpr std::array<PieceType, 4> PROMOTION_PIECES = {
  QUEEN,
  ROOK,
  BISHOP,
  KNIGHT,
};

constexpr void append_pawn_move(
  Color color, Square from, Square to, MoveList& moves) noexcept {
    if (!is_pawn_promotion_square(color, to)) {
        moves.push_back(Move::normal(from, to));
        return;
    }

    for (const PieceType promotion : PROMOTION_PIECES)
        moves.push_back(Move::promotion(from, to, promotion));
}

template<PieceType Piece>
[[nodiscard]] constexpr Bitboard sliding_attacks(
  Square square, const Bitboard& occupied) noexcept {
    static_assert(Piece == BISHOP || Piece == ROOK || Piece == QUEEN);

    if constexpr (Piece == BISHOP)
        return bishop_attacks(square, occupied);
    else if constexpr (Piece == ROOK)
        return rook_attacks(square, occupied);
    else
        return queen_attacks(square, occupied);
}

template<PieceType Piece>
constexpr void generate_sliding_piece_moves(
  const Position& position, MoveList& moves) noexcept {
    static_assert(Piece == BISHOP || Piece == ROOK || Piece == QUEEN);

    const Color us = position.side_to_move();
    const Bitboard friendly = position.pieces(team_of(us));
    const Bitboard occupied = position.occupied();
    Bitboard pieces = position.pieces(us, Piece);

    while (pieces) {
        const Square from = pieces.pop_lsb();
        Bitboard destinations =
          sliding_attacks<Piece>(from, occupied) & ~friendly;

        while (destinations)
            moves.push_back(Move::normal(from, destinations.pop_lsb()));
    }
}

}  // namespace Detail

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

// Appends pseudo-legal king moves for the side to move. Squares occupied by
// either member of that side's team are excluded. Attacked-square constraints
// and castling are not evaluated.
// Precondition: moves has enough remaining capacity for the generated moves.
constexpr void generate_king_moves(
  const Position& position, MoveList& moves) noexcept {
    const Color us = position.side_to_move();
    const Bitboard friendly = position.pieces(team_of(us));
    Bitboard kings = position.pieces(us, KING);

    while (kings) {
        const Square from = kings.pop_lsb();
        Bitboard destinations = king_attacks(from) & ~friendly;

        while (destinations)
            moves.push_back(Move::normal(from, destinations.pop_lsb()));
    }
}

// Appends pseudo-legal pawn pushes, captures, and promotions for the side to
// move. Double pushes require both traversed squares to be empty. Captures are
// limited to pieces on the opposing team. En passant, check, and pin constraints
// are not evaluated.
// Precondition: moves has enough remaining capacity for the generated moves.
constexpr void generate_pawn_moves(
  const Position& position, MoveList& moves) noexcept {
    const Color us = position.side_to_move();
    const Bitboard occupied = position.occupied();
    const Bitboard enemies = occupied & ~position.pieces(team_of(us));
    Bitboard pawns = position.pieces(us, PAWN);

    while (pawns) {
        const Square from = pawns.pop_lsb();
        const Square single = pawn_push_destination(us, from);

        if (single != SQ_NONE && !occupied.test(single)) {
            Detail::append_pawn_move(us, from, single, moves);

            const Square double_push =
              pawn_double_push_destination(us, from);
            if (double_push != SQ_NONE && !occupied.test(double_push))
                moves.push_back(Move::normal(from, double_push));
        }

        Bitboard captures = pawn_attacks(us, from) & enemies;
        while (captures)
            Detail::append_pawn_move(
              us, from, captures.pop_lsb(), moves);
    }
}

// Appends pseudo-legal bishop moves for the side to move. Occupied squares stop
// each ray, and destinations occupied by either member of the moving side's
// team are excluded. Check and pin constraints are not evaluated.
// Precondition: moves has enough remaining capacity for the generated moves.
constexpr void generate_bishop_moves(
  const Position& position, MoveList& moves) noexcept {
    Detail::generate_sliding_piece_moves<BISHOP>(position, moves);
}

// Appends pseudo-legal rook moves for the side to move. Occupied squares stop
// each ray, and destinations occupied by either member of the moving side's
// team are excluded. Check and pin constraints are not evaluated.
// Precondition: moves has enough remaining capacity for the generated moves.
constexpr void generate_rook_moves(
  const Position& position, MoveList& moves) noexcept {
    Detail::generate_sliding_piece_moves<ROOK>(position, moves);
}

// Appends pseudo-legal queen moves for the side to move. Occupied squares stop
// each ray, and destinations occupied by either member of the moving side's
// team are excluded. Check and pin constraints are not evaluated.
// Precondition: moves has enough remaining capacity for the generated moves.
constexpr void generate_queen_moves(
  const Position& position, MoveList& moves) noexcept {
    Detail::generate_sliding_piece_moves<QUEEN>(position, moves);
}

// Appends bishop, rook, and queen moves in that order.
// Precondition: moves has enough remaining capacity for the generated moves.
constexpr void generate_sliding_moves(
  const Position& position, MoveList& moves) noexcept {
    generate_bishop_moves(position, moves);
    generate_rook_moves(position, moves);
    generate_queen_moves(position, moves);
}

// Appends knight, bishop, rook, queen, and king moves in that order.
// Pawn moves and castling are not generated.
// Precondition: moves has enough remaining capacity for the generated moves.
constexpr void generate_nonpawn_moves(
  const Position& position, MoveList& moves) noexcept {
    generate_knight_moves(position, moves);
    generate_sliding_moves(position, moves);
    generate_king_moves(position, moves);
}

}  // namespace Mockingbird
