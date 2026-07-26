#include "position.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

// Rotates a square clockwise around the center of the 14x14 board.
[[nodiscard]] constexpr Mockingbird::Square rotate_clockwise(
  Mockingbird::Square square) noexcept {
    using namespace Mockingbird;

    return make_square(
      File(int(rank_of(square))),
      Rank(BOARD_FILES + 1 - int(file_of(square))));
}

constexpr Mockingbird::Square RED_EN_PASSANT =
  Mockingbird::make_square(Mockingbird::FILE_D, Mockingbird::RANK_3);
constexpr Mockingbird::Square BLUE_EN_PASSANT =
  rotate_clockwise(RED_EN_PASSANT);
constexpr Mockingbird::Square YELLOW_EN_PASSANT =
  rotate_clockwise(BLUE_EN_PASSANT);
constexpr Mockingbird::Square GREEN_EN_PASSANT =
  rotate_clockwise(YELLOW_EN_PASSANT);

constexpr std::array<Mockingbird::Square, Mockingbird::COLOR_NB>
  EN_PASSANT_SQUARES = {
    RED_EN_PASSANT,
    BLUE_EN_PASSANT,
    YELLOW_EN_PASSANT,
    GREEN_EN_PASSANT,
  };

constexpr std::array<
  Mockingbird::CastlingSide,
  Mockingbird::CASTLING_SIDE_NB>
  CASTLING_SIDES = {
    Mockingbird::CastlingSide::KING_SIDE,
    Mockingbird::CastlingSide::QUEEN_SIDE,
  };

static_assert(BLUE_EN_PASSANT
              == Mockingbird::make_square(
                Mockingbird::FILE_C, Mockingbird::RANK_11));
static_assert(YELLOW_EN_PASSANT
              == Mockingbird::make_square(
                Mockingbird::FILE_K, Mockingbird::RANK_12));
static_assert(GREEN_EN_PASSANT
              == Mockingbird::make_square(
                Mockingbird::FILE_L, Mockingbird::RANK_4));
static_assert(rotate_clockwise(GREEN_EN_PASSANT) == RED_EN_PASSANT);
static_assert(Mockingbird::is_ok(
  Mockingbird::CastlingSide::KING_SIDE));
static_assert(Mockingbird::is_ok(
  Mockingbird::CastlingSide::QUEEN_SIDE));
static_assert(!Mockingbird::is_ok(
  Mockingbird::CastlingSide::COUNT));
static_assert(!Mockingbird::is_ok(
  static_cast<Mockingbird::CastlingSide>(255)));
static_assert(Mockingbird::CASTLING_SIDE_NB == 2);
static_assert(sizeof(Mockingbird::CastlingSide) == 1);

// Records a failed condition and allows the remaining tests to run.
void expect(bool condition, std::string_view message) {
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void expect_consistent(const Mockingbird::Position& position) {
    using namespace Mockingbird;

    expect(position.key() == position.recompute_key(),
           "cached key matches the canonical position state");

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
    expect(position.pieces(RED_YELLOW)
             == (expected_by_color[RED] | expected_by_color[YELLOW]),
           "red-yellow occupancy matches the mailbox");
    expect(position.pieces(BLUE_GREEN)
             == (expected_by_color[BLUE] | expected_by_color[GREEN]),
           "blue-green occupancy matches the mailbox");

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

[[nodiscard]] constexpr bool constexpr_en_passant_operations() {
    using namespace Mockingbird;

    Position position;

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);
        if (position.en_passant_square(color) != SQ_NONE)
            return false;

        position.set_en_passant_square(
          color, EN_PASSANT_SQUARES[std::size_t(color)]);
    }

    position.clear_en_passant_square(BLUE);
    if (position.en_passant_square(BLUE) != SQ_NONE
        || position.en_passant_square(RED) != RED_EN_PASSANT
        || position.en_passant_square(YELLOW) != YELLOW_EN_PASSANT
        || position.en_passant_square(GREEN) != GREEN_EN_PASSANT)
        return false;

    position.clear_en_passant_squares();
    for (int color_index = 0; color_index < COLOR_NB; ++color_index)
        if (position.en_passant_square(Color(color_index)) != SQ_NONE)
            return false;

    return true;
}

static_assert(constexpr_en_passant_operations());

[[nodiscard]] constexpr bool constexpr_castling_right_operations() {
    using namespace Mockingbird;

    Position position;

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);
        for (const CastlingSide side : CASTLING_SIDES) {
            if (position.has_castling_right(color, side))
                return false;
            position.set_castling_right(color, side);
        }
    }

    position.clear_castling_right(
      BLUE, CastlingSide::KING_SIDE);
    if (position.has_castling_right(
          BLUE, CastlingSide::KING_SIDE)
        || !position.has_castling_right(
          BLUE, CastlingSide::QUEEN_SIDE))
        return false;

    position.set_castling_right(
      BLUE, CastlingSide::KING_SIDE);
    position.clear_castling_rights(YELLOW);

    for (const CastlingSide side : CASTLING_SIDES) {
        if (position.has_castling_right(YELLOW, side))
            return false;
    }

    position.clear_castling_rights();
    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);
        for (const CastlingSide side : CASTLING_SIDES)
            if (position.has_castling_right(color, side))
                return false;
    }

    return true;
}

static_assert(constexpr_castling_right_operations());

void test_default_position() {
    using namespace Mockingbird;

    Position position;

    expect(position.side_to_move() == RED, "Red moves first in a default position");
    expect(position.occupied().empty(), "default combined occupancy is empty");

    for (int color_index = 0; color_index < COLOR_NB; ++color_index)
        expect(position.pieces(Color(color_index)).empty(),
               "default color occupancy is empty");

    for (int color_index = 0; color_index < COLOR_NB; ++color_index)
        expect(position.en_passant_square(Color(color_index)) == SQ_NONE,
               "default en-passant target is absent");

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);
        for (const CastlingSide side : CASTLING_SIDES)
            expect(!position.has_castling_right(color, side),
                   "default castling right is absent");
    }

    for (int type_index = PAWN; type_index <= KING; ++type_index)
        expect(position.pieces(PieceType(type_index)).empty(),
               "default piece-type occupancy is empty");

    expect_consistent(position);
}

void test_en_passant_state() {
    using namespace Mockingbird;

    Position position;

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);
        position.set_en_passant_square(
          color, EN_PASSANT_SQUARES[std::size_t(color)]);
    }

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);
        expect(position.en_passant_square(color)
                 == EN_PASSANT_SQUARES[std::size_t(color)],
               "each color stores an independent en-passant target");
    }

    constexpr Square replacement =
      make_square(FILE_E, RANK_3);
    position.set_en_passant_square(RED, replacement);
    expect(position.en_passant_square(RED) == replacement,
           "setting a color again replaces its en-passant target");
    expect(position.en_passant_square(BLUE) == BLUE_EN_PASSANT,
           "replacing one target preserves the other colors");

    position.clear_en_passant_square(YELLOW);
    expect(position.en_passant_square(YELLOW) == SQ_NONE,
           "clearing one color removes its en-passant target");
    expect(position.en_passant_square(RED) == replacement
             && position.en_passant_square(BLUE) == BLUE_EN_PASSANT
             && position.en_passant_square(GREEN) == GREEN_EN_PASSANT,
           "clearing one target preserves the other colors");

    const Position copy = position;
    position.clear_en_passant_square(RED);
    expect(copy.en_passant_square(RED) == replacement,
           "a copied position retains its own en-passant state");

    position.clear_en_passant_squares();
    for (int color_index = 0; color_index < COLOR_NB; ++color_index)
        expect(position.en_passant_square(Color(color_index)) == SQ_NONE,
               "clearing all targets resets every color");

    expect_consistent(position);
    expect_consistent(copy);
}

void test_every_castling_right_subset() {
    using namespace Mockingbird;

    constexpr unsigned right_count =
      static_cast<unsigned>(COLOR_NB)
      * static_cast<unsigned>(CASTLING_SIDE_NB);
    constexpr unsigned subset_count = 1U << right_count;

    for (unsigned subset = 0; subset < subset_count; ++subset) {
        Position position;
        unsigned bit_index = 0;

        for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
            const Color color = Color(color_index);
            for (const CastlingSide side : CASTLING_SIDES) {
                const bool expected =
                  (subset & (1U << bit_index)) != 0;

                if (expected) {
                    position.set_castling_right(color, side);
                    position.set_castling_right(color, side);
                } else {
                    position.clear_castling_right(color, side);
                }

                ++bit_index;
            }
        }

        expect_consistent(position);

        bit_index = 0;
        for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
            const Color color = Color(color_index);
            for (const CastlingSide side : CASTLING_SIDES) {
                const bool expected =
                  (subset & (1U << bit_index)) != 0;
                expect(position.has_castling_right(color, side)
                         == expected,
                       "castling-right subset round-trips");
                ++bit_index;
            }
        }
    }
}

void test_castling_right_clearing_and_copying() {
    using namespace Mockingbird;

    for (int cleared_color_index = 0;
         cleared_color_index < COLOR_NB;
         ++cleared_color_index) {
        const Color cleared_color = Color(cleared_color_index);

        for (const CastlingSide cleared_side : CASTLING_SIDES) {
            Position position;
            for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
                const Color color = Color(color_index);
                for (const CastlingSide side : CASTLING_SIDES)
                    position.set_castling_right(color, side);
            }

            position.clear_castling_right(
              cleared_color, cleared_side);

            for (int color_index = 0;
                 color_index < COLOR_NB;
                 ++color_index) {
                const Color color = Color(color_index);
                for (const CastlingSide side : CASTLING_SIDES) {
                    const bool expected =
                      color != cleared_color || side != cleared_side;
                    expect(position.has_castling_right(color, side)
                             == expected,
                           "clearing one castling right preserves the other seven");
                }
            }

            expect_consistent(position);
        }
    }

    for (int cleared_color_index = 0;
         cleared_color_index < COLOR_NB;
         ++cleared_color_index) {
        const Color cleared_color = Color(cleared_color_index);
        Position position;

        for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
            const Color color = Color(color_index);
            for (const CastlingSide side : CASTLING_SIDES)
                position.set_castling_right(color, side);
        }

        position.clear_castling_rights(cleared_color);

        for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
            const Color color = Color(color_index);
            for (const CastlingSide side : CASTLING_SIDES) {
                expect(position.has_castling_right(color, side)
                         == (color != cleared_color),
                       "clearing one color preserves the other six rights");
            }
        }

        expect_consistent(position);
    }

    Position original;
    original.set_castling_right(
      GREEN, CastlingSide::QUEEN_SIDE);
    const Position copy = original;
    original.clear_castling_rights();

    expect(copy.has_castling_right(
             GREEN, CastlingSide::QUEEN_SIDE),
           "a copied position retains its own castling rights");
    expect(!original.has_castling_right(
             GREEN, CastlingSide::QUEEN_SIDE),
           "global clearing removes every castling right");
    expect_consistent(original);
    expect_consistent(copy);
}

void test_raw_mutations_preserve_rule_state() {
    using namespace Mockingbird;

    constexpr Square d4 = make_square(FILE_D, RANK_4);
    constexpr Square d5 = make_square(FILE_D, RANK_5);

    Position position;
    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);
        position.set_en_passant_square(
          color, EN_PASSANT_SQUARES[std::size_t(color)]);
        for (const CastlingSide side : CASTLING_SIDES)
            position.set_castling_right(color, side);
    }

    // Raw state mutations do not apply move-rule expiration.
    position.set_side_to_move(GREEN);
    position.put_piece(R_ROOK, d4);
    position.move_piece(d4, d5);
    position.remove_piece(d5);

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);
        expect(position.en_passant_square(color)
                 == EN_PASSANT_SQUARES[std::size_t(color)],
               "raw position mutations preserve en-passant targets");
        for (const CastlingSide side : CASTLING_SIDES)
            expect(position.has_castling_right(color, side),
                   "raw position mutations preserve castling rights");
    }

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
    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);
        position.set_en_passant_square(
          color, EN_PASSANT_SQUARES[std::size_t(color)]);
        for (const CastlingSide side : CASTLING_SIDES)
            position.set_castling_right(color, side);
    }
    position.clear();

    expect(position.side_to_move() == RED, "clear restores Red as the side to move");
    expect(position.occupied().empty(), "clear resets combined occupancy");
    expect(position.empty(h8), "clear resets the mailbox");

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        expect(position.pieces(Color(color_index)).empty(),
               "clear resets every color occupancy");
        expect(position.en_passant_square(Color(color_index)) == SQ_NONE,
               "clear resets every en-passant target");
        for (const CastlingSide side : CASTLING_SIDES)
            expect(!position.has_castling_right(
                     Color(color_index), side),
                   "clear resets every castling right");
    }

    for (int type_index = PAWN; type_index <= KING; ++type_index)
        expect(position.pieces(PieceType(type_index)).empty(),
               "clear resets every piece-type occupancy");

    expect_consistent(position);
}

}  // namespace

int main() {
    test_default_position();
    test_en_passant_state();
    test_every_castling_right_subset();
    test_castling_right_clearing_and_copying();
    test_raw_mutations_preserve_rule_state();
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
