#include "pawns.h"

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

[[nodiscard]] constexpr int forward_coordinate(
  Mockingbird::Color color, Mockingbird::Square square) {
    using namespace Mockingbird;

    return color == RED    ? rank_of(square)
         : color == BLUE   ? file_of(square)
         : color == YELLOW ? BOARD_RANKS + 1 - rank_of(square)
                           : BOARD_FILES + 1 - file_of(square);
}

[[nodiscard]] constexpr Mockingbird::Square expected_push_destination(
  Mockingbird::Color color, Mockingbird::Square source, int distance) {
    using namespace Mockingbird;

    if (!is_ok(source))
        return SQ_NONE;
    if (distance == 2 && forward_coordinate(color, source) != 2)
        return SQ_NONE;

    const int file_step = color == BLUE ? 1 : color == GREEN ? -1 : 0;
    const int rank_step = color == RED ? 1 : color == YELLOW ? -1 : 0;

    int file = file_of(source);
    int rank = rank_of(source);

    for (int step = 0; step < distance; ++step) {
        file += file_step;
        rank += rank_step;

        if (file < FILE_A || file > FILE_N || rank < RANK_1 || rank > RANK_14)
            return SQ_NONE;
        if (!is_ok(make_square(File(file), Rank(rank))))
            return SQ_NONE;
    }

    return make_square(File(file), Rank(rank));
}

[[nodiscard]] constexpr Mockingbird::Square rotate_clockwise(
  Mockingbird::Square square) {
    using namespace Mockingbird;

    return make_square(
      File(rank_of(square)), Rank(BOARD_FILES + 1 - file_of(square)));
}

[[nodiscard]] constexpr Mockingbird::Square rotate_destination(
  Mockingbird::Square square) {
    return square == Mockingbird::SQ_NONE ? Mockingbird::SQ_NONE
                                          : rotate_clockwise(square);
}

[[nodiscard]] constexpr bool constexpr_pawn_geometry() {
    using namespace Mockingbird;

    constexpr Square d2 = make_square(FILE_D, RANK_2);
    constexpr Square d3 = make_square(FILE_D, RANK_3);
    constexpr Square d4 = make_square(FILE_D, RANK_4);

    return is_pawn_start_square(RED, d2)
        && pawn_push_destination(RED, d2) == d3
        && pawn_double_push_destination(RED, d2) == d4
        && is_pawn_promotion_square(YELLOW, d4);
}

static_assert(constexpr_pawn_geometry());

void test_known_starting_zones() {
    using namespace Mockingbird;

    constexpr Square red = make_square(FILE_D, RANK_2);
    constexpr Square blue = make_square(FILE_B, RANK_4);
    constexpr Square yellow = make_square(FILE_D, RANK_13);
    constexpr Square green = make_square(FILE_M, RANK_4);

    expect(is_pawn_start_square(RED, red), "Red starts on rank 2");
    expect(is_pawn_start_square(BLUE, blue), "Blue starts on file b");
    expect(is_pawn_start_square(YELLOW, yellow), "Yellow starts on rank 13");
    expect(is_pawn_start_square(GREEN, green), "Green starts on file m");

    expect(PAWN_START_SQUARES[RED].popcount() == 8,
           "Red has eight pawn starting squares");
    expect(PAWN_START_SQUARES[BLUE].popcount() == 8,
           "Blue has eight pawn starting squares");
    expect(PAWN_START_SQUARES[YELLOW].popcount() == 8,
           "Yellow has eight pawn starting squares");
    expect(PAWN_START_SQUARES[GREEN].popcount() == 8,
           "Green has eight pawn starting squares");
}

void test_known_promotion_zones() {
    using namespace Mockingbird;

    expect(is_pawn_promotion_square(RED, make_square(FILE_A, RANK_11)),
           "Red promotes on rank 11");
    expect(is_pawn_promotion_square(RED, make_square(FILE_N, RANK_11)),
           "Red promotion zone spans rank 11");
    expect(is_pawn_promotion_square(BLUE, make_square(FILE_K, RANK_1)),
           "Blue promotes on file k");
    expect(is_pawn_promotion_square(BLUE, make_square(FILE_K, RANK_14)),
           "Blue promotion zone spans file k");
    expect(is_pawn_promotion_square(YELLOW, make_square(FILE_A, RANK_4)),
           "Yellow promotes on rank 4");
    expect(is_pawn_promotion_square(YELLOW, make_square(FILE_N, RANK_4)),
           "Yellow promotion zone spans rank 4");
    expect(is_pawn_promotion_square(GREEN, make_square(FILE_D, RANK_1)),
           "Green promotes on file d");
    expect(is_pawn_promotion_square(GREEN, make_square(FILE_D, RANK_14)),
           "Green promotion zone spans file d");

    for (int color_index = 0; color_index < COLOR_NB; ++color_index)
        expect(PAWN_PROMOTION_SQUARES[std::size_t(color_index)].popcount() == 14,
               "each promotion zone contains fourteen squares");
}

void test_known_push_destinations() {
    using namespace Mockingbird;

    constexpr Square red = make_square(FILE_D, RANK_2);
    constexpr Square blue = make_square(FILE_B, RANK_4);
    constexpr Square yellow = make_square(FILE_D, RANK_13);
    constexpr Square green = make_square(FILE_M, RANK_4);

    expect(pawn_push_destination(RED, red) == make_square(FILE_D, RANK_3),
           "Red single push moves north");
    expect(pawn_double_push_destination(RED, red) == make_square(FILE_D, RANK_4),
           "Red double push moves north twice");

    expect(pawn_push_destination(BLUE, blue) == make_square(FILE_C, RANK_4),
           "Blue single push moves east");
    expect(pawn_double_push_destination(BLUE, blue) == make_square(FILE_D, RANK_4),
           "Blue double push moves east twice");

    expect(pawn_push_destination(YELLOW, yellow) == make_square(FILE_D, RANK_12),
           "Yellow single push moves south");
    expect(
      pawn_double_push_destination(YELLOW, yellow) == make_square(FILE_D, RANK_11),
      "Yellow double push moves south twice");

    expect(pawn_push_destination(GREEN, green) == make_square(FILE_L, RANK_4),
           "Green single push moves west");
    expect(
      pawn_double_push_destination(GREEN, green) == make_square(FILE_K, RANK_4),
      "Green double push moves west twice");

    expect(pawn_double_push_destination(
             RED, make_square(FILE_D, RANK_3))
             == SQ_NONE,
           "double push is undefined outside a starting zone");
    expect(pawn_push_destination(RED, make_square(FILE_A, RANK_11)) == SQ_NONE,
           "push destination does not cross a cut-out corner");
}

void test_all_geometry_entries() {
    using namespace Mockingbird;

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);
        int promotion_arrivals = 0;
        int double_pushes = 0;

        for (int source_index = 0; source_index < SQUARE_NB; ++source_index) {
            const Square source = Square(source_index);
            const bool playable = is_ok(source);
            const bool expected_start =
              playable && forward_coordinate(color, source) == 2;
            const bool expected_promotion =
              playable && forward_coordinate(color, source) == 11;
            const Square expected_single =
              expected_push_destination(color, source, 1);
            const Square expected_double =
              expected_push_destination(color, source, 2);

            expect(is_pawn_start_square(color, source) == expected_start,
                   "starting mask matches relative pawn coordinates");
            expect(is_pawn_promotion_square(color, source) == expected_promotion,
                   "promotion mask matches relative pawn coordinates");
            expect(pawn_push_destination(color, source) == expected_single,
                   "single-push table matches coordinate stepping");
            expect(pawn_double_push_destination(color, source) == expected_double,
                   "double-push table matches coordinate stepping");

            if (expected_double != SQ_NONE) {
                ++double_pushes;
                expect(!is_pawn_promotion_square(color, expected_double),
                       "double push does not land in a promotion zone");
            }

            if (expected_single != SQ_NONE
                && is_pawn_promotion_square(color, expected_single))
                ++promotion_arrivals;
        }

        expect(double_pushes == 8, "each color has eight geometric double pushes");
        expect(promotion_arrivals == 14,
               "fourteen single pushes enter each promotion zone");
    }
}

void test_rotational_symmetry() {
    using namespace Mockingbird;

    // A clockwise quarter-turn maps Red to Blue, Blue to Yellow, Yellow to
    // Green, and Green to Red.
    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);
        const Color rotated_color = next_color(color);

        for (int source_index = 0; source_index < SQUARE_NB; ++source_index) {
            const Square source = Square(source_index);
            if (!is_ok(source))
                continue;

            const Square rotated_source = rotate_clockwise(source);
            expect(is_ok(rotated_source), "rotation preserves playable geometry");
            expect(is_pawn_start_square(color, source)
                     == is_pawn_start_square(rotated_color, rotated_source),
                   "rotation preserves pawn starting zones");
            expect(is_pawn_promotion_square(color, source)
                     == is_pawn_promotion_square(rotated_color, rotated_source),
                   "rotation preserves pawn promotion zones");

            const Square single = pawn_push_destination(color, source);
            const Square rotated_single =
              pawn_push_destination(rotated_color, rotated_source);
            expect(rotate_destination(single) == rotated_single,
                   "rotation preserves single-push destinations");

            const Square double_push =
              pawn_double_push_destination(color, source);
            const Square rotated_double =
              pawn_double_push_destination(rotated_color, rotated_source);
            expect(rotate_destination(double_push) == rotated_double,
                   "rotation preserves double-push destinations");
        }
    }
}

}  // namespace

int main() {
    test_known_starting_zones();
    test_known_promotion_zones();
    test_known_push_destinations();
    test_all_geometry_entries();
    test_rotational_symmetry();

    if (failures != 0) {
        std::cerr << failures << " pawn-geometry test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All pawn-geometry tests passed\n";
    return EXIT_SUCCESS;
}
