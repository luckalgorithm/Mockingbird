#include "setup.h"

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

inline constexpr std::array<CastlingSide, CASTLING_SIDE_NB>
  CASTLING_SIDES = {
    CastlingSide::KING_SIDE,
    CastlingSide::QUEEN_SIDE,
};

inline constexpr std::array<PieceType, 8>
  RED_GREEN_BACK_RANK = {
    ROOK,
    KNIGHT,
    BISHOP,
    QUEEN,
    KING,
    BISHOP,
    KNIGHT,
    ROOK,
};

inline constexpr std::array<PieceType, 8>
  BLUE_YELLOW_BACK_RANK = {
    ROOK,
    KNIGHT,
    BISHOP,
    KING,
    QUEEN,
    BISHOP,
    KNIGHT,
    ROOK,
};

[[nodiscard]] constexpr Piece expected_piece(
  Square square) noexcept {
    const int file = file_of(square);
    const int rank = rank_of(square);

    if (rank == RANK_1
        && file >= FILE_D
        && file <= FILE_K) {
        return make_piece(
          RED,
          RED_GREEN_BACK_RANK[
            std::size_t(file - FILE_D)]);
    }

    if (rank == RANK_2
        && file >= FILE_D
        && file <= FILE_K)
        return R_PAWN;

    if (rank == RANK_14
        && file >= FILE_D
        && file <= FILE_K) {
        return make_piece(
          YELLOW,
          BLUE_YELLOW_BACK_RANK[
            std::size_t(file - FILE_D)]);
    }

    if (rank == RANK_13
        && file >= FILE_D
        && file <= FILE_K)
        return Y_PAWN;

    if (file == FILE_A
        && rank >= RANK_4
        && rank <= RANK_11) {
        return make_piece(
          BLUE,
          BLUE_YELLOW_BACK_RANK[
            std::size_t(rank - RANK_4)]);
    }

    if (file == FILE_B
        && rank >= RANK_4
        && rank <= RANK_11)
        return B_PAWN;

    if (file == FILE_N
        && rank >= RANK_4
        && rank <= RANK_11) {
        return make_piece(
          GREEN,
          RED_GREEN_BACK_RANK[
            std::size_t(rank - RANK_4)]);
    }

    if (file == FILE_M
        && rank >= RANK_4
        && rank <= RANK_11)
        return G_PAWN;

    return NO_PIECE;
}

struct ExpectedOccupancy {
    Bitboard occupied;
    std::array<Bitboard, COLOR_NB> by_color;
    std::array<Bitboard, PIECE_TYPE_NB> by_type;
};

[[nodiscard]] constexpr ExpectedOccupancy
make_expected_occupancy() noexcept {
    ExpectedOccupancy expected;

    for (int square_index = 0;
         square_index < SQUARE_NB;
         ++square_index) {
        const Square square = Square(square_index);
        if (!is_ok(square))
            continue;

        const Piece piece = expected_piece(square);
        if (piece == NO_PIECE)
            continue;

        expected.occupied.set(square);
        expected.by_color[
          std::size_t(color_of(piece))].set(square);
        expected.by_type[
          std::size_t(type_of(piece))].set(square);
    }

    return expected;
}

inline constexpr ExpectedOccupancy EXPECTED_OCCUPANCY =
  make_expected_occupancy();

[[nodiscard]] constexpr bool positions_equal(
  const Position& left,
  const Position& right) noexcept {
    if (left.side_to_move() != right.side_to_move()
        || left.occupied() != right.occupied())
        return false;

    for (int square_index = 0;
         square_index < SQUARE_NB;
         ++square_index) {
        const Square square = Square(square_index);
        if (is_ok(square)
            && left.piece_on(square)
                 != right.piece_on(square))
            return false;
    }

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        if (left.pieces(color) != right.pieces(color)
            || left.en_passant_square(color)
                 != right.en_passant_square(color))
            return false;

        for (const CastlingSide side : CASTLING_SIDES) {
            if (left.has_castling_right(color, side)
                != right.has_castling_right(color, side))
                return false;
        }
    }

    for (int type_index = PAWN;
         type_index <= KING;
         ++type_index) {
        if (left.pieces(PieceType(type_index))
            != right.pieces(PieceType(type_index)))
            return false;
    }

    return true;
}

[[nodiscard]] constexpr bool constexpr_setup_is_exact() noexcept {
    const Position position = make_starting_position();

    if (position.side_to_move() != RED
        || position.occupied()
             != EXPECTED_OCCUPANCY.occupied)
        return false;

    for (int square_index = 0;
         square_index < SQUARE_NB;
         ++square_index) {
        const Square square = Square(square_index);
        if (is_ok(square)
            && position.piece_on(square)
                 != expected_piece(square))
            return false;
    }

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        if (position.pieces(color)
              != EXPECTED_OCCUPANCY.by_color[
                   std::size_t(color)]
            || position.en_passant_square(color)
                 != SQ_NONE)
            return false;

        for (const CastlingSide side : CASTLING_SIDES) {
            if (!position.has_castling_right(color, side))
                return false;
        }
    }

    for (int type_index = PAWN;
         type_index <= KING;
         ++type_index) {
        const PieceType piece_type =
          PieceType(type_index);
        if (position.pieces(piece_type)
            != EXPECTED_OCCUPANCY.by_type[
                 std::size_t(piece_type)])
            return false;
    }

    return true;
}

static_assert(constexpr_setup_is_exact());

void test_exact_mailbox() {
    const Position position = make_starting_position();
    bool exact = true;
    int occupied_count = 0;
    int empty_count = 0;

    for (int square_index = 0;
         square_index < SQUARE_NB;
         ++square_index) {
        const Square square = Square(square_index);
        if (!is_ok(square))
            continue;

        const Piece expected = expected_piece(square);
        exact &=
          position.piece_on(square) == expected;

        if (expected == NO_PIECE)
            ++empty_count;
        else
            ++occupied_count;
    }

    expect(exact,
           "all playable squares match the expected placement");
    expect(occupied_count == 64,
           "the expected placement contains 64 pieces");
    expect(empty_count == 96,
           "the other 96 playable squares are empty");
}

void test_occupancy_indexes() {
    const Position position = make_starting_position();

    expect(position.occupied()
             == EXPECTED_OCCUPANCY.occupied,
           "combined occupancy matches the expected mask");
    expect(position.occupied().popcount() == 64,
           "combined occupancy contains 64 squares");

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        expect(
          position.pieces(color)
            == EXPECTED_OCCUPANCY.by_color[
                 std::size_t(color)],
          "color occupancy matches the expected mask");
        expect(position.pieces(color).popcount() == 16,
               "each color starts with 16 pieces");
    }

    constexpr std::array<int, PIECE_TYPE_NB>
      EXPECTED_TYPE_COUNTS = {
        0,
        32,
        8,
        8,
        8,
        4,
        4,
    };

    for (int type_index = PAWN;
         type_index <= KING;
         ++type_index) {
        const PieceType piece_type =
          PieceType(type_index);
        expect(
          position.pieces(piece_type)
            == EXPECTED_OCCUPANCY.by_type[
                 std::size_t(piece_type)],
          "piece-type occupancy matches the expected mask");
        expect(
          position.pieces(piece_type).popcount()
            == EXPECTED_TYPE_COUNTS[
                 std::size_t(piece_type)],
          "piece-type count matches the expected inventory");

        for (int color_index = 0;
             color_index < COLOR_NB;
             ++color_index) {
            const Color color = Color(color_index);
            const int expected_count =
              piece_type == PAWN ? 8
              : piece_type == ROOK
                  || piece_type == KNIGHT
                  || piece_type == BISHOP
                ? 2
                : 1;
            expect(
              position.pieces(color, piece_type)
                == (EXPECTED_OCCUPANCY.by_color[
                      std::size_t(color)]
                    & EXPECTED_OCCUPANCY.by_type[
                      std::size_t(piece_type)]),
              "color-and-type occupancy matches the expected mask");
            expect(
              position.pieces(color, piece_type).popcount()
                == expected_count,
              "each color has the expected piece-type count");
        }
    }
}

void test_rule_state() {
    const Position position = make_starting_position();

    expect(position.side_to_move() == RED,
           "Red is the starting side");

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        expect(
          position.en_passant_square(color) == SQ_NONE,
          "starting en-passant targets are absent");

        for (const CastlingSide side : CASTLING_SIDES) {
            expect(position.has_castling_right(color, side),
                   "each color has both starting castling rights");
        }
    }
}

void test_independent_results() {
    Position first = make_starting_position();
    const Position second = make_starting_position();

    expect(positions_equal(first, second),
           "separate setup calls return equal positions");

    constexpr Square d2 =
      make_square(FILE_D, RANK_2);
    first.remove_piece(d2);
    first.set_side_to_move(BLUE);
    first.clear_castling_right(
      RED, CastlingSide::KING_SIDE);
    first.set_en_passant_square(
      GREEN, make_square(FILE_L, RANK_8));

    expect(second.piece_on(d2) == R_PAWN,
           "mutating one result does not change another mailbox");
    expect(second.side_to_move() == RED,
           "mutating one result does not change another side");
    expect(
      second.has_castling_right(
        RED, CastlingSide::KING_SIDE),
      "mutating one result does not change another castling right");
    expect(second.en_passant_square(GREEN) == SQ_NONE,
           "mutating one result does not change another en-passant target");
    expect(
      positions_equal(second, make_starting_position()),
      "later setup calls return the same starting state");
}

}  // namespace

int main() {
    test_exact_mailbox();
    test_occupancy_indexes();
    test_rule_state();
    test_independent_results();

    if (failures != 0) {
        std::cerr << failures
                  << " setup test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All setup tests passed\n";
    return EXIT_SUCCESS;
}
