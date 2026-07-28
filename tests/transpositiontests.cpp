#include "search.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
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

inline constexpr std::array<
  CastlingSide,
  CASTLING_SIDE_NB>
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

        for (const CastlingSide side :
             CASTLING_SIDES) {
            if (left.has_castling_right(color, side)
                != right.has_castling_right(
                     color, side))
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
                  != right.pieces(
                       color, piece_type))
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

template<std::size_t Size>
[[nodiscard]] bool history_matches(
  PositionHistory history,
  const std::array<PositionKey, Size>& expected) {
    static_assert(Size > 0);

    if (history.size() != expected.size())
        return false;

    for (std::size_t index = expected.size();
         index-- > 1;) {
        if (history.current_key()
              != expected[index])
            return false;

        history.pop(expected[index]);
    }

    return history.size() == 1
        && history.current_key() == expected[0];
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
child_repetition_position() noexcept {
    Position position = kings_only_position();
    position.put_piece(
      B_QUEEN, make_square(FILE_A, RANK_4));
    return position;
}

struct WindowResult {
    SearchDetail::NodeResult result;
    std::uint64_t nodes = 0;
};

[[nodiscard]] WindowResult search_window(
  Position& position,
  const PositionHistory& history,
  int depth,
  Score alpha,
  Score beta,
  TranspositionTable& table) {
    table.new_search();
    PositionHistory working{history};
    SearchDetail::SearchState state{
      SearchDetail::UnlimitedBudget{},
      &table};
    const auto searched =
      SearchDetail::alpha_beta(
        position,
        working,
        depth,
        0,
        alpha,
        beta,
        state);

    assert(searched.has_value());
    return {
      searched ? *searched
               : SearchDetail::NodeResult{},
      state.nodes,
    };
}

static_assert(
  SearchDetail::score_to_table(
    MATE_SCORE - 8, 5)
  == MATE_SCORE - 3);
static_assert(
  SearchDetail::score_from_table(
    MATE_SCORE - 3, 11)
  == MATE_SCORE - 14);
static_assert(
  SearchDetail::score_to_table(
    -MATE_SCORE + 8, 5)
  == -MATE_SCORE + 3);
static_assert(
  SearchDetail::score_from_table(
    -MATE_SCORE + 3, 11)
  == -MATE_SCORE + 14);
static_assert(
  SearchDetail::score_to_table(
    QUEEN_VALUE, MAX_SEARCH_PLY)
  == QUEEN_VALUE);
static_assert(
  SearchDetail::classify_bound(
    100, -100, 100)
  == TranspositionBound::LOWER);
static_assert(
  SearchDetail::classify_bound(
    -100, -100, 100)
  == TranspositionBound::UPPER);
static_assert(
  SearchDetail::classify_bound(
    0, -100, 100)
  == TranspositionBound::EXACT);
static_assert(
  std::is_same_v<
    decltype(search(
      std::declval<Position&>(),
      std::declval<const PositionHistory&>(),
      1,
      std::declval<TranspositionTable&>())),
    SearchResult>);

void test_table_storage_and_collisions() {
    constexpr Move move = Move::normal(
      make_square(FILE_D, RANK_1),
      make_square(FILE_D, RANK_2));
    PositionHistory history{0xA5A5A5A5A5A5A5A5ULL};
    const HistoryContext context = history.context();
    TranspositionTable table{1};

    expect(
      table.bucket_count() == 1
        && table.capacity() == 4
        && table.find(0, context) == nullptr,
      "a one-bucket table starts empty with four entry slots");

    table.store(
      0,
      context,
      MAX_SEARCH_DEPTH,
      17,
      TranspositionBound::EXACT,
      move);
    table.store(
      1,
      context,
      2,
      18,
      TranspositionBound::LOWER,
      move);
    table.store(
      2,
      context,
      3,
      19,
      TranspositionBound::EXACT,
      move);
    table.store(
      3,
      context,
      4,
      20,
      TranspositionBound::UPPER,
      move);

    const TranspositionEntry* zero =
      table.find(0, context);
    expect(
      zero
        && zero->depth == MAX_SEARCH_DEPTH
        && zero->score == 17
        && zero->best_move == move,
      "key zero and the maximum supported depth are stored exactly");
    expect(
      table.find(1, context)
        && table.find(2, context)
        && table.find(3, context),
      "all four colliding full keys remain distinguishable");

    table.store(
      4,
      context,
      5,
      21,
      TranspositionBound::LOWER,
      move);
    expect(
      table.find(0, context)
        && table.find(1, context) == nullptr
        && table.find(2, context)
        && table.find(3, context)
        && table.find(4, context),
      "a full bucket replaces its shallowest non-exact entry");

    table.clear();
    expect(
      table.find(0, context) == nullptr
        && table.find(2, context) == nullptr
        && table.find(3, context) == nullptr
        && table.find(4, context) == nullptr,
      "clear removes every occupied entry");
}

void test_identity_replacement_and_move_hints() {
    constexpr PositionKey key =
      0x123456789ABCDEF0ULL;
    constexpr Move deep_move = Move::normal(
      make_square(FILE_D, RANK_1),
      make_square(FILE_D, RANK_2));
    constexpr Move shallow_move = Move::normal(
      make_square(FILE_E, RANK_1),
      make_square(FILE_E, RANK_2));

    PositionHistory first_history{key};
    PositionHistory second_history{
      key ^ PositionKey{1}};
    second_history.push(key);
    const HistoryContext first =
      first_history.context();
    const HistoryContext second =
      second_history.context();
    TranspositionTable table{1};

    table.store(
      key,
      first,
      8,
      80,
      TranspositionBound::EXACT,
      deep_move);
    table.store(
      key,
      first,
      4,
      40,
      TranspositionBound::LOWER,
      shallow_move);
    const TranspositionEntry* retained =
      table.find(key, first);
    expect(
      retained
        && retained->depth == 8
        && retained->score == 80
        && retained->bound
             == TranspositionBound::EXACT
        && retained->best_move == deep_move,
      "a shallower bound cannot replace a deeper exact entry");

    table.store(
      key,
      first,
      8,
      81,
      TranspositionBound::LOWER,
      shallow_move);
    retained = table.find(key, first);
    expect(
      retained
        && retained->score == 80
        && retained->bound
             == TranspositionBound::EXACT,
      "an equal-depth bound cannot replace an exact entry");

    table.store(
      key,
      second,
      9,
      90,
      TranspositionBound::EXACT,
      shallow_move);
    expect(
      table.find(key, first)
        && table.find(key, second)
        && table.find(
             key ^ PositionKey{1},
             first)
             == nullptr,
      "history contexts and full position keys are checked independently");
    expect(
      table.best_move(key, first)
          == deep_move
        && table.best_move(key, second)
             == shallow_move,
      "an exact history match supplies its own stored move");

    PositionHistory third_history{
      key ^ PositionKey{2}};
    third_history.push(key);
    expect(
      table.find(key, third_history.context())
          == nullptr
        && table.best_move(
             key, third_history.context())
             == shallow_move,
      "another history can receive a move hint without receiving a score");

    const std::uint32_t preceding_generation =
      table.generation();
    table.new_search();
    const TranspositionEntry* refreshed =
      table.probe(key, first);
    expect(
      refreshed
        && table.generation()
             == preceding_generation + 1
        && refreshed->generation
             == table.generation(),
      "a successful probe refreshes the entry generation");
}

void test_generation_replacement_and_hints() {
    constexpr Move first_move = Move::normal(
      make_square(FILE_D, RANK_1),
      make_square(FILE_D, RANK_2));
    constexpr Move second_move = Move::normal(
      make_square(FILE_E, RANK_1),
      make_square(FILE_E, RANK_2));
    PositionHistory history{0xABCDEF0123456789ULL};
    const HistoryContext context = history.context();
    TranspositionTable table{1};

    for (PositionKey key = 0; key < 4; ++key) {
        table.store(
          key,
          context,
          static_cast<int>(key + 1),
          static_cast<Score>(key),
          TranspositionBound::LOWER,
          first_move);
    }

    table.new_search();
    const TranspositionEntry* refreshed =
      table.probe(0, context);
    table.store(
      4,
      context,
      5,
      4,
      TranspositionBound::LOWER,
      second_move);
    expect(
      refreshed
        && table.find(0, context)
        && table.find(1, context) == nullptr
        && table.find(4, context),
      "a refreshed entry survives replacement of a stale collision");

    table.clear();
    constexpr PositionKey shared_key =
      0x0123456789ABCDEFULL;
    PositionHistory old_history{shared_key};
    PositionHistory current_history{
      shared_key ^ PositionKey{1}};
    current_history.push(shared_key);
    PositionHistory other_history{
      shared_key ^ PositionKey{2}};
    other_history.push(shared_key);

    table.store(
      shared_key,
      old_history.context(),
      20,
      20,
      TranspositionBound::EXACT,
      first_move);
    table.new_search();
    table.store(
      shared_key,
      current_history.context(),
      1,
      1,
      TranspositionBound::EXACT,
      second_move);

    expect(
      table.best_move(
        shared_key,
        other_history.context())
          == second_move,
      "a current-generation move hint precedes a deeper stale hint");

    TranspositionTable non_power_of_two{3};
    non_power_of_two.store(
      5,
      context,
      1,
      5,
      TranspositionBound::EXACT,
      first_move);
    expect(
      non_power_of_two.bucket_count() == 3
        && non_power_of_two.find(5, context),
      "non-power-of-two bucket counts use full modulo indexing");
}

void test_bound_storage_and_cutoffs() {
    Position position =
      material_tactic_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);

    struct BoundCase {
        Score alpha;
        Score beta;
        TranspositionBound expected;
    };

    constexpr std::array cases = {
      BoundCase{
        -100, 100,
        TranspositionBound::LOWER},
      BoundCase{
        600, 700,
        TranspositionBound::UPPER},
      BoundCase{
        400, 600,
        TranspositionBound::EXACT},
    };

    for (const BoundCase test : cases) {
        TranspositionTable table;
        const WindowResult searched =
          search_window(
            position,
            history,
            1,
            test.alpha,
            test.beta,
            table);
        const TranspositionEntry* entry =
          table.find(
            position.key(),
            history.context());

        expect(
          searched.result.score == ROOK_VALUE
            && entry
            && entry->depth == 1
            && entry->score == ROOK_VALUE
            && entry->bound == test.expected,
          "search stores bounds against the caller's original window");
    }

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    expect(
      !legal_moves.empty(),
      "the bound fixture has at least one legal move");
    if (legal_moves.empty())
        return;
    const Move legal_hint = legal_moves[0];

    {
        TranspositionTable table;
        table.store(
          position.key(),
          history.context(),
          1,
          100,
          TranspositionBound::LOWER,
          legal_hint);
        const WindowResult cutoff =
          search_window(
            position,
            history,
            1,
            -100,
            100,
            table);
        expect(
          cutoff.nodes == 1
            && cutoff.result.score == 100
            && cutoff.result.best_move
                 == legal_hint,
          "a matching lower bound cuts off at beta");
    }

    {
        TranspositionTable table;
        table.store(
          position.key(),
          history.context(),
          1,
          -100,
          TranspositionBound::UPPER,
          legal_hint);
        const WindowResult cutoff =
          search_window(
            position,
            history,
            1,
            -100,
            100,
            table);
        expect(
          cutoff.nodes == 1
            && cutoff.result.score == -100,
          "a matching upper bound cuts off at alpha");
    }

    {
        TranspositionTable table;
        table.store(
          position.key(),
          history.context(),
          1,
          99,
          TranspositionBound::LOWER,
          legal_hint);
        const WindowResult searched =
          search_window(
            position,
            history,
            1,
            -100,
            100,
            table);
        expect(
          searched.nodes > 1
            && searched.result.score
                 == ROOK_VALUE,
          "a lower bound below beta does not cut off");
    }

    {
        TranspositionTable table;
        table.store(
          position.key(),
          history.context(),
          1,
          -99,
          TranspositionBound::UPPER,
          legal_hint);
        const WindowResult searched =
          search_window(
            position,
            history,
            1,
            -100,
            100,
            table);
        expect(
          searched.nodes > 1
            && searched.result.score
                 == ROOK_VALUE,
          "an upper bound above alpha does not cut off");
    }

    {
        TranspositionTable table;
        table.store(
          position.key(),
          history.context(),
          2,
          1234,
          TranspositionBound::EXACT,
          legal_hint);
        const WindowResult searched =
          search_window(
            position,
            history,
            1,
            -INFINITE_SCORE,
            INFINITE_SCORE,
            table);
        expect(
          searched.nodes > 1
            && searched.result.score
                 == ROOK_VALUE,
          "an entry from another nominal depth supplies no score");
    }

    expect(
      positions_equal(position, original)
        && history_matches(history, keys),
      "bound probes and stores preserve position and history");
}

void test_mate_score_conversion() {
    constexpr Score threshold =
      SearchDetail::TABLE_MATE_THRESHOLD;

    expect(
      SearchDetail::score_to_table(
        MATE_SCORE - 8, 5)
          == MATE_SCORE - 3
        && SearchDetail::score_from_table(
             MATE_SCORE - 3, 11)
             == MATE_SCORE - 14,
      "winning mate distance is relative to the probing ply");
    expect(
      SearchDetail::score_to_table(
        -MATE_SCORE + 8, 5)
          == -MATE_SCORE + 3
        && SearchDetail::score_from_table(
             -MATE_SCORE + 3, 11)
             == -MATE_SCORE + 14,
      "losing mate distance is relative to the probing ply");
    expect(
      SearchDetail::score_to_table(
        threshold - 1, MAX_SEARCH_PLY)
          == threshold - 1
        && SearchDetail::score_to_table(
             -threshold + 1,
             MAX_SEARCH_PLY)
             == -threshold + 1,
      "scores outside the mate band are stored unchanged");
}

void test_warm_table_and_cancellation() {
    Position position =
      material_tactic_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    const SearchResult reference =
      search(position, history, 2);
    TranspositionTable table;

    const SearchResult cold =
      search(position, history, 2, table);
    const SearchResult warm =
      search(position, history, 2, table);
    expect(
      cold.best_move == reference.best_move
        && cold.score == reference.score
        && warm.best_move == reference.best_move
        && warm.score == reference.score
        && warm.nodes == 1
        && warm.nodes < cold.nodes,
      "a warm exact root entry returns the same result in one node");

    const TranspositionEntry* completed =
      table.find(
        position.key(),
        history.context());
    expect(
      completed && completed->depth == 2,
      "the completed root is retained before cancellation");

    table.new_search();
    SearchDetail::SearchBudget budget{
      std::uint64_t{1}, std::nullopt};
    SearchDetail::LimitedSearchState state{
      std::move(budget),
      &table};
    PositionHistory working{history};
    const auto interrupted =
      SearchDetail::alpha_beta(
        position,
        working,
        3,
        0,
        -INFINITE_SCORE,
        INFINITE_SCORE,
        state);
    const TranspositionEntry* after =
      table.find(
        position.key(),
        history.context());

    expect(
      !interrupted
        && interrupted.error()
             == SearchStopReason::NODE_LIMIT
        && state.nodes == 1
        && after
        && after->depth == 2,
      "an interrupted root does not store a partial depth-three entry");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys)
        && working.current_key()
             == position.key(),
      "warm probes and cancellation restore position and history");
}

void test_budget_precedes_cached_results() {
    Position position =
      material_tactic_position();
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    expect(
      !legal_moves.empty(),
      "the budget fixture has at least one legal move");
    if (legal_moves.empty())
        return;

    TranspositionTable table;
    table.store(
      position.key(),
      history.context(),
      1,
      ROOK_VALUE,
      TranspositionBound::EXACT,
      legal_moves[0]);

    SearchDetail::SearchBudget budget{
      std::uint64_t{0}, std::nullopt};
    SearchDetail::LimitedSearchState state{
      std::move(budget),
      &table};
    PositionHistory working{history};
    const auto stopped =
      SearchDetail::alpha_beta(
        position,
        working,
        1,
        0,
        -INFINITE_SCORE,
        INFINITE_SCORE,
        state);

    expect(
      !stopped
        && stopped.error()
             == SearchStopReason::NODE_LIMIT
        && state.nodes == 0,
      "the node budget is checked before an exact table probe");
}

void test_invalid_cached_move_is_ignored() {
    Position position =
      material_tactic_position();
    const Position original = position;
    const std::array keys = {position.key()};
    PositionHistory history = make_history(keys);
    const SearchResult reference =
      search(position, history, 1);
    const Move invalid = Move::normal(
      make_square(FILE_D, RANK_1),
      make_square(FILE_D, RANK_2));

    TranspositionTable table;
    table.store(
      position.key(),
      history.context(),
      1,
      1234,
      TranspositionBound::EXACT,
      invalid);
    const SearchResult result =
      search(position, history, 1, table);

    expect(
      result.best_move == reference.best_move
        && result.score == reference.score
        && result.nodes > 1,
      "an exact entry with a move absent from the legal list is ignored");
    expect(
      positions_equal(position, original)
        && history_matches(history, keys),
      "rejecting an invalid cached move preserves position and history");
}

void test_history_dependent_scores() {
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
    const std::array seeded_keys = {
      child_key,
      filler,
      child_key,
      position.key(),
    };
    const std::array fresh_keys = {
      position.key(),
    };
    PositionHistory seeded =
      make_history(seeded_keys);
    PositionHistory fresh =
      make_history(fresh_keys);
    const SearchResult fresh_reference =
      search(position, fresh, 1);
    TranspositionTable table;

    const SearchResult repeated =
      search(position, seeded, 1, table);
    const SearchResult unrepeated =
      search(position, fresh, 1, table);
    expect(
      repeated.score == DRAW_SCORE
        && repeated.best_move
             == repeating_move
        && unrepeated.score
             == fresh_reference.score
        && unrepeated.score == -QUEEN_VALUE
        && unrepeated.has_move()
        && unrepeated.nodes > 1,
      "the same board retains history-dependent repetition scores");

    const SearchResult repeated_warm =
      search(position, seeded, 1, table);
    const SearchResult fresh_warm =
      search(position, fresh, 1, table);
    expect(
      repeated_warm.score == repeated.score
        && repeated_warm.best_move
             == repeated.best_move
        && repeated_warm.nodes == 1
        && fresh_warm.score
             == unrepeated.score
        && fresh_warm.best_move
             == unrepeated.best_move
        && fresh_warm.nodes == 1,
      "each matching history context reuses only its own exact result");

    TranspositionTable reverse_table;
    const SearchResult fresh_first =
      search(
        position, fresh, 1, reverse_table);
    const SearchResult repeated_second =
      search(
        position, seeded, 1, reverse_table);
    expect(
      fresh_first.score == -QUEEN_VALUE
        && repeated_second.score == DRAW_SCORE
        && repeated_second.best_move
             == repeating_move
        && repeated_second.nodes > 1,
      "history-dependent scores remain separate in either insertion order");

    expect(
      positions_equal(position, original)
        && history_matches(
             seeded, seeded_keys)
        && history_matches(
             fresh, fresh_keys),
      "history-sensitive table searches preserve both histories");
}

void test_terminal_classification_precedes_table() {
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
    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    expect(
      !legal_moves.empty(),
      "the terminal fixture has at least one legal move");
    if (legal_moves.empty())
        return;

    TranspositionTable table;
    table.store(
      key,
      history.context(),
      1,
      QUEEN_VALUE,
      TranspositionBound::EXACT,
      legal_moves[0]);
    const SearchResult result =
      search(position, history, 1, table);

    expect(
      result
        == SearchResult{
             Move::none(),
             DRAW_SCORE,
             1},
      "terminal repetition is classified before an exact table entry");
    expect(
      history_matches(history, keys),
      "terminal table probing preserves the complete history");
}

}  // namespace

int main() {
    test_table_storage_and_collisions();
    test_identity_replacement_and_move_hints();
    test_generation_replacement_and_hints();
    test_bound_storage_and_cutoffs();
    test_mate_score_conversion();
    test_warm_table_and_cancellation();
    test_budget_precedes_cached_results();
    test_invalid_cached_move_is_ignored();
    test_history_dependent_scores();
    test_terminal_classification_precedes_table();

    if (failures != 0) {
        std::cerr << failures
                  << " transposition test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout
      << "All transposition-table tests passed\n";
    return EXIT_SUCCESS;
}
