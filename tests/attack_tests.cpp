#include "attacks.h"

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
    test_all_attack_tables();
    test_all_pawn_attack_tables();
    test_geometric_equivalence();
    test_pawn_geometric_equivalence();

    if (failures != 0) {
        std::cerr << failures << " attack-table test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All attack-table tests passed\n";
    return EXIT_SUCCESS;
}
