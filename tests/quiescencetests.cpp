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

struct QuiescenceResult {
    Score score = DRAW_SCORE;
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
  MAX_SEARCH_PLY
  == MAX_SEARCH_DEPTH + MAX_QUIESCENCE_PLY);
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

        const QuiescenceResult result =
          run_quiescence(position, history);

        expect(
          result.score == DRAW_SCORE
            && result.nodes == 1,
          "a quiet equal-material position stands pat in one node");
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

        const QuiescenceResult result =
          run_quiescence(
            position,
            history,
            0,
            0,
            -INFINITE_SCORE,
            ROOK_VALUE - 1);

        expect(
          result.score == ROOK_VALUE
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
      result.score == ROOK_VALUE
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

    const SearchResult result =
      search(position, history, 0);

    expect(
      evaluate(position) == -400,
      "the hanging-queen fixture has a static score of -400");
    expect(
      result
        == SearchResult{
             Move::none(), ROOK_VALUE, 2},
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

    const QuiescenceResult result =
      run_quiescence(
        position,
        history,
        0,
        0,
        -INFINITE_SCORE,
        DRAW_SCORE);
    expect(
      result.score == 400
        && result.nodes == 2,
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
      evaluate(position) == 300,
      "the poisoned-pawn fixture has the expected static score");

    const std::array expected_scores = {
      Score{300},
      Score{400},
      Score{300},
    };
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
          actual.score
              == expected_scores[
                   std::size_t(remaining)]
            && actual.score == reference.score
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
    const SearchResult depth_zero =
      search(position, history, 0);
    expect(
      depth_zero
        == SearchResult{
             Move::none(), Score{300}, 3},
      "full depth-zero quiescence declines the poisoned capture");

    const SearchResult depth_one =
      search(position, history, 1);
    expect(
      depth_one.has_move()
        && depth_one.best_move != poisoned_capture
        && depth_one.score == 300,
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
      evaluate(position) == -100
        && result.score == -1000
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
        -1100,
        -500);
    expect(
      narrow.score == -1000
        && narrow.nodes == 3,
      "a checked node ignores stand pat even when static evaluation exceeds beta");
    expect(
      positions_equal(position, original)
        && narrow_history.size() == 1
        && narrow_history.current_key()
             == position.key(),
      "the checked narrow-window search restores position and history");

    PositionHistory bounded_history{position.key()};
    const QuiescenceResult bounded =
      run_quiescence(
        position,
        bounded_history,
        MAX_QUIESCENCE_PLY,
        MAX_QUIESCENCE_PLY);
    expect(
      bounded.score == evaluate(position)
        && bounded.nodes == 1
        && bounded_history.size() == 1
        && bounded_history.current_key()
             == position.key(),
      "the hard quiescence bound stops a checked line after classification");
}

void test_four_player_team_recapture() {
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
      evaluate(position) == 800
        && result.score == 800
        && result.nodes == 4
        && result.score == reference.score
        && result.nodes == reference.nodes,
      "Red, Blue, and Yellow tactical plies retain team-negamax perspective");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys),
      "the teammate-recapture line preserves position and history");
}

void test_promotions_and_en_passant() {
    struct Fixture {
        Position position;
        Score expected_score;
        MoveType expected_type;
    };

    std::array fixtures = {
      Fixture{
        quiet_promotion_position(),
        QUEEN_VALUE,
        MoveType::PROMOTION},
      Fixture{
        capture_promotion_position(),
        QUEEN_VALUE,
        MoveType::PROMOTION},
      Fixture{
        en_passant_position(false),
        PAWN_VALUE,
        MoveType::EN_PASSANT},
      Fixture{
        en_passant_position(true),
        PAWN_VALUE,
        MoveType::EN_PASSANT},
      Fixture{
        en_passant_promotion_position(),
        QUEEN_VALUE,
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
            expect(
              result.score == fixture.expected_score,
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
            MAX_QUIESCENCE_PLY);
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
          result.score == DRAW_SCORE
            && result.nodes == 2,
          "a tactical child that repeats for the third time scores as a draw");
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
    test_four_player_team_recapture();
    test_promotions_and_en_passant();
    test_terminal_precedence();
    test_copy_reference_and_state_restoration();

    if (failures != 0) {
        std::cerr << failures
                  << " quiescence test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All quiescence tests passed\n";
    return EXIT_SUCCESS;
}
