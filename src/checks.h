#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <utility>

#include "attacks.h"
#include "position.h"

namespace Mockingbird {

namespace Detail {

// Pawn attack tables are indexed by the pawn's movement direction. Looking
// backward from a target therefore uses the color rotated by 180 degrees.
[[nodiscard]] constexpr Bitboard pawn_attackers_to(
  const Position& position, Square square) noexcept {
    Bitboard attackers;

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);
        const Color reverse = next_color(next_color(color));
        attackers |= pawn_attacks(reverse, square)
                   & position.pieces(color, PAWN);
    }

    return attackers;
}

// The two teams are the only valid Team values.
[[nodiscard]] constexpr Team opposing_team(Team team) noexcept {
    assert(is_ok(team));
    return team == RED_YELLOW ? BLUE_GREEN : RED_YELLOW;
}

// Returns the first occupied square from source in direction. Ray ordering is
// selected from the sign of its mailbox offset.
// Preconditions: direction is valid and source is in the mailbox index range.
[[nodiscard]] constexpr Square first_occupied_on_ray(
  RayDirection direction,
  Square source,
  const Bitboard& occupied) noexcept {
    assert(is_ok(direction));
    assert(static_cast<unsigned>(source) < SQUARE_NB);

    const Bitboard occupied_on_ray =
      ray_attacks(direction, source) & occupied;
    if (occupied_on_ray.empty())
        return SQ_NONE;

    const Direction offset =
      RAY_OFFSETS[std::to_underlying(direction)];
    return int(offset) > 0
      ? occupied_on_ray.lsb()
      : occupied_on_ray.msb();
}

// Adds moving-color pieces that are the sole occupied square between the king
// and an opposing compatible slider in one of the supplied directions.
template<std::size_t DirectionCount>
constexpr void add_slider_blockers(
  Square king,
  const Bitboard& occupied,
  const Bitboard& moving_pieces,
  const std::array<RayDirection, DirectionCount>& directions,
  const Bitboard& opposing_sliders,
  Bitboard& blockers) noexcept {
    for (const RayDirection direction : directions) {
        const Square candidate =
          first_occupied_on_ray(
            direction, king, occupied);
        if (candidate == SQ_NONE
            || !moving_pieces.test(candidate))
            continue;

        const Square pinner =
          first_occupied_on_ray(
            direction, candidate, occupied);
        if (pinner != SQ_NONE
            && opposing_sliders.test(pinner))
            blockers.set(candidate);
    }
}

}  // namespace Detail

// Returns the source squares of every piece that attacks square. occupied
// controls sliding-piece blockers only; it does not relocate or remove pieces.
// Piece colors, types, and source squares are read from position.
// Precondition: square is playable.
[[nodiscard]] constexpr Bitboard attackers_to(
  const Position& position,
  Square square,
  const Bitboard& occupied) noexcept {
    assert(is_ok(square));

    Bitboard attackers = Detail::pawn_attackers_to(position, square);
    attackers |= knight_attacks(square) & position.pieces(KNIGHT);
    attackers |= king_attacks(square) & position.pieces(KING);
    attackers |= rook_attacks(square, occupied)
               & (position.pieces(ROOK) | position.pieces(QUEEN));
    attackers |= bishop_attacks(square, occupied)
               & (position.pieces(BISHOP) | position.pieces(QUEEN));
    return attackers;
}

// Uses the position's current occupancy for sliding-piece blockers.
// Precondition: square is playable.
[[nodiscard]] constexpr Bitboard attackers_to(
  const Position& position, Square square) noexcept {
    return attackers_to(position, square, position.occupied());
}

// Returns only attackers belonging to attacking_team. occupied controls
// sliding-piece blockers only; piece state is read from position.
// Preconditions: square is playable and attacking_team is valid.
[[nodiscard]] constexpr Bitboard attackers_to(
  const Position& position,
  Square square,
  Team attacking_team,
  const Bitboard& occupied) noexcept {
    assert(is_ok(attacking_team));
    return attackers_to(position, square, occupied)
         & position.pieces(attacking_team);
}

// Uses the position's current occupancy for sliding-piece blockers.
// Preconditions: square is playable and attacking_team is valid.
[[nodiscard]] constexpr Bitboard attackers_to(
  const Position& position,
  Square square,
  Team attacking_team) noexcept {
    return attackers_to(
      position, square, attacking_team, position.occupied());
}

// occupied controls sliding-piece blockers only. Attack classes are checked
// from fixed-distance pieces to sliding pieces, with an immediate return after
// the first match.
// Preconditions: square is playable and attacking_team is valid.
[[nodiscard]] constexpr bool is_square_attacked_by_team(
  const Position& position,
  Square square,
  Team attacking_team,
  const Bitboard& occupied) noexcept {
    assert(is_ok(square));
    assert(is_ok(attacking_team));

    const Bitboard team_pieces = position.pieces(attacking_team);

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);
        if (team_of(color) != attacking_team)
            continue;

        const Color reverse = next_color(next_color(color));
        if (pawn_attacks(reverse, square)
            & position.pieces(color, PAWN))
            return true;
    }

    if (knight_attacks(square)
        & team_pieces
        & position.pieces(KNIGHT))
        return true;

    if (king_attacks(square)
        & team_pieces
        & position.pieces(KING))
        return true;

    const Bitboard rook_or_queen =
      team_pieces
      & (position.pieces(ROOK) | position.pieces(QUEEN));
    if (rook_attacks(square, occupied) & rook_or_queen)
        return true;

    const Bitboard bishop_or_queen =
      team_pieces
      & (position.pieces(BISHOP) | position.pieces(QUEEN));
    return bool(bishop_attacks(square, occupied)
                & bishop_or_queen);
}

// Uses the position's current occupancy for sliding-piece blockers.
// Preconditions: square is playable and attacking_team is valid.
[[nodiscard]] constexpr bool is_square_attacked_by_team(
  const Position& position,
  Square square,
  Team attacking_team) noexcept {
    return is_square_attacked_by_team(
      position, square, attacking_team, position.occupied());
}

// Returns moving-color pieces that are the sole occupied square between that
// color's king and an opposing rook, bishop, or queen. A position without
// exactly one king of color has no unique king ray and returns an empty set.
// Precondition: color is valid.
[[nodiscard]] constexpr Bitboard slider_blockers_to_king(
  const Position& position, Color color) noexcept {
    assert(is_ok(color));

    const Bitboard kings = position.pieces(color, KING);
    if (kings.popcount() != 1)
        return {};

    const Bitboard occupied = position.occupied();
    const Bitboard moving_pieces =
      position.pieces(color);
    const Team opponents =
      Detail::opposing_team(team_of(color));
    const Bitboard opposing_pieces =
      position.pieces(opponents);
    const Bitboard opposing_rooks_queens =
      opposing_pieces
      & (position.pieces(ROOK) | position.pieces(QUEEN));
    const Bitboard opposing_bishops_queens =
      opposing_pieces
      & (position.pieces(BISHOP) | position.pieces(QUEEN));

    Bitboard blockers;
    const Square king = kings.lsb();
    Detail::add_slider_blockers(
      king,
      occupied,
      moving_pieces,
      Detail::ROOK_DIRECTIONS,
      opposing_rooks_queens,
      blockers);
    Detail::add_slider_blockers(
      king,
      occupied,
      moving_pieces,
      Detail::BISHOP_DIRECTIONS,
      opposing_bishops_queens,
      blockers);
    return blockers;
}

// Returns the opposing pieces that attack color's king in the current
// position. If the position does not contain exactly one king of that color,
// there is no unique check target and the result is empty.
// Precondition: color is valid.
[[nodiscard]] constexpr Bitboard checkers(
  const Position& position, Color color) noexcept {
    assert(is_ok(color));

    const Bitboard kings = position.pieces(color, KING);
    if (kings.popcount() != 1)
        return {};

    return attackers_to(
      position,
      kings.lsb(),
      Detail::opposing_team(team_of(color)),
      position.occupied());
}

// A position without exactly one king of color is not reported as check.
// Precondition: color is valid.
[[nodiscard]] constexpr bool in_check(
  const Position& position, Color color) noexcept {
    assert(is_ok(color));

    const Bitboard kings = position.pieces(color, KING);
    if (kings.popcount() != 1)
        return false;

    return is_square_attacked_by_team(
      position,
      kings.lsb(),
      Detail::opposing_team(team_of(color)),
      position.occupied());
}

// Evaluates the king belonging to position.side_to_move(). A side without
// exactly one king is not reported as check.
[[nodiscard]] constexpr bool in_check(
  const Position& position) noexcept {
    return in_check(position, position.side_to_move());
}

}  // namespace Mockingbird
