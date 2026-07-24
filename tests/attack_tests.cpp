#include "attacks.h"

#include <array>
#include <cstddef>
#include <cstdint>
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

struct RayCase {
    Mockingbird::RayDirection direction;
    int file_step;
    int rank_step;
};

// Coordinate steps provide an implementation-independent definition of each
// ray direction.
constexpr std::array<RayCase, Mockingbird::RAY_DIRECTION_NB> RAY_CASES = {{
  {Mockingbird::RayDirection::NORTH, 0, 1},
  {Mockingbird::RayDirection::NORTH_EAST, 1, 1},
  {Mockingbird::RayDirection::EAST, 1, 0},
  {Mockingbird::RayDirection::SOUTH_EAST, 1, -1},
  {Mockingbird::RayDirection::SOUTH, 0, -1},
  {Mockingbird::RayDirection::SOUTH_WEST, -1, -1},
  {Mockingbird::RayDirection::WEST, -1, 0},
  {Mockingbird::RayDirection::NORTH_WEST, -1, 1},
}};

constexpr std::array<std::size_t, 4> ROOK_RAY_INDICES = {0, 2, 4, 6};
constexpr std::array<std::size_t, 4> BISHOP_RAY_INDICES = {1, 3, 5, 7};

[[nodiscard]] Mockingbird::Bitboard reference_ray_attacks(
  Mockingbird::Square source, const RayCase& ray, const Mockingbird::Bitboard& occupied) {
    using namespace Mockingbird;

    Bitboard attacks;
    if (!is_ok(source))
        return attacks;

    int file = file_of(source) + ray.file_step;
    int rank = rank_of(source) + ray.rank_step;

    while (file >= FILE_A && file <= FILE_N && rank >= RANK_1 && rank <= RANK_14) {
        const Square destination = make_square(File(file), Rank(rank));
        if (!is_ok(destination))
            break;

        attacks.set(destination);
        if (occupied.test(destination))
            break;

        file += ray.file_step;
        rank += ray.rank_step;
    }

    return attacks;
}

template<std::size_t RayCount>
[[nodiscard]] Mockingbird::Bitboard reference_sliding_attacks(
  Mockingbird::Square source,
  const Mockingbird::Bitboard& occupied,
  const std::array<std::size_t, RayCount>& ray_indices) {
    Mockingbird::Bitboard attacks;

    for (const std::size_t index : ray_indices)
        attacks |= reference_ray_attacks(source, RAY_CASES[index], occupied);

    return attacks;
}

[[nodiscard]] Mockingbird::Bitboard reference_rook_attacks(
  Mockingbird::Square source, const Mockingbird::Bitboard& occupied) {
    return reference_sliding_attacks(source, occupied, ROOK_RAY_INDICES);
}

[[nodiscard]] Mockingbird::Bitboard reference_bishop_attacks(
  Mockingbird::Square source, const Mockingbird::Bitboard& occupied) {
    return reference_sliding_attacks(source, occupied, BISHOP_RAY_INDICES);
}

[[nodiscard]] Mockingbird::Bitboard reference_queen_attacks(
  Mockingbird::Square source, const Mockingbird::Bitboard& occupied) {
    return reference_rook_attacks(source, occupied)
         | reference_bishop_attacks(source, occupied);
}

void test_known_king_attacks() {
    using namespace Mockingbird;

    constexpr Square h8 = make_square(FILE_H, RANK_8);
    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square a4 = make_square(FILE_A, RANK_4);
    constexpr Square k14 = make_square(FILE_K, RANK_14);

    static_assert(king_attacks(h8).popcount() == 8);
    static_assert(king_attacks(d1).popcount() == 3);
    static_assert(king_attacks(a4).popcount() == 3);
    static_assert(king_attacks(k14).popcount() == 3);

    expect(king_attacks(d1).test(make_square(FILE_D, RANK_2)), "d1 attacks d2");
    expect(king_attacks(d1).test(make_square(FILE_E, RANK_1)), "d1 attacks e1");
    expect(king_attacks(d1).test(make_square(FILE_E, RANK_2)), "d1 attacks e2");
    expect(!king_attacks(d1).test(make_square(FILE_C, RANK_1)),
           "d1 does not attack cut-out square c1");

    expect(king_attacks(a4).test(make_square(FILE_A, RANK_5)), "a4 attacks a5");
    expect(king_attacks(a4).test(make_square(FILE_B, RANK_4)), "a4 attacks b4");
    expect(king_attacks(a4).test(make_square(FILE_B, RANK_5)), "a4 attacks b5");
    expect(!king_attacks(a4).test(make_square(FILE_A, RANK_3)),
           "a4 does not attack cut-out square a3");
}

void test_known_knight_attacks() {
    using namespace Mockingbird;

    constexpr Square h8 = make_square(FILE_H, RANK_8);
    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square a4 = make_square(FILE_A, RANK_4);
    constexpr Square k14 = make_square(FILE_K, RANK_14);

    static_assert(knight_attacks(h8).popcount() == 8);
    static_assert(knight_attacks(d1).popcount() == 2);
    static_assert(knight_attacks(a4).popcount() == 2);
    static_assert(knight_attacks(k14).popcount() == 2);

    expect(knight_attacks(d1).test(make_square(FILE_E, RANK_3)), "d1 attacks e3");
    expect(knight_attacks(d1).test(make_square(FILE_F, RANK_2)), "d1 attacks f2");
    expect(!knight_attacks(d1).test(make_square(FILE_C, RANK_3)),
           "d1 does not attack cut-out square c3");
    expect(!knight_attacks(d1).test(make_square(FILE_B, RANK_2)),
           "d1 does not attack cut-out square b2");

    expect(knight_attacks(a4).test(make_square(FILE_B, RANK_6)), "a4 attacks b6");
    expect(knight_attacks(a4).test(make_square(FILE_C, RANK_5)), "a4 attacks c5");
    expect(!knight_attacks(a4).test(make_square(FILE_C, RANK_3)),
           "a4 does not attack cut-out square c3");
}

void test_known_pawn_attacks() {
    using namespace Mockingbird;

    constexpr Square h8 = make_square(FILE_H, RANK_8);

    static_assert(pawn_attacks(RED, h8).popcount() == 2);
    static_assert(pawn_attacks(BLUE, h8).popcount() == 2);
    static_assert(pawn_attacks(YELLOW, h8).popcount() == 2);
    static_assert(pawn_attacks(GREEN, h8).popcount() == 2);

    expect(pawn_attacks(RED, h8).test(make_square(FILE_G, RANK_9)),
           "red pawn on h8 attacks g9");
    expect(pawn_attacks(RED, h8).test(make_square(FILE_I, RANK_9)),
           "red pawn on h8 attacks i9");

    expect(pawn_attacks(BLUE, h8).test(make_square(FILE_I, RANK_9)),
           "blue pawn on h8 attacks i9");
    expect(pawn_attacks(BLUE, h8).test(make_square(FILE_I, RANK_7)),
           "blue pawn on h8 attacks i7");

    expect(pawn_attacks(YELLOW, h8).test(make_square(FILE_I, RANK_7)),
           "yellow pawn on h8 attacks i7");
    expect(pawn_attacks(YELLOW, h8).test(make_square(FILE_G, RANK_7)),
           "yellow pawn on h8 attacks g7");

    expect(pawn_attacks(GREEN, h8).test(make_square(FILE_G, RANK_7)),
           "green pawn on h8 attacks g7");
    expect(pawn_attacks(GREEN, h8).test(make_square(FILE_G, RANK_9)),
           "green pawn on h8 attacks g9");

    constexpr Square d1 = make_square(FILE_D, RANK_1);
    static_assert(pawn_attacks(RED, d1).popcount() == 1);
    static_assert(pawn_attacks(BLUE, d1).popcount() == 1);
    static_assert(pawn_attacks(YELLOW, d1).empty());
    static_assert(pawn_attacks(GREEN, d1).empty());

    expect(pawn_attacks(RED, d1).test(make_square(FILE_E, RANK_2)),
           "red pawn on d1 attacks e2");
    expect(pawn_attacks(BLUE, d1).test(make_square(FILE_E, RANK_2)),
           "blue pawn on d1 attacks e2");
    expect(!pawn_attacks(RED, d1).test(make_square(FILE_C, RANK_2)),
           "red pawn on d1 does not attack cut-out square c2");
}

void test_known_sliding_attacks() {
    using namespace Mockingbird;

    constexpr Square h8 = make_square(FILE_H, RANK_8);
    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square a4 = make_square(FILE_A, RANK_4);

    static_assert(rook_attacks(h8).popcount() == 26);
    static_assert(bishop_attacks(h8).popcount() == 15);
    static_assert(queen_attacks(h8).popcount() == 41);
    static_assert(rook_attacks(d1).popcount() == 20);
    static_assert(bishop_attacks(d1).popcount() == 10);
    static_assert(queen_attacks(d1).popcount() == 30);

    expect(!rook_attacks(d1).test(make_square(FILE_C, RANK_1)),
           "rook ray stops at the lower-left cut-out corner");
    expect(!rook_attacks(d1).test(make_square(FILE_L, RANK_1)),
           "rook ray stops at the lower-right cut-out corner");
    expect(!bishop_attacks(a4).test(make_square(FILE_B, RANK_3)),
           "bishop ray stops at the lower-left cut-out corner");

    Bitboard occupied;
    occupied.set(make_square(FILE_H, RANK_10));
    occupied.set(make_square(FILE_H, RANK_12));
    occupied.set(make_square(FILE_F, RANK_8));
    occupied.set(make_square(FILE_D, RANK_8));
    occupied.set(make_square(FILE_J, RANK_10));
    occupied.set(make_square(FILE_L, RANK_12));

    const Bitboard rook = rook_attacks(h8, occupied);
    expect(rook.test(make_square(FILE_H, RANK_10)), "rook attacks its north blocker");
    expect(!rook.test(make_square(FILE_H, RANK_11)),
           "north blocker hides squares behind it");
    expect(rook.test(make_square(FILE_F, RANK_8)), "rook attacks its west blocker");
    expect(!rook.test(make_square(FILE_E, RANK_8)),
           "west blocker hides squares behind it");

    const Bitboard bishop = bishop_attacks(h8, occupied);
    expect(bishop.test(make_square(FILE_J, RANK_10)),
           "bishop attacks its north-east blocker");
    expect(!bishop.test(make_square(FILE_K, RANK_11)),
           "diagonal blocker hides squares behind it");

    expect(queen_attacks(h8, occupied) == (rook | bishop),
           "queen attacks combine blocked rook and bishop rays");
}

void test_all_attack_tables() {
    using namespace Mockingbird;

    for (int source_index = 0; source_index < SQUARE_NB; ++source_index) {
        const Square source = Square(source_index);
        const Bitboard king = king_attacks(source);
        const Bitboard knight = knight_attacks(source);

        if (!is_ok(source)) {
            expect(king.empty(), "non-playable king source has no attacks");
            expect(knight.empty(), "non-playable knight source has no attacks");
            continue;
        }

        expect(king.popcount() <= 8, "king attack count does not exceed eight");
        expect(knight.popcount() <= 8, "knight attack count does not exceed eight");
        expect(!king.test(source), "king does not attack its source square");
        expect(!knight.test(source), "knight does not attack its source square");

        Bitboard remaining_king_attacks = king;
        while (remaining_king_attacks) {
            const Square destination = remaining_king_attacks.pop_lsb();
            expect(is_ok(destination), "king destination is playable");
            expect(king_attacks(destination).test(source), "king attacks are symmetric");
        }

        Bitboard remaining_knight_attacks = knight;
        while (remaining_knight_attacks) {
            const Square destination = remaining_knight_attacks.pop_lsb();
            expect(is_ok(destination), "knight destination is playable");
            expect(knight_attacks(destination).test(source), "knight attacks are symmetric");
        }
    }
}

void test_all_pawn_attack_tables() {
    using namespace Mockingbird;

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);

        for (int source_index = 0; source_index < SQUARE_NB; ++source_index) {
            const Square source = Square(source_index);
            const Bitboard attacks = pawn_attacks(color, source);

            if (!is_ok(source)) {
                expect(attacks.empty(), "non-playable pawn source has no attacks");
                continue;
            }

            expect(attacks.popcount() <= 2, "pawn attack count does not exceed two");
            expect(!attacks.test(source), "pawn does not attack its source square");

            Bitboard remaining_attacks = attacks;
            while (remaining_attacks) {
                const Square destination = remaining_attacks.pop_lsb();
                expect(is_ok(destination), "pawn destination is playable");
            }
        }
    }
}

void test_geometric_equivalence() {
    using namespace Mockingbird;

    // Compares all 65,536 mailbox source/destination pairs with coordinate
    // distance definitions for king and knight moves.
    for (int source_index = 0; source_index < SQUARE_NB; ++source_index) {
        const Square source = Square(source_index);

        for (int destination_index = 0; destination_index < SQUARE_NB; ++destination_index) {
            const Square destination = Square(destination_index);

            bool expected_king_attack = false;
            bool expected_knight_attack = false;

            if (is_ok(source) && is_ok(destination)) {
                const int file_distance = std::abs(file_of(source) - file_of(destination));
                const int rank_distance = std::abs(rank_of(source) - rank_of(destination));

                expected_king_attack =
                  (file_distance <= 1 && rank_distance <= 1)
                  && (file_distance != 0 || rank_distance != 0);
                expected_knight_attack =
                  (file_distance == 1 && rank_distance == 2)
                  || (file_distance == 2 && rank_distance == 1);
            }

            expect(king_attacks(source).test(destination) == expected_king_attack,
                   "king table matches coordinate distance");
            expect(knight_attacks(source).test(destination) == expected_knight_attack,
                   "knight table matches coordinate distance");
        }
    }
}

void test_all_ray_tables() {
    using namespace Mockingbird;

    const Bitboard occupied;

    // Every source and direction is compared with coordinate stepping. This
    // includes the complete boundary of all four cut-out corners.
    for (int source_index = 0; source_index < SQUARE_NB; ++source_index) {
        const Square source = Square(source_index);

        for (const RayCase& ray : RAY_CASES) {
            expect(ray_attacks(ray.direction, source)
                     == reference_ray_attacks(source, ray, occupied),
                   "ray table matches coordinate stepping");
        }

        expect(rook_attacks(source) == reference_rook_attacks(source, occupied),
               "empty-board rook attacks match coordinate stepping");
        expect(bishop_attacks(source) == reference_bishop_attacks(source, occupied),
               "empty-board bishop attacks match coordinate stepping");
        expect(queen_attacks(source) == reference_queen_attacks(source, occupied),
               "empty-board queen attacks match coordinate stepping");
    }
}

void test_all_directional_blocker_subsets() {
    using namespace Mockingbird;

    // A ray can contain at most thirteen destinations on the 14x14 board.
    std::array<Square, BOARD_FILES - 1> destinations{};

    for (int source_index = 0; source_index < SQUARE_NB; ++source_index) {
        const Square source = Square(source_index);

        for (const RayCase& ray : RAY_CASES) {
            std::size_t destination_count = 0;

            if (is_ok(source)) {
                int file = file_of(source) + ray.file_step;
                int rank = rank_of(source) + ray.rank_step;

                while (file >= FILE_A && file <= FILE_N && rank >= RANK_1
                       && rank <= RANK_14) {
                    const Square destination = make_square(File(file), Rank(rank));
                    if (!is_ok(destination))
                        break;

                    destinations[destination_count++] = destination;
                    file += ray.file_step;
                    rank += ray.rank_step;
                }
            }

            // Enumerates all 2^n occupancy subsets for this directional ray.
            const std::uint32_t subset_count = std::uint32_t{1} << destination_count;
            for (std::uint32_t subset = 0; subset < subset_count; ++subset) {
                Bitboard occupied;

                for (std::size_t index = 0; index < destination_count; ++index) {
                    if ((subset & (std::uint32_t{1} << index)) != 0)
                        occupied.set(destinations[index]);
                }

                expect(ray_attacks(ray.direction, source, occupied)
                         == reference_ray_attacks(source, ray, occupied),
                       "blocked ray matches coordinate stepping");
            }
        }
    }
}

void test_all_single_blockers() {
    using namespace Mockingbird;

    // Tests every mailbox source against a blocker on every mailbox index.
    // Padding and cut-out blocker bits must not affect a playable ray.
    for (int source_index = 0; source_index < SQUARE_NB; ++source_index) {
        const Square source = Square(source_index);

        for (int blocker_index = 0; blocker_index < SQUARE_NB; ++blocker_index) {
            Bitboard occupied;
            occupied.set(Square(blocker_index));

            expect(rook_attacks(source, occupied)
                     == reference_rook_attacks(source, occupied),
                   "rook attacks match every single-blocker position");
            expect(bishop_attacks(source, occupied)
                     == reference_bishop_attacks(source, occupied),
                   "bishop attacks match every single-blocker position");
            expect(queen_attacks(source, occupied)
                     == reference_queen_attacks(source, occupied),
                   "queen attacks match every single-blocker position");
        }
    }
}

void test_pawn_geometric_equivalence() {
    using namespace Mockingbird;

    // Compares all source/destination pairs for all four colors with coordinate
    // distance definitions for pawn captures.
    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);

        for (int source_index = 0; source_index < SQUARE_NB; ++source_index) {
            const Square source = Square(source_index);

            for (int destination_index = 0; destination_index < SQUARE_NB;
                 ++destination_index) {
                const Square destination = Square(destination_index);
                bool expected_attack = false;

                if (is_ok(source) && is_ok(destination)) {
                    const int file_delta = file_of(destination) - file_of(source);
                    const int rank_delta = rank_of(destination) - rank_of(source);

                    expected_attack =
                      color == RED    ? rank_delta == 1 && std::abs(file_delta) == 1
                      : color == BLUE ? file_delta == 1 && std::abs(rank_delta) == 1
                      : color == YELLOW
                        ? rank_delta == -1 && std::abs(file_delta) == 1
                        : file_delta == -1 && std::abs(rank_delta) == 1;
                }

                expect(pawn_attacks(color, source).test(destination) == expected_attack,
                       "pawn table matches color-relative coordinate distance");
            }
        }
    }
}

}  // namespace

int main() {
    test_known_king_attacks();
    test_known_knight_attacks();
    test_known_pawn_attacks();
    test_known_sliding_attacks();
    test_all_attack_tables();
    test_all_pawn_attack_tables();
    test_geometric_equivalence();
    test_all_ray_tables();
    test_all_directional_blocker_subsets();
    test_all_single_blockers();
    test_pawn_geometric_equivalence();

    if (failures != 0) {
        std::cerr << failures << " attack-table test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All attack-table tests passed\n";
    return EXIT_SUCCESS;
}
