#include "position.h"

#include <array>
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

void expect_consistent(const Mockingbird::Position& position) {
    using namespace Mockingbird;

    Bitboard expected_occupied;
    std::array<Bitboard, COLOR_NB> expected_by_color{};
    std::array<Bitboard, PIECE_TYPE_NB> expected_by_type{};

    // Reconstructs every occupancy bitboard from the mailbox representation.
    for (int square_index = 0; square_index < SQUARE_NB; ++square_index) {
        const Square square = Square(square_index);
        if (!is_ok(square))
            continue;

        const Piece piece = position.piece_on(square);
        if (piece == NO_PIECE)
            continue;

        expect(is_ok(piece), "mailbox contains a valid piece encoding");

        expected_occupied.set(square);
        expected_by_color[std::size_t(color_of(piece))].set(square);
        expected_by_type[std::size_t(type_of(piece))].set(square);
    }

    expect(position.occupied() == expected_occupied,
           "combined occupancy matches the mailbox");

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);
        expect(position.pieces(color) == expected_by_color[std::size_t(color)],
               "color occupancy matches the mailbox");
    }

    for (int type_index = PAWN; type_index <= KING; ++type_index) {
        const PieceType piece_type = PieceType(type_index);
        expect(position.pieces(piece_type) == expected_by_type[std::size_t(piece_type)],
               "piece-type occupancy matches the mailbox");

        for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
            const Color color = Color(color_index);
            expect(position.pieces(color, piece_type)
                     == (expected_by_color[std::size_t(color)]
                         & expected_by_type[std::size_t(piece_type)]),
                   "color-and-type occupancy matches the mailbox");
        }
    }
}

[[nodiscard]] constexpr bool constexpr_position_operations() {
    using namespace Mockingbird;

    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square d2 = make_square(FILE_D, RANK_2);

    Position position;
    position.put_piece(R_ROOK, d1);
    if (!position.occupied().test(d1))
        return false;

    if (position.move_piece(d1, d2) != NO_PIECE)
        return false;

    return position.piece_on(d2) == R_ROOK
        && position.pieces(RED, ROOK).test(d2)
        && !position.occupied().test(d1);
}

static_assert(constexpr_position_operations());

void test_default_position() {
    using namespace Mockingbird;

    Position position;

    expect(position.side_to_move() == RED, "Red moves first in a default position");
    expect(position.occupied().empty(), "default combined occupancy is empty");

    for (int color_index = 0; color_index < COLOR_NB; ++color_index)
        expect(position.pieces(Color(color_index)).empty(),
               "default color occupancy is empty");

    for (int type_index = PAWN; type_index <= KING; ++type_index)
        expect(position.pieces(PieceType(type_index)).empty(),
               "default piece-type occupancy is empty");

    expect_consistent(position);
}

void test_all_piece_encodings() {
    using namespace Mockingbird;

    Position position;
    int piece_index = 0;

    // Places one instance of every color and piece-type combination.
    for (int square_index = 0;
         square_index < SQUARE_NB && piece_index < COLOR_NB * (KING - PAWN + 1);
         ++square_index) {
        const Square square = Square(square_index);
        if (!is_ok(square))
            continue;

        const Color color = Color(piece_index / (KING - PAWN + 1));
        const PieceType piece_type = PieceType(PAWN + piece_index % (KING - PAWN + 1));
        position.put_piece(make_piece(color, piece_type), square);
        ++piece_index;
    }

    expect(piece_index == 24, "all twenty-four piece encodings were placed");
    expect(position.occupied().popcount() == 24,
           "combined occupancy contains all placed pieces");

    for (int color_index = 0; color_index < COLOR_NB; ++color_index)
        expect(position.pieces(Color(color_index)).popcount() == 6,
               "each color occupancy contains six piece types");

    for (int type_index = PAWN; type_index <= KING; ++type_index)
        expect(position.pieces(PieceType(type_index)).popcount() == COLOR_NB,
               "each piece-type occupancy contains four colors");

    expect_consistent(position);
}

void test_placement_and_removal() {
    using namespace Mockingbird;

    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square h8 = make_square(FILE_H, RANK_8);

    Position position;
    position.put_piece(R_KING, d1);
    position.put_piece(B_QUEEN, h8);

    expect(position.piece_on(d1) == R_KING, "mailbox contains the placed Red king");
    expect(position.occupied().test(d1), "combined occupancy contains the Red king");
    expect(position.pieces(RED).test(d1), "Red occupancy contains the Red king");
    expect(position.pieces(KING).test(d1), "king occupancy contains the Red king");
    expect(position.pieces(RED, KING).test(d1),
           "Red king occupancy contains the Red king");
    expect_consistent(position);

    expect(position.remove_piece(h8) == B_QUEEN,
           "remove_piece returns the removed Blue queen");
    expect(position.empty(h8), "removed square is empty");
    expect(!position.occupied().test(h8),
           "combined occupancy excludes the removed queen");
    expect(!position.pieces(BLUE).test(h8),
           "Blue occupancy excludes the removed queen");
    expect(!position.pieces(QUEEN).test(h8),
           "queen occupancy excludes the removed queen");
    expect_consistent(position);
}

void test_quiet_move() {
    using namespace Mockingbird;

    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square f2 = make_square(FILE_F, RANK_2);

    Position position;
    position.put_piece(R_KNIGHT, d1);

    expect(position.move_piece(d1, f2) == NO_PIECE,
           "a quiet move reports no captured piece");
    expect(position.empty(d1), "quiet move clears the source mailbox square");
    expect(position.piece_on(f2) == R_KNIGHT,
           "quiet move fills the destination mailbox square");
    expect(!position.occupied().test(d1),
           "quiet move clears the source occupancy bit");
    expect(position.occupied().test(f2),
           "quiet move sets the destination occupancy bit");
    expect(position.pieces(RED, KNIGHT).test(f2),
           "quiet move updates color-and-type occupancy");
    expect(position.side_to_move() == RED,
           "piece relocation does not change the side to move");
    expect_consistent(position);
}

void test_capture() {
    using namespace Mockingbird;

    constexpr Square h8 = make_square(FILE_H, RANK_8);
    constexpr Square h10 = make_square(FILE_H, RANK_10);

    Position position;
    position.put_piece(R_ROOK, h8);
    position.put_piece(B_BISHOP, h10);

    expect(position.move_piece(h8, h10) == B_BISHOP,
           "capture returns the destination piece");
    expect(position.empty(h8), "capture clears the source mailbox square");
    expect(position.piece_on(h10) == R_ROOK,
           "capture replaces the destination mailbox piece");
    expect(position.occupied().popcount() == 1,
           "capture decreases combined occupancy by one");
    expect(position.pieces(RED, ROOK).test(h10),
           "capturing piece occupies its destination");
    expect(position.pieces(BLUE).empty(),
           "captured piece is removed from color occupancy");
    expect(position.pieces(BISHOP).empty(),
           "captured piece is removed from piece-type occupancy");
    expect_consistent(position);
}

void test_same_type_capture() {
    using namespace Mockingbird;

    constexpr Square h8 = make_square(FILE_H, RANK_8);
    constexpr Square h10 = make_square(FILE_H, RANK_10);

    Position position;
    position.put_piece(R_ROOK, h8);
    position.put_piece(B_ROOK, h10);
    position.move_piece(h8, h10);

    expect(position.pieces(ROOK).popcount() == 1,
           "same-type capture retains the moving rook in type occupancy");
    expect(position.pieces(ROOK).test(h10),
           "rook occupancy contains the capturing rook");
    expect_consistent(position);
}

void test_side_to_move_and_clear() {
    using namespace Mockingbird;

    constexpr Square h8 = make_square(FILE_H, RANK_8);

    Position position;
    position.set_side_to_move(GREEN);
    expect(position.side_to_move() == GREEN, "side to move can be set to Green");

    position.put_piece(Y_KING, h8);
    position.clear();

    expect(position.side_to_move() == RED, "clear restores Red as the side to move");
    expect(position.occupied().empty(), "clear resets combined occupancy");
    expect(position.empty(h8), "clear resets the mailbox");

    for (int color_index = 0; color_index < COLOR_NB; ++color_index)
        expect(position.pieces(Color(color_index)).empty(),
               "clear resets every color occupancy");

    for (int type_index = PAWN; type_index <= KING; ++type_index)
        expect(position.pieces(PieceType(type_index)).empty(),
               "clear resets every piece-type occupancy");

    expect_consistent(position);
}

}  // namespace

int main() {
    test_default_position();
    test_all_piece_encodings();
    test_placement_and_removal();
    test_quiet_move();
    test_capture();
    test_same_type_capture();
    test_side_to_move_and_clear();

    if (failures != 0) {
        std::cerr << failures << " position test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All position tests passed\n";
    return EXIT_SUCCESS;
}
