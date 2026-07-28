#include "benchmark.h"

#include <array>
#include <bit>
#include <cassert>
#include <charconv>
#include <chrono>
#include <concepts>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "notation.h"
#include "result.h"
#include "setup.h"

namespace Mockingbird {

namespace {

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

        for (const CastlingSide side :
             CASTLING_SIDES) {
            if (left.has_castling_right(color, side)
                != right.has_castling_right(
                     color, side)) {
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
                  != right.pieces(
                       color, piece_type)) {
                return false;
            }
        }
    }

    return true;
}

[[nodiscard]] constexpr Position
make_teammate_recapture_position() noexcept {
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
make_forced_evasion_position() noexcept {
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
make_special_moves_position() noexcept {
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

[[nodiscard]] constexpr Position
make_pvs_position() noexcept {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      B_KING, make_square(FILE_D, RANK_8));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_13));
    position.put_piece(
      G_KING, make_square(FILE_K, RANK_8));
    position.put_piece(
      Y_BISHOP, make_square(FILE_N, RANK_4));
    position.put_piece(
      B_PAWN, make_square(FILE_J, RANK_14));
    position.set_side_to_move(YELLOW);
    return position;
}

[[nodiscard]] constexpr Position
make_mate_swing_position() noexcept {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      B_KING, make_square(FILE_D, RANK_8));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_13));
    position.put_piece(
      G_KING, make_square(FILE_K, RANK_8));
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

inline constexpr BenchmarkCorpus CORPUS = {{
  {"start", make_starting_position()},
  {"teammate-recapture", make_teammate_recapture_position()},
  {"forced-evasion", make_forced_evasion_position()},
  {"special-moves", make_special_moves_position()},
  {"pvs", make_pvs_position()},
  {"mate-swing", make_mate_swing_position()},
}};

inline constexpr std::uint64_t CHECKSUM_OFFSET =
  0xCBF29CE484222325ULL;
inline constexpr std::uint64_t CHECKSUM_PRIME =
  0x00000100000001B3ULL;

constexpr void mix_byte(
  std::uint64_t& checksum,
  std::uint8_t byte) noexcept {
    checksum ^= byte;
    checksum *= CHECKSUM_PRIME;
}

constexpr void mix_uint32(
  std::uint64_t& checksum,
  std::uint32_t value) noexcept {
    for (unsigned byte_index = 0;
         byte_index < 4;
         ++byte_index) {
        mix_byte(
          checksum,
          static_cast<std::uint8_t>(value & 0xFFU));
        value >>= 8;
    }
}

constexpr void mix_uint64(
  std::uint64_t& checksum,
  std::uint64_t value) noexcept {
    for (unsigned byte_index = 0;
         byte_index < 8;
         ++byte_index) {
        mix_byte(
          checksum,
          static_cast<std::uint8_t>(value & 0xFFU));
        value >>= 8;
    }
}

constexpr void mix_text(
  std::uint64_t& checksum,
  std::string_view text) noexcept {
    mix_uint64(
      checksum,
      static_cast<std::uint64_t>(text.size()));

    for (const char character : text) {
        mix_byte(
          checksum,
          static_cast<std::uint8_t>(
            static_cast<unsigned char>(character)));
    }
}

template<std::integral Integer>
void append_decimal(
  std::string& output,
  Integer value) {
    constexpr std::size_t BUFFER_SIZE =
      static_cast<std::size_t>(
        std::numeric_limits<Integer>::digits10)
      + 3;
    std::array<char, BUFFER_SIZE> buffer{};
    const auto result =
      std::to_chars(
        buffer.data(),
        buffer.data() + buffer.size(),
        value);

    assert(result.ec == std::errc{});
    if (result.ec != std::errc{})
        return;

    output.append(
      buffer.data(),
      static_cast<std::size_t>(
        result.ptr - buffer.data()));
}

void append_checksum(
  std::string& output,
  std::uint64_t checksum) {
    constexpr std::string_view HEX_DIGITS =
      "0123456789abcdef";

    for (int shift = 60; shift >= 0; shift -= 4) {
        const std::size_t digit =
          static_cast<std::size_t>(
            (checksum >> static_cast<unsigned>(shift))
            & 0x0FULL);
        output += HEX_DIGITS[digit];
    }
}

[[nodiscard]] bool is_ongoing_position(
  const BenchmarkCase& benchmark_case) {
    Position position = benchmark_case.position;
    if (position.key() != position.recompute_key())
        return false;

    PositionHistory history{position.key()};
    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    const PositionResult result =
      terminal_result(position, history, legal_moves);
    return result.is_valid() && !result.is_terminal();
}

}  // namespace

const BenchmarkCorpus&
benchmark_corpus() noexcept {
    return CORPUS;
}

std::uint64_t benchmark_checksum(
  const BenchmarkEntries& entries) noexcept {
    std::uint64_t checksum = CHECKSUM_OFFSET;
    mix_uint64(
      checksum,
      static_cast<std::uint64_t>(entries.size()));

    for (const BenchmarkEntry& entry : entries) {
        mix_text(checksum, entry.name);
        mix_uint64(checksum, entry.position_key);
        mix_uint32(
          checksum,
          static_cast<std::uint32_t>(entry.depth));
        mix_uint32(checksum, entry.best_move.raw());
        mix_uint32(
          checksum,
          std::bit_cast<std::uint32_t>(entry.score));
        mix_uint64(checksum, entry.nodes);
    }

    return checksum;
}

BenchmarkRunResult run_benchmark(int depth) {
    if (depth < 1 || depth > MAX_SEARCH_DEPTH) {
        return std::unexpected(
          BenchmarkError::INVALID_DEPTH);
    }

    for (const BenchmarkCase& benchmark_case :
         CORPUS) {
        if (!is_ongoing_position(benchmark_case)) {
            return std::unexpected(
              BenchmarkError::INVALID_POSITION);
        }
    }

    BenchmarkResult result;
    for (std::size_t index = 0;
         index < CORPUS.size();
         ++index) {
        const BenchmarkCase& benchmark_case =
          CORPUS[index];
        Position position = benchmark_case.position;
        const Position original_position = position;
        const PositionKey original_key =
          position.key();
        PositionHistory history{original_key};
        const IterativeLimits limits{
          .max_depth = depth,
          .node_limit = std::nullopt,
          .time_limit = std::nullopt,
        };
        const IterativeResult searched =
          iterative_search(
            position, history, limits);

        if (!positions_equal(
              position, original_position)) {
            return std::unexpected(
              BenchmarkError::POSITION_NOT_RESTORED);
        }

        if (searched.stop
              != IterativeStop::DEPTH_LIMIT
            || !searched.last_completed
            || searched.last_completed->depth
                 != depth) {
            return std::unexpected(
              BenchmarkError::INCOMPLETE_SEARCH);
        }

        if (searched.total_nodes
              > std::numeric_limits<std::uint64_t>::max()
                  - result.total_nodes) {
            return std::unexpected(
              BenchmarkError::NODE_COUNT_OVERFLOW);
        }

        const SearchResult& completed =
          searched.last_completed->result;
        result.entries[index] = {
          benchmark_case.name,
          original_key,
          depth,
          completed.best_move,
          completed.score,
          searched.total_nodes,
        };
        result.total_nodes += searched.total_nodes;
        result.elapsed += searched.elapsed;
    }

    result.checksum =
      benchmark_checksum(result.entries);
    return result;
}

std::string format_benchmark(
  const BenchmarkResult& result) {
    std::string output;
    output.reserve(768);

    for (const BenchmarkEntry& entry :
         result.entries) {
        output += entry.name;
        output += ": depth ";
        append_decimal(output, entry.depth);
        output += ", best ";
        output += serialize_move(entry.best_move);
        output += ", score ";
        append_decimal(output, entry.score);
        output += ", nodes ";
        append_decimal(output, entry.nodes);
        output += '\n';
    }

    output += "Positions: ";
    append_decimal(output, result.entries.size());
    output += "\nTotal nodes: ";
    append_decimal(output, result.total_nodes);
    output += "\nChecksum: ";
    append_checksum(output, result.checksum);
    output += "\nElapsed (ms): ";
    append_decimal(
      output,
      std::chrono::duration_cast<
        std::chrono::milliseconds>(
          result.elapsed)
        .count());

    return output;
}

static_assert(sizeof(Score) == sizeof(std::uint32_t));
static_assert(
  CORPUS.size() == BENCHMARK_CASE_COUNT);
static_assert(
  CORPUS[0].position.key()
  == CORPUS[0].position.recompute_key());
static_assert(
  CORPUS[1].position.key()
  == CORPUS[1].position.recompute_key());
static_assert(
  CORPUS[2].position.key()
  == CORPUS[2].position.recompute_key());
static_assert(
  CORPUS[3].position.key()
  == CORPUS[3].position.recompute_key());
static_assert(
  CORPUS[4].position.key()
  == CORPUS[4].position.recompute_key());
static_assert(
  CORPUS[5].position.key()
  == CORPUS[5].position.recompute_key());

}  // namespace Mockingbird
