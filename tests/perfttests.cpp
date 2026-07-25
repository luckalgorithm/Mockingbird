#include "perft.h"
#include "setup.h"

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

// These counts were produced by copy_perft().
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

[[nodiscard]] constexpr const PerftEntry* find_entry(
  const PerftList& entries,
  Move target) noexcept {
    for (const PerftEntry& entry : entries) {
        if (entry.move == target)
            return &entry;
    }

    return nullptr;
}

[[nodiscard]] constexpr std::uint64_t sum_nodes(
  const PerftList& entries) noexcept {
    std::uint64_t nodes = 0;
    for (const PerftEntry& entry : entries)
        nodes += entry.nodes;

    return nodes;
}

// Each child is traversed in a separate Position copy. This traversal does not
// use perft() or undo_move().
[[nodiscard]] std::uint64_t copy_perft(
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
        nodes += copy_perft(child, depth - 1);
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

void test_copy_traversal_and_restoration() {
    Position position = special_position();
    const Position original = position;

    for (int depth = 0; depth <= 3; ++depth) {
        const std::uint64_t expected =
          copy_perft(original, depth);
        const std::uint64_t actual =
          perft(position, depth);

        expect(
          expected
            == SPECIAL_REGRESSION_COUNTS[
                 std::size_t(depth)],
          "copy traversal matches the special-position regression count");
        expect(actual == expected,
               "perft matches copy-based traversal");
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
          copy_perft(original, depth);
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
      copy_perft(position, 1);
    const std::uint64_t expected_depth_two =
      copy_perft(position, 2);

    for (int rotation = 0; rotation < COLOR_NB; ++rotation) {
        const Position original = position;

        expect(
          copy_perft(position, 1)
              == expected_depth_one
            && copy_perft(position, 2)
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

void test_starting_position_copy_traversal() {
    Position position = make_starting_position();
    const Position original = position;

    for (int depth = 1; depth <= 4; ++depth) {
        const std::uint64_t expected =
          copy_perft(original, depth);
        const std::uint64_t actual =
          perft(position, depth);

        expect(actual == expected,
               "starting-position perft matches copy traversal");
        expect(positions_equal(position, original),
               "starting-position perft restores all state");
    }
}

void test_divide_base_depth_and_order() {
    Position position = special_position();
    const Position original = position;

    const PerftList depth_zero =
      perft_divide(position, 0);
    expect(depth_zero.empty(),
           "depth-zero divide has no root-move entries");
    expect(positions_equal(position, original),
           "depth-zero divide preserves all position state");

    Position generator = position;
    MoveList legal_moves;
    generate_legal_moves(generator, legal_moves);

    const PerftList entries =
      perft_divide(position, 1);

    expect(entries.size() == legal_moves.size(),
           "depth-one divide contains every legal root move");

    bool exact_order =
      entries.size() == legal_moves.size();
    bool unit_counts = true;
    for (std::size_t index = 0;
         index < entries.size();
         ++index) {
        exact_order &=
          entries[index].move == legal_moves[index];
        unit_counts &= entries[index].nodes == 1;
    }

    expect(exact_order,
           "divide preserves legal-move generation order");
    expect(unit_counts,
           "every depth-one divide entry contains one node");
    expect(
      sum_nodes(entries) == perft(position, 1),
      "depth-one divide sum equals perft");
    expect(positions_equal(position, original),
           "depth-one divide preserves all position state");
}

void test_divide_copy_counts_and_sums() {
    Position position = special_position();
    const Position original = position;

    Position generator = original;
    MoveList legal_moves;
    generate_legal_moves(generator, legal_moves);

    for (int depth = 1; depth <= 3; ++depth) {
        const PerftList entries =
          perft_divide(position, depth);

        bool exact_entries =
          entries.size() == legal_moves.size();
        for (std::size_t index = 0;
             index < entries.size();
             ++index) {
            if (index >= legal_moves.size()) {
                exact_entries = false;
                break;
            }

            const PerftEntry& entry = entries[index];
            if (entry.move != legal_moves[index])
                exact_entries = false;

            Position child = original;
            UndoState unused;
            do_move(child, entry.move, unused);
            if (entry.nodes
                != copy_perft(child, depth - 1))
                exact_entries = false;
        }

        expect(
          exact_entries,
          "every divide entry matches copy-per-child traversal");
        expect(
          sum_nodes(entries) == perft(position, depth),
          "divide sum equals perft through depth three");
        expect(positions_equal(position, original),
               "divide restores the special position");
    }
}

void test_divide_special_moves() {
    Position position = special_position();
    const Position original = position;
    const PerftList entries =
      perft_divide(position, 2);

    const CastlingGeometry& kingside =
      castling_geometry(RED, CastlingSide::KING_SIDE);
    const CastlingGeometry& queenside =
      castling_geometry(RED, CastlingSide::QUEEN_SIDE);

    expect(
      find_entry(
        entries,
        Move::castling(
          kingside.king_source,
          kingside.king_destination))
        != nullptr,
      "divide contains kingside castling");
    expect(
      find_entry(
        entries,
        Move::castling(
          queenside.king_source,
          queenside.king_destination))
        != nullptr,
      "divide contains queenside castling");
    expect(
      find_entry(
        entries,
        Move::en_passant(
          make_square(FILE_D, RANK_5),
          make_square(FILE_C, RANK_6)))
        != nullptr,
      "divide contains en passant");

    bool all_promotions_present = true;
    for (const PieceType promotion :
         std::array{QUEEN, ROOK, BISHOP, KNIGHT}) {
        all_promotions_present &=
          find_entry(
            entries,
            Move::promotion(
              make_square(FILE_B, RANK_10),
              make_square(FILE_B, RANK_11),
              promotion))
          != nullptr;
        all_promotions_present &=
          find_entry(
            entries,
            Move::promotion(
              make_square(FILE_B, RANK_10),
              make_square(FILE_C, RANK_11),
              promotion))
          != nullptr;
    }

    expect(all_promotions_present,
           "divide contains every quiet and capture promotion");
    expect(positions_equal(position, original),
           "special-move divide restores all position state");
}

void test_divide_terminal_capture() {
    constexpr Move capture = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));

    Position position = king_capture_position();
    const Position original = position;

    const PerftList horizon =
      perft_divide(position, 1);
    const PerftEntry* horizon_capture =
      find_entry(horizon, capture);
    expect(horizon_capture != nullptr
             && horizon_capture->nodes == 1,
           "king capture counts at the depth-one horizon");
    expect(positions_equal(position, original),
           "depth-one king-capture divide restores the position");

    const PerftList continued =
      perft_divide(position, 2);
    const PerftEntry* continued_capture =
      find_entry(continued, capture);
    expect(continued_capture != nullptr
             && continued_capture->nodes == 0,
           "king capture has no continuation before the horizon");
    expect(
      sum_nodes(continued) == perft(position, 2),
      "king-capture divide sum equals perft");
    expect(positions_equal(position, original),
           "depth-two king-capture divide restores the position");

    Position terminal = original;
    UndoState unused;
    do_move(terminal, capture, unused);
    const Position terminal_original = terminal;
    expect(perft_divide(terminal, 1).empty(),
           "an already-terminal position has no divide entries");
    expect(positions_equal(terminal, terminal_original),
           "terminal divide preserves all position state");
}

void test_divide_rotational_totals() {
    Position position = special_position();
    const std::uint64_t expected =
      sum_nodes(perft_divide(position, 2));

    for (int rotation = 0;
         rotation < COLOR_NB;
         ++rotation) {
        const Position original = position;
        const PerftList entries =
          perft_divide(position, 2);

        expect(sum_nodes(entries) == expected,
               "divide total is invariant under rotation");
        expect(
          sum_nodes(entries) == perft(position, 2),
          "rotated divide total equals perft");
        expect(positions_equal(position, original),
               "rotated divide restores all position state");

        position = rotate_clockwise(position);
    }
}

void test_starting_position_divide_sum() {
    Position position = make_starting_position();
    const Position original = position;
    const PerftList entries =
      perft_divide(position, 4);

    expect(
      sum_nodes(entries) == perft(position, 4),
      "starting-position depth-four divide sum equals perft");
    expect(positions_equal(position, original),
           "starting-position divide restores all state");
}

}  // namespace

int main() {
    test_depth_zero_and_one();
    test_copy_traversal_and_restoration();
    test_special_move_branches();
    test_terminal_king_capture();
    test_rotational_symmetry();
    test_starting_position_copy_traversal();
    test_divide_base_depth_and_order();
    test_divide_copy_counts_and_sums();
    test_divide_special_moves();
    test_divide_terminal_capture();
    test_divide_rotational_totals();
    test_starting_position_divide_sum();

    if (failures != 0) {
        std::cerr << failures
                  << " perft test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All perft tests passed\n";
    return EXIT_SUCCESS;
}
