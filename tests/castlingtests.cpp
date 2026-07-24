#include "castling.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

// Records a failed condition and allows the remaining tests to run.
void expect(bool condition, std::string_view message) {
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

struct GeometryCase {
    Mockingbird::Color color;
    Mockingbird::CastlingSide side;
    Mockingbird::Square king_source;
    Mockingbird::Square rook_source;
    Mockingbird::Square king_transit;
    Mockingbird::Square king_destination;
    Mockingbird::Square rook_destination;
    std::array<Mockingbird::Square, 3> required_empty;
    std::size_t required_empty_count;
};

using namespace Mockingbird;

inline constexpr std::array<GeometryCase, 8> GEOMETRY_CASES = {{
  {
    RED,
    CastlingSide::KING_SIDE,
    make_square(FILE_H, RANK_1),
    make_square(FILE_K, RANK_1),
    make_square(FILE_I, RANK_1),
    make_square(FILE_J, RANK_1),
    make_square(FILE_I, RANK_1),
    {
      make_square(FILE_I, RANK_1),
      make_square(FILE_J, RANK_1),
      SQ_NONE,
    },
    2,
  },
  {
    RED,
    CastlingSide::QUEEN_SIDE,
    make_square(FILE_H, RANK_1),
    make_square(FILE_D, RANK_1),
    make_square(FILE_G, RANK_1),
    make_square(FILE_F, RANK_1),
    make_square(FILE_G, RANK_1),
    {
      make_square(FILE_G, RANK_1),
      make_square(FILE_F, RANK_1),
      make_square(FILE_E, RANK_1),
    },
    3,
  },
  {
    BLUE,
    CastlingSide::KING_SIDE,
    make_square(FILE_A, RANK_7),
    make_square(FILE_A, RANK_4),
    make_square(FILE_A, RANK_6),
    make_square(FILE_A, RANK_5),
    make_square(FILE_A, RANK_6),
    {
      make_square(FILE_A, RANK_6),
      make_square(FILE_A, RANK_5),
      SQ_NONE,
    },
    2,
  },
  {
    BLUE,
    CastlingSide::QUEEN_SIDE,
    make_square(FILE_A, RANK_7),
    make_square(FILE_A, RANK_11),
    make_square(FILE_A, RANK_8),
    make_square(FILE_A, RANK_9),
    make_square(FILE_A, RANK_8),
    {
      make_square(FILE_A, RANK_8),
      make_square(FILE_A, RANK_9),
      make_square(FILE_A, RANK_10),
    },
    3,
  },
  {
    YELLOW,
    CastlingSide::KING_SIDE,
    make_square(FILE_G, RANK_14),
    make_square(FILE_D, RANK_14),
    make_square(FILE_F, RANK_14),
    make_square(FILE_E, RANK_14),
    make_square(FILE_F, RANK_14),
    {
      make_square(FILE_F, RANK_14),
      make_square(FILE_E, RANK_14),
      SQ_NONE,
    },
    2,
  },
  {
    YELLOW,
    CastlingSide::QUEEN_SIDE,
    make_square(FILE_G, RANK_14),
    make_square(FILE_K, RANK_14),
    make_square(FILE_H, RANK_14),
    make_square(FILE_I, RANK_14),
    make_square(FILE_H, RANK_14),
    {
      make_square(FILE_H, RANK_14),
      make_square(FILE_I, RANK_14),
      make_square(FILE_J, RANK_14),
    },
    3,
  },
  {
    GREEN,
    CastlingSide::KING_SIDE,
    make_square(FILE_N, RANK_8),
    make_square(FILE_N, RANK_11),
    make_square(FILE_N, RANK_9),
    make_square(FILE_N, RANK_10),
    make_square(FILE_N, RANK_9),
    {
      make_square(FILE_N, RANK_9),
      make_square(FILE_N, RANK_10),
      SQ_NONE,
    },
    2,
  },
  {
    GREEN,
    CastlingSide::QUEEN_SIDE,
    make_square(FILE_N, RANK_8),
    make_square(FILE_N, RANK_4),
    make_square(FILE_N, RANK_7),
    make_square(FILE_N, RANK_6),
    make_square(FILE_N, RANK_7),
    {
      make_square(FILE_N, RANK_7),
      make_square(FILE_N, RANK_6),
      make_square(FILE_N, RANK_5),
    },
    3,
  },
}};

[[nodiscard]] constexpr Bitboard expected_empty_mask(
  const GeometryCase& test_case) noexcept {
    Bitboard mask;
    for (std::size_t index = 0;
         index < test_case.required_empty_count;
         ++index)
        mask.set(test_case.required_empty[index]);

    return mask;
}

[[nodiscard]] constexpr bool exact_geometries_match() noexcept {
    for (const GeometryCase& test_case : GEOMETRY_CASES) {
        const CastlingGeometry& geometry =
          castling_geometry(test_case.color, test_case.side);

        if (geometry.king_source != test_case.king_source
            || geometry.rook_source != test_case.rook_source
            || geometry.king_transit != test_case.king_transit
            || geometry.king_destination
                 != test_case.king_destination
            || geometry.rook_destination
                 != test_case.rook_destination
            || geometry.required_empty
                 != expected_empty_mask(test_case))
            return false;
    }

    return true;
}

static_assert(exact_geometries_match());

[[nodiscard]] constexpr Position baseline_position(
  const GeometryCase& test_case) noexcept {
    Position position;
    position.set_side_to_move(test_case.color);
    position.set_castling_right(
      test_case.color, test_case.side);
    position.put_piece(
      make_piece(test_case.color, KING),
      test_case.king_source);
    position.put_piece(
      make_piece(test_case.color, ROOK),
      test_case.rook_source);
    return position;
}

[[nodiscard]] constexpr bool constexpr_legality_cases() noexcept {
    const GeometryCase& test_case = GEOMETRY_CASES[0];
    Position position = baseline_position(test_case);
    if (!is_castling_legal(
          position, CastlingSide::KING_SIDE))
        return false;

    position.put_piece(
      B_ROOK, make_square(FILE_H, RANK_4));
    if (is_castling_legal(
          position, CastlingSide::KING_SIDE))
        return false;

    Position blocked = baseline_position(test_case);
    blocked.put_piece(
      R_KNIGHT, make_square(FILE_I, RANK_1));
    return !is_castling_legal(
      blocked, CastlingSide::KING_SIDE);
}

static_assert(constexpr_legality_cases());

[[nodiscard]] constexpr Team opposing_team(Color color) noexcept {
    return team_of(color) == RED_YELLOW
      ? BLUE_GREEN
      : RED_YELLOW;
}

[[nodiscard]] constexpr std::array<Color, 2> team_colors(
  Team team) noexcept {
    return team == RED_YELLOW
      ? std::array<Color, 2>{RED, YELLOW}
      : std::array<Color, 2>{BLUE, GREEN};
}

[[nodiscard]] constexpr bool piece_attacks(
  PieceType piece_type,
  Color color,
  Square source,
  Square target,
  const Bitboard& occupied) noexcept {
    switch (piece_type) {
    case PAWN:
        return pawn_attacks(color, source).test(target);
    case KNIGHT:
        return knight_attacks(source).test(target);
    case BISHOP:
        return bishop_attacks(source, occupied).test(target);
    case ROOK:
        return rook_attacks(source, occupied).test(target);
    case QUEEN:
        return queen_attacks(source, occupied).test(target);
    case KING:
        return king_attacks(source).test(target);
    default:
        return false;
    }
}

// Finds a source that attacks the selected king-path stage without attacking
// an earlier stage. Sources and required-empty squares are excluded.
[[nodiscard]] Square find_isolated_attacker(
  const Position& position,
  const CastlingGeometry& geometry,
  Color color,
  PieceType piece_type,
  int target_stage) {
    const std::array<Square, 3> king_path = {
      geometry.king_source,
      geometry.king_transit,
      geometry.king_destination,
    };

    for (int source_index = 0;
         source_index < SQUARE_NB;
         ++source_index) {
        const Square source = Square(source_index);
        if (!is_ok(source)
            || position.occupied().test(source)
            || geometry.required_empty.test(source))
            continue;

        Bitboard candidate_occupancy = position.occupied();
        candidate_occupancy.set(source);

        bool attacks_earlier_stage = false;
        for (int stage = 0; stage < target_stage; ++stage) {
            Bitboard stage_occupancy = candidate_occupancy;
            if (stage >= 1)
                stage_occupancy.clear(geometry.king_source);
            if (stage >= 2) {
                stage_occupancy.clear(geometry.rook_source);
                stage_occupancy.set(geometry.rook_destination);
            }

            if (piece_attacks(
                  piece_type,
                  color,
                  source,
                  king_path[std::size_t(stage)],
                  stage_occupancy)) {
                attacks_earlier_stage = true;
                break;
            }
        }

        if (attacks_earlier_stage)
            continue;

        Bitboard target_occupancy = candidate_occupancy;
        if (target_stage >= 1)
            target_occupancy.clear(geometry.king_source);
        if (target_stage >= 2) {
            target_occupancy.clear(geometry.rook_source);
            target_occupancy.set(geometry.rook_destination);
        }

        if (piece_attacks(
              piece_type,
              color,
              source,
              king_path[std::size_t(target_stage)],
              target_occupancy))
            return source;
    }

    return SQ_NONE;
}

void test_exact_geometry() {
    for (const GeometryCase& test_case : GEOMETRY_CASES) {
        const CastlingGeometry& geometry =
          castling_geometry(test_case.color, test_case.side);

        expect(geometry.king_source == test_case.king_source,
               "castling king source matches the canonical square");
        expect(geometry.rook_source == test_case.rook_source,
               "castling rook source matches the canonical square");
        expect(geometry.king_transit == test_case.king_transit,
               "castling transit matches the canonical square");
        expect(geometry.king_destination
                 == test_case.king_destination,
               "castling king destination matches the canonical square");
        expect(geometry.rook_destination
                 == test_case.rook_destination,
               "castling rook destination matches the canonical square");
        expect(geometry.required_empty
                 == expected_empty_mask(test_case),
               "castling empty mask matches the canonical squares");
        expect(geometry.required_empty.popcount()
                 == static_cast<int>(
                   test_case.required_empty_count),
               "castling empty mask has the canonical size");

        expect(is_ok(geometry.king_source)
                 && is_ok(geometry.rook_source)
                 && is_ok(geometry.king_transit)
                 && is_ok(geometry.king_destination)
                 && is_ok(geometry.rook_destination),
               "every castling geometry square is playable");
    }
}

void test_baseline_and_rights() {
    constexpr Square unrelated =
      make_square(FILE_H, RANK_8);

    for (const GeometryCase& test_case : GEOMETRY_CASES) {
        Position position = baseline_position(test_case);
        expect(is_castling_legal(position, test_case.side),
               "canonical unobstructed castling is legal");

        position.clear_castling_rights();
        expect(!is_castling_legal(position, test_case.side),
               "castling without its historical right is illegal");

        position.set_castling_right(
          test_case.color,
          test_case.side == CastlingSide::KING_SIDE
            ? CastlingSide::QUEEN_SIDE
            : CastlingSide::KING_SIDE);
        expect(!is_castling_legal(position, test_case.side),
               "the other side's castling right is insufficient");

        Position occupied = baseline_position(test_case);
        occupied.put_piece(
          make_piece(test_case.color, KNIGHT), unrelated);
        expect(is_castling_legal(occupied, test_case.side),
               "occupancy outside the path does not prohibit castling");
    }
}

void test_required_pieces() {
    constexpr Square moved_square =
      make_square(FILE_H, RANK_8);

    for (const GeometryCase& test_case : GEOMETRY_CASES) {
        Position missing_king;
        missing_king.set_side_to_move(test_case.color);
        missing_king.set_castling_right(
          test_case.color, test_case.side);
        missing_king.put_piece(
          make_piece(test_case.color, ROOK),
          test_case.rook_source);
        expect(!is_castling_legal(
                 missing_king, test_case.side),
               "a stored right without its king is insufficient");

        Position wrong_king = missing_king;
        wrong_king.put_piece(
          make_piece(test_case.color, QUEEN),
          test_case.king_source);
        expect(!is_castling_legal(
                 wrong_king, test_case.side),
               "a non-king on the king source is insufficient");

        Position enemy_king = missing_king;
        enemy_king.put_piece(
          make_piece(next_color(test_case.color), KING),
          test_case.king_source);
        expect(!is_castling_legal(
                 enemy_king, test_case.side),
               "another color's king cannot use the stored right");

        Position moved_king = baseline_position(test_case);
        moved_king.move_piece(
          test_case.king_source, moved_square);
        expect(!is_castling_legal(
                 moved_king, test_case.side),
               "a king away from its canonical source cannot castle");

        Position missing_rook;
        missing_rook.set_side_to_move(test_case.color);
        missing_rook.set_castling_right(
          test_case.color, test_case.side);
        missing_rook.put_piece(
          make_piece(test_case.color, KING),
          test_case.king_source);
        expect(!is_castling_legal(
                 missing_rook, test_case.side),
               "a stored right without its rook is insufficient");

        Position wrong_rook = missing_rook;
        wrong_rook.put_piece(
          make_piece(test_case.color, BISHOP),
          test_case.rook_source);
        expect(!is_castling_legal(
                 wrong_rook, test_case.side),
               "a non-rook on the rook source is insufficient");

        Position enemy_rook = missing_rook;
        enemy_rook.put_piece(
          make_piece(next_color(test_case.color), ROOK),
          test_case.rook_source);
        expect(!is_castling_legal(
                 enemy_rook, test_case.side),
               "another color's rook cannot use the stored right");

        Position moved_rook = baseline_position(test_case);
        moved_rook.move_piece(
          test_case.rook_source, moved_square);
        expect(!is_castling_legal(
                 moved_rook, test_case.side),
               "a rook away from its canonical source cannot castle");
    }
}

void test_every_path_blocker() {
    for (const GeometryCase& test_case : GEOMETRY_CASES) {
        for (std::size_t square_index = 0;
             square_index < test_case.required_empty_count;
             ++square_index) {
            const Square blocked =
              test_case.required_empty[square_index];

            for (int color_index = 0;
                 color_index < COLOR_NB;
                 ++color_index) {
                Position position = baseline_position(test_case);
                position.put_piece(
                  make_piece(Color(color_index), KNIGHT),
                  blocked);
                expect(!is_castling_legal(
                         position, test_case.side),
                       "every piece color blocks every required square");
            }
        }
    }
}

void test_enemy_and_friendly_attacks() {
    for (const GeometryCase& test_case : GEOMETRY_CASES) {
        const CastlingGeometry& geometry =
          castling_geometry(test_case.color, test_case.side);

        for (int stage = 0; stage < 3; ++stage) {
            int friendly_pawn_cases = 0;

            for (const Color enemy :
                 team_colors(opposing_team(test_case.color))) {
                for (int type_index = PAWN;
                     type_index <= KING;
                     ++type_index) {
                    const PieceType piece_type =
                      PieceType(type_index);
                    Position position =
                      baseline_position(test_case);
                    const Square attacker =
                      find_isolated_attacker(
                        position,
                        geometry,
                        enemy,
                        piece_type,
                        stage);

                    expect(attacker != SQ_NONE,
                           "an isolated enemy attack case exists");
                    if (attacker == SQ_NONE)
                        continue;

                    position.put_piece(
                      make_piece(enemy, piece_type),
                      attacker);
                    expect(!is_castling_legal(
                             position, test_case.side),
                           "either enemy color attacks every king-path stage");
                }
            }

            for (const Color friendly :
                 team_colors(team_of(test_case.color))) {
                for (int type_index = PAWN;
                     type_index <= KING;
                     ++type_index) {
                    const PieceType piece_type =
                      PieceType(type_index);
                    if (friendly == test_case.color
                        && piece_type == KING)
                        continue;

                    Position position =
                      baseline_position(test_case);
                    const Square attacker =
                      find_isolated_attacker(
                        position,
                        geometry,
                        friendly,
                        piece_type,
                        stage);

                    if (attacker == SQ_NONE) {
                        expect(piece_type == PAWN,
                               "an isolated friendly non-pawn attack case exists");
                        continue;
                    }

                    position.put_piece(
                      make_piece(friendly, piece_type),
                      attacker);
                    expect(is_castling_legal(
                             position, test_case.side),
                           "the mover and teammate do not attack their king");

                    if (piece_type == PAWN)
                        ++friendly_pawn_cases;
                }
            }

            expect(friendly_pawn_cases > 0,
                   "a friendly pawn attacks each king-path stage");
        }
    }
}

void test_slider_blockers() {
    constexpr Square enemy_rook =
      make_square(FILE_H, RANK_5);
    constexpr Square blocker =
      make_square(FILE_H, RANK_3);
    const GeometryCase& test_case = GEOMETRY_CASES[0];

    Position open = baseline_position(test_case);
    open.put_piece(B_ROOK, enemy_rook);
    expect(!is_castling_legal(
             open, CastlingSide::KING_SIDE),
           "an unobstructed enemy slider prohibits castling");

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        Position blocked = baseline_position(test_case);
        blocked.put_piece(B_ROOK, enemy_rook);
        blocked.put_piece(
          make_piece(Color(color_index), PAWN), blocker);
        expect(is_castling_legal(
                 blocked, CastlingSide::KING_SIDE),
               "every piece color blocks an enemy slider");
    }
}

void test_irrelevant_attacked_squares() {
    const GeometryCase& queenside = GEOMETRY_CASES[1];

    Position rook_attacked = baseline_position(queenside);
    rook_attacked.put_piece(
      B_ROOK, make_square(FILE_D, RANK_4));
    expect(is_castling_legal(
             rook_attacked, CastlingSide::QUEEN_SIDE),
           "an attacked castling rook does not endanger the king");

    Position extra_square_attacked =
      baseline_position(queenside);
    extra_square_attacked.put_piece(
      B_KNIGHT, make_square(FILE_D, RANK_3));
    expect(is_castling_legal(
             extra_square_attacked,
             CastlingSide::QUEEN_SIDE),
           "an attacked extra queenside clearance square is allowed");
}

void test_independent_sides() {
    Position position;
    position.set_side_to_move(RED);
    position.set_castling_right(
      RED, CastlingSide::KING_SIDE);
    position.set_castling_right(
      RED, CastlingSide::QUEEN_SIDE);
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_1));
    position.put_piece(
      R_ROOK, make_square(FILE_D, RANK_1));
    position.put_piece(
      R_ROOK, make_square(FILE_K, RANK_1));

    expect(is_castling_legal(
             position, CastlingSide::KING_SIDE),
           "kingside can be legal while both rights are present");
    expect(is_castling_legal(
             position, CastlingSide::QUEEN_SIDE),
           "queenside can be legal while both rights are present");

    position.put_piece(
      R_BISHOP, make_square(FILE_I, RANK_1));
    expect(!is_castling_legal(
             position, CastlingSide::KING_SIDE),
           "a kingside blocker prohibits only kingside castling");
    expect(is_castling_legal(
             position, CastlingSide::QUEEN_SIDE),
           "a kingside blocker preserves queenside castling");
}

}  // namespace

int main() {
    test_exact_geometry();
    test_baseline_and_rights();
    test_required_pieces();
    test_every_path_blocker();
    test_enemy_and_friendly_attacks();
    test_slider_blockers();
    test_irrelevant_attacked_squares();
    test_independent_sides();

    if (failures != 0) {
        std::cerr << failures << " castling test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All castling tests passed\n";
    return EXIT_SUCCESS;
}
