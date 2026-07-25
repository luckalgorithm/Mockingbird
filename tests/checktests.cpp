#include "checks.h"

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

using namespace Mockingbird;

inline constexpr std::array<Team, TEAM_NB> TEAMS = {
  RED_YELLOW,
  BLUE_GREEN,
};

inline constexpr std::array<Direction, 4> ROOK_DIRECTIONS = {
  NORTH,
  EAST,
  SOUTH,
  WEST,
};

inline constexpr std::array<Direction, 4> BISHOP_DIRECTIONS = {
  NORTH_EAST,
  SOUTH_EAST,
  SOUTH_WEST,
  NORTH_WEST,
};

[[nodiscard]] constexpr int absolute(int value) noexcept {
    return value < 0 ? -value : value;
}

template<std::size_t DirectionCount>
[[nodiscard]] constexpr bool reference_slider_attacks(
  Square source,
  Square target,
  const Bitboard& occupied,
  const std::array<Direction, DirectionCount>& directions) noexcept {
    for (const Direction direction : directions) {
        for (Square square = source + direction;
             is_ok(square);
             square = square + direction) {
            if (square == target)
                return true;

            if (occupied.test(square))
                break;
        }
    }

    return false;
}

// This source-oriented oracle uses coordinate deltas and mailbox ray walks.
// It does not use the target-oriented attack lookup performed by checks.h.
[[nodiscard]] constexpr bool reference_piece_attacks(
  Piece piece,
  Square source,
  Square target,
  const Bitboard& occupied) noexcept {
    const int file_delta =
      int(file_of(target)) - int(file_of(source));
    const int rank_delta =
      int(rank_of(target)) - int(rank_of(source));

    switch (type_of(piece)) {
        case PAWN: {
            const Color color = color_of(piece);
            return color == RED
                     ? rank_delta == 1 && absolute(file_delta) == 1
                 : color == BLUE
                     ? file_delta == 1 && absolute(rank_delta) == 1
                 : color == YELLOW
                     ? rank_delta == -1 && absolute(file_delta) == 1
                     : file_delta == -1 && absolute(rank_delta) == 1;
        }

        case KNIGHT:
            return (absolute(file_delta) == 1
                    && absolute(rank_delta) == 2)
                || (absolute(file_delta) == 2
                    && absolute(rank_delta) == 1);

        case BISHOP:
            return reference_slider_attacks(
              source, target, occupied, BISHOP_DIRECTIONS);

        case ROOK:
            return reference_slider_attacks(
              source, target, occupied, ROOK_DIRECTIONS);

        case QUEEN:
            return reference_slider_attacks(
                     source, target, occupied, ROOK_DIRECTIONS)
                || reference_slider_attacks(
                     source, target, occupied, BISHOP_DIRECTIONS);

        case KING:
            return absolute(file_delta) <= 1
                && absolute(rank_delta) <= 1
                && (file_delta != 0 || rank_delta != 0);

        case NO_PIECE_TYPE:
        case PIECE_TYPE_NB:
            return false;
    }

    return false;
}

[[nodiscard]] constexpr Bitboard reference_attackers_to(
  const Position& position,
  Square target,
  const Bitboard& occupied) noexcept {
    Bitboard attackers;

    for (int source_index = 0;
         source_index < SQUARE_NB;
         ++source_index) {
        const Square source = Square(source_index);
        if (!is_ok(source) || position.empty(source))
            continue;

        const Piece piece = position.piece_on(source);
        if (reference_piece_attacks(
              piece, source, target, occupied))
            attackers.set(source);
    }

    return attackers;
}

[[nodiscard]] constexpr Square rotate_clockwise(
  Square square) noexcept {
    return make_square(
      File(int(rank_of(square))),
      Rank(BOARD_FILES + 1 - int(file_of(square))));
}

[[nodiscard]] constexpr Bitboard rotate_clockwise(
  Bitboard bitboard) noexcept {
    Bitboard rotated;

    while (bitboard)
        rotated.set(rotate_clockwise(bitboard.pop_lsb()));

    return rotated;
}

[[nodiscard]] constexpr Position rotate_clockwise(
  const Position& position) noexcept {
    Position rotated;
    rotated.set_side_to_move(
      next_color(position.side_to_move()));

    for (int square_index = 0;
         square_index < SQUARE_NB;
         ++square_index) {
        const Square square = Square(square_index);
        if (!is_ok(square) || position.empty(square))
            continue;

        const Piece piece = position.piece_on(square);
        rotated.put_piece(
          make_piece(
            next_color(color_of(piece)),
            type_of(piece)),
          rotate_clockwise(square));
    }

    return rotated;
}

[[nodiscard]] constexpr Bitboard square_bitboard(
  Square square) noexcept {
    return Bitboard::from_square(square);
}

[[nodiscard]] constexpr bool constexpr_check_cases() noexcept {
    const Square h8 = make_square(FILE_H, RANK_8);
    const Square h11 = make_square(FILE_H, RANK_11);
    const Square f9 = make_square(FILE_F, RANK_9);

    Position position;
    position.put_piece(R_KING, h8);
    position.put_piece(B_ROOK, h11);
    position.put_piece(Y_KNIGHT, f9);

    Bitboard all_attackers;
    all_attackers.set(h11);
    all_attackers.set(f9);

    return attackers_to(position, h8) == all_attackers
        && checkers(position, RED) == square_bitboard(h11)
        && in_check(position, RED)
        && in_check(position)
        && is_square_attacked_by_team(
             position, h8, BLUE_GREEN)
        && is_square_attacked_by_team(
             position, h8, RED_YELLOW);
}

static_assert(constexpr_check_cases());

void test_exhaustive_single_attackers() {
    bool exact_results = true;
    bool team_results = true;
    bool rotational_results = true;
    std::size_t tested_positions = 0;

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);

        for (int type_index = PAWN;
             type_index <= KING;
             ++type_index) {
            const PieceType piece_type =
              PieceType(type_index);
            const Piece piece =
              make_piece(color, piece_type);

            for (int source_index = 0;
                 source_index < SQUARE_NB;
                 ++source_index) {
                const Square source = Square(source_index);
                if (!is_ok(source))
                    continue;

                Position position;
                position.put_piece(piece, source);

                Position rotated;
                const Square rotated_source =
                  rotate_clockwise(source);
                rotated.put_piece(
                  make_piece(
                    next_color(color), piece_type),
                  rotated_source);

                for (int target_index = 0;
                     target_index < SQUARE_NB;
                     ++target_index) {
                    const Square target =
                      Square(target_index);
                    if (!is_ok(target))
                        continue;

                    const bool attacks =
                      reference_piece_attacks(
                        piece,
                        source,
                        target,
                        position.occupied());
                    Bitboard expected;
                    if (attacks)
                        expected.set(source);

                    const Bitboard actual =
                      attackers_to(position, target);
                    if (actual != expected
                        || attackers_to(
                             position,
                             target,
                             position.occupied())
                             != expected)
                        exact_results = false;

                    for (const Team team : TEAMS) {
                        Bitboard expected_team;
                        const bool team_attacks =
                          attacks && team_of(color) == team;
                        if (team_attacks)
                            expected_team.set(source);

                        if (attackers_to(
                              position, target, team)
                              != expected_team
                            || attackers_to(
                                 position,
                                 target,
                                 team,
                                 position.occupied())
                                 != expected_team
                            || is_square_attacked_by_team(
                                 position, target, team)
                                 != team_attacks
                            || is_square_attacked_by_team(
                                 position,
                                 target,
                                 team,
                                 position.occupied())
                                 != team_attacks)
                            team_results = false;
                    }

                    const Square rotated_target =
                      rotate_clockwise(target);
                    if (rotate_clockwise(actual)
                        != attackers_to(
                             rotated, rotated_target))
                        rotational_results = false;

                    ++tested_positions;
                }
            }
        }
    }

    constexpr std::size_t expected_positions =
      std::size_t(COLOR_NB)
      * std::size_t(KING - PAWN + 1)
      * std::size_t(PLAYABLE_SQUARE_NB)
      * std::size_t(PLAYABLE_SQUARE_NB);

    expect(
      tested_positions == expected_positions,
      "all 614,400 single-piece source and target cases are tested");
    expect(
      exact_results,
      "single-piece attacker bitboards match the source-oriented oracle");
    expect(
      team_results,
      "team-filtered bitboards and boolean queries match the oracle");
    expect(
      rotational_results,
      "quarter-turn rotation preserves every single-piece attack query");
}

template<std::size_t DirectionCount>
[[nodiscard]] bool test_blockers_for_slider(
  PieceType piece_type,
  const std::array<Direction, DirectionCount>& directions,
  std::size_t& tested_blockers) {
    bool passed = true;

    for (int target_index = 0;
         target_index < SQUARE_NB;
         ++target_index) {
        const Square target = Square(target_index);
        if (!is_ok(target))
            continue;

        for (const Direction direction : directions) {
            std::array<Square, BOARD_FILES> ray{};
            std::size_t ray_size = 0;

            for (Square square = target + direction;
                 is_ok(square);
                 square = square + direction)
                ray[ray_size++] = square;

            if (ray_size < 2)
                continue;

            const Square attacker = ray[ray_size - 1];
            const Piece piece =
              make_piece(BLUE, piece_type);
            Position position;
            position.put_piece(piece, attacker);

            if (!attackers_to(position, target).test(attacker)
                || !is_square_attacked_by_team(
                     position, target, BLUE_GREEN))
                passed = false;

            for (std::size_t blocker_index = 0;
                 blocker_index + 1 < ray_size;
                 ++blocker_index) {
                Bitboard occupied = position.occupied();
                occupied.set(ray[blocker_index]);

                if (attackers_to(
                      position, target, occupied)
                      .test(attacker)
                    || is_square_attacked_by_team(
                         position,
                         target,
                         BLUE_GREEN,
                         occupied)
                    || reference_piece_attacks(
                         piece,
                         attacker,
                         target,
                         occupied))
                    passed = false;

                ++tested_blockers;
            }
        }
    }

    return passed;
}

void test_sliding_blockers() {
    std::size_t tested_blockers = 0;
    bool passed = true;

    passed =
      test_blockers_for_slider(
        ROOK, ROOK_DIRECTIONS, tested_blockers)
      && passed;
    passed =
      test_blockers_for_slider(
        BISHOP, BISHOP_DIRECTIONS, tested_blockers)
      && passed;
    passed =
      test_blockers_for_slider(
        QUEEN, ROOK_DIRECTIONS, tested_blockers)
      && passed;
    passed =
      test_blockers_for_slider(
        QUEEN, BISHOP_DIRECTIONS, tested_blockers)
      && passed;

    expect(
      tested_blockers > 0,
      "the blocker matrix contains intermediate ray squares");
    expect(
      passed,
      "every intermediate blocker hides a farther rook, bishop, or queen");
}

void test_cutout_boundaries() {
    struct BoundaryCase {
        Square source;
        Square target;
        bool attacks;
    };

    const std::array<BoundaryCase, 5> cases = {{
      {
        make_square(FILE_D, RANK_1),
        make_square(FILE_A, RANK_4),
        false,
      },
      {
        make_square(FILE_D, RANK_14),
        make_square(FILE_A, RANK_11),
        false,
      },
      {
        make_square(FILE_K, RANK_14),
        make_square(FILE_N, RANK_11),
        false,
      },
      {
        make_square(FILE_K, RANK_1),
        make_square(FILE_N, RANK_4),
        false,
      },
      {
        make_square(FILE_D, RANK_4),
        make_square(FILE_A, RANK_7),
        true,
      },
    }};

    bool passed = true;
    for (const BoundaryCase& test_case : cases) {
        Position position;
        position.put_piece(B_BISHOP, test_case.source);

        const bool actual =
          attackers_to(position, test_case.target)
            .test(test_case.source);
        const bool reference =
          reference_piece_attacks(
            B_BISHOP,
            test_case.source,
            test_case.target,
            position.occupied());

        if (actual != test_case.attacks
            || reference != test_case.attacks)
            passed = false;
    }

    expect(
      passed,
      "diagonal attacks stop at cut-out corners and continue along playable edges");
}

void test_pawn_reverse_mapping() {
    const Square target =
      make_square(FILE_H, RANK_8);
    const std::array<Square, COLOR_NB> sources = {
      make_square(FILE_G, RANK_7),
      make_square(FILE_G, RANK_9),
      make_square(FILE_I, RANK_9),
      make_square(FILE_I, RANK_7),
    };

    Position position;
    Bitboard expected;

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Square source =
          sources[std::size_t(color_index)];
        position.put_piece(
          make_piece(Color(color_index), PAWN),
          source);
        expected.set(source);
    }

    Bitboard red_yellow;
    red_yellow.set(sources[RED]);
    red_yellow.set(sources[YELLOW]);
    Bitboard blue_green;
    blue_green.set(sources[BLUE]);
    blue_green.set(sources[GREEN]);

    expect(
      attackers_to(position, target) == expected,
      "all four pawn directions map a target to their source squares");
    expect(
      attackers_to(position, target, RED_YELLOW)
        == red_yellow,
      "Red and Yellow pawn attackers form one team result");
    expect(
      attackers_to(position, target, BLUE_GREEN)
        == blue_green,
      "Blue and Green pawn attackers form one team result");
}

void test_occupancy_contract() {
    const Square h8 = make_square(FILE_H, RANK_8);
    const Square h9 = make_square(FILE_H, RANK_9);
    const Square h11 = make_square(FILE_H, RANK_11);

    Position blocked;
    blocked.put_piece(B_ROOK, h11);
    blocked.put_piece(R_KNIGHT, h9);

    expect(
      !attackers_to(blocked, h8, BLUE_GREEN).test(h11),
      "a position blocker hides a farther rook");

    Bitboard opened = blocked.occupied();
    opened.clear(h9);
    expect(
      attackers_to(
        blocked, h8, BLUE_GREEN, opened)
        == square_bitboard(h11),
      "clearing a blocker from supplied occupancy opens the rook ray");

    Position knight;
    const Square f7 = make_square(FILE_F, RANK_7);
    knight.put_piece(B_KNIGHT, f7);
    expect(
      attackers_to(knight, h8, Bitboard{})
        == square_bitboard(f7),
      "supplied occupancy does not remove fixed-distance attacker sources");

    Position king_path;
    const Square h6 = make_square(FILE_H, RANK_6);
    const Square h7 = make_square(FILE_H, RANK_7);
    king_path.put_piece(R_KING, h7);
    king_path.put_piece(B_ROOK, h11);

    expect(
      !is_square_attacked_by_team(
        king_path, h6, BLUE_GREEN),
      "the king source blocks the rook ray before the king moves");
    Bitboard king_vacated = king_path.occupied();
    king_vacated.clear(h7);
    expect(
      is_square_attacked_by_team(
        king_path,
        h6,
        BLUE_GREEN,
        king_vacated),
      "vacating the king source opens the rook ray to the destination");

    const Square d8 = make_square(FILE_D, RANK_8);
    const Square g8 = make_square(FILE_G, RANK_8);
    const Square h9_ep = make_square(FILE_H, RANK_9);
    const Square i9 = make_square(FILE_I, RANK_9);
    const Square n8 = make_square(FILE_N, RANK_8);

    Position en_passant;
    en_passant.put_piece(R_KING, d8);
    en_passant.put_piece(R_PAWN, g8);
    en_passant.put_piece(B_PAWN, i9);
    en_passant.put_piece(B_ROOK, n8);
    en_passant.set_en_passant_square(BLUE, h9_ep);

    expect(
      !is_square_attacked_by_team(
        en_passant, d8, BLUE_GREEN),
      "the capturing pawn blocks the rook before en passant");
    Bitboard en_passant_occupancy =
      en_passant.occupied();
    en_passant_occupancy.clear(g8);
    en_passant_occupancy.clear(i9);
    en_passant_occupancy.set(h9_ep);
    expect(
      attackers_to(
        en_passant,
        d8,
        BLUE_GREEN,
        en_passant_occupancy)
        .test(n8),
      "the en-passant occupancy change opens the rook ray");
}

[[nodiscard]] Square find_attack_source(
  Piece piece,
  Square target) {
    for (int source_index = 0;
         source_index < SQUARE_NB;
         ++source_index) {
        const Square source = Square(source_index);
        if (!is_ok(source) || source == target)
            continue;

        Bitboard occupied;
        occupied.set(source);
        occupied.set(target);
        if (reference_piece_attacks(
              piece, source, target, occupied))
            return source;
    }

    return SQ_NONE;
}

void test_checker_piece_types_and_teams() {
    const Square king_square =
      make_square(FILE_H, RANK_8);
    bool sources_exist = true;
    bool exact_results = true;
    std::size_t tested_cases = 0;

    for (int king_index = 0;
         king_index < COLOR_NB;
         ++king_index) {
        const Color king_color = Color(king_index);
        const std::array<Color, 3> attacker_colors = {
          next_color(king_color),
          previous_color(king_color),
          next_color(next_color(king_color)),
        };

        for (const Color attacker_color :
             attacker_colors) {
            for (int type_index = PAWN;
                 type_index <= KING;
                 ++type_index) {
                const Piece attacker =
                  make_piece(
                    attacker_color,
                    PieceType(type_index));
                const Square source =
                  find_attack_source(
                    attacker, king_square);
                if (source == SQ_NONE) {
                    sources_exist = false;
                    continue;
                }

                Position position;
                position.set_side_to_move(king_color);
                position.put_piece(
                  make_piece(king_color, KING),
                  king_square);
                position.put_piece(attacker, source);

                Bitboard expected;
                const bool is_opponent =
                  team_of(attacker_color)
                  != team_of(king_color);
                if (is_opponent)
                    expected.set(source);

                if (checkers(position, king_color)
                      != expected
                    || in_check(position, king_color)
                         != is_opponent
                    || in_check(position)
                         != is_opponent)
                    exact_results = false;

                ++tested_cases;
            }
        }
    }

    expect(
      sources_exist,
      "every color and piece type has an attack source for the central king");
    expect(
      tested_cases
        == std::size_t(COLOR_NB) * 3U
             * std::size_t(KING - PAWN + 1),
      "both opponents and the teammate are tested for every king and piece type");
    expect(
      exact_results,
      "checkers include both opponents and exclude the king's teammate");
}

[[nodiscard]] constexpr Position mixed_check_position() noexcept {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_8));
    position.put_piece(
      B_ROOK, make_square(FILE_H, RANK_11));
    position.put_piece(
      B_KNIGHT, make_square(FILE_F, RANK_7));
    position.put_piece(
      G_BISHOP, make_square(FILE_K, RANK_11));
    position.put_piece(
      Y_KNIGHT, make_square(FILE_F, RANK_9));
    position.put_piece(
      R_ROOK, make_square(FILE_H, RANK_5));
    return position;
}

void test_checker_counts_and_rotation() {
    const Square h8 = make_square(FILE_H, RANK_8);
    const Square h11 = make_square(FILE_H, RANK_11);
    const Square f7 = make_square(FILE_F, RANK_7);
    const Square k11 = make_square(FILE_K, RANK_11);
    const Square f9 = make_square(FILE_F, RANK_9);
    const Square h5 = make_square(FILE_H, RANK_5);

    Position position = mixed_check_position();
    Bitboard all_expected;
    all_expected.set(h11);
    all_expected.set(f7);
    all_expected.set(k11);
    all_expected.set(f9);
    all_expected.set(h5);
    Bitboard checkers_expected;
    checkers_expected.set(h11);
    checkers_expected.set(f7);
    checkers_expected.set(k11);

    Color king_color = RED;
    Square king_square = h8;
    bool rotation_passed = true;

    for (int rotation = 0; rotation < COLOR_NB; ++rotation) {
        if (attackers_to(position, king_square)
              != all_expected
            || reference_attackers_to(
                 position,
                 king_square,
                 position.occupied())
                 != all_expected
            || checkers(position, king_color)
                 != checkers_expected
            || !in_check(position, king_color)
            || !in_check(position))
            rotation_passed = false;

        position = rotate_clockwise(position);
        king_color = next_color(king_color);
        king_square = rotate_clockwise(king_square);
        all_expected =
          rotate_clockwise(all_expected);
        checkers_expected =
          rotate_clockwise(checkers_expected);
    }

    expect(
      rotation_passed,
      "all attackers and checkers rotate through all four colors");

    Position reduced = mixed_check_position();
    reduced.remove_piece(h11);
    expect(
      checkers(reduced, RED).popcount() == 2,
      "removing one of three opposing attackers leaves double check");
    reduced.remove_piece(f7);
    expect(
      checkers(reduced, RED) == square_bitboard(k11),
      "removing two opposing attackers leaves one checker");
    reduced.remove_piece(k11);
    expect(
      checkers(reduced, RED).empty()
        && !in_check(reduced, RED),
      "friendly attackers do not keep the king in check");
}

void test_nonunique_king_state() {
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        Position missing;
        missing.set_side_to_move(color);

        expect(
          checkers(missing, color).empty()
            && !in_check(missing, color)
            && !in_check(missing),
          "a missing king has no unique check target");
    }

    Position duplicate;
    duplicate.set_side_to_move(RED);
    duplicate.put_piece(
      R_KING, make_square(FILE_H, RANK_8));
    duplicate.put_piece(
      R_KING, make_square(FILE_I, RANK_8));
    duplicate.put_piece(
      B_ROOK, make_square(FILE_H, RANK_11));

    expect(
      checkers(duplicate, RED).empty()
        && !in_check(duplicate, RED)
        && !in_check(duplicate),
      "duplicate same-color kings have no unique check target");
}

}  // namespace

int main() {
    test_exhaustive_single_attackers();
    test_sliding_blockers();
    test_cutout_boundaries();
    test_pawn_reverse_mapping();
    test_occupancy_contract();
    test_checker_piece_types_and_teams();
    test_checker_counts_and_rotation();
    test_nonunique_king_state();

    if (failures != 0) {
        std::cerr << failures
                  << " check-layer test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All check-layer tests passed\n";
    return EXIT_SUCCESS;
}
