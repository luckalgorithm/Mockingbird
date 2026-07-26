#include "evaluate.h"
#include "setup.h"
#include "transition.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

int failures = 0;

using namespace Mockingbird;

inline constexpr std::array<Color, COLOR_NB> COLORS = {
  RED,
  BLUE,
  YELLOW,
  GREEN,
};

inline constexpr std::array<PieceType, 5>
  MATERIAL_PIECE_TYPES = {
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
};

inline constexpr std::array<PieceType, 4>
  PROMOTION_TYPES = {
    QUEEN,
    ROOK,
    BISHOP,
    KNIGHT,
};

inline constexpr std::array<CastlingSide, CASTLING_SIDE_NB>
  CASTLING_SIDES = {
    CastlingSide::KING_SIDE,
    CastlingSide::QUEEN_SIDE,
};

inline constexpr std::array<Square, COLOR_NB>
  PROMOTION_SOURCES = {
    make_square(FILE_H, RANK_10),
    make_square(FILE_J, RANK_8),
    make_square(FILE_H, RANK_5),
    make_square(FILE_E, RANK_8),
};

inline constexpr std::array<Square, COLOR_NB>
  QUIET_PROMOTION_DESTINATIONS = {
    make_square(FILE_H, RANK_11),
    make_square(FILE_K, RANK_8),
    make_square(FILE_H, RANK_4),
    make_square(FILE_D, RANK_8),
};

inline constexpr std::array<Square, COLOR_NB>
  CAPTURE_PROMOTION_DESTINATIONS = {
    make_square(FILE_G, RANK_11),
    make_square(FILE_K, RANK_9),
    make_square(FILE_I, RANK_4),
    make_square(FILE_D, RANK_7),
};

// Records a failed condition and allows the remaining tests to run.
void expect(bool condition, std::string_view message) {
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

[[nodiscard]] constexpr bool positions_equal(
  const Position& left,
  const Position& right) noexcept {
    if (left.side_to_move() != right.side_to_move()
        || left.key() != right.key()
        || left.recompute_key() != right.recompute_key()
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

    for (const Color color : COLORS) {
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
        const PieceType piece_type =
          PieceType(type_index);
        if (left.pieces(piece_type)
              != right.pieces(piece_type))
            return false;

        for (const Color color : COLORS) {
            if (left.pieces(color, piece_type)
                  != right.pieces(color, piece_type))
                return false;
        }
    }

    return true;
}

[[nodiscard]] constexpr Square rotate_clockwise(
  Square square) noexcept {
    return make_square(
      File(int(rank_of(square))),
      Rank(BOARD_FILES + 1 - int(file_of(square))));
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

    for (const Color color : COLORS) {
        const Color rotated_color = next_color(color);

        for (const CastlingSide side : CASTLING_SIDES) {
            if (position.has_castling_right(color, side)) {
                rotated.set_castling_right(
                  rotated_color, side);
            }
        }

        const Square en_passant =
          position.en_passant_square(color);
        if (en_passant != SQ_NONE) {
            rotated.set_en_passant_square(
              rotated_color,
              rotate_clockwise(en_passant));
        }
    }

    return rotated;
}

[[nodiscard]] constexpr Position
make_aggregate_position() noexcept {
    Position position;

    // Red and Yellow contain 1,830 material units.
    position.put_piece(R_PAWN, make_square(FILE_D, RANK_4));
    position.put_piece(R_QUEEN, make_square(FILE_E, RANK_4));
    position.put_piece(Y_ROOK, make_square(FILE_F, RANK_4));
    position.put_piece(Y_BISHOP, make_square(FILE_G, RANK_4));

    // Blue and Green contain 1,920 material units.
    position.put_piece(B_QUEEN, make_square(FILE_H, RANK_4));
    position.put_piece(B_KNIGHT, make_square(FILE_I, RANK_4));
    position.put_piece(G_ROOK, make_square(FILE_J, RANK_4));
    position.put_piece(G_PAWN, make_square(FILE_K, RANK_4));
    position.put_piece(G_PAWN, make_square(FILE_D, RANK_5));

    position.put_piece(R_KING, make_square(FILE_E, RANK_5));
    position.put_piece(B_KING, make_square(FILE_F, RANK_5));
    position.put_piece(Y_KING, make_square(FILE_G, RANK_5));
    position.put_piece(G_KING, make_square(FILE_H, RANK_5));

    return position;
}

[[nodiscard]] consteval bool
constexpr_evaluation_works() {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(R_QUEEN, make_square(FILE_H, RANK_8));
    position.put_piece(B_ROOK, make_square(FILE_H, RANK_9));
    position.put_piece(Y_KING, make_square(FILE_D, RANK_1));
    position.put_piece(G_KING, make_square(FILE_N, RANK_11));

    return piece_value(PAWN) == 100
        && piece_value(KNIGHT) == 320
        && piece_value(BISHOP) == 330
        && piece_value(ROOK) == 500
        && piece_value(QUEEN) == 900
        && piece_value(KING) == 0
        && material_balance(position, RED_YELLOW) == 400
        && material_balance(position, BLUE_GREEN) == -400
        && evaluate(position) == 400;
}

static_assert(constexpr_evaluation_works());
static_assert(
  std::is_same_v<
    decltype(evaluate(
      std::declval<const Position&>())),
    Score>);
static_assert(
  std::is_same_v<
    decltype(material_balance(
      std::declval<const Position&>(),
      RED_YELLOW)),
    Score>);
static_assert(
  std::is_same_v<
    decltype(piece_value(PAWN)),
    Score>);
static_assert(noexcept(
  evaluate(std::declval<const Position&>())));
static_assert(noexcept(
  material_balance(
    std::declval<const Position&>(),
    RED_YELLOW)));
static_assert(noexcept(piece_value(PAWN)));
static_assert(std::numeric_limits<Score>::is_signed);
static_assert(MAX_MATERIAL_SCORE == 144000);
static_assert(
  MAX_MATERIAL_SCORE
  < std::numeric_limits<Score>::max());
static_assert(
  -MAX_MATERIAL_SCORE
  > std::numeric_limits<Score>::lowest());

void test_exact_piece_values() {
    expect(piece_value(PAWN) == 100,
           "a pawn has a material value of 100");
    expect(piece_value(KNIGHT) == 320,
           "a knight has a material value of 320");
    expect(piece_value(BISHOP) == 330,
           "a bishop has a material value of 330");
    expect(piece_value(ROOK) == 500,
           "a rook has a material value of 500");
    expect(piece_value(QUEEN) == 900,
           "a queen has a material value of 900");
    expect(piece_value(KING) == 0,
           "a king has no material value");
}

void test_every_piece_owner_and_perspective() {
    std::size_t arrangement_count = 0;

    for (const Color owner : COLORS) {
        for (int type_index = PAWN;
             type_index <= KING;
             ++type_index) {
            const PieceType piece_type =
              PieceType(type_index);

            Position position;
            position.put_piece(
              make_piece(owner, piece_type),
              make_square(FILE_H, RANK_8));

            for (const Color perspective : COLORS) {
                position.set_side_to_move(perspective);
                const Position original = position;
                const Score value = piece_value(piece_type);
                const Score expected =
                  team_of(owner) == team_of(perspective)
                    ? value
                    : -value;

                expect(
                  evaluate(position) == expected,
                  "every piece has the expected value for every owner and side-to-move color");
                expect(
                  material_balance(
                    position,
                    team_of(perspective))
                    == expected,
                  "team material balance matches the side-to-move evaluation");
                expect(
                  material_balance(
                    position,
                    team_of(perspective)
                      == RED_YELLOW
                      ? BLUE_GREEN
                      : RED_YELLOW)
                    == -expected,
                  "opposing team material balances are exact negations");
                expect(
                  positions_equal(position, original),
                  "single-piece evaluation does not mutate the position");
                ++arrangement_count;
            }
        }
    }

    expect(
      arrangement_count == 96,
      "all 96 piece-owner and side-to-move arrangements were evaluated");
}

void test_aggregate_team_symmetry() {
    Position position = make_aggregate_position();

    expect(
      material_balance(position, RED_YELLOW) == -90,
      "Red and Yellow material totals 90 fewer units than Blue and Green");
    expect(
      material_balance(position, BLUE_GREEN) == 90,
      "Blue and Green material totals 90 more units than Red and Yellow");

    for (const Color color : COLORS) {
        position.set_side_to_move(color);
        const Score expected =
          team_of(color) == RED_YELLOW ? -90 : 90;

        expect(
          evaluate(position) == expected,
          "each side-to-move color receives its team material balance");
    }
}

void test_starting_position_balance() {
    Position position = make_starting_position();

    expect(
      material_balance(position, RED_YELLOW) == 0
        && material_balance(position, BLUE_GREEN) == 0,
      "both teams have equal material in the starting position");

    for (const Color color : COLORS) {
        position.set_side_to_move(color);
        const Position original = position;

        expect(
          evaluate(position) == 0,
          "the starting position evaluates to zero for every side");
        expect(
          positions_equal(position, original),
          "starting-position evaluation does not mutate the position");
    }
}

void test_rotational_symmetry() {
    Position position = make_aggregate_position();
    position.set_side_to_move(RED);
    const Position original = position;

    for (std::size_t rotation = 0;
         rotation < COLORS.size();
         ++rotation) {
        expect(
          position.side_to_move() == COLORS[rotation],
          "clockwise rotation advances the side-to-move color");
        expect(
          evaluate(position) == -90,
          "material evaluation is invariant under board, color, and turn rotation");
        expect(
          material_balance(
            position,
            team_of(position.side_to_move()))
            == -90,
          "rotated side-to-move team retains its material balance");
        position = rotate_clockwise(position);
    }

    expect(
      positions_equal(position, original),
      "four clockwise rotations restore the complete position");
}

void test_rule_state_and_nonmutation() {
    Position undecorated = make_aggregate_position();
    undecorated.set_side_to_move(YELLOW);

    Position decorated = undecorated;
    for (const Color color : COLORS) {
        for (const CastlingSide side : CASTLING_SIDES)
            decorated.set_castling_right(color, side);
    }

    decorated.set_en_passant_square(
      RED, make_square(FILE_H, RANK_8));
    decorated.set_en_passant_square(
      BLUE, make_square(FILE_I, RANK_8));
    decorated.set_en_passant_square(
      YELLOW, make_square(FILE_H, RANK_9));
    decorated.set_en_passant_square(
      GREEN, make_square(FILE_I, RANK_9));

    expect(
      decorated.key() != undecorated.key(),
      "castling and en-passant state change the position key");
    expect(
      evaluate(decorated) == evaluate(undecorated),
      "castling and en-passant state do not change material evaluation");

    const Position original = decorated;
    const PositionKey original_key = decorated.key();
    const Score evaluation = evaluate(decorated);
    const Score red_yellow =
      material_balance(decorated, RED_YELLOW);
    const Score blue_green =
      material_balance(decorated, BLUE_GREEN);

    expect(evaluation == -90,
           "decorated position retains its side-to-move material score");
    expect(red_yellow == -90 && blue_green == 90,
           "decorated position retains both team material balances");
    expect(
      decorated.key() == original_key
        && decorated.key() == decorated.recompute_key(),
      "evaluation leaves the cached and recomputed keys unchanged");
    expect(
      positions_equal(decorated, original),
      "evaluation leaves pieces, occupancy, turn, castling, and en-passant state unchanged");
}

void test_capture_updates_and_undo() {
    std::size_t capture_count = 0;

    for (const Color moving_color : COLORS) {
        for (const PieceType captured_type :
             MATERIAL_PIECE_TYPES) {
            Position position;
            position.set_side_to_move(moving_color);

            const Square source =
              make_square(FILE_H, RANK_8);
            const Square destination =
              make_square(FILE_H, RANK_9);
            position.put_piece(
              make_piece(moving_color, ROOK),
              source);
            position.put_piece(
              make_piece(
                next_color(moving_color),
                captured_type),
              destination);

            const Position original = position;
            const PositionKey original_key = position.key();
            const Team moving_team =
              team_of(moving_color);
            const Score before =
              material_balance(position, moving_team);
            const Score captured_value =
              piece_value(captured_type);

            expect(
              before == piece_value(ROOK) - captured_value,
              "pre-capture material balance includes the rook and captured piece");
            expect(
              evaluate(position) == before,
              "pre-capture evaluation uses the moving team perspective");

            const Move move =
              Move::normal(source, destination);
            UndoState undo;
            do_move(position, move, undo);

            const Score after =
              material_balance(position, moving_team);
            expect(
              after - before == captured_value,
              "a capture increases fixed-team material balance by the captured value");
            expect(
              after == piece_value(ROOK),
              "post-capture fixed-team balance contains the surviving rook");
            expect(
              evaluate(position) == -after,
              "post-capture evaluation uses the next opposing team perspective");
            expect(
              position.empty(source)
                && position.piece_on(destination)
                     == make_piece(moving_color, ROOK),
              "the capturing rook occupies the destination");
            expect(
              position.key() == position.recompute_key(),
              "capture evaluation observes a consistent position key");

            undo_move(position, move, undo);

            expect(
              positions_equal(position, original)
                && position.key() == original_key,
              "capture undo restores the complete position and key");
            expect(
              evaluate(position) == before,
              "capture undo restores the original evaluation");
            ++capture_count;
        }
    }

    expect(
      capture_count
        == COLORS.size()
             * MATERIAL_PIECE_TYPES.size(),
      "all 20 color and captured-piece combinations were evaluated");
}

void test_promotion_updates_and_undo() {
    std::size_t quiet_count = 0;
    std::size_t capture_count = 0;

    for (const Color moving_color : COLORS) {
        const std::size_t color_index =
          std::size_t(moving_color);
        const Square source =
          PROMOTION_SOURCES[color_index];

        for (const PieceType promotion :
             PROMOTION_TYPES) {
            const Score promotion_value =
              piece_value(promotion);
            const Team moving_team =
              team_of(moving_color);

            Position quiet;
            quiet.set_side_to_move(moving_color);
            quiet.put_piece(
              make_piece(moving_color, PAWN),
              source);
            const Position quiet_original = quiet;
            const PositionKey quiet_key = quiet.key();
            const Score quiet_before =
              material_balance(quiet, moving_team);
            const Move quiet_move =
              Move::promotion(
                source,
                QUIET_PROMOTION_DESTINATIONS[color_index],
                promotion);
            UndoState quiet_undo;

            do_move(quiet, quiet_move, quiet_undo);

            const Score quiet_after =
              material_balance(quiet, moving_team);
            expect(
              quiet_after - quiet_before
                == promotion_value - piece_value(PAWN),
              "quiet promotion replaces the pawn value with the promoted-piece value");
            expect(
              evaluate(quiet) == -quiet_after,
              "quiet promotion advances evaluation to the opposing team perspective");
            expect(
              quiet.piece_on(
                QUIET_PROMOTION_DESTINATIONS[color_index])
                == make_piece(moving_color, promotion),
              "quiet promotion places the selected piece on the destination");
            expect(
              quiet.key() == quiet.recompute_key(),
              "quiet-promotion evaluation observes a consistent position key");

            undo_move(quiet, quiet_move, quiet_undo);

            expect(
              positions_equal(quiet, quiet_original)
                && quiet.key() == quiet_key,
              "quiet-promotion undo restores the complete position and key");
            expect(
              evaluate(quiet) == quiet_before,
              "quiet-promotion undo restores the original evaluation");
            ++quiet_count;

            Position capture;
            capture.set_side_to_move(moving_color);
            capture.put_piece(
              make_piece(moving_color, PAWN),
              source);
            const Square capture_destination =
              CAPTURE_PROMOTION_DESTINATIONS[color_index];
            capture.put_piece(
              make_piece(next_color(moving_color), ROOK),
              capture_destination);
            const Position capture_original = capture;
            const PositionKey capture_key = capture.key();
            const Score capture_before =
              material_balance(capture, moving_team);
            const Move capture_move =
              Move::promotion(
                source,
                capture_destination,
                promotion);
            UndoState capture_undo;

            do_move(
              capture, capture_move, capture_undo);

            const Score capture_after =
              material_balance(capture, moving_team);
            expect(
              capture_after - capture_before
                == promotion_value
                     - piece_value(PAWN)
                     + piece_value(ROOK),
              "capture promotion adds the captured value and replaces the pawn value");
            expect(
              evaluate(capture) == -capture_after,
              "capture promotion advances evaluation to the opposing team perspective");
            expect(
              capture.piece_on(capture_destination)
                == make_piece(moving_color, promotion),
              "capture promotion places the selected piece on the destination");
            expect(
              capture.key() == capture.recompute_key(),
              "capture-promotion evaluation observes a consistent position key");

            undo_move(
              capture, capture_move, capture_undo);

            expect(
              positions_equal(
                capture, capture_original)
                && capture.key() == capture_key,
              "capture-promotion undo restores the complete position and key");
            expect(
              evaluate(capture) == capture_before,
              "capture-promotion undo restores the original evaluation");
            ++capture_count;
        }
    }

    expect(
      quiet_count
        == COLORS.size() * PROMOTION_TYPES.size(),
      "all 16 quiet color and promotion combinations were evaluated");
    expect(
      capture_count
        == COLORS.size() * PROMOTION_TYPES.size(),
      "all 16 capture color and promotion combinations were evaluated");
}

void test_structural_score_bound() {
    Position position;
    int queen_count = 0;

    for (int square_index = 0;
         square_index < SQUARE_NB;
         ++square_index) {
        const Square square = Square(square_index);
        if (!is_ok(square))
            continue;

        position.put_piece(R_QUEEN, square);
        ++queen_count;
    }

    expect(
      queen_count == PLAYABLE_SQUARE_NB,
      "the structural bound position fills all 160 playable squares");
    expect(
      material_balance(position, RED_YELLOW)
        == MAX_MATERIAL_SCORE,
      "the positive structural material bound is exact");
    expect(
      material_balance(position, BLUE_GREEN)
        == -MAX_MATERIAL_SCORE,
      "the negative structural material bound is exact");

    for (const Color color : COLORS) {
        position.set_side_to_move(color);
        const Position original = position;
        const Score expected =
          team_of(color) == RED_YELLOW
            ? MAX_MATERIAL_SCORE
            : -MAX_MATERIAL_SCORE;

        expect(
          evaluate(position) == expected,
          "structural bound evaluation has the expected sign for every side-to-move color");
        expect(
          positions_equal(position, original),
          "structural bound evaluation does not mutate the position");
    }
}

}  // namespace

int main() {
    test_exact_piece_values();
    test_every_piece_owner_and_perspective();
    test_aggregate_team_symmetry();
    test_starting_position_balance();
    test_rotational_symmetry();
    test_rule_state_and_nonmutation();
    test_capture_updates_and_undo();
    test_promotion_updates_and_undo();
    test_structural_score_bound();

    if (failures != 0) {
        std::cerr << failures
                  << " evaluation test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All evaluation tests passed\n";
    return EXIT_SUCCESS;
}
