#include "perft.h"

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

using namespace Mockingbird;

inline constexpr std::array<CastlingSide, CASTLING_SIDE_NB>
  CASTLING_SIDES = {
    CastlingSide::KING_SIDE,
    CastlingSide::QUEEN_SIDE,
};

// These counts were produced by reference_perft()'s copy-per-child traversal.
inline constexpr std::array<std::uint64_t, 4>
  SPECIAL_REGRESSION_COUNTS = {
    1,
    37,
    210,
    1050,
};

inline constexpr std::array<std::uint64_t, 2>
  KING_CAPTURE_REGRESSION_COUNTS = {
    21,
    149,
};

// Fixed perft counts for starting_position().
inline constexpr std::array<std::uint64_t, 4>
  START_REGRESSION_COUNTS = {
    20,
    395,
    7800,
    152050,
};

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
        const PieceType piece_type =
          PieceType(type_index);
        if (left.pieces(piece_type)
              != right.pieces(piece_type))
            return false;

        for (int color_index = 0;
             color_index < COLOR_NB;
             ++color_index) {
            const Color color = Color(color_index);
            if (left.pieces(color, piece_type)
                  != right.pieces(color, piece_type))
                return false;
        }
    }

    return true;
}

[[nodiscard]] constexpr bool contains_move(
  const MoveList& moves,
  Move target) noexcept {
    for (const Move move : moves) {
        if (move == target)
            return true;
    }

    return false;
}

// Each child is traversed in a separate Position copy. The reference does not
// use perft() or undo_move().
[[nodiscard]] std::uint64_t reference_perft(
  const Position& position,
  int depth) {
    if (depth == 0)
        return 1;

    Position generator = position;
    MoveList moves;
    generate_legal_moves(generator, moves);

    std::uint64_t nodes = 0;
    for (const Move move : moves) {
        Position child = position;
        UndoState unused;
        do_move(child, move, unused);
        nodes += reference_perft(child, depth - 1);
    }

    return nodes;
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

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        const Color rotated_color = next_color(color);

        for (const CastlingSide side : CASTLING_SIDES) {
            if (position.has_castling_right(color, side))
                rotated.set_castling_right(
                  rotated_color, side);
        }

        const Square target =
          position.en_passant_square(color);
        if (target != SQ_NONE) {
            rotated.set_en_passant_square(
              rotated_color,
              rotate_clockwise(target));
        }
    }

    return rotated;
}

[[nodiscard]] constexpr Position kings_only_position() noexcept {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      B_KING, make_square(FILE_D, RANK_8));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_13));
    position.put_piece(
      G_KING, make_square(FILE_K, RANK_8));
    return position;
}

[[nodiscard]] constexpr Position special_position() noexcept {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_1));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    position.put_piece(
      Y_KING, make_square(FILE_G, RANK_14));
    position.put_piece(
      G_KING, make_square(FILE_N, RANK_8));

    position.put_piece(
      R_ROOK, make_square(FILE_D, RANK_1));
    position.put_piece(
      R_ROOK, make_square(FILE_K, RANK_1));
    position.set_castling_right(
      RED, CastlingSide::KING_SIDE);
    position.set_castling_right(
      RED, CastlingSide::QUEEN_SIDE);

    position.put_piece(
      R_PAWN, make_square(FILE_D, RANK_5));
    position.put_piece(
      B_PAWN, make_square(FILE_D, RANK_6));
    position.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_6));

    position.put_piece(
      R_PAWN, make_square(FILE_B, RANK_10));
    position.put_piece(
      G_ROOK, make_square(FILE_C, RANK_11));
    return position;
}

[[nodiscard]] constexpr Position king_capture_position() noexcept {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      B_KING, make_square(FILE_D, RANK_8));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_13));
    position.put_piece(
      G_KING, make_square(FILE_F, RANK_8));
    position.put_piece(
      R_ROOK, make_square(FILE_F, RANK_5));
    return position;
}

[[nodiscard]] constexpr Position starting_position() noexcept {
    constexpr std::array<PieceType, 8> RED_BACK_RANK = {
      ROOK,
      KNIGHT,
      BISHOP,
      QUEEN,
      KING,
      BISHOP,
      KNIGHT,
      ROOK,
    };
    constexpr std::array<PieceType, 8> BLUE_BACK_RANK = {
      ROOK,
      KNIGHT,
      BISHOP,
      KING,
      QUEEN,
      BISHOP,
      KNIGHT,
      ROOK,
    };
    constexpr std::array<PieceType, 8> YELLOW_BACK_RANK = {
      ROOK,
      KNIGHT,
      BISHOP,
      KING,
      QUEEN,
      BISHOP,
      KNIGHT,
      ROOK,
    };
    constexpr std::array<PieceType, 8> GREEN_BACK_RANK = {
      ROOK,
      KNIGHT,
      BISHOP,
      QUEEN,
      KING,
      BISHOP,
      KNIGHT,
      ROOK,
    };

    Position position;

    for (int offset = 0; offset < 8; ++offset) {
        const File file = File(int(FILE_D) + offset);
        const Rank rank = Rank(int(RANK_4) + offset);
        const std::size_t index =
          static_cast<std::size_t>(offset);

        position.put_piece(
          make_piece(RED, RED_BACK_RANK[index]),
          make_square(file, RANK_1));
        position.put_piece(
          R_PAWN, make_square(file, RANK_2));

        position.put_piece(
          make_piece(YELLOW, YELLOW_BACK_RANK[index]),
          make_square(file, RANK_14));
        position.put_piece(
          Y_PAWN, make_square(file, RANK_13));

        position.put_piece(
          make_piece(BLUE, BLUE_BACK_RANK[index]),
          make_square(FILE_A, rank));
        position.put_piece(
          B_PAWN, make_square(FILE_B, rank));

        position.put_piece(
          make_piece(GREEN, GREEN_BACK_RANK[index]),
          make_square(FILE_N, rank));
        position.put_piece(
          G_PAWN, make_square(FILE_M, rank));
    }

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        for (const CastlingSide side : CASTLING_SIDES)
            position.set_castling_right(color, side);
    }

    return position;
}

void test_depth_zero_and_one() {
    Position position = kings_only_position();
    const Position original = position;

    expect(perft(position, 0) == 1,
           "depth zero contains the current position");
    expect(positions_equal(position, original),
           "depth-zero perft preserves the position");

    Position generator = position;
    MoveList legal_moves;
    generate_legal_moves(generator, legal_moves);

    expect(
      perft(position, 1)
        == static_cast<std::uint64_t>(legal_moves.size()),
      "depth one equals the number of legal moves");
    expect(positions_equal(position, original),
           "depth-one perft preserves the position");
}

void test_copy_reference_and_restoration() {
    Position position = special_position();
    const Position original = position;

    for (int depth = 0; depth <= 3; ++depth) {
        const std::uint64_t expected =
          reference_perft(original, depth);
        const std::uint64_t actual =
          perft(position, depth);

        expect(
          expected
            == SPECIAL_REGRESSION_COUNTS[
                 std::size_t(depth)],
          "copy traversal matches the special-position regression count");
        expect(actual == expected,
               "perft matches copy-based reference traversal");
        expect(positions_equal(position, original),
               "perft restores all position state");
    }
}

void test_special_move_branches() {
    Position position = special_position();
    MoveList moves;
    generate_legal_moves(position, moves);

    const CastlingGeometry& kingside =
      castling_geometry(RED, CastlingSide::KING_SIDE);
    const CastlingGeometry& queenside =
      castling_geometry(RED, CastlingSide::QUEEN_SIDE);

    expect(contains_move(
             moves,
             Move::castling(
               kingside.king_source,
               kingside.king_destination)),
           "special fixture contains kingside castling");
    expect(contains_move(
             moves,
             Move::castling(
               queenside.king_source,
               queenside.king_destination)),
           "special fixture contains queenside castling");
    expect(contains_move(
             moves,
             Move::en_passant(
               make_square(FILE_D, RANK_5),
               make_square(FILE_C, RANK_6))),
           "special fixture contains en passant");

    for (const PieceType promotion :
         std::array{QUEEN, ROOK, BISHOP, KNIGHT}) {
        expect(contains_move(
                 moves,
                 Move::promotion(
                   make_square(FILE_B, RANK_10),
                   make_square(FILE_B, RANK_11),
                   promotion)),
               "special fixture contains quiet promotion");
        expect(contains_move(
                 moves,
                 Move::promotion(
                   make_square(FILE_B, RANK_10),
                   make_square(FILE_C, RANK_11),
                   promotion)),
               "special fixture contains capture promotion");
    }
}

void test_terminal_king_capture() {
    constexpr Square f5 =
      make_square(FILE_F, RANK_5);
    constexpr Square f8 =
      make_square(FILE_F, RANK_8);
    constexpr Move capture = Move::normal(f5, f8);

    Position position = king_capture_position();
    const Position original = position;
    MoveList moves;
    generate_legal_moves(position, moves);
    expect(contains_move(moves, capture),
           "opposing-king capture is a legal root branch");

    Position terminal = position;
    UndoState unused;
    do_move(terminal, capture, unused);
    const Position terminal_original = terminal;

    expect(perft(terminal, 0) == 1,
           "a terminal position is one depth-zero node");
    expect(perft(terminal, 1) == 0
             && perft(terminal, 2) == 0,
           "a captured king ends deeper traversal");
    expect(positions_equal(terminal, terminal_original),
           "terminal perft preserves the position");

    for (int depth = 1; depth <= 2; ++depth) {
        const std::uint64_t expected =
          reference_perft(original, depth);
        expect(
          expected
            == KING_CAPTURE_REGRESSION_COUNTS[
                 std::size_t(depth - 1)],
          "copy traversal matches the king-capture regression count");
        expect(
          perft(position, depth)
            == expected,
          "king-capture tree matches copy-based traversal");
        expect(positions_equal(position, original),
               "king-capture perft restores the root");
    }
}

void test_rotational_symmetry() {
    Position position = special_position();
    const std::uint64_t expected_depth_one =
      reference_perft(position, 1);
    const std::uint64_t expected_depth_two =
      reference_perft(position, 2);

    for (int rotation = 0; rotation < COLOR_NB; ++rotation) {
        const Position original = position;

        expect(
          reference_perft(position, 1)
              == expected_depth_one
            && reference_perft(position, 2)
                 == expected_depth_two,
          "copy-based counts are invariant under rotation");
        expect(perft(position, 1) == expected_depth_one
                 && perft(position, 2) == expected_depth_two,
               "perft counts are invariant under rotation");
        expect(positions_equal(position, original),
               "rotated perft preserves the position");

        position = rotate_clockwise(position);
    }
}

void test_starting_position_regression() {
    Position position = starting_position();
    const Position original = position;

    for (int depth = 1; depth <= 4; ++depth) {
        const std::uint64_t nodes =
          perft(position, depth);

        expect(
          nodes
            == START_REGRESSION_COUNTS[
                 std::size_t(depth - 1)],
          "starting-position perft matches the regression count");
        expect(positions_equal(position, original),
               "starting-position perft restores all state");
    }
}

}  // namespace

int main() {
    test_depth_zero_and_one();
    test_copy_reference_and_restoration();
    test_special_move_branches();
    test_terminal_king_capture();
    test_rotational_symmetry();
    test_starting_position_regression();

    if (failures != 0) {
        std::cerr << failures
                  << " perft test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All perft tests passed\n";
    return EXIT_SUCCESS;
}
