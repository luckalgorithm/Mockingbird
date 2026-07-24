#include "board.h"

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

void test_players_and_teams() {
    using namespace Mockingbird;

    expect(is_ok(RED_YELLOW), "red-yellow is a valid team");
    expect(is_ok(BLUE_GREEN), "blue-green is a valid team");
    expect(!is_ok(TEAM_NB), "TEAM_NB is not a valid team");

    expect(is_ok(RED), "red is a valid color");
    expect(is_ok(GREEN), "green is a valid color");
    expect(!is_ok(COLOR_NB), "COLOR_NB is not a valid color");

    expect(next_color(RED) == BLUE, "blue follows red");
    expect(next_color(BLUE) == YELLOW, "yellow follows blue");
    expect(next_color(YELLOW) == GREEN, "green follows yellow");
    expect(next_color(GREEN) == RED, "red follows green");

    expect(previous_color(RED) == GREEN, "green precedes red");
    expect(previous_color(BLUE) == RED, "red precedes blue");

    expect(team_of(RED) == RED_YELLOW, "red belongs to the red-yellow team");
    expect(team_of(YELLOW) == RED_YELLOW, "yellow belongs to the red-yellow team");
    expect(team_of(BLUE) == BLUE_GREEN, "blue belongs to the blue-green team");
    expect(team_of(GREEN) == BLUE_GREEN, "green belongs to the blue-green team");
}

void test_piece_encoding() {
    using namespace Mockingbird;

    static_assert(make_piece(RED, PAWN) == R_PAWN);
    static_assert(make_piece(BLUE, KNIGHT) == B_KNIGHT);
    static_assert(make_piece(YELLOW, KING) == Y_KING);
    static_assert(make_piece(GREEN, QUEEN) == G_QUEEN);

    for (int color_value = RED; color_value < COLOR_NB; ++color_value) {
        const Color color = Color(color_value);

        for (int type_value = PAWN; type_value <= KING; ++type_value) {
            const PieceType piece_type = PieceType(type_value);
            const Piece piece = make_piece(color, piece_type);

            expect(is_ok(piece), "encoded piece is valid");
            expect(color_of(piece) == color, "piece color round-trips");
            expect(type_of(piece) == piece_type, "piece type round-trips");
        }
    }

    expect(!is_ok(NO_PIECE), "NO_PIECE is not a valid piece");
    expect(!is_ok(PieceType(NO_PIECE_TYPE)), "NO_PIECE_TYPE is not a valid piece type");
    expect(!is_ok(PieceType(PIECE_TYPE_NB)), "PIECE_TYPE_NB is not a valid piece type");
}

void test_mailbox_geometry() {
    using namespace Mockingbird;

    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square k14 = make_square(FILE_K, RANK_14);

    static_assert(d1 == Square(20));
    static_assert(k14 == Square(235));
    static_assert(file_of(d1) == FILE_D);
    static_assert(rank_of(d1) == RANK_1);
    static_assert(file_of(k14) == FILE_K);
    static_assert(rank_of(k14) == RANK_14);

    static_assert(d1 + NORTH == make_square(FILE_D, RANK_2));
    static_assert(d1 + EAST == make_square(FILE_E, RANK_1));
    static_assert(k14 + SOUTH == make_square(FILE_K, RANK_13));
    static_assert(k14 + WEST == make_square(FILE_J, RANK_14));

    expect(is_ok(d1), "d1 is playable");
    expect(is_ok(k14), "k14 is playable");
    expect(is_ok(make_square(FILE_A, RANK_4)), "a4 is playable");
    expect(is_ok(make_square(FILE_N, RANK_11)), "n11 is playable");

    expect(!is_ok(make_square(FILE_A, RANK_1)), "a1 is a cut-out corner");
    expect(!is_ok(make_square(FILE_C, RANK_3)), "c3 is a cut-out corner");
    expect(!is_ok(make_square(FILE_L, RANK_12)), "l12 is a cut-out corner");
    expect(!is_ok(make_square(FILE_N, RANK_14)), "n14 is a cut-out corner");
    expect(!is_ok(SQ_NONE), "SQ_NONE is not playable");
    expect(!is_ok(Square(-1)), "negative squares are not playable");

    int board_coordinates = 0;
    int playable_squares = 0;

    // Counts every mailbox index instead of sampling individual coordinates.
    for (int index = 0; index < SQUARE_NB; ++index) {
        board_coordinates += is_board_coordinate(Square(index));
        playable_squares += is_ok(Square(index));
    }

    expect(board_coordinates == BOARD_FILES * BOARD_RANKS,
           "the mailbox contains 196 board coordinates");
    expect(playable_squares == PLAYABLE_SQUARE_NB,
           "the board contains 160 playable squares");
}

void test_pawn_directions() {
    using namespace Mockingbird;

    expect(pawn_push(RED) == NORTH, "red pawns move north");
    expect(pawn_push(BLUE) == EAST, "blue pawns move east");
    expect(pawn_push(YELLOW) == SOUTH, "yellow pawns move south");
    expect(pawn_push(GREEN) == WEST, "green pawns move west");
}

void test_square_notation() {
    using namespace Mockingbird;

    const auto d1 = parse_square("d1");
    const auto n4 = parse_square("N4");
    const auto k14 = parse_square("k14");

    expect(d1 == make_square(FILE_D, RANK_1), "d1 parses");
    expect(n4 == make_square(FILE_N, RANK_4), "uppercase files parse");
    expect(k14 == make_square(FILE_K, RANK_14), "two-digit ranks parse");
    expect(d1 && square_name(*d1) == "d1", "d1 round-trips");
    expect(k14 && square_name(*k14) == "k14", "k14 round-trips");

    expect(!parse_square("a1"), "cut-out corners do not parse");
    expect(!parse_square("o4"), "files after n do not parse");
    expect(!parse_square("d15"), "ranks after 14 do not parse");
    expect(!parse_square("d0"), "rank zero does not parse");
    expect(!parse_square("d"), "a missing rank does not parse");
    expect(!parse_square("d4x"), "trailing characters do not parse");
    expect(square_name(SQ_NONE) == "-", "SQ_NONE formats as a hyphen");

    // Verifies notation conversion for all 160 playable squares.
    for (int index = 0; index < SQUARE_NB; ++index) {
        const Square square = Square(index);
        if (!is_ok(square))
            continue;

        expect(parse_square(square_name(square)) == square,
               "playable square notation round-trips");
    }
}

void test_board_storage() {
    using namespace Mockingbird;

    constexpr Square from = make_square(FILE_D, RANK_1);
    constexpr Square to = make_square(FILE_D, RANK_2);

    Board board;
    expect(board.empty(from), "a default board is empty");

    board.put_piece(R_KING, from);
    expect(board.piece_on(from) == R_KING, "put_piece stores the piece");

    board.move_piece(from, to);
    expect(board.empty(from), "move_piece clears the source");
    expect(board.piece_on(to) == R_KING, "move_piece fills the destination");

    expect(board.remove_piece(to) == R_KING, "remove_piece returns the piece");
    expect(board.empty(to), "remove_piece clears the square");

    board.put_piece(B_QUEEN, from);
    board.clear();
    expect(board.empty(from), "clear removes placed pieces");
}

}  // namespace

int main() {
    test_players_and_teams();
    test_piece_encoding();
    test_mailbox_geometry();
    test_pawn_directions();
    test_square_notation();
    test_board_storage();

    if (failures != 0) {
        std::cerr << failures << " board test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All board tests passed\n";
    return EXIT_SUCCESS;
}
