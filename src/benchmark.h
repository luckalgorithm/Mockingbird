#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "iterative.h"

namespace Mockingbird {

inline constexpr int BENCHMARK_DEPTH = 5;
inline constexpr std::size_t BENCHMARK_CASE_COUNT = 6;

struct BenchmarkCase {
    std::string_view name;
    Position position;
};

using BenchmarkCorpus =
  std::array<BenchmarkCase, BENCHMARK_CASE_COUNT>;

struct BenchmarkEntry {
    std::string_view name;
    PositionKey position_key = 0;
    int depth = 0;
    Move best_move = Move::none();
    Score score = DRAW_SCORE;

    // nodes includes every iterative-deepening iteration for this position.
    std::uint64_t nodes = 0;
};

using BenchmarkEntries =
  std::array<BenchmarkEntry, BENCHMARK_CASE_COUNT>;

struct BenchmarkResult {
    BenchmarkEntries entries{};
    std::uint64_t total_nodes = 0;
    std::uint64_t checksum = 0;

    // elapsed is the sum of the elapsed search durations for every position.
    SearchDuration elapsed{};
};

enum class BenchmarkError : std::uint8_t {
    INVALID_DEPTH,
    INVALID_POSITION,
    INCOMPLETE_SEARCH,
    POSITION_NOT_RESTORED,
    NODE_COUNT_OVERFLOW,
};

using BenchmarkRunResult =
  std::expected<BenchmarkResult, BenchmarkError>;

// The returned reference remains valid for the lifetime of the program.
[[nodiscard]] const BenchmarkCorpus&
benchmark_corpus() noexcept;

// Searches every corpus position independently to the requested depth.
[[nodiscard]] BenchmarkRunResult run_benchmark(
  int depth = BENCHMARK_DEPTH);

// The checksum encodes the ordered entry names, keys, depths, moves, scores,
// and node counts. Elapsed time is not encoded.
[[nodiscard]] std::uint64_t benchmark_checksum(
  const BenchmarkEntries& entries) noexcept;

// The elapsed-time line is informational. The function does not append a
// trailing newline.
[[nodiscard]] std::string format_benchmark(
  const BenchmarkResult& result);

static_assert(BENCHMARK_DEPTH >= 1);
static_assert(BENCHMARK_DEPTH <= MAX_SEARCH_DEPTH);
static_assert(BENCHMARK_CASE_COUNT > 0);

}  // namespace Mockingbird
