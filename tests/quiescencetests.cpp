#include "quiescence.h"
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

void expect(bool condition, std::string_view message) {
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

[[nodiscard]] constexpr bool contains_move(
  const MoveList& moves,
  Move expected) noexcept {
    for (const Move move : moves) {
        if (move == expected)
            return true;
    }

    return false;
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

// These king squares remain separated from the tactical fixtures below.
[[nodiscard]] constexpr Position
separated_kings() noexcept {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_13));
    position.put_piece(
      G_KING, make_square(FILE_N, RANK_8));
    return position;
}

[[nodiscard]] constexpr Position
hanging_queen_position() noexcept {
    Position position = separated_kings();
    position.put_piece(
      R_ROOK, make_square(FILE_F, RANK_5));
    position.put_piece(
      B_QUEEN, make_square(FILE_F, RANK_8));
    return position;
}

[[nodiscard]] constexpr Position
poisoned_pawn_position() noexcept {
    Position position = separated_kings();
    position.put_piece(
      R_QUEEN, make_square(FILE_F, RANK_5));
    position.put_piece(
      B_PAWN, make_square(FILE_F, RANK_8));
    position.put_piece(
      B_ROOK, make_square(FILE_F, RANK_10));
    return position;
}

// Red has one legal move, d1-e2. The move is a quiet king evasion.
[[nodiscard]] constexpr Position
quiet_evasion_position() noexcept {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_D, RANK_1));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_1));
    position.put_piece(
      Y_PAWN, make_square(FILE_D, RANK_2));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_4));
    position.put_piece(
      B_KNIGHT, make_square(FILE_F, RANK_2));
    position.put_piece(
      G_KING, make_square(FILE_N, RANK_11));
    return position;
}

[[nodiscard]] constexpr Position
forced_evasion_capture_position() noexcept {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_D, RANK_1));
    position.put_piece(
      R_QUEEN, make_square(FILE_H, RANK_5));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    position.put_piece(
      B_ROOK, make_square(FILE_D, RANK_4));
    position.put_piece(
      B_ROOK, make_square(FILE_H, RANK_8));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_1));
    position.put_piece(
      G_KING, make_square(FILE_N, RANK_10));
    return position;
}

[[nodiscard]] constexpr Position
teammate_recapture_position() noexcept {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      R_QUEEN, make_square(FILE_F, RANK_5));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    position.put_piece(
      B_PAWN, make_square(FILE_F, RANK_6));
    position.put_piece(
      B_ROOK, make_square(FILE_F, RANK_8));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_13));
    position.put_piece(
      Y_ROOK, make_square(FILE_F, RANK_4));
    position.put_piece(
      G_KING, make_square(FILE_K, RANK_8));
    return position;
}

[[nodiscard]] constexpr Position
blocked_corner(bool checked) noexcept {
    Position position = quiet_evasion_position();
    position.put_piece(
      Y_PAWN, make_square(FILE_E, RANK_2));

    if (!checked) {
        position.remove_piece(
          make_square(FILE_F, RANK_2));
    }

    return position;
}

// Green can capture j10 with its rook. The resulting Red position is the
// non-checking blocked corner above and therefore ends in stalemate.
[[nodiscard]] constexpr Position
terminal_capture_position() noexcept {
    Position position = blocked_corner(false);
    position.set_side_to_move(GREEN);
    position.put_piece(
      G_ROOK, make_square(FILE_N, RANK_10));
    position.put_piece(
      R_PAWN, make_square(FILE_J, RANK_10));
    position.put_piece(
      Y_BISHOP, make_square(FILE_H, RANK_12));
    return position;
}

// Red is checked by h8 and can capture that rook with the queen. Green's rook
// can then recapture the queen, so the evasion is a losing local exchange.
[[nodiscard]] constexpr Position
checked_losing_capture_position() noexcept {
    Position position = separated_kings();
    position.remove_piece(
      make_square(FILE_N, RANK_8));
    position.put_piece(
      G_KING, make_square(FILE_N, RANK_7));
    position.put_piece(
      R_QUEEN, make_square(FILE_F, RANK_8));
    position.put_piece(
      B_ROOK, make_square(FILE_H, RANK_8));
    position.put_piece(
      G_ROOK, make_square(FILE_H, RANK_10));
    return position;
}

[[nodiscard]] constexpr Position
quiet_promotion_position() noexcept {
    Position position = separated_kings();
    position.put_piece(
      R_PAWN, make_square(FILE_H, RANK_10));
    return position;
}

[[nodiscard]] constexpr Position
capture_promotion_position() noexcept {
    Position position = quiet_promotion_position();
    position.put_piece(
      B_ROOK, make_square(FILE_G, RANK_11));
    return position;
}

[[nodiscard]] constexpr Position
en_passant_position(bool occupied_target) noexcept {
    Position position = separated_kings();
    position.put_piece(
      R_PAWN, make_square(FILE_D, RANK_5));
    position.put_piece(
      B_PAWN, make_square(FILE_D, RANK_6));

    if (occupied_target) {
        position.put_piece(
          G_ROOK, make_square(FILE_C, RANK_6));
    }

    position.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_6));
    return position;
}

[[nodiscard]] constexpr Position
en_passant_promotion_position() noexcept {
    Position position = separated_kings();
    position.put_piece(
      R_PAWN, make_square(FILE_B, RANK_10));
    position.put_piece(
      G_ROOK, make_square(FILE_C, RANK_11));
    position.put_piece(
      B_PAWN, make_square(FILE_D, RANK_11));
    position.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_11));
    return position;
}

[[nodiscard]] constexpr Position
king_capture_position() noexcept {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_13));
    position.put_piece(
      G_KING, make_square(FILE_F, RANK_8));
    position.put_piece(
      R_ROOK, make_square(FILE_F, RANK_5));
    return position;
}

// Red has three independent queen captures. None of the resulting pieces
// checks either opposing king, and every child retains legal moves.
[[nodiscard]] constexpr Position
late_capture_position() noexcept {
    Position position = separated_kings();
    position.remove_piece(
      make_square(FILE_N, RANK_8));
    position.put_piece(
      G_KING, make_square(FILE_N, RANK_10));
    position.put_piece(
      R_ROOK, make_square(FILE_F, RANK_5));
    position.put_piece(
      R_KNIGHT, make_square(FILE_D, RANK_5));
    position.put_piece(
      R_BISHOP, make_square(FILE_H, RANK_7));
    position.put_piece(
      B_QUEEN, make_square(FILE_F, RANK_8));
    position.put_piece(
      B_QUEEN, make_square(FILE_E, RANK_7));
    position.put_piece(
      B_QUEEN, make_square(FILE_K, RANK_10));
    return position;
}

struct QuiescenceResult {
    Score score = DRAW_SCORE;
    std::uint64_t nodes = 0;
};

struct TableQuiescenceResult {
    SearchDetail::QuiescenceResult result{};
    std::uint64_t nodes = 0;
};

[[nodiscard]] QuiescenceResult run_quiescence(
  Position& position,
  PositionHistory& history,
  int ply = 0,
  int quiescence_ply = 0,
  Score alpha = -INFINITE_SCORE,
  Score beta = INFINITE_SCORE) {
    SearchDetail::SearchState state;
    const auto score =
      SearchDetail::quiescence(
        position,
        history,
        ply,
        quiescence_ply,
        alpha,
        beta,
        state);
    assert(score.has_value());
    return {
      score ? *score : DRAW_SCORE,
      state.nodes,
    };
}

[[nodiscard]] TableQuiescenceResult
run_table_quiescence(
  Position& position,
  PositionHistory& history,
  TranspositionTable& table,
  int ply = 0,
  int quiescence_ply = 0,
  Score alpha = -INFINITE_SCORE,
  Score beta = INFINITE_SCORE,
  bool transposition_allowed = true) {
    table.new_search();
    SearchDetail::SearchState state{
      SearchDetail::UnlimitedBudget{},
      &table};
    const auto result =
      SearchDetail::quiescence_with_repetition(
        position,
        history,
        ply,
        quiescence_ply,
        alpha,
        beta,
        state,
        transposition_allowed);
    assert(result.has_value());
    return {
      result ? *result
             : SearchDetail::QuiescenceResult{},
      state.nodes,
    };
}

[[nodiscard]] QuiescenceResult
run_quiescence_after(
  Position& position,
  PositionHistory& history,
  Square previous_destination,
  int ply = 0,
  int quiescence_ply = 0,
  Score alpha = -INFINITE_SCORE,
  Score beta = INFINITE_SCORE) {
    SearchDetail::SearchState state;
    const auto result =
      SearchDetail::quiescence_with_repetition(
        position,
        history,
        ply,
        quiescence_ply,
        alpha,
        beta,
        state,
        true,
        std::nullopt,
        previous_destination);
    assert(result.has_value());
    return {
      result ? result->score : DRAW_SCORE,
      state.nodes,
    };
}

struct ReferenceResult {
    Score score = DRAW_SCORE;
    std::uint64_t nodes = 0;
};

[[nodiscard]] Score reference_terminal_score(
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

[[nodiscard]] constexpr bool reference_is_tactical(
  const Position& position,
  Move move) noexcept {
    return move.type() == MoveType::EN_PASSANT
        || move.is_promotion()
        || !position.empty(move.to());
}

// The reference traversal copies every child and searches without alpha-beta
// bounds, move ordering, undo_move(), or shared search state.
[[nodiscard]] ReferenceResult reference_quiescence(
  Position position,
  PositionHistory history,
  int ply,
  int remaining_plies) {
    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    const PositionResult position_result =
      terminal_result(position, history, legal_moves);

    if (position_result.is_terminal()) {
        return {
          reference_terminal_score(
            position_result,
            team_of(position.side_to_move()),
            ply),
          1,
        };
    }

    if (remaining_plies == 0)
        return {evaluate(position), 1};

    const bool checked = in_check(position);
    ReferenceResult result;
    result.score =
      checked ? -INFINITE_SCORE : evaluate(position);
    result.nodes = 1;

    for (const Move move : legal_moves) {
        if (!checked
            && !reference_is_tactical(
                 position, move))
            continue;

        Position child_position = position;
        UndoState unused;
        do_move(child_position, move, unused);

        PositionHistory child_history = history;
        child_history.push(child_position.key());
        const ReferenceResult child =
          reference_quiescence(
            child_position,
            child_history,
            ply + 1,
            remaining_plies - 1);
        result.nodes += child.nodes;

        const Score candidate = -child.score;
        if (candidate > result.score)
            result.score = candidate;
    }

    return result;
}

static_assert(DRAW_SCORE == 0);
static_assert(MAX_QUIESCENCE_PLY > 0);
static_assert(
  QUIESCENCE_DELTA_MARGIN
  == 3 * PAWN_VALUE);
static_assert(
  SearchDetail::quiescence_exchange_threshold(
    Score{0},
    QUIESCENCE_DELTA_MARGIN,
    true)
  == 0);
static_assert(
  SearchDetail::quiescence_exchange_threshold(
    Score{0},
    QUIESCENCE_DELTA_MARGIN + Score{1},
    true)
  == 1);
static_assert(
  SearchDetail::quiescence_exchange_threshold(
    Score{0},
    QUIESCENCE_DELTA_MARGIN + Score{1},
    false)
  == 0);
static_assert(
  MAX_SEARCH_PLY
  == MAX_SEARCH_DEPTH
       + MAX_QUIESCENCE_CHECK_PLY);
static_assert(
  SearchDetail::terminal_score(
    PositionResult::king_capture(RED_YELLOW),
    RED_YELLOW,
    MAX_SEARCH_PLY)
  == MATE_SCORE - MAX_SEARCH_PLY);
static_assert(
  std::is_same_v<
    decltype(SearchDetail::quiescence(
      std::declval<Position&>(),
      std::declval<PositionHistory&>(),
      0,
      0,
      DRAW_SCORE,
      DRAW_SCORE + 1,
      std::declval<SearchDetail::SearchState&>())),
    std::expected<Score, SearchStopReason>>);
static_assert(
  std::is_same_v<
    decltype(SearchDetail::quiescence_with_repetition(
      std::declval<Position&>(),
      std::declval<PositionHistory&>(),
      0,
      0,
      DRAW_SCORE,
      DRAW_SCORE + 1,
      std::declval<SearchDetail::SearchState&>())),
    std::expected<
      SearchDetail::QuiescenceResult,
      SearchStopReason>>);
static_assert(!noexcept(
  SearchDetail::quiescence(
    std::declval<Position&>(),
    std::declval<PositionHistory&>(),
    0,
    0,
    DRAW_SCORE,
    DRAW_SCORE + 1,
    std::declval<SearchDetail::SearchState&>())));

void test_quiet_stand_pat_and_beta_cutoff() {
    {
        Position position = separated_kings();
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const Score stand_pat = evaluate(position);

        const QuiescenceResult result =
          run_quiescence(position, history);

        expect(
          result.score == stand_pat
            && result.nodes == 1,
          "a quiet position stands pat in one node");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "quiet stand pat preserves position and history");
    }

    {
        Position position = separated_kings();
        position.put_piece(
          R_ROOK, make_square(FILE_F, RANK_6));
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const Score stand_pat = evaluate(position);

        const QuiescenceResult result =
          run_quiescence(
            position,
            history,
            0,
            0,
            -INFINITE_SCORE,
            stand_pat);

        expect(
          result.score == stand_pat
            && result.nodes == 1,
          "stand pat returns its fail-soft score above beta");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "a stand-pat beta cutoff preserves position and history");
    }
}

void test_quiet_checks_are_not_extended() {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      R_ROOK, make_square(FILE_E, RANK_5));
    position.put_piece(
      B_KING, make_square(FILE_F, RANK_8));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_13));
    position.put_piece(
      G_KING, make_square(FILE_N, RANK_10));
    const Position original = position;
    const Move quiet_check = Move::normal(
      make_square(FILE_E, RANK_5),
      make_square(FILE_F, RANK_5));

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    expect(
      contains_move(legal_moves, quiet_check)
        && !is_tactical_move(
             position, quiet_check),
      "the quiet-check fixture contains its non-tactical rook move");

    UndoState undo;
    do_move(position, quiet_check, undo);
    expect(
      in_check(position),
      "the excluded quiet move checks the next side");
    undo_move(position, quiet_check, undo);

    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    const QuiescenceResult result =
      run_quiescence(position, history);

    expect(
      result.score == evaluate(position)
        && result.nodes == 1,
      "a non-capturing check is outside the initial quiescence frontier");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys),
      "quiet-check classification and quiescence preserve all state");
}

void test_hanging_piece_at_depth_zero() {
    Position position = hanging_queen_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    PositionHistory reference_history =
      make_history(keys);
    const Score stand_pat = evaluate(position);
    const QuiescenceResult reference =
      run_quiescence(
        position, reference_history);

    const SearchResult result =
      search(position, history, 0);

    expect(
      !result.has_move()
        && result.score == reference.score
        && result.nodes == reference.nodes
        && result.score > stand_pat,
      "depth zero resolves the hanging queen without returning a root move");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys),
      "depth-zero tactical search preserves position and history");
}

void test_tactical_beta_cutoff() {
    Position position = hanging_queen_position();
    position.put_piece(
      B_PAWN, make_square(FILE_F, RANK_4));
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    const Move queen_capture = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));
    const Move pawn_capture = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_4));
    expect(
      contains_move(legal_moves, queen_capture)
        && contains_move(legal_moves, pawn_capture)
        && move_order_score(
             position, queen_capture)
             > move_order_score(
                 position, pawn_capture),
      "the tactical-cutoff fixture contains two ordered captures");

    Position child_position = position;
    UndoState unused;
    do_move(
      child_position, queen_capture, unused);
    const std::array child_keys = {
      position.key(),
      child_position.key(),
    };
    PositionHistory child_history =
      make_history(child_keys);
    const QuiescenceResult child =
      run_quiescence(
        child_position,
        child_history,
        1,
        1);
    const Score cutoff_score = -child.score;

    const QuiescenceResult result =
      run_quiescence(
        position,
        history,
        0,
        0,
        -INFINITE_SCORE,
        cutoff_score);
    expect(
      evaluate(position) < cutoff_score
        && result.score == cutoff_score
        && result.nodes == child.nodes + 1,
      "the first ordered capture produces a fail-soft beta cutoff");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys),
      "a tactical beta cutoff preserves position and history");
}

void test_poisoned_capture_and_bound() {
    Position position = poisoned_pawn_position();
    const Position original = position;
    const Move poisoned_capture = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));
    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);

    expect(
      contains_move(legal_moves, poisoned_capture)
        && is_tactical_move(
             position, poisoned_capture),
      "the poisoned-pawn capture is legal and tactical");
    expect(
      material_balance(
        position,
        team_of(position.side_to_move())) == 300,
      "the poisoned-pawn fixture has the expected material balance");

    const std::array expected_nodes = {
      std::uint64_t{1},
      std::uint64_t{2},
      std::uint64_t{3},
    };

    for (int remaining = 0;
         remaining <= 2;
         ++remaining) {
        const int quiescence_ply =
          MAX_QUIESCENCE_PLY - remaining;
        PositionHistory history{position.key()};
        const QuiescenceResult actual =
          run_quiescence(
            position,
            history,
            quiescence_ply,
            quiescence_ply);
        const ReferenceResult reference =
          reference_quiescence(
            position,
            history,
            quiescence_ply,
            remaining);

        expect(
          actual.score == reference.score
            && actual.nodes
                 == expected_nodes[
                      std::size_t(remaining)]
            && actual.nodes == reference.nodes,
          "the quiescence bound exposes and then resolves the recapture");
        expect(
          positions_equal(position, original)
            && history.size() == 1
            && history.current_key()
                 == position.key(),
          "each bounded recapture search restores position and history");
    }

    PositionHistory history{position.key()};
    PositionHistory quiescence_history{
      position.key()};
    const QuiescenceResult expected_depth_zero =
      run_quiescence(
        position, quiescence_history);
    const SearchResult depth_zero =
      search(position, history, 0);
    expect(
      !depth_zero.has_move()
        && depth_zero.score
             == expected_depth_zero.score
        && depth_zero.nodes
             == expected_depth_zero.nodes,
      "full depth-zero quiescence declines the poisoned capture");

    const SearchResult depth_one =
      search(position, history, 1);
    expect(
      depth_one.has_move()
        && depth_one.best_move != poisoned_capture,
      "depth-one search does not choose the poisoned capture");
    expect(
      positions_equal(position, original)
        && history_matches(
             history,
             std::array{position.key()}),
      "poisoned-capture searches preserve position and history");
}

void test_quiet_check_evasion() {
    Position position =
      forced_evasion_capture_position();
    const Position original = position;
    const Move evasion = Move::normal(
      make_square(FILE_D, RANK_1),
      make_square(FILE_E, RANK_2));
    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);

    expect(
      in_check(position)
        && legal_moves.size() == 1
        && legal_moves[0] == evasion
        && !is_tactical_move(position, evasion),
      "the checked fixture has one quiet legal evasion");

    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    const QuiescenceResult result =
      run_quiescence(position, history);
    const ReferenceResult reference =
      reference_quiescence(
        position,
        history,
        0,
        2);

    expect(
      material_balance(
        position,
        team_of(position.side_to_move())) == -100
        && result.nodes == 3
        && result.score == reference.score
        && result.nodes == reference.nodes,
      "quiescence searches a quiet evasion and the following capture");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys),
      "quiet-evasion search preserves position and history");

    PositionHistory narrow_history{position.key()};
    const QuiescenceResult narrow =
      run_quiescence(
        position,
        narrow_history,
        0,
        0,
        reference.score - PAWN_VALUE,
        reference.score + PAWN_VALUE);
    expect(
      narrow.score == reference.score
        && narrow.nodes == 3,
      "a checked node ignores stand pat even when static evaluation exceeds beta");
    expect(
      positions_equal(position, original)
        && narrow_history.size() == 1
        && narrow_history.current_key()
             == position.key(),
      "the checked narrow-window search restores position and history");

    PositionHistory bounded_history{position.key()};
    Position evaded = position;
    UndoState evasion_undo;
    do_move(evaded, evasion, evasion_undo);
    const QuiescenceResult bounded =
      run_quiescence(
        position,
        bounded_history,
        MAX_QUIESCENCE_PLY,
        MAX_QUIESCENCE_PLY);
    expect(
      bounded.score == -evaluate(evaded)
        && bounded.nodes == 2
        && bounded_history.size() == 1
        && bounded_history.current_key()
             == position.key(),
      "the tactical bound extends a checked line through its legal evasion");

    PositionHistory hard_history{position.key()};
    const QuiescenceResult hard =
      run_quiescence(
        position,
        hard_history,
        MAX_QUIESCENCE_CHECK_PLY,
        MAX_QUIESCENCE_CHECK_PLY);
    expect(
      hard.score == evaluate(position)
        && hard.nodes == 1
        && hard_history.size() == 1
        && hard_history.current_key()
             == position.key(),
      "the separate check bound terminates a pathological checking line");
}

void test_proven_losing_capture_pruning() {
    Position position = teammate_recapture_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    const ReferenceResult reference =
      reference_quiescence(
        position,
        history,
        0,
        3);
    const QuiescenceResult result =
      run_quiescence(position, history);

    expect(
      material_balance(
        position,
        team_of(position.side_to_move())) == 800
        && result.nodes == 1
        && result.score == reference.score
        && reference.nodes == 4,
      "a proven losing non-checking capture is pruned without changing the exact score");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys),
      "losing-capture pruning preserves position and history");
}

void test_exchange_pruning_guards() {
    {
        Position position =
          teammate_recapture_position();
        const Move capture = Move::normal(
          make_square(FILE_F, RANK_5),
          make_square(FILE_F, RANK_6));
        MoveList legal_moves;
        generate_legal_moves(position, legal_moves);

        std::size_t full_budget =
          MAX_QUIESCENCE_EXCHANGE_NODES;
        std::size_t empty_budget = 0;
        expect(
          contains_move(legal_moves, capture)
            && SearchDetail::is_quiescence_exchange_candidate(
                 position, capture, false)
            && SearchDetail::quiescence_exchange_is_proven_losing(
                 position, capture, full_budget)
            && !SearchDetail::quiescence_exchange_is_proven_losing(
                  position, capture, empty_budget)
            && empty_budget == 0,
          "only a completed bounded exchange proof can prune a capture");

        UndoState undo;
        do_move(position, capture, undo);
        expect(
          !SearchDetail::selective_quiescence_capture_requires_search(
             position, RED_YELLOW),
          "an ordinary nonterminal losing capture passes every pruning guard");
    }

    {
        Position position =
          checked_losing_capture_position();
        const Position original = position;
        const Move capture = Move::normal(
          make_square(FILE_F, RANK_8),
          make_square(FILE_H, RANK_8));
        MoveList legal_moves;
        generate_legal_moves(position, legal_moves);
        std::size_t exchange_budget =
          MAX_QUIESCENCE_EXCHANGE_NODES;

        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        constexpr int initial_quiescence_ply =
          MAX_QUIESCENCE_PLY - 2;
        const QuiescenceResult actual =
          run_quiescence(
            position,
            history,
            initial_quiescence_ply,
            initial_quiescence_ply);
        const ReferenceResult reference =
          reference_quiescence(
            position,
            history,
            initial_quiescence_ply,
            2);

        expect(
          in_check(position)
            && contains_move(legal_moves, capture)
            && SearchDetail::quiescence_exchange_is_proven_losing(
                 position, capture, exchange_budget)
            && !SearchDetail::is_quiescence_exchange_candidate(
                  position, capture, true)
            && actual.score == reference.score
            && actual.nodes == reference.nodes,
          "a checked node searches a losing capture with exact-reference parity");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "checked-node exchange guards preserve position and history");
    }

    {
        Position position = poisoned_pawn_position();
        const Move capture = Move::normal(
          make_square(FILE_F, RANK_5),
          make_square(FILE_F, RANK_8));
        std::size_t exchange_budget =
          MAX_QUIESCENCE_EXCHANGE_NODES;
        expect(
          SearchDetail::is_quiescence_exchange_candidate(
            position, capture, false)
            && SearchDetail::quiescence_exchange_is_proven_losing(
                 position, capture, exchange_budget),
          "the checking counterexample reaches the exchange classifier");

        UndoState undo;
        do_move(position, capture, undo);
        expect(
          SearchDetail::quiescence_team_has_checked_king(
            position, BLUE_GREEN)
            && SearchDetail::selective_quiescence_capture_requires_search(
                 position, RED_YELLOW),
          "a losing capture that checks either opposing king remains searchable");
    }

    {
        Position promotion =
          capture_promotion_position();
        const Move promotion_capture = Move::promotion(
          make_square(FILE_H, RANK_10),
          make_square(FILE_G, RANK_11),
          QUEEN);
        MoveList promotion_moves;
        generate_legal_moves(
          promotion, promotion_moves);

        Position king_capture =
          king_capture_position();
        const Move capture = Move::normal(
          make_square(FILE_F, RANK_5),
          make_square(FILE_F, RANK_8));
        MoveList king_moves;
        generate_legal_moves(
          king_capture, king_moves);

        expect(
          contains_move(
            promotion_moves, promotion_capture)
            && !SearchDetail::is_quiescence_exchange_candidate(
                  promotion, promotion_capture, false)
            && contains_move(king_moves, capture)
            && !SearchDetail::is_quiescence_exchange_candidate(
                  king_capture, capture, false),
          "promotions and opposing-king captures bypass exchange pruning");
    }

    {
        Position position =
          terminal_capture_position();
        const Position original = position;
        const Move capture = Move::normal(
          make_square(FILE_N, RANK_10),
          make_square(FILE_J, RANK_10));
        MoveList legal_moves;
        generate_legal_moves(position, legal_moves);
        std::size_t exchange_budget =
          MAX_QUIESCENCE_EXCHANGE_NODES;
        const bool proven_losing =
          SearchDetail::quiescence_exchange_is_proven_losing(
            position, capture, exchange_budget);

        Position child = position;
        UndoState unused;
        do_move(child, capture, unused);

        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const QuiescenceResult actual =
          run_quiescence(position, history);
        const ReferenceResult reference =
          reference_quiescence(
            position, history, 0, 4);

        expect(
          contains_move(legal_moves, capture)
            && proven_losing
            && !SearchDetail::quiescence_team_has_checked_king(
                  child, RED_YELLOW)
            && !has_legal_move(child)
            && !SearchDetail::selective_quiescence_capture_requires_search(
                  child, BLUE_GREEN)
            && actual.score == evaluate(position)
            && actual.nodes == 1
            && reference.nodes == 2,
          "a nonchecking losing capture into stalemate follows selective pruning");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "terminal-child exchange guards preserve position and history");
    }
}

void test_delta_pruning_guards() {
    {
        Position position =
          hanging_queen_position();
        position.remove_piece(
          make_square(FILE_N, RANK_8));
        position.put_piece(
          G_KING,
          make_square(FILE_N, RANK_10));
        const Position original = position;
        const Move capture = Move::normal(
          make_square(FILE_F, RANK_5),
          make_square(FILE_F, RANK_8));
        const Score parent_static_score =
          evaluate(position);
        const Score exchange_value =
          static_exchange_evaluation(
            position, capture);
        const Score skipped_alpha =
          parent_static_score
          + exchange_value
          + QUIESCENCE_DELTA_MARGIN
          + Score{1};
        const Score borderline_alpha =
          skipped_alpha - Score{1};
        std::size_t skipped_budget =
          MAX_QUIESCENCE_EXCHANGE_NODES;
        std::size_t borderline_budget =
          MAX_QUIESCENCE_EXCHANGE_NODES;

        expect(
          SearchDetail::is_quiescence_exchange_candidate(
            position, capture, false)
            && SearchDetail::is_quiescence_delta_candidate(
                 position, capture, false)
            && exchange_value == QUEEN_VALUE
            && SearchDetail::quiescence_exchange_threshold(
                 parent_static_score,
                 skipped_alpha,
                 true)
                 == exchange_value + Score{1}
            && SearchDetail::quiescence_immediate_gain_is_below(
                 position,
                 capture,
                 exchange_value + Score{1})
            && !SearchDetail::quiescence_immediate_gain_is_below(
                  position,
                  capture,
                  exchange_value)
            && SearchDetail::quiescence_exchange_is_proven_below(
                 position,
                 capture,
                 exchange_value + Score{1},
                 skipped_budget)
            && !SearchDetail::quiescence_exchange_is_proven_below(
                  position,
                  capture,
                  exchange_value,
                  borderline_budget),
          "delta classification prunes strictly below alpha and keeps equality");

        PositionHistory skipped_history{
          position.key()};
        const QuiescenceResult skipped =
          run_quiescence(
            position,
            skipped_history,
            0,
            0,
            skipped_alpha,
            skipped_alpha + Score{1});
        PositionHistory borderline_history{
          position.key()};
        const QuiescenceResult borderline =
          run_quiescence(
            position,
            borderline_history,
            0,
            0,
            borderline_alpha,
            borderline_alpha + Score{1});
        PositionHistory table_history{
          position.key()};
        TranspositionTable table;
        const TableQuiescenceResult table_cold =
          run_table_quiescence(
            position,
            table_history,
            table,
            0,
            0,
            skipped_alpha,
            skipped_alpha + Score{1});
        const TranspositionEntry* table_entry =
          table.find(
            position.key(),
            table_history.context());
        const TableQuiescenceResult table_warm =
          run_table_quiescence(
            position,
            table_history,
            table,
            0,
            0,
            skipped_alpha,
            skipped_alpha + Score{1});
        expect(
          skipped.score == parent_static_score
            && skipped.nodes == 1
            && borderline.nodes == 2,
          "a below-alpha capture is skipped while the exact boundary is searched");
        expect(
          table_cold.result.score
              == parent_static_score
            && table_cold.result.best_move.is_none()
            && table_cold.result.stand_pat
            && table_entry
            && table_entry->depth == 0
            && table_entry->bound
                 == TranspositionBound::UPPER
            && table_entry->best_move.is_none()
            && table_entry->stand_pat
            && table_warm.result.score
                 == table_cold.result.score
            && table_warm.result.stand_pat
            && table_warm.nodes == 1,
          "a delta fail-low stores and reuses a stand-pat quiescence upper bound");
        expect(
          positions_equal(position, original)
            && skipped_history.size() == 1
            && skipped_history.current_key()
                 == position.key()
            && borderline_history.size() == 1
            && borderline_history.current_key()
                 == position.key()
            && table_history.size() == 1
            && table_history.current_key()
                 == position.key(),
          "delta skip and boundary re-admission restore position and history");
    }

    {
        Position position =
          poisoned_pawn_position();
        const Position original = position;
        const Move capture = Move::normal(
          make_square(FILE_F, RANK_5),
          make_square(FILE_F, RANK_8));
        const Score alpha =
          evaluate(position)
          + QUIESCENCE_DELTA_MARGIN
          + PAWN_VALUE
          + Score{1};
        const Score threshold =
          SearchDetail::quiescence_exchange_threshold(
            evaluate(position), alpha, true);
        std::size_t exchange_budget =
          MAX_QUIESCENCE_EXCHANGE_NODES;

        Position child = position;
        UndoState unused;
        do_move(child, capture, unused);
        PositionHistory history{position.key()};
        const QuiescenceResult result =
          run_quiescence(
            position,
            history,
            0,
            0,
            alpha,
            alpha + Score{1});

        expect(
          SearchDetail::is_quiescence_delta_candidate(
            position, capture, false)
            && threshold == PAWN_VALUE + Score{1}
            && SearchDetail::quiescence_exchange_is_proven_below(
                 position,
                 capture,
                 threshold,
                 exchange_budget)
            && SearchDetail::quiescence_team_has_checked_king(
                 child, BLUE_GREEN)
            && SearchDetail::selective_quiescence_capture_requires_search(
                 child, RED_YELLOW)
            && result.nodes > 1,
          "a below-threshold capture that checks the opposing team is searched");
        expect(
          positions_equal(position, original)
            && history.size() == 1
            && history.current_key()
                 == position.key(),
          "checking delta re-admission restores position and history");
    }

    {
        Position position =
          capture_promotion_position();
        const Position original = position;
        const Move capture = Move::promotion(
          make_square(FILE_H, RANK_10),
          make_square(FILE_G, RANK_11),
          QUEEN);
        const Score alpha =
          evaluate(position)
          + QUIESCENCE_DELTA_MARGIN
          + ExchangeDetail::MAX_EXCHANGE_IMMEDIATE_GAIN
          + Score{1};
        PositionHistory history{position.key()};
        const QuiescenceResult result =
          run_quiescence(
            position,
            history,
            0,
            0,
            alpha,
            alpha + Score{1});

        expect(
          !SearchDetail::is_quiescence_delta_candidate(
            position, capture, false)
            && result.nodes > 1,
          "capture promotions bypass delta pruning");
        expect(
          positions_equal(position, original)
            && history.size() == 1
            && history.current_key()
                 == position.key(),
          "promotion delta guards restore position and history");
    }

    {
        Position position =
          king_capture_position();
        const Position original = position;
        const Move capture = Move::normal(
          make_square(FILE_F, RANK_5),
          make_square(FILE_F, RANK_8));
        const Score alpha =
          evaluate(position)
          + QUIESCENCE_DELTA_MARGIN
          + MAX_PIECE_VALUE
          + Score{1};
        PositionHistory history{position.key()};
        const QuiescenceResult result =
          run_quiescence(
            position,
            history,
            0,
            0,
            alpha,
            alpha + Score{1});

        expect(
          !SearchDetail::is_quiescence_delta_candidate(
            position, capture, false)
            && result.score == MATE_SCORE - Score{1}
            && result.nodes == 2,
          "opposing-king captures bypass delta pruning");
        expect(
          positions_equal(position, original)
            && history.size() == 1
            && history.current_key()
                 == position.key(),
          "king-capture delta guards restore position and history");
    }

    {
        Position position =
          terminal_capture_position();
        const Position original = position;
        const Move capture = Move::normal(
          make_square(FILE_N, RANK_10),
          make_square(FILE_J, RANK_10));
        const Score alpha =
          evaluate(position)
          + QUIESCENCE_DELTA_MARGIN
          + Score{1};
        const Score threshold =
          SearchDetail::quiescence_exchange_threshold(
            evaluate(position), alpha, true);
        std::size_t exchange_budget =
          MAX_QUIESCENCE_EXCHANGE_NODES;

        Position child = position;
        UndoState unused;
        do_move(child, capture, unused);
        PositionHistory history{position.key()};
        const QuiescenceResult result =
          run_quiescence(
            position,
            history,
            0,
            0,
            alpha,
            alpha + Score{1});

        expect(
          SearchDetail::is_quiescence_delta_candidate(
            position, capture, false)
            && SearchDetail::quiescence_exchange_is_proven_below(
                 position,
                 capture,
                 threshold,
                 exchange_budget)
            && !has_legal_move(child)
            && !SearchDetail::selective_quiescence_capture_requires_search(
                  child, BLUE_GREEN)
            && result.score == evaluate(position)
            && result.nodes == 1,
          "a nonchecking terminal capture follows delta pruning");
        expect(
          positions_equal(position, original)
            && history.size() == 1
            && history.current_key()
                 == position.key(),
          "terminal delta re-admission restores position and history");
    }

    {
        Position position =
          teammate_recapture_position();
        const Move capture = Move::normal(
          make_square(FILE_F, RANK_5),
          make_square(FILE_F, RANK_6));
        const Score alpha =
          evaluate(position)
          + QUIESCENCE_DELTA_MARGIN
          + Score{1};
        const Score threshold =
          SearchDetail::quiescence_exchange_threshold(
            evaluate(position), alpha, true);
        std::size_t empty_budget = 0;

        expect(
          threshold == 1
            && !SearchDetail::quiescence_exchange_is_proven_below(
                  position,
                  capture,
                  threshold,
                  empty_budget)
            && empty_budget == 0,
          "an unknown bounded exchange result remains searchable under delta pruning");
    }
}

void test_late_capture_pruning_guards_and_parity() {
    Position position = late_capture_position();
    const Position original = position;
    const Move rook_capture = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));
    const Move knight_capture = Move::normal(
      make_square(FILE_D, RANK_5),
      make_square(FILE_E, RANK_7));
    const Move bishop_capture = Move::normal(
      make_square(FILE_H, RANK_7),
      make_square(FILE_K, RANK_10));
    constexpr Square unrelated_destination =
      make_square(FILE_D, RANK_4);

    MoveList legal_moves;
    generate_legal_tactical_moves(
      position, legal_moves);
    expect(
      legal_moves.size() == 3
        && contains_move(legal_moves, rook_capture)
        && contains_move(legal_moves, knight_capture)
        && contains_move(legal_moves, bishop_capture),
      "the late-capture fixture contains three independent queen captures");

    expect(
      !SearchDetail::is_late_quiescence_capture_candidate(
        position,
        rook_capture,
        false,
        SQ_NONE,
        QUIESCENCE_LATE_CAPTURE_LIMIT)
        && !SearchDetail::is_late_quiescence_capture_candidate(
          position,
          rook_capture,
          false,
          rook_capture.to(),
          QUIESCENCE_LATE_CAPTURE_LIMIT)
        && !SearchDetail::is_late_quiescence_capture_candidate(
          position,
          rook_capture,
          false,
          unrelated_destination,
          QUIESCENCE_LATE_CAPTURE_LIMIT - 1)
        && !SearchDetail::is_late_quiescence_capture_candidate(
          position,
          rook_capture,
          true,
          unrelated_destination,
          QUIESCENCE_LATE_CAPTURE_LIMIT)
        && SearchDetail::is_late_quiescence_capture_candidate(
          position,
          rook_capture,
          false,
          unrelated_destination,
          QUIESCENCE_LATE_CAPTURE_LIMIT),
      "late-capture pruning requires known context, two searched captures, and a non-recapture at a nonchecked node");

    {
        Position promotion =
          capture_promotion_position();
        const Move capture = Move::promotion(
          make_square(FILE_H, RANK_10),
          make_square(FILE_G, RANK_11),
          QUEEN);
        Position king_capture =
          king_capture_position();
        const Move capture_king = Move::normal(
          make_square(FILE_F, RANK_5),
          make_square(FILE_F, RANK_8));

        expect(
          !SearchDetail::is_late_quiescence_capture_candidate(
            promotion,
            capture,
            false,
            unrelated_destination,
            QUIESCENCE_LATE_CAPTURE_LIMIT)
            && !SearchDetail::is_late_quiescence_capture_candidate(
              king_capture,
              capture_king,
              false,
              unrelated_destination,
              QUIESCENCE_LATE_CAPTURE_LIMIT),
          "promotions and opposing-king captures bypass late-capture pruning");
    }

    {
        Position checking =
          poisoned_pawn_position();
        const Move capture = Move::normal(
          make_square(FILE_F, RANK_5),
          make_square(FILE_F, RANK_8));
        const Team moving_team =
          team_of(checking.side_to_move());
        const bool late_candidate =
          SearchDetail::is_late_quiescence_capture_candidate(
            checking,
            capture,
            false,
            unrelated_destination,
            QUIESCENCE_LATE_CAPTURE_LIMIT);
        UndoState undo;
        do_move(checking, capture, undo);

        expect(
          late_candidate
            && SearchDetail::selective_quiescence_capture_requires_search(
                 checking, moving_team),
          "a late capture that checks either opposing king remains searchable");
    }

    {
        Position terminal =
          terminal_capture_position();
        const Move capture = Move::normal(
          make_square(FILE_N, RANK_10),
          make_square(FILE_J, RANK_10));
        const Team moving_team =
          team_of(terminal.side_to_move());
        const bool late_candidate =
          SearchDetail::is_late_quiescence_capture_candidate(
            terminal,
            capture,
            false,
            unrelated_destination,
            QUIESCENCE_LATE_CAPTURE_LIMIT);
        UndoState undo;
        do_move(terminal, capture, undo);

        expect(
          late_candidate
            && !SearchDetail::selective_quiescence_capture_requires_search(
                  terminal, moving_team),
          "a nonchecking terminal capture remains eligible for late pruning");
    }

    const std::array keys = {position.key()};
    PositionHistory unpruned_history =
      make_history(keys);
    PositionHistory pruned_history =
      make_history(keys);
    PositionHistory reference_history =
      make_history(keys);
    const QuiescenceResult unpruned =
      run_quiescence_after(
        position,
        unpruned_history,
        SQ_NONE);
    const QuiescenceResult pruned =
      run_quiescence_after(
        position,
        pruned_history,
        unrelated_destination);
    const ReferenceResult reference =
      reference_quiescence(
        position,
        reference_history,
        0,
        2);

    expect(
      pruned.score == unpruned.score
        && pruned.score == reference.score
        && pruned.nodes < unpruned.nodes
        && unpruned.nodes <= reference.nodes,
      "late-capture pruning removes a redundant third capture with exact-reference score parity");
    expect(
      positions_equal(position, original)
        && history_matches(unpruned_history, keys)
        && history_matches(pruned_history, keys)
        && history_matches(reference_history, keys),
      "late-capture pruning preserves position and every history context");
}

void test_promotions_and_en_passant() {
    struct Fixture {
        Position position;
        MoveType expected_type;
    };

    std::array fixtures = {
      Fixture{
        quiet_promotion_position(),
        MoveType::PROMOTION},
      Fixture{
        capture_promotion_position(),
        MoveType::PROMOTION},
      Fixture{
        en_passant_position(false),
        MoveType::EN_PASSANT},
      Fixture{
        en_passant_position(true),
        MoveType::EN_PASSANT},
      Fixture{
        en_passant_promotion_position(),
        MoveType::EN_PASSANT},
    };

    for (Fixture& fixture : fixtures) {
        for (int rotation = 0;
             rotation < COLOR_NB;
             ++rotation) {
            const Position original = fixture.position;
            const std::array keys = {
              fixture.position.key(),
            };
            PositionHistory history = make_history(keys);
            MoveList legal_moves;
            generate_legal_moves(
              fixture.position, legal_moves);

            expect(
              contains_move_type(
                legal_moves,
                fixture.expected_type),
              "the rotated special fixture generates its tactical move type");

            const QuiescenceResult result =
              run_quiescence(
                fixture.position, history);
            const ReferenceResult reference =
              reference_quiescence(
                fixture.position,
                history,
                0,
                4);
            expect(
              result.score == reference.score,
              "quiescence returns the expected rotated special-move score");
            expect(
              positions_equal(
                fixture.position, original)
                && history_matches(
                     history, keys),
              "special-move quiescence restores position and history");

            fixture.position =
              rotate_clockwise(
                fixture.position);
        }
    }
}

void test_quiescence_table_reuse_and_bounds() {
    {
        Position position =
          hanging_queen_position();
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const Move capture = Move::normal(
          make_square(FILE_F, RANK_5),
          make_square(FILE_F, RANK_8));
        TranspositionTable table;

        const TableQuiescenceResult cold =
          run_table_quiescence(
            position, history, table);
        const TranspositionEntry* cold_entry =
          table.find(
            position.key(), history.context());
        expect(
          cold.result.best_move == capture
            && !cold.result.stand_pat
            && cold.nodes > 1
            && cold_entry
            && cold_entry->depth == 0
            && cold_entry->score
                 == cold.result.score
            && cold_entry->bound
                 == TranspositionBound::EXACT
            && cold_entry->best_move == capture
            && !cold_entry->stand_pat,
          "a completed tactical quiescence root stores its exact move and score");

        const TableQuiescenceResult warm =
          run_table_quiescence(
            position, history, table);
        expect(
          warm.result.score
              == cold.result.score
            && warm.result.best_move == capture
            && !warm.result.stand_pat
            && warm.nodes == 1
            && warm.nodes < cold.nodes,
          "a warm exact quiescence entry returns its tactical result in one node");

        TranspositionTable upper_table;
        const TableQuiescenceResult upper =
          run_table_quiescence(
            position,
            history,
            upper_table,
            0,
            0,
            cold.result.score,
            cold.result.score + Score{1});
        const TranspositionEntry* upper_entry =
          upper_table.find(
            position.key(), history.context());
        const TableQuiescenceResult upper_warm =
          run_table_quiescence(
            position,
            history,
            upper_table,
            0,
            0,
            cold.result.score,
            cold.result.score + Score{1});
        expect(
          upper.result.score == cold.result.score
            && upper_entry
            && upper_entry->bound
                 == TranspositionBound::UPPER
            && upper_warm.result.score
                 == upper.result.score
            && upper_warm.nodes == 1,
          "a quiescence fail-low stores and reuses an upper bound");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "tactical quiescence table probes preserve position and history");
    }

    {
        Position position = separated_kings();
        position.put_piece(
          R_ROOK, make_square(FILE_F, RANK_6));
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const Score stand_pat = evaluate(position);
        TranspositionTable table;

        const TableQuiescenceResult lower =
          run_table_quiescence(
            position,
            history,
            table,
            0,
            0,
            stand_pat - Score{1},
            stand_pat);
        const TranspositionEntry* lower_entry =
          table.find(
            position.key(), history.context());
        expect(
          lower.result.score == stand_pat
            && lower.result.best_move.is_none()
            && lower.result.stand_pat
            && lower_entry
            && lower_entry->depth == 0
            && lower_entry->bound
                 == TranspositionBound::LOWER
            && lower_entry->best_move.is_none()
            && lower_entry->stand_pat,
          "a stand-pat cutoff stores an explicit depth-zero lower bound");

        const TableQuiescenceResult warm =
          run_table_quiescence(
            position,
            history,
            table,
            0,
            0,
            stand_pat - Score{1},
            stand_pat);
        expect(
          warm.result.score == stand_pat
            && warm.result.best_move.is_none()
            && warm.result.stand_pat
            && warm.nodes == 1,
          "a warm stand-pat lower bound cuts off safely");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "stand-pat table probes preserve position and history");
    }
}

void test_quiescence_table_checked_mate_and_terminal_safety() {
    {
        Position position =
          forced_evasion_capture_position();
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const Move evasion = Move::normal(
          make_square(FILE_D, RANK_1),
          make_square(FILE_E, RANK_2));
        TranspositionTable table;

        const TableQuiescenceResult cold =
          run_table_quiescence(
            position, history, table);
        const TranspositionEntry* entry =
          table.find(
            position.key(), history.context());
        const TableQuiescenceResult warm =
          run_table_quiescence(
            position, history, table);
        expect(
          in_check(position)
            && cold.result.best_move == evasion
            && !cold.result.stand_pat
            && entry
            && entry->best_move == evasion
            && !entry->stand_pat
            && warm.result.score
                 == cold.result.score
            && warm.result.best_move == evasion
            && warm.nodes == 1
            && cold.nodes > warm.nodes,
          "a checked quiescence root stores and reuses a legal evasion");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "checked quiescence table reuse restores all state");
    }

    {
        Position position = king_capture_position();
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const Move capture = Move::normal(
          make_square(FILE_F, RANK_5),
          make_square(FILE_F, RANK_8));
        TranspositionTable table;
        constexpr int stored_ply = 5;
        constexpr int probing_ply = 11;

        const TableQuiescenceResult cold =
          run_table_quiescence(
            position,
            history,
            table,
            stored_ply);
        const TranspositionEntry* entry =
          table.find(
            position.key(), history.context());
        const TableQuiescenceResult warm =
          run_table_quiescence(
            position,
            history,
            table,
            probing_ply);
        expect(
          cold.result.score
              == MATE_SCORE - Score{stored_ply + 1}
            && cold.result.best_move == capture
            && entry
            && entry->score == MATE_SCORE - 1
            && warm.result.score
                 == MATE_SCORE
                      - Score{probing_ply + 1}
            && warm.result.best_move == capture
            && warm.nodes == 1,
          "quiescence table reuse reconstructs mate distance at the probing ply");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "mate-distance quiescence probes preserve all state");
    }

    {
        Position position = blocked_corner(true);
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        TranspositionTable table;
        table.store_quiescence(
          position.key(),
          history.context(),
          QUEEN_VALUE,
          TranspositionBound::EXACT,
          Move::none(),
          true);
        const std::uint32_t stored_generation =
          table.generation();

        const TableQuiescenceResult terminal =
          run_table_quiescence(
            position, history, table);
        const TranspositionEntry* retained =
          table.find(position.key());
        expect(
          terminal.result.score == -MATE_SCORE
            && terminal.result.best_move.is_none()
            && !terminal.result.stand_pat
            && terminal.nodes == 1
            && retained
            && retained->score == QUEEN_VALUE
            && retained->generation
                 == stored_generation,
          "terminal classification bypasses conflicting quiescence table data");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "terminal table bypass preserves position and history");
    }
}

void test_positive_depth_table_entries_do_not_define_quiescence() {
    Position position = separated_kings();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    MoveList legal_moves;
    MoveList tactical_moves;
    generate_legal_moves(position, legal_moves);
    generate_legal_tactical_moves(
      position, tactical_moves);
    expect(
      !legal_moves.empty()
        && tactical_moves.empty()
        && !is_tactical_move(
             position, legal_moves[0]),
      "the positive-depth hint fixture has only quiet quiescence choices");
    if (legal_moves.empty()
        || !tactical_moves.empty()) {
        return;
    }

    struct BoundCase {
        TranspositionBound bound;
        Score stored_score;
        Score alpha;
        Score beta;
    };
    const std::array cases = {
      BoundCase{
        TranspositionBound::EXACT,
        QUEEN_VALUE,
        Score{-1},
        Score{1}},
      BoundCase{
        TranspositionBound::LOWER,
        Score{1},
        Score{-1},
        DRAW_SCORE},
      BoundCase{
        TranspositionBound::UPPER,
        Score{-1},
        DRAW_SCORE,
        Score{1}},
    };

    for (const BoundCase test : cases) {
        TranspositionTable table;
        table.store(
          position.key(),
          history.context(),
          4,
          test.stored_score,
          test.bound,
          legal_moves[0]);

        const TableQuiescenceResult result =
          run_table_quiescence(
            position,
            history,
            table,
            0,
            0,
            test.alpha,
            test.beta);
        const TranspositionEntry* retained =
          table.find(
            position.key(), history.context());
        expect(
          result.result.score == evaluate(position)
            && result.result.best_move.is_none()
            && result.result.stand_pat
            && result.nodes == 1
            && retained
            && retained->depth == 4
            && retained->score
                 == test.stored_score
            && retained->bound == test.bound
            && retained->best_move
                 == legal_moves[0]
            && !retained->stand_pat,
          "a positive-depth bound with a quiet hint cannot define a quiescence result");
    }

    expect(
      positions_equal(position, original)
        && history_matches(history, keys),
      "positive-depth quiescence probes preserve position and history");
}

void test_quiescence_table_history_horizon_and_cancellation() {
    {
        Position position =
          hanging_queen_position();
        const Position original = position;
        const std::array root_keys = {
          position.key()};
        PositionHistory root_history =
          make_history(root_keys);
        TranspositionTable table;
        const TableQuiescenceResult cold =
          run_table_quiescence(
            position, root_history, table);

        const PositionKey alternate_key =
          position.key()
          ^ PositionKey{0x8181818181818181ULL};
        const std::array alternate_keys = {
          alternate_key, position.key()};
        PositionHistory alternate_history =
          make_history(alternate_keys);
        const TableQuiescenceResult alternate =
          run_table_quiescence(
            position, alternate_history, table);
        expect(
          cold.result.score
              == alternate.result.score
            && alternate.nodes == cold.nodes
            && !table.find(
                 position.key(),
                 root_history.context())
            && table.find(
                 position.key(),
                 alternate_history.context()),
          "a stale quiescence score requires the same repetition context");

        TranspositionTable horizon_table;
        PositionHistory horizon_history =
          make_history(root_keys);
        const TableQuiescenceResult horizon =
          run_table_quiescence(
            position,
            horizon_history,
            horizon_table,
            1,
            1);
        expect(
          horizon.result.score == cold.result.score
            && !horizon_table.find(
                 position.key()),
          "recursive quiescence horizons do not publish depth-zero root entries");

        TranspositionTable disabled_table;
        PositionHistory disabled_history =
          make_history(root_keys);
        const TableQuiescenceResult disabled =
          run_table_quiescence(
            position,
            disabled_history,
            disabled_table,
            0,
            0,
            -INFINITE_SCORE,
            INFINITE_SCORE,
            false);
        expect(
          disabled.result.score == cold.result.score
            && !disabled_table.find(
                 position.key()),
          "runtime transposition isolation suppresses quiescence stores");

        const Move invalid = Move::normal(
          make_square(FILE_D, RANK_1),
          make_square(FILE_D, RANK_2));
        TranspositionTable invalid_table;
        invalid_table.store_quiescence(
          position.key(),
          root_history.context(),
          QUEEN_VALUE,
          TranspositionBound::EXACT,
          invalid,
          false);
        PositionHistory invalid_history =
          make_history(root_keys);
        const TableQuiescenceResult invalid_result =
          run_table_quiescence(
            position,
            invalid_history,
            invalid_table);
        const TranspositionEntry* corrected =
          invalid_table.find(
            position.key(),
            invalid_history.context());
        expect(
          invalid_result.result.score
              == cold.result.score
            && invalid_result.nodes == cold.nodes
            && corrected
            && corrected->best_move
                 == cold.result.best_move,
          "an invalid depth-zero move prevents score reuse and is replaced by a legal result");
        expect(
          positions_equal(position, original)
            && history_matches(
                 root_history, root_keys)
            && history_matches(
                 alternate_history,
                 alternate_keys)
            && history_matches(
                 horizon_history, root_keys)
            && history_matches(
                 disabled_history, root_keys)
            && history_matches(
                 invalid_history, root_keys),
          "history and horizon table guards preserve every root state");
    }

    {
        Position position = separated_kings();
        const Position original = position;
        const PositionKey repeated =
          position.key()
          ^ PositionKey{0x8282828282828282ULL};
        const std::array repeated_keys = {
          repeated, repeated, position.key()};
        PositionHistory history =
          make_history(repeated_keys);
        TranspositionTable table;
        table.store_quiescence(
          position.key(),
          history.context(),
          QUEEN_VALUE,
          TranspositionBound::EXACT,
          Move::none(),
          true);
        const std::uint32_t generation =
          table.generation();

        const TableQuiescenceResult result =
          run_table_quiescence(
            position, history, table);
        const TranspositionEntry* retained =
          table.find(position.key());
        expect(
          result.result.score == evaluate(position)
            && result.result.repetition_sensitive
            && result.nodes == 1
            && retained
            && retained->score == QUEEN_VALUE
            && retained->generation == generation,
          "a repetition-sensitive quiescence root neither probes nor overwrites the table");
        expect(
          positions_equal(position, original)
            && history_matches(
                 history, repeated_keys),
          "repetition-gated table access preserves the complete history");
    }

    {
        Position position =
          hanging_queen_position();
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        TranspositionTable table;
        table.new_search();
        SearchDetail::SearchBudget budget{
          std::uint64_t{1}, std::nullopt};
        SearchDetail::LimitedSearchState state{
          std::move(budget), &table};
        const auto interrupted =
          SearchDetail::quiescence_with_repetition(
            position,
            history,
            0,
            0,
            -INFINITE_SCORE,
            INFINITE_SCORE,
            state);
        expect(
          !interrupted
            && interrupted.error()
                 == SearchStopReason::NODE_LIMIT
            && state.nodes == 1
            && !table.find(position.key())
            && positions_equal(position, original)
            && history_matches(history, keys),
          "an interrupted quiescence root restores state without storing a partial entry");
    }
}

void test_terminal_precedence() {
    {
        Position position = blocked_corner(true);
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);

        const QuiescenceResult result =
          run_quiescence(
            position,
            history,
            MAX_SEARCH_PLY,
            MAX_QUIESCENCE_CHECK_PLY);
        expect(
          result.score
              == -(MATE_SCORE - MAX_SEARCH_PLY)
            && result.nodes == 1,
          "terminal classification precedes the exact quiescence boundary");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "boundary checkmate preserves position and history");
    }

    {
        Position position = hanging_queen_position();
        const Position original = position;
        const PositionKey key = position.key();
        const std::array keys = {
          key,
          key ^ PositionKey{0x1111111111111111ULL},
          key,
          key ^ PositionKey{0x2222222222222222ULL},
          key,
        };
        PositionHistory history = make_history(keys);

        const QuiescenceResult result =
          run_quiescence(position, history);
        expect(
          result.score == DRAW_SCORE
            && result.nodes == 1,
          "threefold repetition precedes a hanging-piece continuation");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "repetition quiescence preserves position and history");
    }

    {
        Position position = hanging_queen_position();
        const Position original = position;
        const Move capture = Move::normal(
          make_square(FILE_F, RANK_5),
          make_square(FILE_F, RANK_8));
        Position child = position;
        UndoState unused;
        do_move(child, capture, unused);
        const PositionKey child_key = child.key();
        const std::array keys = {
          child_key,
          child_key
            ^ PositionKey{0x4444444444444444ULL},
          child_key,
          position.key(),
        };
        PositionHistory history = make_history(keys);

        const QuiescenceResult result =
          run_quiescence(position, history);
        expect(
          result.score != DRAW_SCORE
            && result.nodes >= 2,
          "an irreversible tactical child excludes unreachable repetitions");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "child-repetition quiescence preserves position and history");
    }

    {
        Position position = king_capture_position();
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);

        const QuiescenceResult result =
          run_quiescence(position, history);
        expect(
          result.score == MATE_SCORE - 1
            && result.nodes == 2,
          "quiescence recognizes an immediate opposing-king capture");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "king-capture quiescence preserves position and history");
    }

    {
        Position position = blocked_corner(true);
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);

        const QuiescenceResult result =
          run_quiescence(position, history);
        expect(
          result.score == -MATE_SCORE
            && result.nodes == 1,
          "checkmate precedes stand pat");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "checkmate quiescence preserves position and history");
    }

    {
        Position position = blocked_corner(false);
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);

        const QuiescenceResult result =
          run_quiescence(position, history);
        expect(
          result.score == DRAW_SCORE
            && result.nodes == 1,
          "stalemate precedes stand pat");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "stalemate quiescence preserves position and history");
    }

    {
        Position position = separated_kings();
        position.remove_piece(
          position.pieces(BLUE, KING).lsb());
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);

        const QuiescenceResult result =
          run_quiescence(position, history);
        expect(
          result.score == MATE_SCORE
            && result.nodes == 1,
          "a previously captured opposing king precedes evaluation");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "captured-king quiescence preserves position and history");
    }
}

void test_repetition_sensitivity() {
    Position position = separated_kings();
    const Position original = position;
    const PositionKey earlier =
      position.key()
      ^ PositionKey{0x4444444444444444ULL};
    const std::array keys = {
      earlier,
      earlier,
      position.key(),
    };
    PositionHistory history = make_history(keys);
    SearchDetail::SearchState state;
    const auto result =
      SearchDetail::quiescence_with_repetition(
        position,
        history,
        0,
        0,
        -INFINITE_SCORE,
        INFINITE_SCORE,
        state);

    expect(
      result
        && result->score == evaluate(position)
        && result->repetition_sensitive
        && state.nodes == 1,
      "quiescence reports a repeated noncurrent ancestor at a quiet node");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys),
      "repetition-sensitive quiescence preserves position and history");
}

void test_copy_reference_and_state_restoration() {
    std::array positions = {
      hanging_queen_position(),
      poisoned_pawn_position(),
      forced_evasion_capture_position(),
      teammate_recapture_position(),
      quiet_promotion_position(),
      en_passant_position(false),
      en_passant_position(true),
      en_passant_promotion_position(),
    };

    constexpr int remaining_plies = 4;
    constexpr int initial_quiescence_ply =
      MAX_QUIESCENCE_PLY - remaining_plies;

    for (Position& position : positions) {
        const Position original = position;
        const PositionKey key = position.key();
        const std::array keys = {
          key ^ PositionKey{0x3333333333333333ULL},
          key,
        };
        PositionHistory history = make_history(keys);
        const std::size_t original_capacity =
          history.capacity();

        const ReferenceResult reference =
          reference_quiescence(
            position,
            history,
            initial_quiescence_ply,
            remaining_plies);
        const QuiescenceResult actual =
          run_quiescence(
            position,
            history,
            initial_quiescence_ply,
            initial_quiescence_ply);

        expect(
          actual.score == reference.score,
          "alpha-beta quiescence matches the copy-based reference score");
        expect(
          actual.nodes <= reference.nodes,
          "alpha-beta quiescence visits no more nodes than the reference");
        expect(
          positions_equal(position, original),
          "reference comparison restores every position field");
        expect(
          history.capacity() == original_capacity
            && history_matches(history, keys),
          "reference comparison preserves the complete history");
    }
}

}  // namespace

int main() {
    test_quiet_stand_pat_and_beta_cutoff();
    test_quiet_checks_are_not_extended();
    test_hanging_piece_at_depth_zero();
    test_tactical_beta_cutoff();
    test_poisoned_capture_and_bound();
    test_quiet_check_evasion();
    test_proven_losing_capture_pruning();
    test_exchange_pruning_guards();
    test_delta_pruning_guards();
    test_late_capture_pruning_guards_and_parity();
    test_promotions_and_en_passant();
    test_quiescence_table_reuse_and_bounds();
    test_quiescence_table_checked_mate_and_terminal_safety();
    test_positive_depth_table_entries_do_not_define_quiescence();
    test_quiescence_table_history_horizon_and_cancellation();
    test_terminal_precedence();
    test_repetition_sensitivity();
    test_copy_reference_and_state_restoration();

    if (failures != 0) {
        std::cerr << failures
                  << " quiescence test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All quiescence tests passed\n";
    return EXIT_SUCCESS;
}
