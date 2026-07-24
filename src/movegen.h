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

constexpr void append_en_passant_move(
  Color color, Square from, Square to, MoveList& moves) noexcept {
    if (!is_pawn_promotion_square(color, to)) {
        moves.push_back(Move::en_passant(from, to));
        return;
    }

    for (const PieceType promotion : PROMOTION_PIECES)
        moves.push_back(Move::en_passant(from, to, promotion));
}

// Reversed pawn-capture offsets map a target to its possible source squares.
[[nodiscard]] constexpr const Bitboard& pawn_attack_sources(
  Color color, Square target) noexcept {
    return pawn_attacks(
      next_color(next_color(color)), target);
}

struct EnPassantTargets {
    // Both members contain the same validated targets.
    std::array<Square, COLOR_NB> by_owner{};
    Bitboard squares;
};

[[nodiscard]] constexpr EnPassantTargets eligible_en_passant_targets(
  const Position& position, Color moving_color) noexcept {
    EnPassantTargets targets;
    targets.by_owner.fill(SQ_NONE);

    const Bitboard moving_team = position.pieces(team_of(moving_color));

    for (int owner_index = 0; owner_index < COLOR_NB; ++owner_index) {
        const Color owner = Color(owner_index);
        if (team_of(owner) == team_of(moving_color))
            continue;

        const Square target = position.en_passant_square(owner);
        if (target == SQ_NONE || moving_team.test(target))
            continue;

        // The target lies between the pawn's starting and destination squares.
        const Square source = target - pawn_push(owner);
        if (!is_ok(source)
            || pawn_push_destination(owner, source) != target)
            continue;

        const Square victim =
          pawn_double_push_destination(owner, source);
        if (victim == SQ_NONE
            || position.piece_on(victim) != make_piece(owner, PAWN))
            continue;

        targets.by_owner[std::size_t(owner)] = target;
        targets.squares.set(target);
    }

    return targets;
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
// limited to pieces on the opposing team. En-passant targets are accepted only
// for opposing colors with the owning pawn on its double-push destination.
// An opposing piece on an en-passant target is captured together with the
// owning pawn. Check and pin constraints are not evaluated.
// Precondition: moves has enough remaining capacity for the generated moves.
constexpr void generate_pawn_moves(
  const Position& position, MoveList& moves) noexcept {
    const Color us = position.side_to_move();
    const Bitboard occupied = position.occupied();
    const Bitboard enemies = occupied & ~position.pieces(team_of(us));
    const Bitboard us_pawns = position.pieces(us, PAWN);
    const Detail::EnPassantTargets en_passant =
      Detail::eligible_en_passant_targets(position, us);
    Bitboard pawns = us_pawns;

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

        // Enemy-occupied en-passant targets are generated only by the loop below.
        Bitboard captures =
          pawn_attacks(us, from) & enemies & ~en_passant.squares;
        while (captures)
            Detail::append_pawn_move(
              us, from, captures.pop_lsb(), moves);
    }

    // En-passant moves follow quiet moves and ordinary captures.
    for (int owner_index = 0; owner_index < COLOR_NB; ++owner_index) {
        const Square target =
          en_passant.by_owner[std::size_t(owner_index)];
        if (target == SQ_NONE)
            continue;

        Bitboard capturers =
          Detail::pawn_attack_sources(us, target) & us_pawns;
        while (capturers) {
            const Square from = capturers.pop_lsb();
            Detail::append_en_passant_move(
              us, from, target, moves);
        }
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
