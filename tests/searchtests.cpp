#include "search.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
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

[[nodiscard]] constexpr bool contains_move_type(
  const MoveList& moves,
  MoveType expected) noexcept {
    for (const Move move : moves) {
        if (move.type() == expected)
            return true;
    }

    return false;
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

template<std::size_t Size>
[[nodiscard]] PositionHistory make_history(
  const std::array<PositionKey, Size>& keys) {
    static_assert(Size > 0);

    PositionHistory history{keys[0]};
    for (std::size_t index = 1;
         index < keys.size();
         ++index)
        history.push(keys[index]);

    return history;
}

// Popping a copy exposes every stored key in reverse chronological order.
template<std::size_t Size>
[[nodiscard]] bool history_matches(
  PositionHistory history,
  const std::array<PositionKey, Size>& expected) {
    static_assert(Size > 0);

    if (history.size() != expected.size())
        return false;

    for (std::size_t index = expected.size();
         index-- > 1;) {
        if (history.current_key() != expected[index])
            return false;

        history.pop(expected[index]);
    }

    return history.size() == 1
        && history.current_key() == expected[0];
}

[[nodiscard]] constexpr Square rotate_clockwise(
  Square square) noexcept {
    return make_square(
      File(int(rank_of(square))),
      Rank(BOARD_FILES + 1 - int(file_of(square))));
}

[[nodiscard]] constexpr Move rotate_clockwise(
  Move move) noexcept {
    assert(move.type() == MoveType::NORMAL);
    return Move::normal(
      rotate_clockwise(move.from()),
      rotate_clockwise(move.to()));
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

[[nodiscard]] constexpr Position
kings_only_position() noexcept {
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

[[nodiscard]] constexpr Position
material_tactic_position() noexcept {
    Position position = kings_only_position();
    position.put_piece(
      R_ROOK, make_square(FILE_F, RANK_5));
    position.put_piece(
      B_QUEEN, make_square(FILE_F, RANK_8));
    return position;
}

[[nodiscard]] constexpr Position
king_capture_position() noexcept {
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

// The Red king is confined to d1, e1, d2, and e2 by board geometry.
[[nodiscard]] constexpr Position blocked_corner(
  bool checked) noexcept {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_D, RANK_1));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_1));
    position.put_piece(
      Y_PAWN, make_square(FILE_D, RANK_2));
    position.put_piece(
      Y_PAWN, make_square(FILE_E, RANK_2));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_4));
    position.put_piece(
      G_KING, make_square(FILE_N, RANK_11));

    if (checked) {
        position.put_piece(
          B_KNIGHT, make_square(FILE_F, RANK_2));
    }

    return position;
}

[[nodiscard]] constexpr Position
child_repetition_position() noexcept {
    Position position = kings_only_position();
    position.put_piece(
      B_QUEEN, make_square(FILE_A, RANK_4));
    return position;
}

[[nodiscard]] constexpr Position
special_move_position() noexcept {
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

struct ExhaustiveResult {
    Move best_move = Move::none();
    Score score = DRAW_SCORE;
    std::uint64_t nodes = 0;
};

[[nodiscard]] Score exhaustive_terminal_score(
  const PositionResult& result,
  Team perspective,
  int ply) {
    const auto winner = result.winning_team();
    if (!winner)
        return DRAW_SCORE;

    const Score win_score =
      MATE_SCORE - static_cast<Score>(ply);
    return *winner == perspective
        ? win_score
        : -win_score;
}

// This reference traversal copies each child and does not use alpha-beta
// bounds, undo_move(), or the search implementation.
[[nodiscard]] ExhaustiveResult exhaustive_search(
  Position position,
  PositionHistory history,
  int depth,
  int ply = 0) {
    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    const PositionResult position_result =
      terminal_result(position, history, legal_moves);

    if (position_result.is_terminal()) {
        return {
          Move::none(),
          exhaustive_terminal_score(
            position_result,
            team_of(position.side_to_move()),
            ply),
          1,
        };
    }

    if (depth == 0) {
        return {
          Move::none(),
          evaluate(position),
          1,
        };
    }

    ExhaustiveResult result;
    result.score = -INFINITE_SCORE;
    result.nodes = 1;

    for (const Move move : legal_moves) {
        Position child_position = position;
        UndoState unused;
        do_move(child_position, move, unused);

        PositionHistory child_history = history;
        child_history.push(child_position.key());

        const ExhaustiveResult child =
          exhaustive_search(
            child_position,
            child_history,
            depth - 1,
            ply + 1);
        const Score candidate = -child.score;
        result.nodes += child.nodes;

        if (candidate > result.score) {
            result.score = candidate;
            result.best_move = move;
        }
    }

    return result;
}

static_assert(DRAW_SCORE == 0);
static_assert(MAX_SEARCH_DEPTH > 0);
static_assert(
  MATE_SCORE - MAX_SEARCH_DEPTH
  > MAX_MATERIAL_SCORE);
static_assert(
  SearchDetail::terminal_score(
    PositionResult::king_capture(RED_YELLOW),
    RED_YELLOW,
    0)
  == MATE_SCORE);
static_assert(
  SearchDetail::terminal_score(
    PositionResult::checkmate(RED_YELLOW),
    BLUE_GREEN,
    MAX_SEARCH_DEPTH)
  == -(MATE_SCORE - MAX_SEARCH_DEPTH));
static_assert(
  SearchDetail::terminal_score(
    PositionResult::stalemate(),
    RED_YELLOW,
    MAX_SEARCH_DEPTH)
  == DRAW_SCORE);
static_assert(
  std::is_same_v<
    decltype(search(
      std::declval<Position&>(),
      std::declval<const PositionHistory&>(),
      0)),
    SearchResult>);
static_assert(!noexcept(
  search(
    std::declval<Position&>(),
    std::declval<const PositionHistory&>(),
    0)));
static_assert(
  SearchResult{
    Move::normal(
      make_square(FILE_D, RANK_1),
      make_square(FILE_D, RANK_2)),
    DRAW_SCORE,
    1}.has_move());

void test_depth_zero_material_evaluation() {
    Position position = material_tactic_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    const std::size_t original_capacity =
      history.capacity();

    const SearchResult result =
      search(position, history, 0);

    expect(evaluate(position) == -400,
           "the horizon fixture has the expected material balance");
    expect(
      result
        == SearchResult{
             Move::none(), -400, 1},
      "depth zero evaluates the current position without choosing a move");
    expect(
      positions_equal(position, original),
      "depth-zero search preserves every position field");
    expect(
      history.capacity() == original_capacity
        && history_matches(history, keys),
      "depth-zero search preserves the complete history");
}

void test_material_capture_for_every_color() {
    Position position = material_tactic_position();
    Move expected = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));

    for (int rotation = 0;
         rotation < COLOR_NB;
         ++rotation) {
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const std::size_t original_capacity =
          history.capacity();

        MoveList legal_moves;
        generate_legal_moves(position, legal_moves);
        expect(
          positions_equal(position, original),
          "legal generation preserves the rotated tactic");

        const SearchResult result =
          search(position, history, 1);

        expect(
          result.best_move == expected
            && result.score == ROOK_VALUE
            && result.nodes
                 == 1
                    + static_cast<std::uint64_t>(
                        legal_moves.size()),
          "depth one selects the unique queen capture for each color");
        expect(
          positions_equal(position, original),
          "rotated material search preserves every position field");
        expect(
          history.capacity() == original_capacity
            && history_matches(history, keys),
          "rotated material search preserves the complete history");

        position = rotate_clockwise(position);
        expected = rotate_clockwise(expected);
    }
}

void test_terminal_positions_precede_evaluation() {
    {
        Position position = blocked_corner(true);
        const Position original = position;
        const PositionKey key = position.key();
        const std::array repeated_keys = {
          key,
          key ^ PositionKey{0x1111111111111111ULL},
          key,
          key ^ PositionKey{0x2222222222222222ULL},
          key,
        };
        PositionHistory history =
          make_history(repeated_keys);

        const SearchResult result =
          search(position, history, 4);

        expect(
          result
            == SearchResult{
                 Move::none(), -MATE_SCORE, 1},
          "checkmate outranks simultaneous repetition and material");
        expect(
          positions_equal(position, original)
            && history_matches(
                 history, repeated_keys),
          "checkmate search preserves the root state");
    }

    {
        Position position = blocked_corner(false);
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);

        const SearchResult result =
          search(position, history, 4);

        expect(
          result
            == SearchResult{
                 Move::none(), DRAW_SCORE, 1},
          "stalemate scores as a draw");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "stalemate search preserves the root state");
    }

    {
        Position position = kings_only_position();
        position.remove_piece(
          position.pieces(BLUE, KING).lsb());
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);

        const SearchResult result =
          search(position, history, 4);

        expect(
          result
            == SearchResult{
                 Move::none(), MATE_SCORE, 1},
          "a previously captured opposing king scores as a win");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "king-capture terminal search preserves the root state");
    }
}

void test_immediate_king_capture() {
    Position position = king_capture_position();
    Move capture = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));

    for (int rotation = 0;
         rotation < COLOR_NB;
         ++rotation) {
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);

        const SearchResult result =
          search(position, history, 2);

        expect(
          result.best_move == capture
            && result.score == MATE_SCORE - 1,
          "every color chooses an immediate opposing-king capture");
        expect(
          positions_equal(position, original),
          "rotated king-capture search restores every position field");
        expect(
          history_matches(history, keys),
          "rotated king-capture search restores the complete history");

        position = rotate_clockwise(position);
        capture = rotate_clockwise(capture);
    }
}

void test_root_and_child_repetition() {
    {
        Position position = kings_only_position();
        const Position original = position;
        const PositionKey key = position.key();
        const std::array keys = {
          key,
          key ^ PositionKey{0x3333333333333333ULL},
          key,
          key ^ PositionKey{0x4444444444444444ULL},
          key,
        };
        PositionHistory history = make_history(keys);

        const SearchResult result =
          search(position, history, 3);

        expect(
          result
            == SearchResult{
                 Move::none(), DRAW_SCORE, 1},
          "a root threefold repetition scores as a draw");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "root-repetition search preserves position and history");
    }

    {
        Position position =
          child_repetition_position();
        const Position original = position;
        const Move repeating_move = Move::normal(
          make_square(FILE_H, RANK_5),
          make_square(FILE_H, RANK_6));

        Position child = position;
        UndoState unused;
        do_move(child, repeating_move, unused);
        const PositionKey child_key = child.key();
        const PositionKey filler =
          position.key()
          ^ PositionKey{0x5555555555555555ULL};
        const std::array keys = {
          child_key,
          filler,
          child_key,
          position.key(),
        };
        PositionHistory history = make_history(keys);

        MoveList legal_moves;
        generate_legal_moves(position, legal_moves);
        const SearchResult result =
          search(position, history, 1);

        expect(
          result.best_move == repeating_move
            && result.score == DRAW_SCORE
            && result.nodes
                 == 1
                    + static_cast<std::uint64_t>(
                        legal_moves.size()),
          "search recognizes a threefold repetition created by a child move");
        expect(
          positions_equal(position, original),
          "child-repetition search restores every position field");
        expect(
          history_matches(history, keys),
          "child-repetition search restores the complete history");
    }
}

void test_special_move_state_restoration() {
    Position position = special_move_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    expect(
      contains_move_type(
        legal_moves, MoveType::NORMAL)
        && contains_move_type(
             legal_moves, MoveType::PROMOTION)
        && contains_move_type(
             legal_moves, MoveType::CASTLING)
        && contains_move_type(
             legal_moves, MoveType::EN_PASSANT),
      "the state-restoration fixture contains every board move type");
    const SearchResult result =
      search(position, history, 1);

    expect(
      result.has_move()
        && result.nodes
             == 1
                + static_cast<std::uint64_t>(
                    legal_moves.size()),
      "depth-one search visits every legal special-move branch");
    expect(
      positions_equal(position, original),
      "special-move search restores castling and en-passant state");
    expect(
      history_matches(history, keys),
      "special-move search restores the complete history");
}

void test_alpha_beta_matches_exhaustive_search() {
    {
        Position position = kings_only_position();
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);

        MoveList legal_moves;
        generate_legal_moves(position, legal_moves);
        const ExhaustiveResult exhaustive =
          exhaustive_search(position, history, 2);
        const SearchResult pruned =
          search(position, history, 2);

        expect(
          !legal_moves.empty()
            && exhaustive.best_move == legal_moves[0]
            && pruned.best_move == exhaustive.best_move,
          "equal scores retain the first generated root move");
        expect(
          pruned.score == exhaustive.score
            && pruned.score == DRAW_SCORE,
          "alpha-beta returns the exhaustive equal-leaf score");
        expect(
          pruned.nodes < exhaustive.nodes,
          "alpha-beta prunes an equal-leaf search tree");
        expect(
          positions_equal(position, original),
          "an equal-leaf search with cutoffs restores the position");
        expect(
          history_matches(history, keys),
          "an equal-leaf search with cutoffs restores the history");
    }

    {
        Position position =
          material_tactic_position();
        position.put_piece(
          B_ROOK, make_square(FILE_F, RANK_10));
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const Move expected = Move::normal(
          make_square(FILE_F, RANK_5),
          make_square(FILE_F, RANK_8));

        const ExhaustiveResult exhaustive =
          exhaustive_search(position, history, 2);
        const SearchResult pruned =
          search(position, history, 2);

        expect(
          exhaustive.best_move == expected
            && pruned.best_move == expected
            && pruned.score == exhaustive.score
            && pruned.score == -ROOK_VALUE,
          "alpha-beta matches exhaustive search through a queen-rook exchange");
        expect(
          pruned.nodes < exhaustive.nodes,
          "alpha-beta prunes a nonuniform material search tree");
        expect(
          positions_equal(position, original),
          "a nonuniform search with cutoffs restores the position");
        expect(
          history_matches(history, keys),
          "a nonuniform search with cutoffs restores the history");
    }
}

void test_invalid_inputs_return_without_searching_in_release() {
#ifdef NDEBUG
    Position position = kings_only_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);

    expect(
      search(position, history, -1) == SearchResult{}
        && search(
             position,
             history,
             MAX_SEARCH_DEPTH + 1)
             == SearchResult{},
      "release search rejects depths outside the supported range");

    PositionHistory stale_history{
      position.key() ^ PositionKey{1}};
    expect(
      search(position, stale_history, 1)
        == SearchResult{},
      "release search rejects a history whose current key is stale");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys),
      "rejected release searches preserve the valid root state");
#endif
}

}  // namespace

int main() {
    test_depth_zero_material_evaluation();
    test_material_capture_for_every_color();
    test_terminal_positions_precede_evaluation();
    test_immediate_king_capture();
    test_root_and_child_repetition();
    test_special_move_state_restoration();
    test_alpha_beta_matches_exhaustive_search();
    test_invalid_inputs_return_without_searching_in_release();

    if (failures != 0) {
        std::cerr << failures
                  << " search test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All search tests passed\n";
    return EXIT_SUCCESS;
}
