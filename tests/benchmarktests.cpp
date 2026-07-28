#include "benchmark.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "result.h"

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

[[nodiscard]] constexpr bool positions_equal(
  const Position& left,
  const Position& right) noexcept {
    if (left.side_to_move() != right.side_to_move()
        || left.key() != right.key()
        || left.recompute_key()
             != right.recompute_key()
        || left.occupied() != right.occupied()) {
        return false;
    }

    for (int square_index = 0;
         square_index < SQUARE_NB;
         ++square_index) {
        const Square square = Square(square_index);
        if (is_ok(square)
            && left.piece_on(square)
                 != right.piece_on(square)) {
            return false;
        }
    }

    for (const Color color : COLORS) {
        if (left.pieces(color) != right.pieces(color)
            || left.en_passant_square(color)
                 != right.en_passant_square(color)) {
            return false;
        }

        for (const CastlingSide side : CASTLING_SIDES) {
            if (left.has_castling_right(color, side)
                != right.has_castling_right(color, side)) {
                return false;
            }
        }
    }

    for (int type_index = PAWN;
         type_index <= KING;
         ++type_index) {
        const PieceType piece_type =
          PieceType(type_index);
        if (left.pieces(piece_type)
              != right.pieces(piece_type)) {
            return false;
        }

        for (const Color color : COLORS) {
            if (left.pieces(color, piece_type)
                  != right.pieces(color, piece_type)) {
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] constexpr bool entries_equal(
  const BenchmarkEntries& left,
  const BenchmarkEntries& right) noexcept {
    for (std::size_t index = 0;
         index < left.size();
         ++index) {
        const BenchmarkEntry& left_entry =
          left[index];
        const BenchmarkEntry& right_entry =
          right[index];
        if (left_entry.name != right_entry.name
            || left_entry.position_key
                 != right_entry.position_key
            || left_entry.depth != right_entry.depth
            || left_entry.best_move
                 != right_entry.best_move
            || left_entry.score != right_entry.score
            || left_entry.nodes != right_entry.nodes) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool corpus_matches(
  const BenchmarkCorpus& left,
  const BenchmarkCorpus& right) noexcept {
    for (std::size_t index = 0;
         index < left.size();
         ++index) {
        if (left[index].name != right[index].name
            || !positions_equal(
                 left[index].position,
                 right[index].position)) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool contains_move_type(
  const MoveList& moves,
  MoveType move_type) noexcept {
    for (const Move move : moves) {
        if (move.type() == move_type)
            return true;
    }

    return false;
}

void test_corpus_contract() {
    constexpr std::array<
      std::string_view,
      BENCHMARK_CASE_COUNT> EXPECTED_NAMES = {
        "start",
        "teammate-recapture",
        "forced-evasion",
        "special-moves",
        "pvs",
        "mate-swing",
    };
    const BenchmarkCorpus& corpus =
      benchmark_corpus();

    expect(
      corpus.size() == BENCHMARK_CASE_COUNT,
      "the corpus has the declared number of positions");

    for (std::size_t index = 0;
         index < corpus.size();
         ++index) {
        const BenchmarkCase& benchmark_case =
          corpus[index];
        const Position& position =
          benchmark_case.position;

        expect(
          benchmark_case.name == EXPECTED_NAMES[index],
          "the corpus retains its documented order");
        expect(
          !benchmark_case.name.empty(),
          "every corpus position has a nonempty name");
        expect(
          position.key() == position.recompute_key(),
          "every corpus position has a matching incremental key");

        for (const Color color : COLORS) {
            expect(
              position.pieces(color, KING).popcount() == 1,
              "every corpus position has one king for each color");
        }

        Position working = position;
        PositionHistory history{working.key()};
        MoveList legal_moves;
        generate_legal_moves(working, legal_moves);
        const PositionResult result =
          terminal_result(
            working, history, legal_moves);
        expect(
          result.is_valid()
            && !result.is_terminal()
            && !legal_moves.empty(),
          "every corpus root is a valid ongoing position");
        expect(
          positions_equal(working, position)
            && history.current_key() == position.key(),
          "root classification preserves the corpus position");

        for (std::size_t prior = 0;
             prior < index;
             ++prior) {
            expect(
              benchmark_case.name
                != corpus[prior].name,
              "corpus names are unique");
            expect(
              position.key()
                != corpus[prior].position.key(),
              "corpus position keys are unique");
        }
    }

    expect(
      corpus[0].position.occupied().popcount() == 64,
      "the start case contains the complete starting material");
    expect(
      corpus[1].position.pieces(RED, QUEEN).popcount() == 1
        && corpus[1].position.pieces(YELLOW, ROOK).popcount() == 1
        && corpus[1].position.pieces(BLUE_GREEN).popcount() >= 3,
      "the teammate-recapture case contains both attacking teammates");
    expect(
      in_check(corpus[2].position),
      "the forced-evasion case starts with the moving king checked");

    Position special = corpus[3].position;
    MoveList special_moves;
    generate_legal_moves(special, special_moves);
    expect(
      contains_move_type(
        special_moves, MoveType::NORMAL)
        && contains_move_type(
             special_moves, MoveType::PROMOTION)
        && contains_move_type(
             special_moves, MoveType::CASTLING)
        && contains_move_type(
             special_moves, MoveType::EN_PASSANT),
      "the special-moves case contains every board-move type");
    expect(
      corpus[4].position.side_to_move() == YELLOW
        && corpus[4].position.pieces(
             YELLOW, BISHOP).popcount() == 1,
      "the PVS case starts from the Yellow bishop fixture");
    expect(
      corpus[5].position.pieces(QUEEN).popcount() == 2
        && corpus[5].position.pieces(KNIGHT).popcount() == 2
        && corpus[5].position.pieces(ROOK).popcount() == 1,
      "the mate-swing case contains its mixed tactical material");
}

void test_shallow_run_repeatability() {
    constexpr int DEPTH = 1;
    const BenchmarkCorpus original =
      benchmark_corpus();
    const BenchmarkRunResult first =
      run_benchmark(DEPTH);
    const BenchmarkRunResult second =
      run_benchmark(DEPTH);

    expect(
      first.has_value() && second.has_value(),
      "two shallow benchmark runs complete");
    if (!first || !second)
        return;

    expect(
      entries_equal(
        first->entries, second->entries)
        && first->total_nodes
             == second->total_nodes
        && first->checksum == second->checksum,
      "shallow benchmark results are independent of elapsed time");
    expect(
      corpus_matches(
        benchmark_corpus(), original),
      "repeated benchmark runs preserve the complete corpus");

    std::uint64_t expected_total = 0;
    for (std::size_t index = 0;
         index < original.size();
         ++index) {
        const BenchmarkCase& benchmark_case =
          original[index];
        Position position = benchmark_case.position;
        const Position root = position;
        PositionHistory history{position.key()};
        const IterativeLimits limits{
          .max_depth = DEPTH,
          .node_limit = std::nullopt,
          .time_limit = std::nullopt,
        };
        const IterativeResult direct =
          iterative_search(
            position, history, limits);

        expect(
          direct.stop == IterativeStop::DEPTH_LIMIT
            && direct.last_completed
            && direct.last_completed->depth == DEPTH,
          "each direct shallow search completes its requested depth");
        if (!direct.last_completed)
            continue;

        const BenchmarkEntry& entry =
          first->entries[index];
        const SearchResult& completed =
          direct.last_completed->result;
        expect(
          entry.name == benchmark_case.name
            && entry.position_key == root.key()
            && entry.depth == DEPTH
            && entry.best_move
                 == completed.best_move
            && entry.score == completed.score
            && entry.nodes == direct.total_nodes,
          "each benchmark entry matches an independent iterative search");
        expect(
          positions_equal(position, root)
            && history.current_key() == root.key(),
          "each independent iterative search restores its root state");

        expected_total += direct.total_nodes;
    }

    expect(
      first->total_nodes == expected_total
        && first->checksum
             == benchmark_checksum(first->entries),
      "the benchmark reports the sum and checksum of its entries");
}

void test_synthetic_format() {
    BenchmarkResult result;
    result.entries = {{
      {
        "negative",
        PositionKey{1},
        2,
        Move::none(),
        Score{-123},
        std::uint64_t{456},
      },
      {
        "normal",
        PositionKey{2},
        3,
        Move::normal(
          make_square(FILE_D, RANK_1),
          make_square(FILE_D, RANK_2)),
        Score{789},
        std::uint64_t{10},
      },
      {
        "zero-a",
        PositionKey{3},
        0,
        Move::none(),
        DRAW_SCORE,
        std::uint64_t{0},
      },
      {
        "zero-b",
        PositionKey{4},
        0,
        Move::none(),
        DRAW_SCORE,
        std::uint64_t{0},
      },
      {
        "zero-c",
        PositionKey{5},
        0,
        Move::none(),
        DRAW_SCORE,
        std::uint64_t{0},
      },
      {
        "zero-d",
        PositionKey{6},
        0,
        Move::none(),
        DRAW_SCORE,
        std::uint64_t{0},
      },
    }};
    result.total_nodes = 466;
    result.checksum = 0x0123456789ABCDEFULL;
    result.elapsed =
      std::chrono::duration_cast<SearchDuration>(
        std::chrono::milliseconds{1234});

    const std::string expected =
      "negative: depth 2, best none, score -123, nodes 456\n"
      "normal: depth 3, best d1-d2, score 789, nodes 10\n"
      "zero-a: depth 0, best none, score 0, nodes 0\n"
      "zero-b: depth 0, best none, score 0, nodes 0\n"
      "zero-c: depth 0, best none, score 0, nodes 0\n"
      "zero-d: depth 0, best none, score 0, nodes 0\n"
      "Positions: 6\n"
      "Total nodes: 466\n"
      "Checksum: 0123456789abcdef\n"
      "Elapsed (ms): 1234";
    expect(
      format_benchmark(result) == expected,
      "benchmark formatting is exact for signed scores and absent moves");
}

void test_checksum_sensitivity() {
    BenchmarkEntries entries{};
    for (std::size_t index = 0;
         index < entries.size();
         ++index) {
        entries[index] = {
          benchmark_corpus()[index].name,
          benchmark_corpus()[index].position.key(),
          static_cast<int>(index + 1),
          Move::none(),
          static_cast<Score>(index),
          static_cast<std::uint64_t>(index + 10),
        };
    }

    const std::uint64_t baseline =
      benchmark_checksum(entries);
    const auto differs =
      [&](const BenchmarkEntries& changed) {
          return benchmark_checksum(changed)
            != baseline;
      };

    BenchmarkEntries changed_name = entries;
    changed_name[0].name = "renamed";
    expect(
      differs(changed_name),
      "the checksum encodes entry names");

    BenchmarkEntries changed_key = entries;
    changed_key[0].position_key ^= PositionKey{1};
    expect(
      differs(changed_key),
      "the checksum encodes position keys");

    BenchmarkEntries changed_depth = entries;
    ++changed_depth[0].depth;
    expect(
      differs(changed_depth),
      "the checksum encodes search depths");

    BenchmarkEntries changed_move = entries;
    changed_move[0].best_move =
      Move::normal(
        make_square(FILE_D, RANK_1),
        make_square(FILE_D, RANK_2));
    expect(
      differs(changed_move),
      "the checksum encodes best moves");

    BenchmarkEntries changed_score = entries;
    ++changed_score[0].score;
    expect(
      differs(changed_score),
      "the checksum encodes signed scores");

    BenchmarkEntries changed_nodes = entries;
    ++changed_nodes[0].nodes;
    expect(
      differs(changed_nodes),
      "the checksum encodes node counts");

    BenchmarkEntries changed_order = entries;
    std::swap(
      changed_order[0], changed_order[1]);
    expect(
      differs(changed_order),
      "the checksum encodes corpus order");
}

void test_invalid_depths() {
    const BenchmarkRunResult zero =
      run_benchmark(0);
    const BenchmarkRunResult excessive =
      run_benchmark(MAX_SEARCH_DEPTH + 1);

    expect(
      !zero
        && zero.error()
             == BenchmarkError::INVALID_DEPTH
        && !excessive
        && excessive.error()
             == BenchmarkError::INVALID_DEPTH,
      "benchmark runs reject depths outside the search range");
}

}  // namespace

int main() {
    test_corpus_contract();
    test_shallow_run_repeatability();
    test_synthetic_format();
    test_checksum_sensitivity();
    test_invalid_depths();

    if (failures != 0) {
        std::cerr << failures
                  << " benchmark test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All benchmark tests passed\n";
    return EXIT_SUCCESS;
}
