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

inline constexpr std::array<CastlingSide, CASTLING_SIDE_NB>
  CASTLING_SIDES = {
    CastlingSide::KING_SIDE,
    CastlingSide::QUEEN_SIDE,
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
        || left.static_evaluation_state()
             != right.static_evaluation_state()
        || left.static_evaluation_state()
             != left.recompute_static_evaluation_state()
        || right.static_evaluation_state()
             != right.recompute_static_evaluation_state()
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
        const PieceType piece_type = PieceType(type_index);
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

[[nodiscard]] constexpr Score score_for(
  Position position,
  Color side_to_move) noexcept {
    position.set_side_to_move(side_to_move);
    return evaluate(position);
}

[[nodiscard]] constexpr Position
make_asymmetric_position() noexcept {
    Position position;
    position.set_side_to_move(RED);

    position.put_piece(R_KING, make_square(FILE_H, RANK_1));
    position.put_piece(R_PAWN, make_square(FILE_G, RANK_2));
    position.put_piece(R_PAWN, make_square(FILE_H, RANK_2));
    position.put_piece(R_KNIGHT, make_square(FILE_F, RANK_6));
    position.put_piece(R_BISHOP, make_square(FILE_E, RANK_5));
    position.put_piece(R_BISHOP, make_square(FILE_J, RANK_6));
    position.put_piece(R_ROOK, make_square(FILE_D, RANK_4));

    position.put_piece(Y_KING, make_square(FILE_G, RANK_14));
    position.put_piece(Y_PAWN, make_square(FILE_G, RANK_11));
    position.put_piece(Y_QUEEN, make_square(FILE_I, RANK_10));
    position.put_piece(Y_ROOK, make_square(FILE_D, RANK_10));

    position.put_piece(B_KING, make_square(FILE_A, RANK_8));
    position.put_piece(B_PAWN, make_square(FILE_C, RANK_7));
    position.put_piece(B_PAWN, make_square(FILE_C, RANK_8));
    position.put_piece(B_KNIGHT, make_square(FILE_E, RANK_8));
    position.put_piece(B_QUEEN, make_square(FILE_F, RANK_10));

    position.put_piece(G_KING, make_square(FILE_N, RANK_7));
    position.put_piece(G_PAWN, make_square(FILE_K, RANK_6));
    position.put_piece(G_BISHOP, make_square(FILE_J, RANK_9));
    position.put_piece(G_ROOK, make_square(FILE_K, RANK_11));

    return position;
}

[[nodiscard]] consteval bool constexpr_evaluation_works() {
    Position position;
    position.put_piece(R_PAWN, make_square(FILE_H, RANK_8));
    position.put_piece(B_ROOK, make_square(FILE_F, RANK_10));
    position.put_piece(Y_QUEEN, make_square(FILE_I, RANK_9));
    position.put_piece(G_KING, make_square(FILE_N, RANK_8));

    const Score red = score_for(position, RED);
    const Score blue = score_for(position, BLUE);
    const Score yellow = score_for(position, YELLOW);
    const Score green = score_for(position, GREEN);

    return material_balance(position, RED_YELLOW) == 500
        && material_balance(position, BLUE_GREEN) == -500
        && red == yellow
        && blue == green
        && red == -blue
        && red != 0;
}

static_assert(constexpr_evaluation_works());
static_assert(
  EvaluationDetail::blend({48, 0}, 48) == 48);
static_assert(
  EvaluationDetail::blend({0, 48}, 0) == 48);
static_assert(
  EvaluationDetail::blend({48, 0}, 24) == 24);
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
static_assert(noexcept(
  evaluate(std::declval<const Position&>())));
static_assert(noexcept(
  material_balance(
    std::declval<const Position&>(),
    RED_YELLOW)));
static_assert(std::numeric_limits<Score>::is_signed);
static_assert(MAX_MATERIAL_SCORE == 144000);
static_assert(MAX_EVALUATION_SCORE == 344000);

void test_material_api() {
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

    for (const Color owner : COLORS) {
        for (int type_index = PAWN;
             type_index <= KING;
             ++type_index) {
            const PieceType piece_type = PieceType(type_index);
            Position position;
            position.put_piece(
              make_piece(owner, piece_type),
              make_square(FILE_H, RANK_8));

            const Score value = piece_value(piece_type);
            const Score expected =
              team_of(owner) == RED_YELLOW
                ? value
                : -value;
            expect(
              material_balance(position, RED_YELLOW) == expected,
              "material balance preserves every piece value and owner");
            expect(
              material_balance(position, BLUE_GREEN) == -expected,
              "opposing material balances are exact negations");
        }
    }
}

void test_team_perspective_and_nonmutation() {
    Position position = make_asymmetric_position();
    const Position original = position;

    const Score red = score_for(position, RED);
    const Score blue = score_for(position, BLUE);
    const Score yellow = score_for(position, YELLOW);
    const Score green = score_for(position, GREEN);

    expect(red == yellow,
           "allied colors receive the same static score");
    expect(blue == green,
           "the opposing allied colors receive the same static score");
    expect(red == -blue,
           "opposing team perspectives are exact negations");
    expect(red != material_balance(position, RED_YELLOW),
           "the asymmetric fixture exercises positional evaluation");
    expect(positions_equal(position, original),
           "evaluation does not mutate any position field");
}

void test_starting_position_balance() {
    Position position = make_starting_position();

    expect(
      material_balance(position, RED_YELLOW) == 0,
      "the starting teams have equal material");

    for (const Color color : COLORS) {
        position.set_side_to_move(color);
        const Position original = position;
        expect(evaluate(position) == 0,
               "the rotationally balanced starting position scores zero");
        expect(positions_equal(position, original),
               "starting-position evaluation is non-mutating");
    }
}

void test_rotational_symmetry() {
    Position position = make_asymmetric_position();
    const Position original = position;
    const Score original_evaluation = evaluate(position);

    for (int rotation = 0; rotation < COLOR_NB; ++rotation) {
        expect(
          evaluate(position) == original_evaluation,
          "board, color, and turn rotation preserves evaluation");
        position = rotate_clockwise(position);
    }

    expect(positions_equal(position, original),
           "four rotations restore the complete position");
}

void test_rule_state_is_evaluation_neutral() {
    Position plain = make_asymmetric_position();
    Position decorated = plain;

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

    expect(decorated.key() != plain.key(),
           "rule state changes the position key");
    expect(evaluate(decorated) == evaluate(plain),
           "inactive rule state does not affect static evaluation");
    expect(decorated.key() == decorated.recompute_key(),
           "evaluation preserves the cached position key");
}

void test_pawn_advancement_and_orientation() {
    Position home;
    home.put_piece(R_PAWN, make_square(FILE_H, RANK_2));

    Position advanced;
    advanced.put_piece(R_PAWN, make_square(FILE_H, RANK_10));

    expect(score_for(advanced, RED) > score_for(home, RED),
           "an advanced pawn scores above the same pawn near home");

    const Score expected = score_for(advanced, RED);
    advanced.set_side_to_move(RED);
    for (int rotation = 0; rotation < COLOR_NB; ++rotation) {
        expect(evaluate(advanced) == expected,
               "pawn advancement has the same value in every orientation");
        advanced = rotate_clockwise(advanced);
    }
}

void test_pawn_structure() {
    using EvaluationDetail::PawnFileCounts;
    using EvaluationDetail::pawn_structure_score;

    PawnFileCounts healthy_files{};
    healthy_files[FILE_G] = 1;
    healthy_files[FILE_H] = 1;
    Bitboard healthy_pawns;
    const Square g4 = make_square(FILE_G, RANK_4);
    const Square h5 = make_square(FILE_H, RANK_5);
    healthy_pawns.set(g4);
    healthy_pawns.set(h5);

    const EvaluationDetail::TaperedScore unsupported =
      pawn_structure_score(
        healthy_files, healthy_pawns, {});
    const EvaluationDetail::TaperedScore supported =
      pawn_structure_score(
        healthy_files,
        healthy_pawns,
        pawn_attacks(RED, g4));
    expect(
      unsupported.middlegame == 0
        && unsupported.endgame == 0,
      "adjacent undoubled pawn files receive no structure penalty");
    expect(
      supported.middlegame
          == EvaluationDetail::SUPPORTED_PAWN_BONUS.middlegame
        && supported.endgame
             == EvaluationDetail::SUPPORTED_PAWN_BONUS.endgame,
      "a same-color pawn attack gives one exact support bonus");

    PawnFileCounts weak_files{};
    weak_files[FILE_H] = 2;
    const EvaluationDetail::TaperedScore weak =
      pawn_structure_score(
        weak_files, healthy_pawns, {});
    expect(
      weak.middlegame
          == -EvaluationDetail::DOUBLED_PAWN_PENALTY.middlegame
             - 2
               * EvaluationDetail::ISOLATED_PAWN_PENALTY.middlegame
        && weak.endgame
             == -EvaluationDetail::DOUBLED_PAWN_PENALTY.endgame
                - 2
                  * EvaluationDetail::ISOLATED_PAWN_PENALTY.endgame,
      "doubled isolated pawns receive both exact penalties");

    Position structure;
    structure.put_piece(R_PAWN, g4);
    structure.put_piece(R_PAWN, h5);
    structure.put_piece(
      R_PAWN, make_square(FILE_H, RANK_7));
    structure.put_piece(
      B_PAWN, make_square(FILE_C, RANK_8));
    structure.set_side_to_move(RED);
    const Position original = structure;
    const Score expected = evaluate(structure);
    expect(
      expected == -score_for(structure, BLUE),
      "pawn structure preserves opposing team signs");

    for (int rotation = 0;
         rotation < COLOR_NB;
         ++rotation) {
        expect(
          evaluate(structure) == expected,
          "relative pawn files preserve pawn-structure rotation symmetry");
        structure = rotate_clockwise(structure);
    }
    expect(
      positions_equal(structure, original),
      "four pawn-structure rotations restore the complete position");
}

void test_centralization_and_mobility() {
    Position rim;
    rim.put_piece(R_KNIGHT, make_square(FILE_D, RANK_1));

    Position center;
    center.put_piece(R_KNIGHT, make_square(FILE_H, RANK_8));

    expect(score_for(center, RED) > score_for(rim, RED),
           "a centralized mobile knight scores above a rim knight");

    Position closed_line;
    closed_line.put_piece(R_ROOK, make_square(FILE_H, RANK_5));
    closed_line.put_piece(R_ROOK, make_square(FILE_H, RANK_10));
    closed_line.put_piece(R_PAWN, make_square(FILE_H, RANK_8));
    closed_line.put_piece(B_PAWN, make_square(FILE_H, RANK_12));

    Position semi_open_line;
    semi_open_line.put_piece(R_ROOK, make_square(FILE_H, RANK_5));
    semi_open_line.put_piece(R_ROOK, make_square(FILE_H, RANK_10));
    semi_open_line.put_piece(R_PAWN, make_square(FILE_G, RANK_8));
    semi_open_line.put_piece(B_PAWN, make_square(FILE_H, RANK_12));

    Position open_line;
    open_line.put_piece(R_ROOK, make_square(FILE_H, RANK_5));
    open_line.put_piece(R_ROOK, make_square(FILE_H, RANK_10));
    open_line.put_piece(R_PAWN, make_square(FILE_G, RANK_8));
    open_line.put_piece(B_PAWN, make_square(FILE_G, RANK_12));

    expect(
      score_for(semi_open_line, RED)
        > score_for(closed_line, RED),
      "a semi-open rook line improves mobility and evaluation");
    expect(
      score_for(open_line, RED)
        > score_for(semi_open_line, RED),
      "a pawn-free rook line scores above a semi-open line");
}

void test_team_threats() {
    Position quiet;
    quiet.put_piece(R_KNIGHT, make_square(FILE_G, RANK_7));
    quiet.put_piece(B_QUEEN, make_square(FILE_I, RANK_10));

    Position attacking;
    attacking.put_piece(R_KNIGHT, make_square(FILE_H, RANK_8));
    attacking.put_piece(B_QUEEN, make_square(FILE_I, RANK_10));

    expect(
      knight_attacks(make_square(FILE_H, RANK_8))
        .test(make_square(FILE_I, RANK_10)),
      "the attacking fixture places the queen in the knight attack map");
    expect(
      !knight_attacks(make_square(FILE_G, RANK_7))
         .test(make_square(FILE_I, RANK_10)),
      "the quiet fixture leaves the queen outside the knight attack map");
    expect(score_for(attacking, RED) > score_for(quiet, RED),
           "attacking an opposing queen improves the team score");
}

void test_king_pressure_curve() {
    using EvaluationDetail::KingPressure;
    using EvaluationDetail::king_pressure_penalty;

    const EvaluationDetail::TaperedScore none =
      king_pressure_penalty(KingPressure{}, 0);
    const EvaluationDetail::TaperedScore single =
      king_pressure_penalty({1, 5}, 1);
    const EvaluationDetail::TaperedScore coordinated =
      king_pressure_penalty({3, 14}, 5);
    const EvaluationDetail::TaperedScore saturated =
      king_pressure_penalty({20, 100}, 8);
    const EvaluationDetail::TaperedScore beyond_cap =
      king_pressure_penalty({40, 300}, 8);

    expect(
      none.middlegame == 0
        && none.endgame == 0,
      "an unattacked king receives no nonlinear pressure penalty");
    expect(
      coordinated.middlegame > single.middlegame
        && coordinated.endgame > single.endgame,
      "additional weighted attackers and ring coverage increase king pressure");
    expect(
      saturated.middlegame == beyond_cap.middlegame
        && saturated.endgame == beyond_cap.endgame,
      "king pressure remains bounded beyond the saturation point");
    expect(
      saturated.middlegame
          == EvaluationDetail::KING_PRESSURE_INPUT_CAP
             * EvaluationDetail::KING_PRESSURE_INPUT_CAP
             / EvaluationDetail::KING_PRESSURE_DIVISOR
        && saturated.endgame
             == saturated.middlegame
                / EvaluationDetail::KING_PRESSURE_ENDGAME_DIVISOR,
      "the pressure cap and endgame scaling are exact");
}

void test_king_safety_and_shelter() {
    Position exposed;
    exposed.put_piece(R_KING, make_square(FILE_H, RANK_1));
    exposed.put_piece(R_PAWN, make_square(FILE_D, RANK_2));
    exposed.put_piece(R_PAWN, make_square(FILE_E, RANK_2));
    exposed.put_piece(R_PAWN, make_square(FILE_F, RANK_2));

    Position sheltered;
    sheltered.put_piece(R_KING, make_square(FILE_H, RANK_1));
    sheltered.put_piece(R_PAWN, make_square(FILE_G, RANK_2));
    sheltered.put_piece(R_PAWN, make_square(FILE_H, RANK_2));
    sheltered.put_piece(R_PAWN, make_square(FILE_I, RANK_2));

    expect(score_for(sheltered, RED) > score_for(exposed, RED),
           "a three-pawn king shelter improves evaluation");

    Position safe_king;
    safe_king.put_piece(R_KING, make_square(FILE_H, RANK_1));
    safe_king.put_piece(B_ROOK, make_square(FILE_G, RANK_8));

    Position attacked_king;
    attacked_king.put_piece(R_KING, make_square(FILE_H, RANK_1));
    attacked_king.put_piece(B_ROOK, make_square(FILE_H, RANK_8));

    expect(score_for(safe_king, RED) > score_for(attacked_king, RED),
           "opposing attacks on a king reduce its team score");

    Position single_color_attack;
    single_color_attack.put_piece(
      R_KING, make_square(FILE_H, RANK_1));
    single_color_attack.put_piece(
      B_ROOK, make_square(FILE_H, RANK_8));
    single_color_attack.put_piece(
      B_ROOK, make_square(FILE_G, RANK_8));

    Position coordinated_attack;
    coordinated_attack.put_piece(
      R_KING, make_square(FILE_H, RANK_1));
    coordinated_attack.put_piece(
      B_ROOK, make_square(FILE_H, RANK_8));
    coordinated_attack.put_piece(
      G_ROOK, make_square(FILE_G, RANK_8));

    expect(
      score_for(single_color_attack, RED)
        > score_for(coordinated_attack, RED),
      "king danger grows when both opposing colors coordinate pressure");
}

void test_bishop_pair() {
    Position split_pair;
    split_pair.put_piece(R_BISHOP, make_square(FILE_F, RANK_7));
    split_pair.put_piece(Y_BISHOP, make_square(FILE_I, RANK_8));

    Position same_color_pair;
    same_color_pair.put_piece(R_BISHOP, make_square(FILE_F, RANK_7));
    same_color_pair.put_piece(R_BISHOP, make_square(FILE_I, RANK_8));

    expect(
      score_for(same_color_pair, RED)
        > score_for(split_pair, RED),
      "two bishops owned by one color receive a bishop-pair bonus");
}

void test_move_update_and_undo() {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(R_ROOK, make_square(FILE_H, RANK_5));
    position.put_piece(B_QUEEN, make_square(FILE_H, RANK_8));

    const Position original = position;
    const Score original_evaluation = evaluate(position);
    const Score original_material =
      material_balance(position, RED_YELLOW);
    const Move move = Move::normal(
      make_square(FILE_H, RANK_5),
      make_square(FILE_H, RANK_8));
    UndoState undo;

    do_move(position, move, undo);

    expect(
      material_balance(position, RED_YELLOW)
        == original_material + QUEEN_VALUE,
      "a capture updates the preserved material API exactly");
    expect(position.key() == position.recompute_key(),
           "post-move evaluation observes a consistent key");
    expect(evaluate(position) != original_evaluation,
           "a material-changing move changes static evaluation");

    undo_move(position, move, undo);

    expect(positions_equal(position, original),
           "move undo restores the complete position");
    expect(evaluate(position) == original_evaluation,
           "move undo restores the complete evaluation");
}

void test_score_bound() {
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

    expect(queen_count == PLAYABLE_SQUARE_NB,
           "the bound fixture fills all playable squares");
    expect(
      material_balance(position, RED_YELLOW)
        == MAX_MATERIAL_SCORE,
      "the material upper bound remains exact");

    const Score red = score_for(position, RED);
    const Score blue = score_for(position, BLUE);
    expect(red <= MAX_EVALUATION_SCORE
             && red >= -MAX_EVALUATION_SCORE,
           "static evaluation remains within its declared bounds");
    expect(red == -blue,
           "bounded opposing scores remain exact negations");

    Position pawn_heavy;
    for (int square_index = 0;
         square_index < SQUARE_NB;
         ++square_index) {
        const Square square = Square(square_index);
        if (is_ok(square))
            pawn_heavy.put_piece(R_PAWN, square);
    }
    const Score pawn_red =
      score_for(pawn_heavy, RED);
    const Score pawn_blue =
      score_for(pawn_heavy, BLUE);
    expect(
      pawn_red <= MAX_EVALUATION_SCORE
        && pawn_red >= -MAX_EVALUATION_SCORE
        && pawn_red == -pawn_blue,
      "maximal pawn structure remains bounded and team-antisymmetric");
}

}  // namespace

int main() {
    test_material_api();
    test_team_perspective_and_nonmutation();
    test_starting_position_balance();
    test_rotational_symmetry();
    test_rule_state_is_evaluation_neutral();
    test_pawn_advancement_and_orientation();
    test_pawn_structure();
    test_centralization_and_mobility();
    test_team_threats();
    test_king_pressure_curve();
    test_king_safety_and_shelter();
    test_bishop_pair();
    test_move_update_and_undo();
    test_score_bound();

    if (failures != 0) {
        std::cerr << failures
                  << " evaluation test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All evaluation tests passed\n";
    return EXIT_SUCCESS;
}
