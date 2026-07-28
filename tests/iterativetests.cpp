#include "iterative.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
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
nested_quiescence_position() noexcept {
    Position position =
      forced_evasion_capture_position();
    position.put_piece(
      Y_ROOK, make_square(FILE_H, RANK_4));
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
repeated_fail_low_position() noexcept {
    Position position = kings_only_position();
    position.put_piece(
      G_ROOK, make_square(FILE_E, RANK_8));
    position.put_piece(
      R_PAWN, make_square(FILE_D, RANK_11));
    position.put_piece(
      B_QUEEN, make_square(FILE_A, RANK_11));
    position.put_piece(
      Y_KNIGHT, make_square(FILE_L, RANK_8));
    return position;
}

[[nodiscard]] constexpr Position
mate_swing_position() noexcept {
    Position position = kings_only_position();
    position.put_piece(
      G_QUEEN, make_square(FILE_K, RANK_7));
    position.put_piece(
      R_KNIGHT, make_square(FILE_F, RANK_8));
    position.put_piece(
      B_QUEEN, make_square(FILE_M, RANK_5));
    position.put_piece(
      Y_KNIGHT, make_square(FILE_G, RANK_3));
    position.put_piece(
      G_ROOK, make_square(FILE_G, RANK_7));
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

// The moving king is confined to d1, e1, d2, and e2 by board geometry.
[[nodiscard]] constexpr Position
blocked_corner(bool checked) noexcept {
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

[[nodiscard]] IterativeLimits depth_limits(
  int max_depth) {
    IterativeLimits limits;
    limits.max_depth = max_depth;
    return limits;
}

[[nodiscard]] IterativeLimits node_limits(
  int max_depth,
  std::uint64_t node_limit) {
    IterativeLimits limits =
      depth_limits(max_depth);
    limits.node_limit = node_limit;
    return limits;
}

static_assert(SearchClock::is_steady);
static_assert(
  std::is_same_v<
    decltype(iterative_search(
      std::declval<Position&>(),
      std::declval<const PositionHistory&>(),
      std::declval<const IterativeLimits&>())),
    IterativeResult>);
static_assert(
  IterativeLimits{}.max_depth == 1);
static_assert(
  !IterativeResult{}.has_completed_iteration());
static_assert(!IterativeResult{}.has_move());

void test_budget_primitives() {
    {
        SearchDetail::SearchBudget budget{
          std::uint64_t{2}, std::nullopt};
        std::uint64_t nodes = 0;

        expect(
          budget.enter_node(nodes).has_value()
            && budget.enter_node(nodes).has_value()
            && nodes == 2,
          "the budget admits exactly the permitted nodes");

        const auto stopped =
          budget.enter_node(nodes);
        expect(
          !stopped
            && stopped.error()
                 == SearchStopReason::NODE_LIMIT
            && budget.stop_reason()
                 == SearchStopReason::NODE_LIMIT
            && nodes == 2,
          "the budget rejects the first node beyond the node limit");
    }

    {
        SearchDetail::SearchBudget budget{
          std::nullopt,
          SearchClock::time_point::min()};
        std::uint64_t nodes = 0;
        const auto stopped =
          budget.enter_node(nodes);

        expect(
          !stopped
            && stopped.error()
                 == SearchStopReason::TIME_LIMIT
            && nodes == 0,
          "a past deadline stops before the first node");
    }

    {
        SearchDetail::SearchBudget budget{
          std::uint64_t{0},
          SearchClock::time_point::min()};
        std::uint64_t nodes = 0;
        const auto stopped =
          budget.enter_node(nodes);

        expect(
          !stopped
            && stopped.error()
                 == SearchStopReason::NODE_LIMIT
            && nodes == 0,
          "the node limit precedes an expired deadline");
    }

    const SearchClock::time_point start{
      SearchDuration{1}};
    expect(
      SearchDetail::make_deadline(
        start, SearchDuration::max())
        == SearchClock::time_point::max(),
      "deadline construction saturates at the clock maximum");

    SearchDetail::UnlimitedBudget unlimited;
    std::uint64_t maximum_nodes =
      std::numeric_limits<std::uint64_t>::max();
    const auto exhausted =
      unlimited.enter_node(maximum_nodes);
    expect(
      !exhausted
        && exhausted.error()
             == SearchStopReason::NODE_LIMIT
        && maximum_nodes
             == std::numeric_limits<
                  std::uint64_t>::max(),
      "the unlimited counter cannot wrap beyond uint64 maximum");
}

void test_aspiration_primitives() {
    using namespace IterationDetail;

    const AspirationWindow centered =
      make_aspiration_window(
        DRAW_SCORE,
        INITIAL_ASPIRATION_HALF_WIDTH);
    expect(
      centered.alpha == -PAWN_VALUE / 2
        && centered.beta == PAWN_VALUE / 2
        && centered.contains_exact(DRAW_SCORE)
        && !centered.contains_exact(centered.alpha)
        && !centered.contains_exact(centered.beta),
      "aspiration boundaries distinguish exact scores from bounds");

    const AspirationWindow near_win =
      make_aspiration_window(
        INFINITE_SCORE - 10,
        INITIAL_ASPIRATION_HALF_WIDTH);
    const AspirationWindow near_loss =
      make_aspiration_window(
        -INFINITE_SCORE + 10,
        INITIAL_ASPIRATION_HALF_WIDTH);
    expect(
      near_win.alpha
          == INFINITE_SCORE - 10
               - PAWN_VALUE / 2
        && near_win.beta == INFINITE_SCORE
        && near_loss.alpha == -INFINITE_SCORE
        && near_loss.beta
             == -INFINITE_SCORE + 10
                  + PAWN_VALUE / 2,
      "aspiration windows clamp to the alpha-beta score range");

    const AspirationWindow near_mate =
      make_aspiration_window(
        MATE_SCORE - 1,
        INITIAL_ASPIRATION_HALF_WIDTH);
    expect(
      near_mate.alpha
          == MATE_SCORE - 1
               - PAWN_VALUE / 2
        && near_mate.beta
             == MATE_SCORE - 1
                  + PAWN_VALUE / 2,
      "mate scores retain a valid narrow aspiration window");

    expect(
      widen_aspiration_half_width(
        DRAW_SCORE,
        Score{400},
        INITIAL_ASPIRATION_HALF_WIDTH)
          == 401
        && widen_aspiration_half_width(
             DRAW_SCORE,
             Score{-900},
             INITIAL_ASPIRATION_HALF_WIDTH)
             == 901
        && widen_aspiration_half_width(
             DRAW_SCORE,
             -INFINITE_SCORE,
             FULL_ASPIRATION_HALF_WIDTH / 2)
             == FULL_ASPIRATION_HALF_WIDTH
        && make_aspiration_window(
             MATE_SCORE,
             FULL_ASPIRATION_HALF_WIDTH)
             .is_full(),
      "aspiration widening covers fail-soft bounds and reaches full range");
}

void test_completed_iterations_match_fixed_search() {
    {
        Position position = kings_only_position();
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);

        const SearchResult depth_one =
          search(position, history, 1);
        const SearchResult depth_two =
          search(position, history, 2);
        const SearchResult depth_three =
          search(position, history, 3);
        const IterativeResult iterative =
          iterative_search(
            position,
            history,
            depth_limits(3));

        expect(
          depth_one.nodes == 9
            && depth_two.nodes == 24
            && depth_three.nodes == 95,
          "the kings-only fixed-depth baselines retain their node counts");
        expect(
          iterative.stop
              == IterativeStop::DEPTH_LIMIT
            && iterative.last_completed
            && iterative.last_completed->depth == 3
            && iterative.last_completed->result
                 == depth_three
            && iterative.total_nodes
                 == depth_one.nodes
                    + depth_two.nodes
                    + depth_three.nodes
            && iterative.elapsed
                 >= SearchDuration::zero(),
          "unlimited iterative deepening reports the deepest fixed-depth result");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "completed iterative deepening preserves position and history");
    }

    Position position =
      material_tactic_position();
    for (int rotation = 0;
         rotation < COLOR_NB;
         ++rotation) {
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const SearchResult fixed =
          search(position, history, 2);
        const SearchResult shallow =
          search(position, history, 1);
        const IterativeResult iterative =
          iterative_search(
            position,
            history,
            depth_limits(2));

        expect(
          iterative.last_completed
            && iterative.last_completed->depth == 2
            && iterative.last_completed->result
                 == fixed
            && iterative.total_nodes
                 == shallow.nodes + fixed.nodes,
          "rotated iterative results match fixed-depth search");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "rotated iterative search preserves position and history");

        position = rotate_clockwise(position);
    }
}

void test_exact_node_limits() {
    Position position = kings_only_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    const SearchResult depth_one =
      search(position, history, 1);
    const SearchResult depth_two =
      search(position, history, 2);

    struct Boundary {
        std::uint64_t limit;
        int completed_depth;
        IterativeStop stop;
    };

    const std::array boundaries = {
      Boundary{0, 0, IterativeStop::NODE_LIMIT},
      Boundary{8, 0, IterativeStop::NODE_LIMIT},
      Boundary{9, 1, IterativeStop::NODE_LIMIT},
      Boundary{32, 1, IterativeStop::NODE_LIMIT},
      Boundary{33, 2, IterativeStop::DEPTH_LIMIT},
    };

    for (const Boundary boundary : boundaries) {
        const IterativeResult result =
          iterative_search(
            position,
            history,
            node_limits(2, boundary.limit));

        expect(
          result.total_nodes == boundary.limit
            && result.total_nodes
                 <= boundary.limit
            && result.stop == boundary.stop,
          "iterative search observes each exact cumulative node boundary");

        if (boundary.completed_depth == 0) {
            expect(
              !result.last_completed,
              "an interrupted first iteration publishes no partial result");
        } else {
            const SearchResult expected =
              boundary.completed_depth == 1
                ? depth_one
                : depth_two;
            expect(
              result.last_completed
                && result.last_completed->depth
                     == boundary.completed_depth
                && result.last_completed->result
                     == expected,
              "a node-limited run retains only its deepest completed iteration");
        }

        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "every exact node boundary preserves position and history");
    }

    const IterativeResult exact_final =
      iterative_search(
        position,
        history,
        node_limits(1, depth_one.nodes));
    expect(
      exact_final.stop
          == IterativeStop::DEPTH_LIMIT
        && exact_final.total_nodes
             == depth_one.nodes
        && exact_final.last_completed
        && exact_final.last_completed->result
             == depth_one,
      "the requested depth takes precedence when its final node uses the limit");
}

void test_nested_cancellation() {
    {
        Position position =
          forced_evasion_capture_position();
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const SearchResult completed =
          search(position, history, 1);

        const IterativeResult result =
          iterative_search(
            position,
            history,
            node_limits(2, 4));

        expect(
          completed.nodes == 3
            && result.stop
                 == IterativeStop::NODE_LIMIT
            && result.total_nodes == 4
            && result.last_completed
            && result.last_completed->depth == 1
            && result.last_completed->result
                 == completed,
          "alpha-beta cancellation retains the completed depth-one result");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "alpha-beta cancellation restores position and history");
    }

    {
        Position position =
          nested_quiescence_position();
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const SearchResult completed =
          search(position, history, 1);

        const IterativeResult result =
          iterative_search(
            position,
            history,
            node_limits(2, 9));

        expect(
          completed.nodes == 6
            && result.stop
                 == IterativeStop::NODE_LIMIT
            && result.total_nodes == 9
            && result.last_completed
            && result.last_completed->depth == 1
            && result.last_completed->result
                 == completed,
          "nested quiescence cancellation cannot publish a partial score");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "nested quiescence cancellation restores position and history");
    }
}

void test_partial_iteration_never_leaks() {
    Position position =
      teammate_recapture_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    const IterativeResult completed =
      iterative_search(
        position,
        history,
        depth_limits(2));
    expect(
      completed.stop
          == IterativeStop::DEPTH_LIMIT
        && completed.last_completed
        && completed.last_completed->depth == 2,
      "the partial-iteration fixture completes depth two");

    const std::uint64_t partial_limit =
      completed.total_nodes + 1;

    const IterativeResult result =
      iterative_search(
        position,
        history,
        node_limits(3, partial_limit));
    expect(
      result.stop == IterativeStop::NODE_LIMIT
        && result.total_nodes == partial_limit
        && result.last_completed
             == completed.last_completed,
      "an interrupted depth-three root cannot replace depth two");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys),
      "partial-root cancellation restores position and history");
}

void test_aspiration_researches() {
    {
        Position position =
          teammate_recapture_position();
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const SearchResult depth_two =
          search(position, history, 2);
        const SearchResult depth_three =
          search(position, history, 3);
        const IterativeResult result =
          iterative_search(
            position,
            history,
            depth_limits(3));
        const Score aspiration_width =
          static_cast<Score>(
            IterationDetail::INITIAL_ASPIRATION_HALF_WIDTH);

        expect(
          depth_three.score
              >= depth_two.score
                   + aspiration_width
            && result.stop
                 == IterativeStop::DEPTH_LIMIT
            && result.last_completed
            && result.last_completed->depth == 3
            && result.last_completed->attempts > 1
            && result.last_completed->result.score
                 == depth_three.score
            && result.last_completed->result.best_move
                 == depth_three.best_move,
          "a fail-high is re-searched to the fixed-depth result");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "fail-high re-searches preserve position and history");
    }

    {
        Position position =
          forced_evasion_capture_position();
        position.set_side_to_move(YELLOW);
        position.put_piece(
          Y_PAWN, make_square(FILE_E, RANK_2));
        position.put_piece(
          Y_PAWN, make_square(FILE_F, RANK_2));
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const SearchResult depth_one =
          search(position, history, 1);
        const SearchResult depth_two =
          search(position, history, 2);
        const IterativeResult shallow =
          iterative_search(
            position,
            history,
            depth_limits(1));
        const IterativeResult complete =
          iterative_search(
            position,
            history,
            depth_limits(2));
        const Score aspiration_width =
          static_cast<Score>(
            IterationDetail::INITIAL_ASPIRATION_HALF_WIDTH);

        expect(
          depth_two.score
              <= depth_one.score
                   - aspiration_width
            && complete.stop
                 == IterativeStop::DEPTH_LIMIT
            && complete.last_completed
            && complete.last_completed->depth == 2
            && complete.last_completed->attempts > 1
            && complete.last_completed->result.score
                 == depth_two.score
            && complete.last_completed->result.best_move
                 == depth_two.best_move,
          "a fail-low is re-searched to the fixed-depth result");

        const std::uint64_t interrupted_limit =
          complete.total_nodes - 1;
        const IterativeResult interrupted =
          iterative_search(
            position,
            history,
            node_limits(
              2, interrupted_limit));
        expect(
          interrupted.stop
              == IterativeStop::NODE_LIMIT
            && interrupted.total_nodes
                 == interrupted_limit
            && interrupted.last_completed
                 == shallow.last_completed,
          "an interrupted aspiration re-search retains the prior depth");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "fail-low and interrupted re-searches preserve position and history");
    }

    {
        Position position =
          repeated_fail_low_position();
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const SearchResult depth_one =
          search(position, history, 1);
        const SearchResult depth_two =
          search(position, history, 2);
        const IterativeResult result =
          iterative_search(
            position,
            history,
            depth_limits(2));
        const Score aspiration_width =
          static_cast<Score>(
            IterationDetail::INITIAL_ASPIRATION_HALF_WIDTH);

        expect(
          depth_two.score
              <= depth_one.score
                   - aspiration_width
            && result.stop
                 == IterativeStop::DEPTH_LIMIT
            && result.last_completed
            && result.last_completed->depth == 2
            && result.last_completed->attempts >= 3
            && result.last_completed->result.score
                 == depth_two.score
            && result.last_completed->result.best_move
                 == depth_two.best_move,
          "successive fail-low bounds are widened until the score is exact");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "successive aspiration re-searches preserve position and history");
    }

    {
        Position position = mate_swing_position();
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const SearchResult depth_one =
          search(position, history, 1);
        const SearchResult depth_two =
          search(position, history, 2);
        const IterativeResult result =
          iterative_search(
            position,
            history,
            depth_limits(2));

        expect(
          depth_one.score == Score{-1660}
            && depth_two.score
                 == -MATE_SCORE + Score{4}
            && result.stop
                 == IterativeStop::DEPTH_LIMIT
            && result.last_completed
            && result.last_completed->depth == 2
            && result.last_completed->attempts >= 3
            && result.last_completed->result.score
                 == depth_two.score
            && result.last_completed->result.has_move(),
          "mate-band fail-low bounds are re-searched to an exact score");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "mate-band aspiration re-searches preserve position and history");
    }
}

void test_previous_result_and_table_ordering() {
    Position position =
      teammate_recapture_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);

    const SearchResult fixed_one =
      search(position, history, 1);
    const SearchResult fixed_two =
      search(position, history, 2);
    const SearchResult fixed_three =
      search(position, history, 3);
    const IterativeResult depth_two =
      iterative_search(
        position,
        history,
        depth_limits(2));
    const IterativeResult depth_three =
      iterative_search(
        position,
        history,
        depth_limits(3));

    expect(
      fixed_one.nodes == 52
        && fixed_two.nodes == 265
        && depth_two.total_nodes == 155
        && depth_two.last_completed
        && depth_two.last_completed->attempts == 1
        && depth_two.last_completed->result.nodes
             == 103
        && depth_two.last_completed->result.score
             == fixed_two.score
        && depth_two.last_completed->result.best_move
             == fixed_two.best_move,
      "history-and-killer-assisted search has the expected "
      "depth-two result and node shape");
    expect(
      fixed_three.nodes == 1164
        && depth_three.total_nodes == 1172
        && depth_three.last_completed
        && depth_three.last_completed->result.nodes
             == 1017
        && depth_three.last_completed->result.score
             == fixed_three.score
        && depth_three.last_completed->result.best_move
             == fixed_three.best_move,
      "history-and-killer-assisted search has the expected "
      "depth-three result and node shape");

    const std::uint64_t complete_limit =
      depth_two.total_nodes;
    const IterativeResult interrupted =
      iterative_search(
        position,
        history,
        node_limits(2, complete_limit - 1));
    const IterativeResult exact =
      iterative_search(
        position,
        history,
        node_limits(2, complete_limit));
    expect(
      interrupted.stop
          == IterativeStop::NODE_LIMIT
        && interrupted.total_nodes
             == complete_limit - 1
        && interrupted.last_completed
        && interrupted.last_completed->depth == 1
        && exact.stop
             == IterativeStop::DEPTH_LIMIT
        && exact.total_nodes
             == complete_limit
        && exact.last_completed
        && exact.last_completed->depth == 2,
      "node limits remain exact across reordered cached iterations");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys),
      "cached iterative searches preserve position and history");
}

void test_every_special_state_interruption() {
    Position position = special_move_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    const std::size_t original_capacity =
      history.capacity();

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
      "the interruption fixture contains every board move type");

    const SearchResult complete =
      search(position, history, 1);

    for (std::uint64_t limit = 0;
         limit < complete.nodes;
         ++limit) {
        const IterativeResult result =
          iterative_search(
            position,
            history,
            node_limits(1, limit));

        expect(
          result.stop
              == IterativeStop::NODE_LIMIT
            && result.total_nodes == limit
            && !result.last_completed,
          "every incomplete depth-one node prefix is discarded");
        expect(
          positions_equal(position, original),
          "every special-state interruption restores all position fields");
        expect(
          history.capacity() == original_capacity
            && history_matches(history, keys),
          "every special-state interruption preserves the complete history");
    }

    const IterativeResult exact =
      iterative_search(
        position,
        history,
        node_limits(1, complete.nodes));
    expect(
      exact.stop == IterativeStop::DEPTH_LIMIT
        && exact.last_completed
        && exact.last_completed->result == complete,
      "the complete special-state iteration is retained at its exact limit");
}

void test_time_limits() {
    Position position = kings_only_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);

    IterativeLimits zero_time =
      depth_limits(2);
    zero_time.time_limit =
      SearchDuration::zero();
    const IterativeResult stopped =
      iterative_search(
        position, history, zero_time);
    expect(
      stopped.stop == IterativeStop::TIME_LIMIT
        && stopped.total_nodes == 0
        && !stopped.last_completed,
      "a zero time limit stops before the first node");

    IterativeLimits both_zero = zero_time;
    both_zero.node_limit = 0;
    const IterativeResult node_first =
      iterative_search(
        position, history, both_zero);
    expect(
      node_first.stop == IterativeStop::NODE_LIMIT
        && node_first.total_nodes == 0
        && !node_first.last_completed,
      "a zero node limit precedes a zero time limit");

    IterativeLimits generous =
      depth_limits(2);
    generous.time_limit =
      std::chrono::duration_cast<SearchDuration>(
        std::chrono::hours{1});
    const SearchResult depth_one =
      search(position, history, 1);
    const SearchResult depth_two =
      search(position, history, 2);
    const IterativeResult completed =
      iterative_search(
        position, history, generous);
    expect(
      completed.stop
          == IterativeStop::DEPTH_LIMIT
        && completed.last_completed
        && completed.last_completed->result
             == depth_two
        && completed.total_nodes
             == depth_one.nodes
                + depth_two.nodes,
      "a generous time limit completes the requested depth");

    IterativeLimits saturated =
      depth_limits(1);
    saturated.time_limit =
      SearchDuration::max();
    const IterativeResult maximum =
      iterative_search(
        position, history, saturated);
    expect(
      maximum.stop == IterativeStop::DEPTH_LIMIT
        && maximum.last_completed
        && maximum.last_completed->result
             == depth_one,
      "a saturated deadline supports a completed iteration");

    expect(
      positions_equal(position, original)
        && history_matches(history, keys),
      "time-limited searches preserve position and history");
}

void test_terminal_positions_stop_after_one_iteration() {
    {
        Position position = blocked_corner(true);
        const Position original = position;
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);

        const IterativeResult result =
          iterative_search(
            position,
            history,
            node_limits(
              MAX_SEARCH_DEPTH, 1));

        expect(
          result.stop
              == IterativeStop::TERMINAL_POSITION
            && result.total_nodes == 1
            && result.last_completed
            && result.last_completed->depth == 1
            && result.last_completed->attempts == 1
            && result.last_completed->result
                 == SearchResult{
                      Move::none(),
                      -MATE_SCORE,
                      1}
            && !result.has_move(),
          "checkmate completes one iteration and stops");
        expect(
          positions_equal(position, original)
            && history_matches(history, keys),
          "terminal iterative search preserves position and history");
    }

    {
        Position position = blocked_corner(false);
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const IterativeResult result =
          iterative_search(
            position,
            history,
            node_limits(4, 1));

        expect(
          result.stop
              == IterativeStop::TERMINAL_POSITION
            && result.total_nodes == 1
            && result.last_completed
            && result.last_completed->result
                 == SearchResult{
                      Move::none(),
                      DRAW_SCORE,
                      1},
          "stalemate completes one iteration and stops");
    }

    {
        Position position = kings_only_position();
        const PositionKey key = position.key();
        const std::array keys = {
          key,
          key ^ PositionKey{0x1111111111111111ULL},
          key,
          key ^ PositionKey{0x2222222222222222ULL},
          key,
        };
        PositionHistory history = make_history(keys);
        const IterativeResult result =
          iterative_search(
            position,
            history,
            node_limits(4, 1));

        expect(
          result.stop
              == IterativeStop::TERMINAL_POSITION
            && result.total_nodes == 1
            && result.last_completed
            && result.last_completed->result.score
                 == DRAW_SCORE,
          "root repetition completes one iteration and stops");
        expect(
          history_matches(history, keys),
          "terminal repetition preserves the complete history");
    }

    {
        Position position = kings_only_position();
        position.remove_piece(
          position.pieces(BLUE, KING).lsb());
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const IterativeResult result =
          iterative_search(
            position,
            history,
            node_limits(4, 1));

        expect(
          result.stop
              == IterativeStop::TERMINAL_POSITION
            && result.total_nodes == 1
            && result.last_completed
            && result.last_completed->result
                 == SearchResult{
                      Move::none(),
                      MATE_SCORE,
                      1},
          "a previously captured opposing king stops after one iteration");
    }

    {
        Position position = blocked_corner(true);
        const std::array keys = {position.key()};
        PositionHistory history = make_history(keys);
        const IterativeResult result =
          iterative_search(
            position,
            history,
            node_limits(4, 0));

        expect(
          result.stop == IterativeStop::NODE_LIMIT
            && result.total_nodes == 0
            && !result.last_completed,
          "a zero budget does not classify a terminal root");
    }
}

void test_invalid_inputs_in_release() {
#ifdef NDEBUG
    Position position = kings_only_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);

    std::array invalid_limits = {
      depth_limits(0),
      depth_limits(-1),
      depth_limits(MAX_SEARCH_DEPTH + 1),
    };
    IterativeLimits negative_time =
      depth_limits(1);
    negative_time.time_limit =
      SearchDuration{-1};

    for (const IterativeLimits& limits :
         invalid_limits) {
        const IterativeResult result =
          iterative_search(
            position, history, limits);
        expect(
          result.stop
              == IterativeStop::INVALID_LIMITS
            && result.total_nodes == 0
            && !result.last_completed,
          "release iterative search rejects an invalid depth limit");
    }

    const IterativeResult negative =
      iterative_search(
        position, history, negative_time);
    expect(
      negative.stop
          == IterativeStop::INVALID_LIMITS
        && negative.total_nodes == 0
        && !negative.last_completed,
      "release iterative search rejects a negative time limit");

    PositionHistory stale_history{
      position.key() ^ PositionKey{1}};
    const IterativeResult stale =
      iterative_search(
        position,
        stale_history,
        depth_limits(1));
    expect(
      stale.stop == IterativeStop::INVALID_INPUT
        && stale.total_nodes == 0
        && !stale.last_completed,
      "release iterative search rejects a stale history");

    Position invalid = position;
    invalid.put_piece(
      R_KING, make_square(FILE_D, RANK_4));
    PositionHistory invalid_history{
      invalid.key()};
    const IterativeResult bad_layout =
      iterative_search(
        invalid,
        invalid_history,
        depth_limits(1));
    expect(
      bad_layout.stop
          == IterativeStop::INVALID_INPUT
        && bad_layout.total_nodes == 0
        && !bad_layout.last_completed,
      "release iterative search rejects an invalid king layout");

    expect(
      positions_equal(position, original)
        && history_matches(history, keys),
      "rejected iterative searches preserve valid input state");
#endif
}

}  // namespace

int main() {
    test_budget_primitives();
    test_aspiration_primitives();
    test_completed_iterations_match_fixed_search();
    test_exact_node_limits();
    test_nested_cancellation();
    test_partial_iteration_never_leaks();
    test_aspiration_researches();
    test_previous_result_and_table_ordering();
    test_every_special_state_interruption();
    test_time_limits();
    test_terminal_positions_stop_after_one_iteration();
    test_invalid_inputs_in_release();

    if (failures != 0) {
        std::cerr << failures
                  << " iterative test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout
      << "All iterative-deepening tests passed\n";
    return EXIT_SUCCESS;
}
