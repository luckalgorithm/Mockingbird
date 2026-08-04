#include "search.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
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
main_search_exchange_position(
  bool checking_capture = false) noexcept {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      B_KING,
      checking_capture
        ? make_square(FILE_F, RANK_10)
        : make_square(FILE_A, RANK_4));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_13));
    position.put_piece(
      G_KING, make_square(FILE_N, RANK_11));

    position.put_piece(
      R_PAWN, make_square(FILE_H, RANK_7));
    position.put_piece(
      B_ROOK, make_square(FILE_I, RANK_8));

    position.put_piece(
      R_BISHOP, make_square(FILE_F, RANK_6));
    position.put_piece(
      B_PAWN, make_square(FILE_H, RANK_8));
    position.put_piece(
      G_ROOK, make_square(FILE_H, RANK_10));
    return position;
}

[[nodiscard]] constexpr Position
null_move_reduction_position() noexcept {
    Position position = kings_only_position();
    position.put_piece(
      R_ROOK, make_square(FILE_F, RANK_5));
    position.put_piece(
      R_QUEEN, make_square(FILE_H, RANK_6));
    return position;
}

[[nodiscard]] constexpr Position
pawn_only_position() noexcept {
    Position position = kings_only_position();
    position.put_piece(
      R_PAWN, make_square(FILE_H, RANK_6));
    return position;
}

[[nodiscard]] constexpr Position
pvs_research_position() noexcept {
    Position position = kings_only_position();
    position.put_piece(
      Y_BISHOP, make_square(FILE_N, RANK_4));
    position.put_piece(
      B_PAWN, make_square(FILE_J, RANK_14));
    position.set_side_to_move(YELLOW);
    return position;
}

[[nodiscard]] constexpr Position
team_check_position() noexcept {
    Position position = kings_only_position();
    position.put_piece(
      R_ROOK, make_square(FILE_D, RANK_5));
    return position;
}

[[nodiscard]] constexpr Position
teammate_check_position() noexcept {
    Position position = kings_only_position();
    position.put_piece(
      R_ROOK, make_square(FILE_K, RANK_5));
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
terminal_exchange_position() noexcept {
    Position position = blocked_corner(false);
    position.set_side_to_move(GREEN);
    position.put_piece(
      G_BISHOP, make_square(FILE_J, RANK_10));
    position.put_piece(
      Y_PAWN, make_square(FILE_H, RANK_8));
    position.put_piece(
      Y_ROOK, make_square(FILE_H, RANK_6));
    return position;
}

[[nodiscard]] constexpr Position
null_move_stalemate_position() noexcept {
    Position position = blocked_corner(false);
    position.put_piece(
      G_ROOK, make_square(FILE_K, RANK_11));
    position.set_side_to_move(GREEN);
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
[[nodiscard]] ExhaustiveResult exhaustive_quiescence(
  Position position,
  PositionHistory history,
  int ply,
  int quiescence_ply) {
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

    if (quiescence_ply == MAX_QUIESCENCE_PLY) {
        return {
          Move::none(),
          evaluate(position),
          1,
        };
    }

    const bool checked = in_check(position);
    ExhaustiveResult result;
    result.score =
      checked ? -INFINITE_SCORE : evaluate(position);
    result.nodes = 1;

    for (const Move move : legal_moves) {
        if (!checked
            && !is_tactical_move(position, move))
            continue;

        Position child_position = position;
        UndoState unused;
        do_move(child_position, move, unused);

        PositionHistory child_history = history;
        child_history.push(child_position.key());
        const ExhaustiveResult child =
          exhaustive_quiescence(
            child_position,
            child_history,
            ply + 1,
            quiescence_ply + 1);
        const Score candidate = -child.score;
        result.nodes += child.nodes;

        if (candidate > result.score)
            result.score = candidate;
    }

    return result;
}

// This fixed-depth reference uses the full-width quiescence traversal at each
// horizon node.
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

    if (depth == 0)
        return exhaustive_quiescence(
          position, history, ply, 0);

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
  MATE_SCORE - MAX_SEARCH_PLY
  > MAX_EVALUATION_SCORE);
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
    MAX_SEARCH_PLY)
  == -(MATE_SCORE - MAX_SEARCH_PLY));
static_assert(
  SearchDetail::terminal_score(
    PositionResult::stalemate(),
    RED_YELLOW,
    MAX_SEARCH_PLY)
  == DRAW_SCORE);
static_assert(
  SearchDetail::pvs_scout_beta(
    -INFINITE_SCORE)
  == -INFINITE_SCORE + 1);
static_assert(
  SearchDetail::pvs_scout_beta(
    INFINITE_SCORE - 1)
  == INFINITE_SCORE);
static_assert(!SearchDetail::is_pvs_scout_move(0));
static_assert(SearchDetail::is_pvs_scout_move(1));
static_assert(
  !SearchDetail::repetition_sensitive_after_final_child(
    false, false, false, false));
static_assert(
  SearchDetail::repetition_sensitive_after_final_child(
    true, false, false, true));
static_assert(
  SearchDetail::repetition_sensitive_after_final_child(
    false, false, true, true));
static_assert(
  !SearchDetail::repetition_sensitive_after_final_child(
    false, true, false, true));
static_assert(
  SearchDetail::repetition_sensitive_after_final_child(
    false, true, false, false));
static_assert(
  SearchDetail::is_null_window(
    Score{0}, Score{1}));
static_assert(
  !SearchDetail::is_null_window(
    Score{0}, Score{2}));
static_assert(
  !SearchDetail::is_mate_score_window(
    -SearchDetail::TABLE_MATE_THRESHOLD
      + Score{1},
    SearchDetail::TABLE_MATE_THRESHOLD
      - Score{1}));
static_assert(
  SearchDetail::is_mate_score_window(
    -SearchDetail::TABLE_MATE_THRESHOLD,
    Score{0}));
static_assert(
  SearchDetail::is_mate_score_window(
    Score{0},
    SearchDetail::TABLE_MATE_THRESHOLD));
static_assert(
  !SearchDetail::is_mate_score_window(
    -INFINITE_SCORE,
    INFINITE_SCORE));
static_assert(
  !SearchDetail::is_mate_score_window(
    -SearchDetail::TABLE_MATE_THRESHOLD,
    SearchDetail::TABLE_MATE_THRESHOLD));
static_assert(
  SearchDetail::mate_distance_window(
    -INFINITE_SCORE,
    INFINITE_SCORE,
    5)
  == SearchDetail::SearchWindowBounds{
       -MATE_SCORE + Score{6},
       MATE_SCORE - Score{6},
     });
static_assert(
  SearchDetail::mate_distance_window(
    MATE_SCORE - Score{6},
    MATE_SCORE - Score{5},
    5)
    .cutoff());
static_assert(SearchDetail::LATE_MOVE_MIN_DEPTH == 2);
static_assert(
  SearchDetail::LATE_MOVE_MIN_QUIET_ORDINAL == 1);
static_assert(
  SearchDetail::integer_log2(1) == 0
  && SearchDetail::integer_log2(4) == 2
  && SearchDetail::integer_log2(13) == 3);
static_assert(
  !SearchDetail::lmr_verification_required(
    SearchDetail::LATE_MOVE_REDUCTION,
    Score{0},
    Score{0}));
static_assert(
  SearchDetail::lmr_verification_required(
    SearchDetail::LATE_MOVE_REDUCTION,
    Score{1},
    Score{0}));
static_assert(
  !SearchDetail::lmr_verification_required(
    0,
    Score{1},
    Score{0}));
static_assert(
  !SearchDetail::team_has_checked_king(
    kings_only_position(),
    BLUE_GREEN));
static_assert(
  SearchDetail::team_has_checked_king(
    team_check_position(),
    BLUE_GREEN));
static_assert(
  SearchDetail::team_has_checked_king(
    teammate_check_position(),
    BLUE_GREEN));
static_assert(
  !SearchDetail::team_has_checked_king(
    team_check_position(),
    RED_YELLOW));
static_assert(
  !SearchDetail::color_has_non_pawn_material(
    kings_only_position(),
    RED));
static_assert(
  SearchDetail::color_has_non_pawn_material(
    material_tactic_position(),
    RED));
static_assert(
  !SearchDetail::color_has_non_pawn_material(
    pvs_research_position(),
    RED));
static_assert(
  SearchDetail::color_has_non_pawn_material(
    pvs_research_position(),
    YELLOW));
static_assert(SearchDetail::NULL_MOVE_MIN_DEPTH == 4);
static_assert(
  SearchDetail::null_move_child_depth(
    SearchDetail::NULL_MOVE_MIN_DEPTH)
  == SearchDetail::NULL_MOVE_MIN_CHILD_DEPTH);
static_assert(
  SearchDetail::null_move_child_depth(10)
  == SearchDetail::NULL_MOVE_MIN_CHILD_DEPTH);
static_assert(
  SearchDetail::null_move_child_depth(11) == 1);
static_assert(
  SearchDetail::null_move_child_depth(
    MAX_SEARCH_DEPTH)
  == 164);
static_assert(
  SearchDetail::null_move_pruning_allowed(
    SearchDetail::NULL_MOVE_MIN_DEPTH,
    true,
    false,
    false,
    true,
    false,
    true,
    Score{200},
    Score{200}));
static_assert(
  !SearchDetail::null_move_pruning_allowed(
    SearchDetail::NULL_MOVE_MIN_DEPTH - 1,
    true,
    false,
    false,
    true,
    false,
    true,
    Score{200},
    Score{200}));
static_assert(
  !SearchDetail::null_move_pruning_allowed(
    SearchDetail::NULL_MOVE_MIN_DEPTH,
    false,
    false,
    false,
    true,
    false,
    true,
    Score{200},
    Score{200}));
static_assert(
  !SearchDetail::null_move_pruning_allowed(
    SearchDetail::NULL_MOVE_MIN_DEPTH,
    true,
    true,
    false,
    true,
    false,
    true,
    Score{200},
    Score{200}));
static_assert(
  !SearchDetail::null_move_pruning_allowed(
    SearchDetail::NULL_MOVE_MIN_DEPTH,
    true,
    false,
    true,
    true,
    false,
    true,
    Score{200},
    Score{200}));
static_assert(
  !SearchDetail::null_move_pruning_allowed(
    SearchDetail::NULL_MOVE_MIN_DEPTH,
    true,
    false,
    false,
    false,
    false,
    true,
    Score{200},
    Score{200}));
static_assert(
  !SearchDetail::null_move_pruning_allowed(
    SearchDetail::NULL_MOVE_MIN_DEPTH,
    true,
    false,
    false,
    true,
    true,
    true,
    Score{200},
    Score{200}));
static_assert(
  !SearchDetail::null_move_pruning_allowed(
    SearchDetail::NULL_MOVE_MIN_DEPTH,
    true,
    false,
    false,
    true,
    false,
    false,
    Score{200},
    Score{200}));
static_assert(
  !SearchDetail::null_move_pruning_allowed(
    SearchDetail::NULL_MOVE_MIN_DEPTH,
    true,
    false,
    false,
    true,
    false,
    true,
    Score{199},
    Score{200}));
static_assert(
  SearchDetail::is_mate_score(
    SearchDetail::TABLE_MATE_THRESHOLD));
static_assert(
  !SearchDetail::is_mate_score(
    SearchDetail::TABLE_MATE_THRESHOLD - 1));
static_assert(std::is_nothrow_destructible_v<
              SearchDetail::NullMoveState>);
static_assert(
  SearchDetail::reverse_futility_margin(1)
  == Score{3} * PAWN_VALUE);
static_assert(
  SearchDetail::reverse_futility_margin(2)
  == Score{5} * PAWN_VALUE);
static_assert(
  SearchDetail::reverse_futility_pruning_allowed(
    1,
    true,
    false,
    false,
    true,
    Score{500},
    Score{200}));
static_assert(
  !SearchDetail::reverse_futility_pruning_allowed(
    1,
    false,
    false,
    false,
    true,
    Score{500},
    Score{200}));
static_assert(
  !SearchDetail::reverse_futility_pruning_allowed(
    1,
    true,
    false,
    true,
    true,
    Score{500},
    Score{200}));
static_assert(
  !SearchDetail::reverse_futility_pruning_allowed(
    1,
    true,
    false,
    false,
    false,
    Score{500},
    Score{200}));
static_assert(
  !SearchDetail::reverse_futility_pruning_allowed(
    1,
    true,
    true,
    false,
    true,
    Score{500},
    Score{200}));
static_assert(
  SearchDetail::late_move_futility_pruning_allowed(
    SearchDetail::LATE_MOVE_FUTILITY_DEPTH,
    SearchDetail::LATE_MOVE_MIN_QUIET_ORDINAL,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    false,
    Score{0},
    PAWN_VALUE));
static_assert(
  !SearchDetail::late_move_futility_pruning_allowed(
    SearchDetail::LATE_MOVE_FUTILITY_DEPTH,
    SearchDetail::LATE_MOVE_MIN_QUIET_ORDINAL,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    true,
    false,
    Score{0},
    PAWN_VALUE));
static_assert(
  !SearchDetail::late_move_futility_pruning_allowed(
    SearchDetail::LATE_MOVE_FUTILITY_DEPTH,
    SearchDetail::LATE_MOVE_MIN_QUIET_ORDINAL,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    true,
    Score{0},
    PAWN_VALUE));
static_assert(
  !SearchDetail::late_move_futility_pruning_allowed(
    SearchDetail::LATE_MOVE_FUTILITY_DEPTH,
    SearchDetail::LATE_MOVE_MIN_QUIET_ORDINAL,
    false,
    false,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    false,
    Score{0},
    PAWN_VALUE));
static_assert(
  !SearchDetail::late_move_futility_pruning_allowed(
    SearchDetail::LATE_MOVE_FUTILITY_DEPTH,
    SearchDetail::LATE_MOVE_MIN_QUIET_ORDINAL,
    false,
    true,
    MoveType::PROMOTION,
    PAWN,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    false,
    Score{0},
    PAWN_VALUE));
static_assert(
  !SearchDetail::late_move_futility_pruning_allowed(
    SearchDetail::LATE_MOVE_FUTILITY_DEPTH,
    SearchDetail::LATE_MOVE_MIN_QUIET_ORDINAL,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerMoves::PRIMARY_PRIORITY,
    HistoryScore{0},
    false,
    false,
    false,
    Score{0},
    PAWN_VALUE));
static_assert(
  SearchDetail::parent_futility_pruning_allowed(
    1,
    0,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    false,
    Score{0},
    SearchDetail::PARENT_FUTILITY_BASE_MARGIN));
static_assert(
  SearchDetail::parent_futility_pruning_allowed(
    SearchDetail::PARENT_FUTILITY_MAX_DEPTH,
    SearchDetail::PARENT_FUTILITY_MAX_REDUCED_DEPTH,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    false,
    Score{0},
    SearchDetail::PARENT_FUTILITY_BASE_MARGIN
      + SearchDetail::PARENT_FUTILITY_DEPTH_MARGIN
          * SearchDetail::PARENT_FUTILITY_MAX_REDUCED_DEPTH));
static_assert(
  !SearchDetail::parent_futility_pruning_allowed(
    SearchDetail::PARENT_FUTILITY_MAX_DEPTH + 1,
    0,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    false,
    Score{0},
    SearchDetail::PARENT_FUTILITY_BASE_MARGIN));
static_assert(
  !SearchDetail::parent_futility_pruning_allowed(
    SearchDetail::PARENT_FUTILITY_MAX_DEPTH,
    SearchDetail::PARENT_FUTILITY_MAX_REDUCED_DEPTH + 1,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    false,
    Score{0},
    SearchDetail::PARENT_FUTILITY_BASE_MARGIN));
static_assert(
  !SearchDetail::parent_futility_pruning_allowed(
    1,
    0,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    false,
    Score{0},
    SearchDetail::PARENT_FUTILITY_BASE_MARGIN
      - Score{1}));
static_assert(
  !SearchDetail::parent_futility_pruning_allowed(
    1,
    0,
    true,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    false,
    Score{0},
    SearchDetail::PARENT_FUTILITY_BASE_MARGIN));
static_assert(
  !SearchDetail::parent_futility_pruning_allowed(
    1,
    0,
    false,
    false,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    false,
    Score{0},
    SearchDetail::PARENT_FUTILITY_BASE_MARGIN));
static_assert(
  !SearchDetail::parent_futility_pruning_allowed(
    1,
    0,
    false,
    true,
    MoveType::PROMOTION,
    PAWN,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    false,
    Score{0},
    SearchDetail::PARENT_FUTILITY_BASE_MARGIN));
static_assert(
  !SearchDetail::parent_futility_pruning_allowed(
    1,
    0,
    false,
    true,
    MoveType::NORMAL,
    KING,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    false,
    Score{0},
    SearchDetail::PARENT_FUTILITY_BASE_MARGIN));
static_assert(
  !SearchDetail::parent_futility_pruning_allowed(
    1,
    0,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerMoves::PRIMARY_PRIORITY,
    HistoryScore{0},
    false,
    false,
    false,
    Score{0},
    SearchDetail::PARENT_FUTILITY_BASE_MARGIN));
static_assert(
  !SearchDetail::parent_futility_pruning_allowed(
    1,
    0,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{1},
    false,
    false,
    false,
    Score{0},
    SearchDetail::PARENT_FUTILITY_BASE_MARGIN));
static_assert(
  !SearchDetail::parent_futility_pruning_allowed(
    1,
    0,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    true,
    false,
    false,
    Score{0},
    SearchDetail::PARENT_FUTILITY_BASE_MARGIN));
static_assert(
  !SearchDetail::parent_futility_pruning_allowed(
    1,
    0,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    true,
    false,
    Score{0},
    SearchDetail::PARENT_FUTILITY_BASE_MARGIN));
static_assert(
  !SearchDetail::parent_futility_pruning_allowed(
    1,
    0,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    true,
    Score{0},
    SearchDetail::PARENT_FUTILITY_BASE_MARGIN));
static_assert(
  SearchDetail::adaptive_late_move_reduction(
    8,
    8,
    true,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerMoves::PRIMARY_PRIORITY,
    HistoryScore{0},
    false,
    false)
  == SearchDetail::LATE_MOVE_REDUCTION);
static_assert(
  SearchDetail::adaptive_late_move_reduction(
    8,
    8,
    true,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{1},
    false,
    false)
  == SearchDetail::LATE_MOVE_REDUCTION);
static_assert(
  SearchDetail::adaptive_late_move_reduction(
    8,
    8,
    true,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false)
  > SearchDetail::LATE_MOVE_REDUCTION);
static_assert(
  SearchDetail::adaptive_late_move_reduction(
    8,
    0,
    true,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerMoves::PRIMARY_PRIORITY,
    HistoryScore{0},
    false,
    false)
  == 0);
static_assert(
  SearchDetail::adaptive_late_move_reduction(
    8,
    8,
    true,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false)
  == 6);
static_assert(
  !SearchDetail::late_move_count_pruning_allowed(
    1,
    1,
    true,
    3,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    false));
static_assert(
  SearchDetail::late_move_count_pruning_allowed(
    1,
    1,
    true,
    4,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    false));
static_assert(
  !SearchDetail::late_move_count_pruning_allowed(
    1,
    0,
    true,
    4,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    false));
static_assert(
  SearchDetail::late_move_count_pruning_allowed(
    2,
    1,
    true,
    3,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerPriority{0},
    HistoryScore{0},
    false,
    false,
    false));
static_assert(
  !SearchDetail::late_move_count_pruning_allowed(
    2,
    1,
    true,
    3,
    false,
    true,
    MoveType::NORMAL,
    ROOK,
    KillerMoves::PRIMARY_PRIORITY,
    HistoryScore{0},
    false,
    false,
    false));
static_assert(
  SearchDetail::tactical_late_move_reduction_allowed(
    SearchDetail::TACTICAL_LATE_MOVE_MIN_DEPTH,
    true,
    false,
    MoveType::NORMAL,
    false,
    PAWN,
    true,
    false,
    false,
    false,
    false));
static_assert(
  !SearchDetail::tactical_late_move_reduction_allowed(
    SearchDetail::TACTICAL_LATE_MOVE_MIN_DEPTH,
    true,
    false,
    MoveType::NORMAL,
    false,
    PAWN,
    false,
    false,
    false,
    false,
    false));
static_assert(
  !SearchDetail::tactical_late_move_reduction_allowed(
    SearchDetail::TACTICAL_LATE_MOVE_MIN_DEPTH,
    true,
    false,
    MoveType::NORMAL,
    false,
    PAWN,
    true,
    false,
    true,
    false,
    false));
static_assert(
  !SearchDetail::tactical_late_move_reduction_allowed(
    SearchDetail::TACTICAL_LATE_MOVE_MIN_DEPTH,
    true,
    false,
    MoveType::NORMAL,
    false,
    PAWN,
    true,
    false,
    false,
    true,
    false));
static_assert(
  SearchDetail::capture_futility_pruning_allowed(
    0,
    true,
    false,
    MoveType::NORMAL,
    false,
    PAWN,
    false,
    false,
    false,
    false,
    Score{0},
    SearchDetail::CAPTURE_FUTILITY_BASE_MARGIN
      + PAWN_VALUE));
static_assert(
  !SearchDetail::capture_futility_pruning_allowed(
    0,
    true,
    false,
    MoveType::NORMAL,
    false,
    PAWN,
    false,
    false,
    false,
    false,
    Score{0},
    SearchDetail::CAPTURE_FUTILITY_BASE_MARGIN
      + PAWN_VALUE - Score{1}));
static_assert(
  !SearchDetail::capture_futility_pruning_allowed(
    0,
    true,
    false,
    MoveType::NORMAL,
    false,
    PAWN,
    false,
    true,
    false,
    false,
    Score{0},
    SearchDetail::CAPTURE_FUTILITY_BASE_MARGIN
      + PAWN_VALUE));
static_assert(
  !SearchDetail::capture_futility_pruning_allowed(
    SearchDetail::CAPTURE_FUTILITY_MAX_CHILD_DEPTH + 1,
    true,
    false,
    MoveType::NORMAL,
    false,
    PAWN,
    false,
    false,
    false,
    false,
    Score{0},
    INFINITE_SCORE));
static_assert(
  SearchDetail::nonpv_first_move_reduction_allowed(
    SearchDetail::NONPV_FIRST_MOVE_REDUCTION_MIN_DEPTH,
    1,
    true,
    false,
    false,
    MoveType::NORMAL,
    false,
    false,
    false,
    false,
    false,
    false));
static_assert(
  !SearchDetail::nonpv_first_move_reduction_allowed(
    SearchDetail::NONPV_FIRST_MOVE_REDUCTION_MIN_DEPTH,
    1,
    true,
    false,
    true,
    MoveType::NORMAL,
    false,
    false,
    false,
    false,
    false,
    false));
static_assert(
  !SearchDetail::nonpv_first_move_reduction_allowed(
    SearchDetail::NONPV_FIRST_MOVE_REDUCTION_MIN_DEPTH,
    1,
    true,
    false,
    false,
    MoveType::NORMAL,
    false,
    false,
    true,
    false,
    false,
    false));
static_assert(
  !SearchDetail::pvs_research_required(
    Score{0}, Score{0}, Score{2}));
static_assert(
  SearchDetail::pvs_research_required(
    Score{1}, Score{0}, Score{2}));
static_assert(
  !SearchDetail::pvs_research_required(
    Score{2}, Score{0}, Score{2}));
static_assert(
  SearchDetail::main_search_exchange_threshold(1)
  == -SearchDetail::
       MAIN_SEARCH_EXCHANGE_DEPTH_MARGIN);
static_assert(
  SearchDetail::main_search_exchange_threshold(
    SearchDetail::MAIN_SEARCH_EXCHANGE_MAX_DEPTH)
  == -static_cast<Score>(
         SearchDetail::MAIN_SEARCH_EXCHANGE_MAX_DEPTH)
       * SearchDetail::MAIN_SEARCH_EXCHANGE_DEPTH_MARGIN);
static_assert(
  SearchDetail::main_search_exchange_prunes_child(
    true, false, false));
static_assert(
  !SearchDetail::main_search_exchange_prunes_child(
    true, true, false));
static_assert(
  !SearchDetail::main_search_exchange_prunes_child(
    true, false, true));
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

void test_depth_zero_quiescence() {
    Position position = material_tactic_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    const std::size_t original_capacity =
      history.capacity();
    const ExhaustiveResult reference =
      exhaustive_search(position, history, 0);

    const SearchResult result =
      search(position, history, 0);

    expect(
      material_balance(position, RED_YELLOW)
        == Score{-400},
           "the horizon fixture has the expected material balance");
    expect(
      result.best_move == Move::none()
        && result.score == reference.score
        && result.nodes <= reference.nodes
        && result.nodes > 1,
      "depth zero resolves the hanging queen without choosing a move");
    expect(
      positions_equal(position, original),
      "depth-zero quiescence preserves every position field");
    expect(
      history.capacity() == original_capacity
        && history_matches(history, keys),
      "depth-zero quiescence preserves the complete history");
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
        const ExhaustiveResult reference =
          exhaustive_search(position, history, 1);

        const SearchResult result =
          search(position, history, 1);

        expect(
          result.best_move == expected
            && result.best_move
                 == reference.best_move
            && result.score == reference.score,
          "depth one selects the unique queen capture for each color");
        expect(
          result.nodes <= reference.nodes
            && result.nodes > 1,
          "depth one prunes without exceeding the exhaustive traversal");
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

void test_mate_distance_window_pruning() {
    Position position = kings_only_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);

    SearchDetail::SearchState winning_state;
    PositionHistory winning_history{history};
    const auto winning_bound =
      SearchDetail::alpha_beta(
        position,
        winning_history,
        1,
        5,
        MATE_SCORE - Score{6},
        MATE_SCORE - Score{5},
        winning_state);

    SearchDetail::SearchState losing_state;
    PositionHistory losing_history{history};
    const auto losing_bound =
      SearchDetail::alpha_beta(
        position,
        losing_history,
        1,
        5,
        -MATE_SCORE + Score{5},
        -MATE_SCORE + Score{6},
        losing_state);

    expect(
      winning_bound
        && winning_bound->score
             == MATE_SCORE - Score{6}
        && OrderingDetail::contains_move(
             legal_moves,
             winning_bound->best_move)
        && winning_state.nodes == 1,
      "an unattainable faster win is rejected in one node");
    expect(
      losing_bound
        && losing_bound->score
             == -MATE_SCORE + Score{6}
        && OrderingDetail::contains_move(
             legal_moves,
             losing_bound->best_move)
        && losing_state.nodes == 1,
      "an unattainable faster loss is rejected in one node");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys)
        && winning_history.current_key()
             == position.key()
        && losing_history.current_key()
             == position.key(),
      "mate-distance cutoffs preserve position and history");
}

void test_forward_pruning_toggle() {
    Position position = material_tactic_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);

    SearchDetail::SearchState pruned_state;
    PositionHistory pruned_history{history};
    const auto pruned =
      SearchDetail::alpha_beta(
        position,
        pruned_history,
        1,
        0,
        -INFINITE_SCORE,
        INFINITE_SCORE,
        pruned_state);

    SearchDetail::SearchState reference_state;
    PositionHistory reference_history{history};
    const auto reference =
      SearchDetail::alpha_beta<true, false>(
        position,
        reference_history,
        1,
        0,
        -INFINITE_SCORE,
        INFINITE_SCORE,
        reference_state);

    expect(
      pruned
        && reference
        && pruned->score == reference->score
        && pruned->best_move == reference->best_move
        && pruned_state.nodes < reference_state.nodes,
      "late-move futility pruning preserves the depth-one result");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys)
        && pruned_history.current_key()
             == position.key()
        && reference_history.current_key()
             == position.key(),
      "enabled and disabled forward pruning restore root state");
}

void test_main_search_exchange_pruning_guards() {
    Position position =
      main_search_exchange_position();
    const Position original = position;
    const Move losing_capture = Move::normal(
      make_square(FILE_F, RANK_6),
      make_square(FILE_H, RANK_8));
    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);

    std::size_t boundary_budget =
      SearchDetail::MAX_MAIN_SEARCH_EXCHANGE_NODES;
    std::size_t below_budget =
      SearchDetail::MAX_MAIN_SEARCH_EXCHANGE_NODES;
    std::size_t exhausted_budget = 0;
    expect(
      OrderingDetail::contains_move(
        legal_moves, losing_capture)
        && static_exchange_evaluation(
             position, losing_capture)
             == Score{-230}
        && !SearchDetail::
             main_search_exchange_is_proven_below(
               position,
               losing_capture,
               Score{-230},
               boundary_budget)
        && SearchDetail::
             main_search_exchange_is_proven_below(
               position,
               losing_capture,
               Score{-229},
               below_budget)
        && !SearchDetail::
             main_search_exchange_is_proven_below(
               position,
               losing_capture,
               Score{-229},
               exhausted_budget),
      "main-search exchange pruning is strict at its threshold and retains UNKNOWN results");

    expect(
      SearchDetail::
        main_search_exchange_pruning_candidate(
          position,
          losing_capture,
          SearchDetail::MAIN_SEARCH_EXCHANGE_MAX_DEPTH,
          true,
          false,
          false,
          false)
        && !SearchDetail::
             main_search_exchange_pruning_candidate(
               position,
               losing_capture,
               SearchDetail::MAIN_SEARCH_EXCHANGE_MAX_DEPTH
                 + 1,
               true,
               false,
               false,
               false)
        && !SearchDetail::
             main_search_exchange_pruning_candidate(
               position,
               losing_capture,
               1,
               false,
               false,
               false,
               false)
        && !SearchDetail::
             main_search_exchange_pruning_candidate(
               position,
               losing_capture,
               1,
               true,
               true,
               false,
               false)
        && !SearchDetail::
             main_search_exchange_pruning_candidate(
               position,
               losing_capture,
               1,
               true,
               false,
               true,
               false)
        && !SearchDetail::
             main_search_exchange_pruning_candidate(
               position,
               losing_capture,
               1,
               true,
               false,
               false,
               true),
      "depth, scout, check, mate-window, and repetition guards restrict exchange pruning");

    Position special = special_move_position();
    const Move promotion = Move::promotion(
      make_square(FILE_B, RANK_10),
      make_square(FILE_C, RANK_11),
      QUEEN);
    const Move en_passant = Move::en_passant(
      make_square(FILE_D, RANK_5),
      make_square(FILE_C, RANK_6));
    Position king_capture = king_capture_position();
    const Move captures_king = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));
    MoveList special_moves;
    MoveList king_moves;
    generate_legal_moves(special, special_moves);
    generate_legal_moves(king_capture, king_moves);
    expect(
      OrderingDetail::contains_move(
        special_moves, promotion)
        && OrderingDetail::contains_move(
             special_moves, en_passant)
        && OrderingDetail::contains_move(
             king_moves, captures_king)
        && !SearchDetail::
             main_search_exchange_pruning_candidate(
               special,
               promotion,
               1,
               true,
               false,
               false,
               false)
        && !SearchDetail::
             main_search_exchange_pruning_candidate(
               special,
               en_passant,
               1,
               true,
               false,
               false,
               false)
        && !SearchDetail::
             main_search_exchange_pruning_candidate(
               king_capture,
               captures_king,
               1,
               true,
               false,
               false,
               false),
      "promotions, en-passant captures, and opposing-king captures bypass exchange pruning");

    Position queen_position =
      main_search_exchange_position();
    queen_position.remove_piece(
      make_square(FILE_F, RANK_6));
    queen_position.put_piece(
      R_QUEEN, make_square(FILE_F, RANK_6));
    const Move queen_capture = Move::normal(
      make_square(FILE_F, RANK_6),
      make_square(FILE_H, RANK_8));
    Position rook_position =
      main_search_exchange_position();
    rook_position.remove_piece(
      make_square(FILE_F, RANK_6));
    rook_position.put_piece(
      R_ROOK, make_square(FILE_F, RANK_6));
    rook_position.put_piece(
      B_PAWN, make_square(FILE_F, RANK_8));
    rook_position.put_piece(
      G_ROOK, make_square(FILE_F, RANK_10));
    const Move rook_capture = Move::normal(
      make_square(FILE_F, RANK_6),
      make_square(FILE_F, RANK_8));
    const Move pawn_capture = Move::normal(
      make_square(FILE_H, RANK_7),
      make_square(FILE_I, RANK_8));
    MoveList queen_moves;
    MoveList rook_moves;
    generate_legal_moves(queen_position, queen_moves);
    generate_legal_moves(rook_position, rook_moves);
    expect(
      OrderingDetail::contains_move(
        queen_moves, queen_capture)
        && OrderingDetail::contains_move(
             rook_moves, rook_capture)
        && OrderingDetail::contains_move(
             legal_moves, pawn_capture)
        && !SearchDetail::
             main_search_exchange_pruning_candidate(
               queen_position,
               queen_capture,
               1,
               true,
               false,
               false,
               false)
        && !SearchDetail::
             main_search_exchange_pruning_candidate(
               rook_position,
               rook_capture,
               1,
               true,
               false,
               false,
               false)
        && !SearchDetail::
             main_search_exchange_pruning_candidate(
               position,
               pawn_capture,
               1,
               true,
               false,
               false,
               false),
      "pawn, rook, and queen captures remain outside minor-piece exchange pruning");

    expect(
      SearchDetail::main_search_exchange_prunes_child(
        true, false, false)
        && !SearchDetail::main_search_exchange_prunes_child(
             false, false, false)
        && !SearchDetail::main_search_exchange_prunes_child(
             true, true, false)
        && !SearchDetail::main_search_exchange_prunes_child(
             true, false, true)
        && positions_equal(position, original),
      "checking and terminal children are re-admitted after bounded exchange classification");
}

void test_main_search_exchange_pruning_reentry() {
    struct ReentryCase {
        Position position;
        Move capture;
        bool expected_check;
        bool expected_terminal;
    };
    std::array cases = {
      ReentryCase{
        main_search_exchange_position(true),
        Move::normal(
          make_square(FILE_F, RANK_6),
          make_square(FILE_H, RANK_8)),
        true,
        false,
      },
      ReentryCase{
        terminal_exchange_position(),
        Move::normal(
          make_square(FILE_J, RANK_10),
          make_square(FILE_H, RANK_8)),
        false,
        true,
      },
    };

    for (ReentryCase& test : cases) {
        Position& position = test.position;
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        MoveList legal_moves;
        generate_legal_moves(position, legal_moves);
        std::size_t exchange_budget =
          SearchDetail::MAX_MAIN_SEARCH_EXCHANGE_NODES;
        const bool proven_below =
          SearchDetail::
            main_search_exchange_is_proven_below(
              position,
              test.capture,
              SearchDetail::
                main_search_exchange_threshold(1),
              exchange_budget);
        bool opposing_king_checked = false;
        bool child_terminal = false;
        {
            SearchDetail::ChildState child{
              position, history, test.capture};
            opposing_king_checked =
              SearchDetail::team_has_checked_king(
                position,
                team_of(position.side_to_move()));
            child_terminal =
              terminal_result(position, history)
                .is_terminal();
        }

        expect(
          OrderingDetail::contains_move(
            legal_moves, test.capture)
            && proven_below
            && opposing_king_checked
                 == test.expected_check
            && child_terminal
                 == test.expected_terminal
            && !SearchDetail::
                 main_search_exchange_prunes_child(
                   proven_below,
                   opposing_king_checked,
                   child_terminal)
            && positions_equal(position, original)
            && history_matches(history, keys),
          "a checking or terminal losing capture is re-admitted with complete state restoration");

        SearchDetail::SearchState enabled_state;
        SearchDetail::SearchState disabled_state;
        PositionHistory enabled_history{history};
        PositionHistory disabled_history{history};
        const auto enabled =
          SearchDetail::alpha_beta<
            true, true, false, true>(
              position,
              enabled_history,
              1,
              0,
              -INFINITE_SCORE,
              INFINITE_SCORE,
              enabled_state);
        const auto disabled =
          SearchDetail::alpha_beta<
            true, true, false, false>(
              position,
              disabled_history,
              1,
              0,
              -INFINITE_SCORE,
              INFINITE_SCORE,
              disabled_state);
        expect(
          enabled
            && disabled
            && enabled->score == disabled->score
            && enabled->best_move
                 == disabled->best_move
            && enabled_state.nodes
                 == disabled_state.nodes,
          "child re-admission preserves the complete depth-one traversal");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys)
            && enabled_history.current_key()
                 == position.key()
            && disabled_history.current_key()
                 == position.key(),
          "enabled and disabled re-admission searches restore root state");
    }
}

void test_main_search_exchange_pruning_parity_and_cancellation() {
    Position position =
      main_search_exchange_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);

    SearchDetail::SearchState enabled_state;
    SearchDetail::SearchState disabled_state;
    PositionHistory enabled_history{history};
    PositionHistory disabled_history{history};
    const auto enabled =
      SearchDetail::alpha_beta<
        false, false, false, true>(
          position,
          enabled_history,
          1,
          0,
          -INFINITE_SCORE,
          INFINITE_SCORE,
          enabled_state);
    const auto disabled =
      SearchDetail::alpha_beta<
        false, false, false, false>(
          position,
          disabled_history,
          1,
          0,
          -INFINITE_SCORE,
          INFINITE_SCORE,
          disabled_state);
    expect(
      enabled
        && disabled
        && enabled->score == disabled->score
        && enabled->best_move
             == disabled->best_move,
      "exchange pruning preserves the nominal full-depth score and move");
    expect(
      enabled_state.nodes < disabled_state.nodes,
      "exchange pruning materially reduces the focused full-depth traversal");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys)
        && enabled_history.current_key()
             == position.key()
        && disabled_history.current_key()
             == position.key(),
      "enabled and disabled exchange searches restore root state");
    if (!enabled)
        return;

    bool observed_interruption = false;
    for (std::uint64_t limit = 0;
         limit < enabled_state.nodes;
         ++limit) {
        SearchDetail::SearchBudget budget{
          limit, std::nullopt};
        SearchDetail::LimitedSearchState state{
          std::move(budget)};
        Position working = position;
        PositionHistory working_history{history};
        const auto interrupted =
          SearchDetail::alpha_beta<
            false, false, false, true>(
              working,
              working_history,
              1,
              0,
              -INFINITE_SCORE,
              INFINITE_SCORE,
              state);
        observed_interruption =
          observed_interruption || !interrupted;
        expect(
          !interrupted
            && interrupted.error()
                 == SearchStopReason::NODE_LIMIT
            && state.nodes == limit,
          "every incomplete exchange-pruned prefix reports its node limit");
        expect(
          positions_equal(working, original)
            && working_history.current_key()
                 == position.key()
            && history_matches(history, keys),
          "every exchange-pruned interruption restores position and history");
    }

    expect(
      observed_interruption,
      "the focused exchange search exercises cancellation");
}

void test_null_move_state_restoration() {
    Position position = null_move_reduction_position();
    position.set_en_passant_square(
      RED, make_square(FILE_H, RANK_3));
    position.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_6));
    const Position original = position;
    const PositionKey repeated =
      position.key()
      ^ PositionKey{0x3131313131313131ULL};
    const std::array keys = {
      repeated,
      repeated,
      position.key(),
    };
    PositionHistory history = make_history(keys);
    const HistoryContext original_context =
      history.context();

    {
        SearchDetail::NullMoveState null_move{
          position, history};
        expect(
          position.side_to_move() == BLUE
            && position.en_passant_square(RED)
                 == SQ_NONE
            && position.en_passant_square(BLUE)
                 == original.en_passant_square(BLUE),
          "a null move advances one color and clears only its en-passant target");
        expect(
          history.current_key() == position.key()
            && history.current_count() == 1
            && history.context().length == 1
            && !history.has_repeated_position(),
          "a null move searches from a fresh repetition-history segment");
    }

    expect(
      positions_equal(position, original)
        && history.context() == original_context
        && history_matches(history, keys),
      "null-move destruction restores position and history exactly");
}

void test_null_move_score_and_move_parity() {
    {
        Position position = material_tactic_position();
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory enabled_history =
          make_history(keys);
        PositionHistory disabled_history =
          make_history(keys);
        SearchDetail::SearchState enabled_state;
        SearchDetail::SearchState disabled_state;
        const auto enabled =
          SearchDetail::alpha_beta<true, true, true>(
            position,
            enabled_history,
            5,
            0,
            -INFINITE_SCORE,
            INFINITE_SCORE,
            enabled_state);
        const auto disabled =
          SearchDetail::alpha_beta<true, true, false>(
            position,
            disabled_history,
            5,
            0,
            -INFINITE_SCORE,
            INFINITE_SCORE,
            disabled_state);

        expect(
          enabled
            && disabled
            && enabled->score == disabled->score
            && enabled->best_move
                 == disabled->best_move,
          "null-move pruning preserves the depth-five tactical score and move");
        expect(
          positions_equal(position, original)
            && history_matches(
                 enabled_history, keys)
            && history_matches(
                 disabled_history, keys),
          "tactical null-move comparison restores both search histories");
    }

    {
        Position position = pawn_only_position();
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory enabled_history =
          make_history(keys);
        PositionHistory disabled_history =
          make_history(keys);
        SearchDetail::SearchState enabled_state;
        SearchDetail::SearchState disabled_state;
        const auto enabled =
          SearchDetail::alpha_beta<true, true, true>(
            position,
            enabled_history,
            SearchDetail::NULL_MOVE_MIN_DEPTH,
            0,
            Score{-1},
            DRAW_SCORE,
            enabled_state);
        const auto disabled =
          SearchDetail::alpha_beta<true, true, false>(
            position,
            disabled_history,
            SearchDetail::NULL_MOVE_MIN_DEPTH,
            0,
            Score{-1},
            DRAW_SCORE,
            disabled_state);

        expect(
          enabled
            && disabled
            && enabled->score == disabled->score
            && enabled->best_move
                 == disabled->best_move
            && enabled_state.nodes
                 == disabled_state.nodes,
          "the non-pawn-material guard preserves a pawn-only zugzwang-risk search");
        expect(
          positions_equal(position, original)
            && history_matches(
                 enabled_history, keys)
            && history_matches(
                 disabled_history, keys),
          "pawn-only null-move comparison restores both histories");
    }
}

void test_null_move_cutoff_and_cancellation() {
    Position position = null_move_reduction_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory enabled_history =
      make_history(keys);
    PositionHistory disabled_history =
      make_history(keys);
    SearchDetail::SearchState enabled_state;
    SearchDetail::SearchState disabled_state;
    constexpr Score alpha = DRAW_SCORE;
    constexpr Score beta = DRAW_SCORE + 1;
    constexpr int depth = 5;

    const auto enabled =
      SearchDetail::alpha_beta<true, false, true>(
        position,
        enabled_history,
        depth,
        0,
        alpha,
        beta,
        enabled_state);
    const auto disabled =
      SearchDetail::alpha_beta<true, false, false>(
        position,
        disabled_history,
        depth,
        0,
        alpha,
        beta,
        disabled_state);
    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);

    expect(
      evaluate(position) >= beta
        && enabled
        && disabled
        && enabled->score >= beta
        && disabled->score >= beta
        && OrderingDetail::contains_move(
             legal_moves, enabled->best_move)
        && !SearchDetail::is_mate_score(
             enabled->score)
        && enabled_state.nodes
             < disabled_state.nodes,
      "a safe non-mate null cutoff returns a legal bound move with fewer nodes");

    PositionHistory consecutive_history =
      make_history(keys);
    SearchDetail::SearchState consecutive_state;
    const auto consecutive_disabled =
      SearchDetail::alpha_beta<true, true, true>(
        position,
        consecutive_history,
        SearchDetail::NULL_MOVE_MIN_DEPTH,
        0,
        alpha,
        beta,
        consecutive_state,
        Move::none(),
        false);
    PositionHistory policy_disabled_history =
      make_history(keys);
    SearchDetail::SearchState policy_disabled_state;
    const auto policy_disabled =
      SearchDetail::alpha_beta<true, true, false>(
        position,
        policy_disabled_history,
        SearchDetail::NULL_MOVE_MIN_DEPTH,
        0,
        alpha,
        beta,
        policy_disabled_state);
    expect(
      consecutive_disabled
        && policy_disabled
        && consecutive_disabled->score
             == policy_disabled->score
        && consecutive_disabled->best_move
             == policy_disabled->best_move
        && consecutive_state.nodes
             == policy_disabled_state.nodes,
      "the runtime guard prevents consecutive null moves at the current node");

    Position passed = position;
    const Color passing_color =
      passed.side_to_move();
    passed.clear_en_passant_square(
      passing_color);
    passed.set_side_to_move(
      next_color(passing_color));
    MoveList passed_moves;
    generate_legal_moves(passed, passed_moves);
    expect(
      !passed_moves.empty(),
      "the artificial null position has a legal cached-move fixture");
    if (passed_moves.empty())
        return;

    PositionHistory passed_history{passed.key()};
    TranspositionTable table;
    table.store(
      passed.key(),
      passed_history.context(),
      depth,
      -QUEEN_VALUE,
      TranspositionBound::EXACT,
      passed_moves[0]);
    const std::uint32_t stored_generation =
      table.generation();
    table.new_search();
    PositionHistory table_history =
      make_history(keys);
    SearchDetail::SearchState table_state{
      SearchDetail::UnlimitedBudget{},
      &table};
    const auto isolated =
      SearchDetail::alpha_beta<true, true, true>(
        position,
        table_history,
        depth,
        0,
        alpha,
        beta,
        table_state);
    const TranspositionEntry* passed_entry =
      table.find(passed.key());
    expect(
      isolated
        && isolated->score >= beta
        && passed_entry
        && passed_entry->generation
             == stored_generation
        && passed_entry->score == -QUEEN_VALUE
        && table.find(position.key()) == nullptr,
      "the complete artificial null subtree neither probes nor stores table entries");

    SearchDetail::SearchBudget budget{
      std::uint64_t{1}, std::nullopt};
    SearchDetail::LimitedSearchState limited_state{
      std::move(budget)};
    PositionHistory limited_history =
      make_history(keys);
    const auto interrupted =
      SearchDetail::alpha_beta<true, true, true>(
        position,
        limited_history,
        depth,
        0,
        alpha,
        beta,
        limited_state);
    expect(
      !interrupted
        && interrupted.error()
             == SearchStopReason::NODE_LIMIT
        && limited_state.nodes == 1
        && positions_equal(position, original)
        && history_matches(limited_history, keys),
      "cancellation at an artificial null quiescence entry restores all state");
}

void test_null_move_rejects_stalemate_cutoff() {
    Position position =
      null_move_stalemate_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory enabled_history = make_history(keys);
    PositionHistory disabled_history = make_history(keys);
    SearchDetail::SearchState enabled_state;
    SearchDetail::SearchState disabled_state;

    const auto enabled =
      SearchDetail::alpha_beta<true, false, true>(
        position,
        enabled_history,
        SearchDetail::NULL_MOVE_MIN_DEPTH,
        0,
        Score{-1},
        DRAW_SCORE,
        enabled_state);
    const auto disabled =
      SearchDetail::alpha_beta<true, false, false>(
        position,
        disabled_history,
        SearchDetail::NULL_MOVE_MIN_DEPTH,
        0,
        Score{-1},
        DRAW_SCORE,
        disabled_state);

    expect(
      enabled
        && disabled
        && enabled->score == disabled->score
        && enabled->best_move == disabled->best_move
        && enabled_state.nodes == disabled_state.nodes,
      "an artificial stalemate cannot produce a null-move cutoff");
    expect(
      positions_equal(position, original)
        && history_matches(enabled_history, keys)
        && history_matches(disabled_history, keys),
      "a rejected null stalemate restores root position and history");
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
                 >= 1
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
    const ExhaustiveResult reference =
      exhaustive_search(position, history, 1);

    expect(
      result.has_move()
        && result.best_move == reference.best_move
        && result.score == reference.score
        && result.nodes <= reference.nodes,
      "depth-one pruning preserves the exhaustive special-move result");
    expect(
      positions_equal(position, original),
      "special-move search restores castling and en-passant state");
    expect(
      history_matches(history, keys),
      "special-move search restores the complete history");
}

void test_required_pvs_research() {
    Position position = pvs_research_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    const Move expected = Move::normal(
      make_square(FILE_N, RANK_4),
      make_square(FILE_G, RANK_11));

    MoveList ordered_moves;
    generate_legal_moves(position, ordered_moves);
    MoveOrderingBuffer ordering_buffer;
    order_moves(
      position,
      ordered_moves,
      ordering_buffer);
    expect(
      ordered_moves.size() >= 2
        && ordered_moves[0] != expected
        && OrderingDetail::contains_move(
             ordered_moves, expected),
      "the exact PVS fixture orders its unique best move after another move");
    if (ordered_moves.size() < 2
        || !OrderingDetail::contains_move(
             ordered_moves, expected)) {
        return;
    }

    std::expected<
      SearchDetail::NodeResult,
      SearchStopReason> first_child{
        SearchDetail::NodeResult{}};
    SearchDetail::SearchState first_state;
    PositionHistory first_history{history};
    {
        SearchDetail::ChildState child{
          position,
          first_history,
          ordered_moves[0]};
        first_child =
          SearchDetail::alpha_beta(
            position,
            first_history,
            2,
            1,
            -INFINITE_SCORE,
            INFINITE_SCORE,
            first_state);
    }
    expect(
      first_child.has_value(),
      "the first ordered PVS child completes");
    if (!first_child)
        return;

    const Score first_score =
      -first_child->score;
    const Score scout_beta =
      SearchDetail::pvs_scout_beta(
        first_score);

    std::expected<
      SearchDetail::NodeResult,
      SearchStopReason> scout_child{
        SearchDetail::NodeResult{}};
    SearchDetail::SearchState scout_state;
    PositionHistory scout_history{history};
    {
        SearchDetail::ChildState child{
          position,
          scout_history,
          expected};
        scout_child =
          SearchDetail::alpha_beta(
            position,
            scout_history,
            2,
            1,
            -scout_beta,
            -first_score,
            scout_state);
    }
    expect(
      scout_child.has_value(),
      "the later PVS scout completes");
    if (!scout_child)
        return;

    std::expected<
      SearchDetail::NodeResult,
      SearchStopReason> full_child{
        SearchDetail::NodeResult{}};
    SearchDetail::SearchState full_state;
    PositionHistory full_history{history};
    {
        SearchDetail::ChildState child{
          position,
          full_history,
          expected};
        full_child =
          SearchDetail::alpha_beta(
            position,
            full_history,
            2,
            1,
            -INFINITE_SCORE,
            -first_score,
            full_state);
    }
    expect(
      full_child.has_value(),
      "the later PVS full-window search completes");
    if (!full_child)
        return;

    const Score scout_score =
      -scout_child->score;
    const Score exact_score =
      -full_child->score;
    expect(
      SearchDetail::pvs_research_required(
             scout_score,
             first_score,
             INFINITE_SCORE)
        && exact_score > scout_score,
      "the scout bound requires a full search to recover the exact score");

    const ExhaustiveResult exhaustive =
      exhaustive_search(position, history, 3);
    Position expected_child = position;
    UndoState expected_undo;
    do_move(
      expected_child,
      expected,
      expected_undo);
    PositionHistory expected_child_history{history};
    expected_child_history.push(
      expected_child.key());
    const PositionKey expected_child_key =
      expected_child.key();

    TranspositionTable root_table;
    root_table.new_search();
    SearchDetail::SearchState root_state{
      SearchDetail::UnlimitedBudget{},
      &root_table};
    PositionHistory root_history{history};
    const auto root =
      SearchDetail::alpha_beta(
        position,
        root_history,
        3,
        0,
        -INFINITE_SCORE,
        INFINITE_SCORE,
        root_state);
    expect(
      root
        && exhaustive.best_move == expected
        && root->best_move == expected
        && root->score == exact_score
        && root->score == exhaustive.score
        && root_state.nodes < exhaustive.nodes,
      "recursive PVS matches the exhaustive result after its re-search");
    const TranspositionEntry* root_entry =
      root_table.find(position.key());
    const TranspositionEntry* exact_child_entry =
      root_table.find(expected_child_key);
    expect(
      root_entry
        && root_entry->depth == 3
        && root_entry->bound
             == TranspositionBound::EXACT
        && root_entry->score == exact_score
        && exact_child_entry
        && exact_child_entry->depth == 2
        && exact_child_entry->bound
             == TranspositionBound::EXACT
        && exact_child_entry->score
             == -exact_score,
      "the full re-search replaces its scout bound with exact table entries");

    bool saw_scout_before_research = false;
    for (std::uint64_t limit = 0;
         limit < root_state.nodes;
         ++limit) {
        TranspositionTable limited_table;
        limited_table.new_search();
        SearchDetail::SearchBudget budget{
          limit, std::nullopt};
        SearchDetail::LimitedSearchState limited{
          std::move(budget),
          &limited_table};
        PositionHistory limited_history{history};
        const auto interrupted =
          SearchDetail::alpha_beta(
            position,
            limited_history,
            3,
            0,
            -INFINITE_SCORE,
            INFINITE_SCORE,
            limited);

        const TranspositionEntry* scout_entry =
          limited_table.find(
            expected_child_key);
        if (scout_entry
            && scout_entry->depth == 2
            && scout_entry->bound
                 == TranspositionBound::UPPER) {
            saw_scout_before_research = true;
        }

        expect(
          !interrupted
            && interrupted.error()
                 == SearchStopReason::NODE_LIMIT
            && limited.nodes == limit
            && !limited_table.find(
                 position.key()),
          "every incomplete PVS prefix omits a root table entry");
        expect(
          limited_history.current_key()
              == position.key()
            && positions_equal(position, original),
          "every interrupted PVS prefix restores root state");
    }
    expect(
      saw_scout_before_research,
      "an interrupted full re-search retains its completed scout bound");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys)
        && first_history.current_key()
             == position.key()
        && scout_history.current_key()
             == position.key()
        && full_history.current_key()
             == position.key()
        && root_history.current_key()
             == position.key(),
      "PVS scouts, re-searches, and cancellation restore all root state");
}

void test_late_move_reduction_verification() {
    Position position = pvs_research_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    const Move reduced_move = Move::normal(
      make_square(FILE_N, RANK_4),
      make_square(FILE_L, RANK_6));
    const Move expected = Move::normal(
      make_square(FILE_N, RANK_4),
      make_square(FILE_I, RANK_9));

    SearchDetail::SearchState ordering_state;
    MoveList ordered_moves;
    generate_legal_moves(position, ordered_moves);
    order_moves(
      position,
      ordered_moves,
      ordering_state.ordering_buffer,
      ordering_state.quiet_history,
      ordering_state.killer_moves(0),
      Move::none());

    std::size_t quiet_ordinal = 0;
    bool found_reduced_move = false;
    for (const Move move : ordered_moves) {
        if (is_tactical_move(position, move))
            continue;

        if (move == reduced_move) {
            found_reduced_move = true;
            break;
        }
        ++quiet_ordinal;
    }

    Position reduced_child = position;
    UndoState unused;
    do_move(
      reduced_child, reduced_move, unused);
    const bool gives_team_check =
      SearchDetail::team_has_checked_king(
        reduced_child,
        team_of(reduced_child.side_to_move()));

    expect(
      found_reduced_move
        && quiet_ordinal
             == SearchDetail::
                  LATE_MOVE_MIN_QUIET_ORDINAL
        && !in_check(position)
        && !gives_team_check
        && SearchDetail::adaptive_late_move_reduction(
             SearchDetail::LATE_MOVE_MIN_DEPTH,
             quiet_ordinal,
             true,
             false,
             true,
             reduced_move.type(),
             BISHOP,
             KillerPriority{0},
             HistoryScore{0},
             false,
             gives_team_check)
             == SearchDetail::LATE_MOVE_REDUCTION,
      "the integration move satisfies every reduction condition");
    if (!found_reduced_move)
        return;

    constexpr Score alpha = 204;
    constexpr Score beta = 205;
    constexpr int nominal_child_depth =
      SearchDetail::LATE_MOVE_MIN_DEPTH - 1;
    const PositionKey child_key =
      reduced_child.key();

    PositionHistory reduced_probe_history{history};
    reduced_probe_history.push(child_key);
    SearchDetail::SearchState reduced_probe_state;
    const auto reduced_probe =
      SearchDetail::alpha_beta<false, true, false>(
        reduced_child,
        reduced_probe_history,
        0,
        1,
        -beta,
        -alpha,
        reduced_probe_state);

    PositionHistory nominal_probe_history{history};
    nominal_probe_history.push(child_key);
    SearchDetail::SearchState nominal_probe_state;
    const auto nominal_probe =
      SearchDetail::alpha_beta<false, true, false>(
        reduced_child,
        nominal_probe_history,
        nominal_child_depth,
        1,
        -beta,
        -alpha,
        nominal_probe_state);
    expect(
      reduced_probe
        && nominal_probe
        && -reduced_probe->score > alpha
        && -nominal_probe->score <= alpha,
      "the reduced probe is optimistic while nominal depth fails low");

    TranspositionTable reduced_table;
    reduced_table.new_search();
    SearchDetail::SearchState reduced_state{
      SearchDetail::UnlimitedBudget{},
      &reduced_table};
    PositionHistory reduced_search_history{
      history};
    const auto reduced_result =
      SearchDetail::alpha_beta<true, true, false>(
        position,
        reduced_search_history,
        SearchDetail::LATE_MOVE_MIN_DEPTH,
        0,
        alpha,
        beta,
        reduced_state);

    TranspositionTable reference_table;
    reference_table.new_search();
    SearchDetail::SearchState reference_state{
      SearchDetail::UnlimitedBudget{},
      &reference_table};
    PositionHistory reference_history{history};
    const auto reference_result =
      SearchDetail::alpha_beta<false, true, false>(
        position,
        reference_history,
        SearchDetail::LATE_MOVE_MIN_DEPTH,
        0,
        alpha,
        beta,
        reference_state);

    const TranspositionEntry* verified_child =
      reduced_table.find(child_key);
    const TranspositionEntry* root_entry =
      reduced_table.find(position.key());
    expect(
      reduced_result
        && reference_result
        && reduced_result->score
             == reference_result->score
        && reduced_result->best_move
             == reference_result->best_move
        && reduced_result->best_move == expected,
      "full-depth verification corrects the optimistic reduced result");
    expect(
      verified_child
        && verified_child->depth
             == nominal_child_depth
        && root_entry
        && root_entry->depth
             == SearchDetail::LATE_MOVE_MIN_DEPTH
        && root_entry->bound
             == TranspositionBound::LOWER,
      "the optimistic reduced probe is replaced by nominal-depth verification");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys)
        && reduced_search_history.current_key()
             == position.key()
        && reference_history.current_key()
             == position.key(),
      "reduced and unreduced searches restore position and history");
    if (!reduced_result)
        return;

    bool saw_completed_verification = false;
    for (std::uint64_t limit = 0;
         limit < reduced_state.nodes;
         ++limit) {
        TranspositionTable table;
        table.new_search();
        SearchDetail::SearchBudget budget{
          limit, std::nullopt};
        SearchDetail::LimitedSearchState state{
          std::move(budget),
          &table};
        Position working = position;
        PositionHistory working_history{history};
        const auto interrupted =
          SearchDetail::alpha_beta<true, true, false>(
            working,
            working_history,
            SearchDetail::LATE_MOVE_MIN_DEPTH,
            0,
            alpha,
            beta,
            state);

        const TranspositionEntry* child_entry =
          table.find(child_key);
        if (child_entry
            && child_entry->depth
                 == nominal_child_depth) {
            saw_completed_verification = true;
        }

        expect(
          !interrupted
            && interrupted.error()
                 == SearchStopReason::NODE_LIMIT
            && state.nodes == limit
            && !table.find(
                 position.key()),
          "every incomplete reduced-search prefix is discarded");
        expect(
          positions_equal(working, original)
            && working_history.current_key()
                 == position.key()
            && history_matches(history, keys),
          "every reduced-search interruption restores root state");
    }

    expect(
      saw_completed_verification,
      "an interrupted root can contain a completed child verification");
}

void test_dynamic_late_move_reduction_parity() {
    std::array positions = {
      material_tactic_position(),
      pvs_research_position(),
    };

    for (Position& position : positions) {
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory reduced_history =
          make_history(keys);
        PositionHistory full_history =
          make_history(keys);
        SearchDetail::SearchState reduced_state;
        SearchDetail::SearchState full_state;
        const auto reduced =
          SearchDetail::alpha_beta<true, false, false>(
            position,
            reduced_history,
            5,
            0,
            -INFINITE_SCORE,
            INFINITE_SCORE,
            reduced_state);
        const auto full =
          SearchDetail::alpha_beta<false, false, false>(
            position,
            full_history,
            5,
            0,
            -INFINITE_SCORE,
            INFINITE_SCORE,
            full_state);

        expect(
          reduced
            && full
            && reduced->score == full->score
            && reduced->best_move == full->best_move,
          "dynamic late-move reductions preserve the depth-five tactical result");
        expect(
          reduced_state.nodes < full_state.nodes,
          "dynamic late-move reductions lower tactical search nodes");
        expect(
          positions_equal(position, original)
            && history_matches(
                 reduced_history, keys)
            && history_matches(
                 full_history, keys),
          "reduced and full tactical searches restore all state");
    }
}

void test_pvs_research_and_cancellation() {
    Position position = material_tactic_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    const Move expected = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));

    MoveList ordered_moves;
    generate_legal_moves(position, ordered_moves);
    Move quiet_preferred = Move::none();
    for (const Move move : ordered_moves) {
        if (!is_tactical_move(position, move)) {
            quiet_preferred = move;
            break;
        }
    }

    expect(
      quiet_preferred.is_board_move(),
      "the PVS fixture contains a quiet preferred move");
    if (!quiet_preferred.is_board_move())
        return;

    MoveOrderingBuffer ordering_buffer;
    order_moves(
      position,
      ordered_moves,
      ordering_buffer,
      quiet_preferred);
    expect(
      !ordered_moves.empty()
        && ordered_moves[0] == quiet_preferred
        && quiet_preferred != expected,
      "the quiet preferred move precedes the unique best move");

    const ExhaustiveResult exhaustive =
      exhaustive_search(position, history, 1);
    TranspositionTable complete_table{64};
    complete_table.new_search();
    SearchDetail::SearchState complete_state{
      SearchDetail::UnlimitedBudget{},
      &complete_table};
    PositionHistory complete_history{history};
    const auto complete =
      SearchDetail::alpha_beta(
        position,
        complete_history,
        1,
        0,
        -INFINITE_SCORE,
        INFINITE_SCORE,
        complete_state,
        quiet_preferred);

    expect(
      complete
        && exhaustive.best_move == expected
        && complete->best_move == expected
        && complete->score == exhaustive.score,
      "a later ordered improvement is re-searched to the exhaustive score");
    const TranspositionEntry* completed_entry =
      complete_table.find(position.key());
    expect(
      completed_entry
        && completed_entry->depth == 1
        && completed_entry->bound
             == TranspositionBound::EXACT
        && completed_entry->best_move == expected,
      "the completed PVS root stores its exact result");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys)
        && complete_history.current_key()
             == position.key(),
      "a completed PVS re-search restores position and history");
    if (!complete)
        return;

    TranspositionTable cutoff_table{64};
    cutoff_table.new_search();
    SearchDetail::SearchState cutoff_state{
      SearchDetail::UnlimitedBudget{},
      &cutoff_table};
    PositionHistory cutoff_history{history};
    const auto cutoff =
      SearchDetail::alpha_beta(
        position,
        cutoff_history,
        1,
        0,
        -INFINITE_SCORE,
        DRAW_SCORE,
        cutoff_state,
        quiet_preferred);
    const TranspositionEntry* cutoff_entry =
      cutoff_table.find(position.key());
    expect(
      cutoff
        && cutoff->best_move == expected
        && cutoff->score >= DRAW_SCORE
        && cutoff_state.nodes
             < complete_state.nodes
        && cutoff_entry
        && cutoff_entry->bound
             == TranspositionBound::LOWER,
      "a later scout at beta cuts off without a full re-search");

    for (std::uint64_t limit = 0;
         limit < complete_state.nodes;
         ++limit) {
        TranspositionTable table{64};
        table.new_search();
        SearchDetail::SearchBudget budget{
          limit, std::nullopt};
        SearchDetail::LimitedSearchState state{
          std::move(budget),
          &table};
        PositionHistory working{history};
        const auto interrupted =
          SearchDetail::alpha_beta(
            position,
            working,
            1,
            0,
            -INFINITE_SCORE,
            INFINITE_SCORE,
            state,
            quiet_preferred);

        expect(
          !interrupted
            && interrupted.error()
                 == SearchStopReason::NODE_LIMIT
            && state.nodes == limit
            && !table.find(
                 position.key()),
          "every incomplete PVS node prefix is discarded");
        expect(
          positions_equal(position, original)
            && working.current_key()
                 == position.key()
            && history_matches(history, keys),
          "every PVS interruption restores position and history");
    }
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
            && pruned.best_move == exhaustive.best_move,
          "alpha-beta retains the exhaustive kings-only move");
        expect(
          pruned.score == exhaustive.score,
          "alpha-beta returns the exhaustive kings-only score");
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
            && pruned.score == exhaustive.score,
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
    test_depth_zero_quiescence();
    test_material_capture_for_every_color();
    test_mate_distance_window_pruning();
    test_forward_pruning_toggle();
    test_main_search_exchange_pruning_guards();
    test_main_search_exchange_pruning_reentry();
    test_main_search_exchange_pruning_parity_and_cancellation();
    test_null_move_state_restoration();
    test_null_move_score_and_move_parity();
    test_null_move_cutoff_and_cancellation();
    test_null_move_rejects_stalemate_cutoff();
    test_terminal_positions_precede_evaluation();
    test_immediate_king_capture();
    test_root_and_child_repetition();
    test_special_move_state_restoration();
    test_required_pvs_research();
    test_late_move_reduction_verification();
    test_dynamic_late_move_reduction_parity();
    test_pvs_research_and_cancellation();
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
