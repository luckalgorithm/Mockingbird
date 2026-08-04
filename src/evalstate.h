#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "types.h"

namespace Mockingbird {

namespace StaticEvaluationDetail {

using Value = std::int32_t;

inline constexpr std::array<Value, PIECE_TYPE_NB> MATERIAL_VALUES = {
  0, 100, 320, 330, 500, 900, 0,
};
inline constexpr std::array<int, PIECE_TYPE_NB> PHASE_WEIGHTS = {
  0, 0, 1, 1, 2, 4, 0,
};
inline constexpr std::array<Value, PIECE_TYPE_NB>
  CENTRALIZATION_MIDDLEGAME = {
    0, 1, 4, 3, 1, 1, -4,
};
inline constexpr std::array<Value, PIECE_TYPE_NB>
  CENTRALIZATION_ENDGAME = {
    0, 1, 3, 3, 2, 1, 5,
};

struct RelativeCoordinates {
    int file;
    int rank;
};

struct WideSquareValue {
    Value middlegame = 0;
    Value endgame = 0;
};

struct SquareValue {
    std::int16_t middlegame = 0;
    std::int16_t endgame = 0;
};

[[nodiscard]] constexpr RelativeCoordinates relative_coordinates(
  Color color,
  Square square) noexcept {
    assert(is_ok(color));
    assert(is_ok(square));

    const int file = int(file_of(square));
    const int rank = int(rank_of(square));

    switch (color) {
        case RED:
            return {file, rank};
        case BLUE:
            return {BOARD_FILES + 1 - rank, file};
        case YELLOW:
            return {
              BOARD_FILES + 1 - file,
              BOARD_RANKS + 1 - rank,
            };
        case GREEN:
            return {rank, BOARD_RANKS + 1 - file};
        case COLOR_NB:
            break;
    }

    return {};
}

[[nodiscard]] constexpr int absolute_value(int value) noexcept {
    return value < 0 ? -value : value;
}

[[nodiscard]] constexpr Value centralization(Square square) noexcept {
    assert(is_ok(square));

    const int file_distance =
      absolute_value(2 * int(file_of(square)) - (BOARD_FILES + 1));
    const int rank_distance =
      absolute_value(2 * int(rank_of(square)) - (BOARD_RANKS + 1));
    return static_cast<Value>(18 - file_distance - rank_distance);
}

[[nodiscard]] constexpr int pawn_advancement(
  Color color,
  Square square) noexcept {
    const int relative_rank =
      relative_coordinates(color, square).rank;
    const int advancement = relative_rank - 2;

    if (advancement < 0)
        return 0;
    if (advancement > 9)
        return 9;
    return advancement;
}

[[nodiscard]] constexpr WideSquareValue make_wide_square_value(
  PieceType piece_type,
  Color color,
  Square square) noexcept {
    assert(is_ok(piece_type));
    assert(is_ok(color));
    assert(is_ok(square));

    const Value center = centralization(square);
    Value middlegame =
      center
      * CENTRALIZATION_MIDDLEGAME[std::size_t(piece_type)];
    Value endgame =
      center
      * CENTRALIZATION_ENDGAME[std::size_t(piece_type)];

    if (piece_type == PAWN) {
        const Value advancement =
          static_cast<Value>(pawn_advancement(color, square));
        middlegame +=
          2 * advancement + advancement * advancement / 2;
        endgame +=
          4 * advancement + advancement * advancement;
    }

    return {middlegame, endgame};
}

struct SquareValueBounds {
    Value minimum_middlegame = 0;
    Value maximum_middlegame = 0;
    Value minimum_endgame = 0;
    Value maximum_endgame = 0;
};

[[nodiscard]] consteval SquareValueBounds make_square_value_bounds() {
    SquareValueBounds bounds;

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        for (int type_index = PAWN;
             type_index <= KING;
             ++type_index) {
            const PieceType piece_type = PieceType(type_index);
            for (int square_index = 0;
                 square_index < SQUARE_NB;
                 ++square_index) {
                const Square square = Square(square_index);
                if (!is_ok(square))
                    continue;

                const WideSquareValue value =
                  make_wide_square_value(piece_type, color, square);
                if (value.middlegame < bounds.minimum_middlegame)
                    bounds.minimum_middlegame = value.middlegame;
                if (value.middlegame > bounds.maximum_middlegame)
                    bounds.maximum_middlegame = value.middlegame;
                if (value.endgame < bounds.minimum_endgame)
                    bounds.minimum_endgame = value.endgame;
                if (value.endgame > bounds.maximum_endgame)
                    bounds.maximum_endgame = value.endgame;
            }
        }
    }

    return bounds;
}

inline constexpr SquareValueBounds SQUARE_VALUE_BOUNDS =
  make_square_value_bounds();

static_assert(
  SQUARE_VALUE_BOUNDS.minimum_middlegame
    >= std::numeric_limits<std::int16_t>::min());
static_assert(
  SQUARE_VALUE_BOUNDS.maximum_middlegame
    <= std::numeric_limits<std::int16_t>::max());
static_assert(
  SQUARE_VALUE_BOUNDS.minimum_endgame
    >= std::numeric_limits<std::int16_t>::min());
static_assert(
  SQUARE_VALUE_BOUNDS.maximum_endgame
    <= std::numeric_limits<std::int16_t>::max());

[[nodiscard]] constexpr Value magnitude(Value value) noexcept {
    return value < 0 ? -value : value;
}

inline constexpr Value MAX_MIDDLEGAME_MAGNITUDE =
  magnitude(SQUARE_VALUE_BOUNDS.minimum_middlegame)
      > magnitude(SQUARE_VALUE_BOUNDS.maximum_middlegame)
    ? magnitude(SQUARE_VALUE_BOUNDS.minimum_middlegame)
    : magnitude(SQUARE_VALUE_BOUNDS.maximum_middlegame);
inline constexpr Value MAX_ENDGAME_MAGNITUDE =
  magnitude(SQUARE_VALUE_BOUNDS.minimum_endgame)
      > magnitude(SQUARE_VALUE_BOUNDS.maximum_endgame)
    ? magnitude(SQUARE_VALUE_BOUNDS.minimum_endgame)
    : magnitude(SQUARE_VALUE_BOUNDS.maximum_endgame);
inline constexpr Value MAX_SQUARE_MAGNITUDE =
  MAX_MIDDLEGAME_MAGNITUDE > MAX_ENDGAME_MAGNITUDE
    ? MAX_MIDDLEGAME_MAGNITUDE
    : MAX_ENDGAME_MAGNITUDE;

// One piece can occupy each playable square, so these products bound either
// signed team aggregate for every mailbox-valid inventory.
static_assert(
  MAX_MIDDLEGAME_MAGNITUDE * PLAYABLE_SQUARE_NB
    <= std::numeric_limits<std::int16_t>::max());
static_assert(
  MAX_ENDGAME_MAGNITUDE * PLAYABLE_SQUARE_NB
    <= std::numeric_limits<std::int16_t>::max());

[[nodiscard]] constexpr SquareValue make_square_value(
  PieceType piece_type,
  Color color,
  Square square) noexcept {
    const WideSquareValue value =
      make_wide_square_value(piece_type, color, square);
    assert(
      value.middlegame >= std::numeric_limits<std::int16_t>::min()
      && value.middlegame <= std::numeric_limits<std::int16_t>::max());
    assert(
      value.endgame >= std::numeric_limits<std::int16_t>::min()
      && value.endgame <= std::numeric_limits<std::int16_t>::max());
    return {
      static_cast<std::int16_t>(value.middlegame),
      static_cast<std::int16_t>(value.endgame),
    };
}

inline constexpr std::size_t NONPAWN_TYPE_NB =
  std::size_t(KING - KNIGHT + 1);

[[nodiscard]] consteval auto make_nonpawn_square_values() {
    std::array<
      std::array<SquareValue, SQUARE_NB>,
      NONPAWN_TYPE_NB>
      values{};

    for (int type_index = KNIGHT;
         type_index <= KING;
         ++type_index) {
        const PieceType piece_type = PieceType(type_index);
        for (int square_index = 0;
             square_index < SQUARE_NB;
             ++square_index) {
            const Square square = Square(square_index);
            if (is_ok(square)) {
                values[std::size_t(piece_type - KNIGHT)]
                      [std::size_t(square)] =
                  make_square_value(piece_type, RED, square);
            }
        }
    }

    return values;
}

[[nodiscard]] consteval auto make_pawn_square_values() {
    std::array<
      std::array<SquareValue, SQUARE_NB>,
      COLOR_NB>
      values{};

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        for (int square_index = 0;
             square_index < SQUARE_NB;
             ++square_index) {
            const Square square = Square(square_index);
            if (is_ok(square)) {
                values[std::size_t(color)]
                      [std::size_t(square)] =
                  make_square_value(PAWN, color, square);
            }
        }
    }

    return values;
}

inline constexpr auto NONPAWN_SQUARE_VALUES =
  make_nonpawn_square_values();
inline constexpr auto PAWN_SQUARE_VALUES =
  make_pawn_square_values();

[[nodiscard]] constexpr SquareValue square_value(
  Piece piece,
  Square square) noexcept {
    assert(is_ok(piece));
    assert(is_ok(square));

    const PieceType piece_type = type_of(piece);
    if (piece_type == PAWN) {
        return PAWN_SQUARE_VALUES[
          std::size_t(color_of(piece))][std::size_t(square)];
    }
    return NONPAWN_SQUARE_VALUES[
      std::size_t(piece_type - KNIGHT)][std::size_t(square)];
}

[[nodiscard]] constexpr int team_sign(Piece piece) noexcept {
    assert(is_ok(piece));
    return team_of(color_of(piece)) == RED_YELLOW ? 1 : -1;
}

[[nodiscard]] constexpr std::size_t pawn_file_index(
  int relative_file) noexcept {
    assert(relative_file >= FILE_A);
    assert(relative_file <= FILE_N);
    return std::size_t(relative_file - FILE_A);
}

// Relative files align with mailbox files for Red and Yellow and with mailbox
// ranks for Blue and Green. Axis reversal does not affect equality.
[[nodiscard]] constexpr int pawn_file_axis_coordinate(
  Color color,
  Square square) noexcept {
    assert(is_ok(color));
    assert(is_ok(square));
    return color == RED || color == YELLOW
      ? int(file_of(square))
      : int(rank_of(square));
}

[[nodiscard]] constexpr Value checked_square_aggregate(
  Value value) noexcept {
    assert(value >= -MAX_SQUARE_MAGNITUDE * PLAYABLE_SQUARE_NB);
    assert(value <= MAX_SQUARE_MAGNITUDE * PLAYABLE_SQUARE_NB);
    return value;
}

}  // namespace StaticEvaluationDetail

// Stores board-local evaluation terms from Red and Yellow's perspective.
// Occupancy-dependent terms are calculated from the current position.
struct StaticEvaluationState {
    using PawnFileCounts =
      std::array<std::uint8_t, BOARD_FILES>;

    std::int32_t material = 0;
    int phase = 0;
    std::int32_t square_middlegame = 0;
    std::int32_t square_endgame = 0;
    std::array<PawnFileCounts, COLOR_NB> pawn_file_counts{};
    std::array<std::uint8_t, COLOR_NB> bishop_counts{};

    constexpr void add_piece(Piece piece, Square square) noexcept {
        assert(is_ok(piece));
        assert(is_ok(square));

        const int sign = StaticEvaluationDetail::team_sign(piece);
        const PieceType piece_type = type_of(piece);
        const StaticEvaluationDetail::SquareValue value =
          StaticEvaluationDetail::square_value(piece, square);
        material += sign
                  * StaticEvaluationDetail::MATERIAL_VALUES[
                      std::size_t(piece_type)];
        phase += StaticEvaluationDetail::PHASE_WEIGHTS[
          std::size_t(piece_type)];
        square_middlegame =
          StaticEvaluationDetail::checked_square_aggregate(
            std::int32_t{square_middlegame}
            + sign * std::int32_t{value.middlegame});
        square_endgame =
          StaticEvaluationDetail::checked_square_aggregate(
            std::int32_t{square_endgame}
            + sign * std::int32_t{value.endgame});

        const Color color = color_of(piece);
        if (piece_type == PAWN) {
            const int file =
              StaticEvaluationDetail::relative_coordinates(
                color, square).file;
            ++pawn_file_counts[std::size_t(color)]
                              [StaticEvaluationDetail::pawn_file_index(
                                file)];
        } else if (piece_type == BISHOP) {
            ++bishop_counts[std::size_t(color)];
        }
    }

    constexpr void remove_piece(Piece piece, Square square) noexcept {
        assert(is_ok(piece));
        assert(is_ok(square));

        const int sign = StaticEvaluationDetail::team_sign(piece);
        const PieceType piece_type = type_of(piece);
        const StaticEvaluationDetail::SquareValue value =
          StaticEvaluationDetail::square_value(piece, square);
        material -= sign
                  * StaticEvaluationDetail::MATERIAL_VALUES[
                      std::size_t(piece_type)];
        phase -= StaticEvaluationDetail::PHASE_WEIGHTS[
          std::size_t(piece_type)];
        square_middlegame =
          StaticEvaluationDetail::checked_square_aggregate(
            std::int32_t{square_middlegame}
            - sign * std::int32_t{value.middlegame});
        square_endgame =
          StaticEvaluationDetail::checked_square_aggregate(
            std::int32_t{square_endgame}
            - sign * std::int32_t{value.endgame});

        const Color color = color_of(piece);
        if (piece_type == PAWN) {
            const int file =
              StaticEvaluationDetail::relative_coordinates(
                color, square).file;
            std::uint8_t& count =
              pawn_file_counts[std::size_t(color)]
                              [StaticEvaluationDetail::pawn_file_index(
                                file)];
            assert(count > 0);
            --count;
        } else if (piece_type == BISHOP) {
            std::uint8_t& count =
              bishop_counts[std::size_t(color)];
            assert(count > 0);
            --count;
        }
    }

    constexpr void relocate_piece(
      Piece piece,
      Square from,
      Square to) noexcept {
        assert(is_ok(piece));
        assert(is_ok(from));
        assert(is_ok(to));

        const int sign = StaticEvaluationDetail::team_sign(piece);
        const StaticEvaluationDetail::SquareValue from_value =
          StaticEvaluationDetail::square_value(piece, from);
        const StaticEvaluationDetail::SquareValue to_value =
          StaticEvaluationDetail::square_value(piece, to);
        square_middlegame =
          StaticEvaluationDetail::checked_square_aggregate(
            std::int32_t{square_middlegame}
            + sign
              * (std::int32_t{to_value.middlegame}
                 - std::int32_t{from_value.middlegame}));
        square_endgame =
          StaticEvaluationDetail::checked_square_aggregate(
            std::int32_t{square_endgame}
            + sign
              * (std::int32_t{to_value.endgame}
                 - std::int32_t{from_value.endgame}));

        if (type_of(piece) == PAWN) {
            const Color color = color_of(piece);
            if (StaticEvaluationDetail::pawn_file_axis_coordinate(
                  color, from)
                == StaticEvaluationDetail::pawn_file_axis_coordinate(
                  color, to)) {
                return;
            }

            const int from_file =
              StaticEvaluationDetail::relative_coordinates(
                color, from).file;
            const int to_file =
              StaticEvaluationDetail::relative_coordinates(
                color, to).file;
            assert(from_file != to_file);
            std::uint8_t& from_count =
              pawn_file_counts[std::size_t(color)]
                              [StaticEvaluationDetail::pawn_file_index(
                                from_file)];
            assert(from_count > 0);
            --from_count;
            ++pawn_file_counts[std::size_t(color)]
                              [StaticEvaluationDetail::pawn_file_index(
                                to_file)];
        }
    }

    [[nodiscard]] friend constexpr bool operator==(
      const StaticEvaluationState&,
      const StaticEvaluationState&) noexcept = default;
};

static_assert(StaticEvaluationDetail::NONPAWN_TYPE_NB == 5);
static_assert(sizeof(StaticEvaluationDetail::SquareValue) == 4);
static_assert(
  sizeof(StaticEvaluationDetail::NONPAWN_SQUARE_VALUES)
    == 5 * SQUARE_NB * sizeof(StaticEvaluationDetail::SquareValue));
static_assert(
  sizeof(StaticEvaluationDetail::PAWN_SQUARE_VALUES)
    == std::size_t(COLOR_NB) * std::size_t(SQUARE_NB)
       * sizeof(StaticEvaluationDetail::SquareValue));
static_assert(
  sizeof(StaticEvaluationDetail::NONPAWN_SQUARE_VALUES)
      + sizeof(StaticEvaluationDetail::PAWN_SQUARE_VALUES)
    == 9'216);
static_assert(sizeof(StaticEvaluationState) == 76);

}  // namespace Mockingbird
