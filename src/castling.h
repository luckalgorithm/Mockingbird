#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <utility>

#include "checks.h"

namespace Mockingbird {

// CastlingGeometry contains the fixed squares for one color and side.
// required_empty contains every square strictly between the king and rook.
struct CastlingGeometry {
    Square king_source;
    Square rook_source;
    Square king_transit;
    Square king_destination;
    Square rook_destination;
    Bitboard required_empty;
};

namespace Detail {

// King sources and kingside directions follow the standard four-player
// starting arrangement in Red, Blue, Yellow, and Green order.
inline constexpr std::array<Square, COLOR_NB> CASTLING_KING_SOURCES = {
  make_square(FILE_H, RANK_1),
  make_square(FILE_A, RANK_7),
  make_square(FILE_G, RANK_14),
  make_square(FILE_N, RANK_8),
};

inline constexpr std::array<Direction, COLOR_NB>
  CASTLING_KING_SIDE_DIRECTIONS = {
    EAST,
    SOUTH,
    WEST,
    NORTH,
};

[[nodiscard]] constexpr Square advance_square(
  Square square, Direction direction, int distance) noexcept {
    for (int step = 0; step < distance; ++step)
        square = square + direction;

    return square;
}

[[nodiscard]] constexpr CastlingGeometry make_castling_geometry(
  Color color, CastlingSide side) noexcept {
    const Square king_source =
      CASTLING_KING_SOURCES[std::size_t(color)];
    const Direction king_side_direction =
      CASTLING_KING_SIDE_DIRECTIONS[std::size_t(color)];
    const Direction direction =
      side == CastlingSide::KING_SIDE
        ? king_side_direction
        : Direction(-int(king_side_direction));
    const int rook_distance =
      side == CastlingSide::KING_SIDE ? 3 : 4;

    CastlingGeometry geometry{
      .king_source = king_source,
      .rook_source =
        advance_square(king_source, direction, rook_distance),
      .king_transit = advance_square(king_source, direction, 1),
      .king_destination =
        advance_square(king_source, direction, 2),
      .rook_destination =
        advance_square(king_source, direction, 1),
      .required_empty = {},
    };

    for (int distance = 1; distance < rook_distance; ++distance)
        geometry.required_empty.set(
          advance_square(king_source, direction, distance));

    return geometry;
}

[[nodiscard]] consteval auto make_castling_geometries() {
    std::array<
      std::array<CastlingGeometry, CASTLING_SIDE_NB>,
      COLOR_NB> geometries{};

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);
        geometries[std::size_t(color)][std::to_underlying(
          CastlingSide::KING_SIDE)] =
          make_castling_geometry(
            color, CastlingSide::KING_SIDE);
        geometries[std::size_t(color)][std::to_underlying(
          CastlingSide::QUEEN_SIDE)] =
          make_castling_geometry(
            color, CastlingSide::QUEEN_SIDE);
    }

    return geometries;
}

inline constexpr auto CASTLING_GEOMETRIES =
  make_castling_geometries();

[[nodiscard]] consteval bool castling_geometries_are_valid() {
    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        for (std::size_t side_index = 0;
             side_index < CASTLING_SIDE_NB;
             ++side_index) {
            const CastlingGeometry& geometry =
              CASTLING_GEOMETRIES[std::size_t(color_index)][side_index];

            if (!is_ok(geometry.king_source)
                || !is_ok(geometry.rook_source)
                || !is_ok(geometry.king_transit)
                || !is_ok(geometry.king_destination)
                || !is_ok(geometry.rook_destination))
                return false;

            const int expected_empty_count =
              side_index
                  == std::to_underlying(CastlingSide::KING_SIDE)
                ? 2
                : 3;
            if (geometry.required_empty.popcount()
                  != expected_empty_count
                || !geometry.required_empty.test(
                  geometry.king_transit)
                || !geometry.required_empty.test(
                  geometry.king_destination)
                || geometry.required_empty.test(
                  geometry.king_source)
                || geometry.required_empty.test(
                  geometry.rook_source)
                || geometry.rook_destination
                     != geometry.king_transit)
                return false;
        }
    }

    return true;
}

}  // namespace Detail

// Preconditions: color and side are valid.
[[nodiscard]] constexpr const CastlingGeometry& castling_geometry(
  Color color, CastlingSide side) noexcept {
    assert(is_ok(color));
    assert(is_ok(side));
    return Detail::CASTLING_GEOMETRIES[std::size_t(color)]
                                       [std::to_underlying(side)];
}

// Returns whether the side to move may castle on side in the current
// position. The stored right, canonical pieces, path occupancy, and attacks
// on the king's source, transit, and destination are evaluated.
// Precondition: side is valid.
[[nodiscard]] constexpr bool is_castling_legal(
  const Position& position, CastlingSide side) noexcept {
    assert(is_ok(side));

    const Color color = position.side_to_move();
    if (!position.has_castling_right(color, side))
        return false;

    const CastlingGeometry& geometry =
      castling_geometry(color, side);
    if (position.piece_on(geometry.king_source)
          != make_piece(color, KING)
        || position.piece_on(geometry.rook_source)
             != make_piece(color, ROOK))
        return false;

    const Bitboard occupied = position.occupied();
    if (occupied & geometry.required_empty)
        return false;

    const Team opponents =
      Detail::opposing_team(team_of(color));
    if (is_square_attacked_by_team(
          position,
          geometry.king_source,
          opponents,
          occupied))
        return false;

    // The king source no longer blocks attacks after the king starts moving.
    Bitboard transit_occupancy = occupied;
    transit_occupancy.clear(geometry.king_source);
    if (is_square_attacked_by_team(
          position,
          geometry.king_transit,
          opponents,
          transit_occupancy))
        return false;

    // The destination attack test uses the rook's final occupancy.
    Bitboard destination_occupancy = transit_occupancy;
    destination_occupancy.clear(geometry.rook_source);
    destination_occupancy.set(geometry.rook_destination);
    return !is_square_attacked_by_team(
      position,
      geometry.king_destination,
      opponents,
      destination_occupancy);
}

static_assert(Detail::castling_geometries_are_valid());
static_assert(
  castling_geometry(RED, CastlingSide::KING_SIDE).king_source
  == make_square(FILE_H, RANK_1));
static_assert(
  castling_geometry(GREEN, CastlingSide::QUEEN_SIDE).king_destination
  == make_square(FILE_N, RANK_6));

}  // namespace Mockingbird
