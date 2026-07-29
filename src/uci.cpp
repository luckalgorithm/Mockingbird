#include "uci.h"

#include <atomic>
#include <array>
#include <bit>
#include <cassert>
#include <charconv>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <istream>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include "benchmark.h"
#include "fen.h"
#include "iterative.h"
#include "notation.h"
#include "perft.h"
#include "result.h"
#include "setup.h"
#include "transition.h"

namespace Mockingbird {

namespace {

inline constexpr std::string_view ENGINE_NAME =
  "Mockingbird";
inline constexpr std::string_view ENGINE_AUTHOR =
  "Mockingbird contributors";
inline constexpr int DEFAULT_SEARCH_DEPTH = 5;
inline constexpr std::size_t DEFAULT_HASH_MEGABYTES = 16;
inline constexpr std::size_t MIN_HASH_MEGABYTES = 1;
inline constexpr std::size_t MAX_HASH_MEGABYTES = 1024;
inline constexpr std::uint64_t DEFAULT_MOVE_OVERHEAD_MS = 10;
inline constexpr std::uint64_t MAX_MOVE_OVERHEAD_MS = 5000;
inline constexpr std::uint64_t CLOCK_ALLOCATION_DIVISOR = 20;

[[nodiscard]] constexpr bool is_ascii_whitespace(
  char character) noexcept {
    return character == ' '
        || character == '\t'
        || character == '\n'
        || character == '\r'
        || character == '\f'
        || character == '\v';
}

[[nodiscard]] constexpr char ascii_lower(
  char character) noexcept {
    return character >= 'A' && character <= 'Z'
        ? static_cast<char>(
            character - 'A' + 'a')
        : character;
}

[[nodiscard]] std::string lowercase(
  std::string_view text) {
    std::string result;
    result.reserve(text.size());

    for (const char character : text)
        result += ascii_lower(character);

    return result;
}

class TokenStream {
  public:
    constexpr explicit TokenStream(
      std::string_view text) noexcept
        : remaining_(text) {}

    [[nodiscard]] constexpr
    std::optional<std::string_view> next() noexcept {
        while (!remaining_.empty()
               && is_ascii_whitespace(
                    remaining_.front())) {
            remaining_.remove_prefix(1);
        }

        if (remaining_.empty())
            return std::nullopt;

        std::size_t length = 0;
        while (length < remaining_.size()
               && !is_ascii_whitespace(
                    remaining_[length])) {
            ++length;
        }

        const std::string_view token =
          remaining_.substr(0, length);
        remaining_.remove_prefix(length);
        return token;
    }

  private:
    std::string_view remaining_;
};

template<std::integral Integer>
void write_integer(
  std::ostream& output,
  Integer value) {
    std::array<
      char,
      static_cast<std::size_t>(
        std::numeric_limits<Integer>::digits10 + 3)>
      buffer{};
    const auto result = std::to_chars(
      buffer.data(),
      buffer.data() + buffer.size(),
      value);
    assert(result.ec == std::errc{});
    output.write(
      buffer.data(),
      static_cast<std::streamsize>(
        result.ptr - buffer.data()));
}

void write_text(
  std::ostream& output,
  std::string_view text) {
    output.write(
      text.data(),
      static_cast<std::streamsize>(
        text.size()));
}

void write_info_string(
  std::ostream& output,
  std::string_view message) {
    write_text(output, "info string ");
    write_text(output, message);
    write_text(output, "\n");
}

// Serializes complete protocol responses and flushes them before releasing the
// output stream to another thread.
class ProtocolOutput {
  public:
    explicit ProtocolOutput(
      std::ostream& output) noexcept
        : output_(output) {}

    template<typename Writer>
    void write(Writer&& writer) {
        const std::scoped_lock lock{mutex_};
        std::forward<Writer>(writer)(output_);
        output_.flush();
    }

  private:
    std::ostream& output_;
    std::mutex mutex_;
};

template<std::unsigned_integral Integer>
[[nodiscard]] bool parse_unsigned(
  std::string_view text,
  Integer& value) noexcept {
    if (text.empty())
        return false;

    for (const char character : text) {
        if (character < '0' || character > '9')
            return false;
    }

    const auto result = std::from_chars(
      text.data(),
      text.data() + text.size(),
      value);
    return result.ec == std::errc{}
        && result.ptr == text.data() + text.size();
}

[[nodiscard]] bool parse_depth(
  std::string_view text,
  int& depth) noexcept {
    unsigned value = 0;
    if (!parse_unsigned(text, value)
        || value == 0
        || value
             > static_cast<unsigned>(
                 MAX_SEARCH_DEPTH)) {
        return false;
    }

    depth = static_cast<int>(value);
    return true;
}

[[nodiscard]] constexpr std::size_t
hash_bucket_count(
  std::size_t megabytes) noexcept {
    constexpr std::size_t bytes_per_megabyte =
      1024U * 1024U;
    constexpr std::size_t bytes_per_bucket =
      sizeof(TranspositionEntry)
      * TranspositionTable::BUCKET_SIZE;
    static_assert(bytes_per_bucket > 0);

    const std::size_t bytes =
      megabytes * bytes_per_megabyte;
    const std::size_t complete_buckets =
      bytes / bytes_per_bucket;
    const std::size_t positive_count =
      complete_buckets == 0
        ? 1
        : complete_buckets;

    // TranspositionTable indexes buckets with a bit mask, so its bucket count
    // must remain a power of two.
    return std::bit_floor(positive_count);
}

[[nodiscard]] std::unique_ptr<TranspositionTable>
make_table(std::size_t megabytes) {
    assert(megabytes >= MIN_HASH_MEGABYTES);
    assert(megabytes <= MAX_HASH_MEGABYTES);
    return std::make_unique<TranspositionTable>(
      hash_bucket_count(megabytes));
}

struct GoRequest {
    std::optional<int> depth;
    std::optional<int> perft_depth;
    std::optional<std::uint64_t> nodes;
    std::optional<std::uint64_t> movetime_ms;
    std::array<
      std::optional<std::uint64_t>,
      COLOR_NB>
      time_ms{};
    std::array<
      std::optional<std::uint64_t>,
      COLOR_NB>
      increment_ms{};
    std::optional<std::uint64_t> legacy_time_ms;
    std::optional<std::uint64_t> legacy_increment_ms;
    std::optional<std::uint64_t> moves_to_go;
    bool infinite = false;
    bool ponder = false;
    bool unsupported = false;
};

[[nodiscard]] bool read_unsigned_value(
  TokenStream& tokens,
  std::uint64_t& value) noexcept {
    const auto token = tokens.next();
    return token
        && parse_unsigned(*token, value);
}

[[nodiscard]] bool read_depth_value(
  TokenStream& tokens,
  int& depth) noexcept {
    const auto token = tokens.next();
    return token
        && parse_depth(*token, depth);
}

[[nodiscard]] std::expected<
  GoRequest,
  std::string_view>
parse_go_request(TokenStream tokens) noexcept {
    GoRequest request;

    while (const auto token = tokens.next()) {
        const std::string name = lowercase(*token);
        std::uint64_t unsigned_value = 0;

        if (name == "depth") {
            int depth = 0;
            if (!read_depth_value(tokens, depth))
                return std::unexpected(
                  "invalid go depth");
            request.depth = depth;
        } else if (name == "nodes") {
            if (!read_unsigned_value(
                  tokens, unsigned_value)) {
                return std::unexpected(
                  "invalid go node limit");
            }
            request.nodes = unsigned_value;
        } else if (name == "movetime") {
            if (!read_unsigned_value(
                  tokens, unsigned_value)) {
                return std::unexpected(
                  "invalid go movetime");
            }
            request.movetime_ms = unsigned_value;
        } else if (name == "rtime") {
            if (!read_unsigned_value(
                  tokens, unsigned_value)) {
                return std::unexpected(
                  "invalid red time");
            }
            request.time_ms[RED] = unsigned_value;
        } else if (name == "btime") {
            if (!read_unsigned_value(
                  tokens, unsigned_value)) {
                return std::unexpected(
                  "invalid blue time");
            }
            request.time_ms[BLUE] = unsigned_value;
        } else if (name == "ytime") {
            if (!read_unsigned_value(
                  tokens, unsigned_value)) {
                return std::unexpected(
                  "invalid yellow time");
            }
            request.time_ms[YELLOW] = unsigned_value;
        } else if (name == "gtime") {
            if (!read_unsigned_value(
                  tokens, unsigned_value)) {
                return std::unexpected(
                  "invalid green time");
            }
            request.time_ms[GREEN] = unsigned_value;
        } else if (name == "rinc") {
            if (!read_unsigned_value(
                  tokens, unsigned_value)) {
                return std::unexpected(
                  "invalid red increment");
            }
            request.increment_ms[RED] =
              unsigned_value;
        } else if (name == "binc") {
            if (!read_unsigned_value(
                  tokens, unsigned_value)) {
                return std::unexpected(
                  "invalid blue increment");
            }
            request.increment_ms[BLUE] =
              unsigned_value;
        } else if (name == "yinc") {
            if (!read_unsigned_value(
                  tokens, unsigned_value)) {
                return std::unexpected(
                  "invalid yellow increment");
            }
            request.increment_ms[YELLOW] =
              unsigned_value;
        } else if (name == "ginc") {
            if (!read_unsigned_value(
                  tokens, unsigned_value)) {
                return std::unexpected(
                  "invalid green increment");
            }
            request.increment_ms[GREEN] =
              unsigned_value;
        } else if (name == "wtime") {
            if (!read_unsigned_value(
                  tokens, unsigned_value)) {
                return std::unexpected(
                  "invalid legacy time");
            }
            request.legacy_time_ms =
              unsigned_value;
        } else if (name == "winc") {
            if (!read_unsigned_value(
                  tokens, unsigned_value)) {
                return std::unexpected(
                  "invalid legacy increment");
            }
            request.legacy_increment_ms =
              unsigned_value;
        } else if (name == "movestogo") {
            if (!read_unsigned_value(
                  tokens, unsigned_value)
                || unsigned_value == 0) {
                return std::unexpected(
                  "invalid moves-to-go value");
            }
            request.moves_to_go = unsigned_value;
        } else if (name == "perft") {
            int depth = 0;
            if (!read_depth_value(tokens, depth))
                return std::unexpected(
                  "invalid perft depth");
            request.perft_depth = depth;
        } else if (name == "infinite") {
            request.infinite = true;
        } else if (name == "ponder") {
            request.ponder = true;
        } else if (name == "searchmoves"
                   || name == "mate") {
            request.unsupported = true;
            break;
        } else {
            return std::unexpected(
              "unknown go parameter");
        }
    }

    if (request.legacy_time_ms) {
        // The legacy white clock represents the Red/Yellow team, and the
        // black clock represents the Blue/Green team.
        if (!request.time_ms[RED])
            request.time_ms[RED] =
              request.legacy_time_ms;
        if (!request.time_ms[YELLOW])
            request.time_ms[YELLOW] =
              request.legacy_time_ms;
        if (request.time_ms[BLUE]
            && !request.time_ms[GREEN]) {
            request.time_ms[GREEN] =
              request.time_ms[BLUE];
        }
    }

    if (request.legacy_increment_ms) {
        if (!request.increment_ms[RED]) {
            request.increment_ms[RED] =
              request.legacy_increment_ms;
        }
        if (!request.increment_ms[YELLOW]) {
            request.increment_ms[YELLOW] =
              request.legacy_increment_ms;
        }
        if (request.increment_ms[BLUE]
            && !request.increment_ms[GREEN]) {
            request.increment_ms[GREEN] =
              request.increment_ms[BLUE];
        }
    }

    return request;
}

[[nodiscard]] constexpr std::uint64_t
subtract_overhead(
  std::uint64_t duration_ms,
  std::uint64_t overhead_ms) noexcept {
    return duration_ms > overhead_ms
        ? duration_ms - overhead_ms
        : 0;
}

[[nodiscard]] std::optional<std::uint64_t>
allocated_time(
  const GoRequest& request,
  Color side_to_move,
  std::uint64_t overhead_ms) noexcept {
    if (request.movetime_ms) {
        return subtract_overhead(
          *request.movetime_ms,
          overhead_ms);
    }

    const auto remaining =
      request.time_ms[
        static_cast<std::size_t>(
          side_to_move)];
    if (!remaining)
        return std::nullopt;

    const std::uint64_t usable =
      subtract_overhead(
        *remaining, overhead_ms);
    if (usable == 0)
        return 0;

    const std::uint64_t increment =
      request.increment_ms[
        static_cast<std::size_t>(
          side_to_move)]
        .value_or(0);
    const std::uint64_t divisor =
      request.moves_to_go.value_or(
        CLOCK_ALLOCATION_DIVISOR);
    const std::uint64_t base =
      *remaining / divisor;
    const std::uint64_t increment_share =
      increment / 2;

    // The current allocation uses a fraction of remaining time plus half of
    // the increment, capped at the usable clock.
    const std::uint64_t desired =
      base
          > std::numeric_limits<
              std::uint64_t>::max()
              - increment_share
        ? std::numeric_limits<
            std::uint64_t>::max()
        : base + increment_share;

    return desired < usable
        ? desired
        : usable;
}

[[nodiscard]] constexpr std::uint64_t
bounded_milliseconds(
  std::uint64_t milliseconds) noexcept {
    constexpr auto maximum =
      static_cast<std::uint64_t>(
        std::numeric_limits<
          std::chrono::milliseconds::rep>::max());
    return milliseconds > maximum
        ? maximum
        : milliseconds;
}

[[nodiscard]] SearchDuration search_duration(
  std::uint64_t milliseconds) noexcept {
    using Milliseconds =
      std::chrono::milliseconds;
    return std::chrono::duration_cast<SearchDuration>(
      Milliseconds{
        static_cast<Milliseconds::rep>(
          bounded_milliseconds(
            milliseconds))});
}

[[nodiscard]] constexpr Score
root_terminal_score(
  const PositionResult& result,
  const Position& position) noexcept {
    return SearchDetail::terminal_score(
      result,
      team_of(position.side_to_move()),
      0);
}

void write_score(
  std::ostream& output,
  Score score) {
    if (score >= SearchDetail::TABLE_MATE_THRESHOLD) {
        write_text(output, "mate ");
        write_integer(
          output,
          MATE_SCORE - score);
        return;
    }

    if (score <= -SearchDetail::TABLE_MATE_THRESHOLD) {
        write_text(output, "mate ");
        write_integer(
          output,
          -MATE_SCORE - score);
        return;
    }

    write_text(output, "cp ");
    write_integer(output, score);
}

[[nodiscard]] std::uint64_t elapsed_milliseconds(
  SearchDuration duration) noexcept {
    const auto milliseconds =
      std::chrono::duration_cast<
        std::chrono::milliseconds>(
          duration)
        .count();
    return milliseconds <= 0
        ? 0
        : static_cast<std::uint64_t>(
            milliseconds);
}

void write_search_result(
  std::ostream& output,
  int depth,
  Score score,
  std::uint64_t nodes,
  std::uint64_t elapsed_ms,
  Move best_move) {
    write_text(output, "info depth ");
    write_integer(output, depth);
    write_text(output, " score ");
    write_score(output, score);
    write_text(output, " nodes ");
    write_integer(output, nodes);
    write_text(output, " time ");
    write_integer(output, elapsed_ms);
    if (best_move.is_board_move()) {
        write_text(output, " pv ");
        write_text(
          output,
          serialize_move(best_move));
    }
    write_text(output, "\nbestmove ");
    if (best_move.is_board_move()) {
        write_text(
          output,
          serialize_move(best_move));
    } else {
        write_text(output, "0000");
    }
    write_text(output, "\n");
    output.flush();
}

[[nodiscard]] constexpr std::string_view
result_name(
  const PositionResult& result) noexcept {
    switch (result.type()) {
        case ResultType::ONGOING:
            return "ongoing";

        case ResultType::KING_CAPTURE:
        case ResultType::CHECKMATE:
            return result.winning_team()
                     == RED_YELLOW
                ? "ry_win"
                : "bg_win";

        case ResultType::STALEMATE:
        case ResultType::THREEFOLD_REPETITION:
            return "draw";

        case ResultType::INVALID_POSITION:
            return "invalid";
    }

    return "invalid";
}

class UciSession {
  public:
    explicit UciSession(
      ProtocolOutput& output)
        : output_(output),
          position_(make_starting_position()),
          history_(position_.key()),
          table_(
            make_table(
              DEFAULT_HASH_MEGABYTES)) {}

    ~UciSession() {
        stop_search();
    }

    UciSession(const UciSession&) = delete;
    UciSession& operator=(const UciSession&) =
      delete;

    void identify() {
        output_.write(
          [](std::ostream& output) {
              write_text(output, "id name ");
              write_text(output, ENGINE_NAME);
              write_text(output, "\nid author ");
              write_text(output, ENGINE_AUTHOR);
              write_text(
                output,
                "\noption name Threads type spin"
                " default 1 min 1 max 1"
                "\noption name Hash type spin"
                " default 16 min 1 max 1024"
                "\noption name Clear Hash type button"
                "\noption name MultiPV type spin"
                " default 1 min 1 max 1"
                "\noption name Move Overhead type spin"
                " default 10 min 0 max 5000"
                "\noption name UCI_Variant type combo"
                " default 4pc var 4pc"
                "\nuciok\n");
          });
    }

    void ready() {
        output_.write(
          [](std::ostream& output) {
              write_text(output, "readyok\n");
          });
    }

    void new_game() {
        stop_search();
        table_->clear();
    }

    void stop() {
        stop_search();
    }

    void set_option(
      TokenStream tokens) {
        stop_search();

        const auto name_keyword = tokens.next();
        if (!name_keyword
            || lowercase(*name_keyword)
                 != "name") {
            output_.write(
              [](std::ostream& output) {
                  write_info_string(
                    output,
                    "invalid setoption command");
              });
            return;
        }

        std::string name;
        std::string value;
        bool reading_value = false;
        while (const auto token = tokens.next()) {
            if (!reading_value
                && lowercase(*token) == "value") {
                reading_value = true;
                continue;
            }

            std::string& destination =
              reading_value ? value : name;
            if (!destination.empty())
                destination += ' ';
            destination.append(*token);
        }

        const std::string normalized_name =
          lowercase(name);
        const std::string normalized_value =
          lowercase(value);

        std::optional<std::string> error;
        if (normalized_name == "clear hash") {
            table_->clear();
        } else if (normalized_name == "threads") {
            std::uint64_t threads = 0;
            if (!parse_unsigned(
                  normalized_value, threads)
                || threads != 1) {
                error =
                  "Threads supports only value 1";
            }
        } else if (normalized_name == "hash") {
            std::uint64_t megabytes = 0;
            if (!parse_unsigned(
                  normalized_value, megabytes)
                || megabytes
                     < MIN_HASH_MEGABYTES
                || megabytes
                     > MAX_HASH_MEGABYTES) {
                error =
                  "Hash value is outside 1..1024";
            } else {
                try {
                    auto replacement =
                      make_table(
                        static_cast<std::size_t>(
                          megabytes));
                    table_ = std::move(replacement);
                } catch (const std::bad_alloc&) {
                    error = "Hash allocation failed";
                }
            }
        } else if (normalized_name == "multipv") {
            if (normalized_value != "1") {
                error =
                  "MultiPV supports only value 1";
            }
        } else if (
          normalized_name == "move overhead") {
            std::uint64_t overhead = 0;
            if (!parse_unsigned(
                  normalized_value, overhead)
                || overhead
                     > MAX_MOVE_OVERHEAD_MS) {
                error =
                  "Move Overhead value is outside 0..5000";
            } else {
                move_overhead_ms_ = overhead;
            }
        } else if (
          normalized_name == "uci_variant") {
            if (normalized_value != "4pc") {
                error =
                  "UCI_Variant supports only 4pc";
            }
        } else {
            error =
              "No such option: ";
            *error += name;
        }

        if (error) {
            output_.write(
              [&](std::ostream& output) {
                  write_info_string(
                    output, *error);
              });
        }
    }

    void set_position(
      TokenStream tokens) {
        stop_search();

        const auto source = tokens.next();
        if (!source) {
            invalidate_position(
              "invalid position command");
            return;
        }

        Position candidate;
        bool moves_follow = false;
        const std::string source_name =
          lowercase(*source);
        if (source_name == "startpos"
            || source_name == "start") {
            candidate = make_starting_position();

            const auto suffix = tokens.next();
            if (suffix) {
                if (lowercase(*suffix) != "moves") {
                    invalidate_position(
                      "invalid position suffix");
                    return;
                }
                moves_follow = true;
            }
        } else if (source_name == "fen") {
            std::string fen;
            while (const auto token = tokens.next()) {
                if (lowercase(*token) == "moves") {
                    moves_follow = true;
                    break;
                }

                if (!fen.empty())
                    fen += ' ';
                fen.append(*token);
            }

            if (fen.empty()) {
                invalidate_position(
                  "invalid position FEN");
                return;
            }

            const FenParseResult parsed =
              parse_fen(fen);
            if (!parsed) {
                invalidate_position(
                  "invalid position FEN");
                return;
            }
            candidate = *parsed;
        } else {
            invalidate_position(
              "invalid position source");
            return;
        }

        PositionHistory candidate_history{
          candidate.key()};

        // Position and history are committed only after every supplied move
        // has matched a legal move in sequence.
        while (moves_follow) {
            const auto move_text = tokens.next();
            if (!move_text)
                break;

            const auto move =
              parse_move(candidate, *move_text);
            if (!move
                || !move->is_board_move()) {
                std::string message =
                  "Illegal move ignored: '";
                message.append(*move_text);
                message += '\'';
                invalidate_position(
                  message);
                return;
            }

            UndoState undo;
            do_move(candidate, *move, undo);
            candidate_history.push(
              candidate.key());
        }

        position_ = std::move(candidate);
        history_ =
          std::move(candidate_history);
        position_valid_ = true;
    }

    void go(
      TokenStream tokens) {
        stop_search();

        if (!position_valid_) {
            write_search_error(
              "invalid position");
            return;
        }

        const auto request =
          parse_go_request(tokens);
        if (!request) {
            write_search_error(
              request.error());
            return;
        }

        if (request->unsupported) {
            write_search_error(
              "unsupported go parameter");
            return;
        }

        if (request->perft_depth) {
            Position working = position_;
            const std::uint64_t nodes =
              perft(
                working,
                *request->perft_depth);
            output_.write(
              [nodes](std::ostream& output) {
                  write_text(
                    output, "Nodes searched: ");
                  write_integer(output, nodes);
                  write_text(output, "\n");
              });
            return;
        }

        MoveList legal_moves;
        generate_legal_moves(
          position_, legal_moves);
        const PositionResult root_result =
          terminal_result(
            position_,
            history_,
            legal_moves);
        if (!root_result.is_valid()) {
            position_valid_ = false;
            write_search_error(
              "invalid position");
            return;
        }

        if (root_result.is_terminal()) {
            const Score score =
              root_terminal_score(
                root_result, position_);
            output_.write(
              [score](std::ostream& output) {
                  write_search_result(
                    output,
                    0,
                    score,
                    0,
                    0,
                    Move::none());
              });
            return;
        }

        assert(!legal_moves.empty());

        // A legal root move remains available when a zero node or time limit
        // prevents the first iterative-deepening iteration from completing.
        const Move fallback_move =
          legal_moves[0];
        IterativeLimits limits;
        const bool wait_for_stop =
          request->infinite
          || request->ponder;
        const auto time_ms =
          wait_for_stop
            ? std::optional<std::uint64_t>{}
            : allocated_time(
                *request,
                position_.side_to_move(),
                move_overhead_ms_);
        const bool has_finite_limit =
          request->depth
          || request->nodes
          || time_ms;

        limits.max_depth =
          request->depth.value_or(
            has_finite_limit || wait_for_stop
              ? MAX_SEARCH_DEPTH
              : DEFAULT_SEARCH_DEPTH);
        limits.node_limit =
          request->nodes;
        if (time_ms) {
            limits.time_limit =
              search_duration(*time_ms);
        }
        limits.external_stop = &stop_requested_;

        Position search_position = position_;
        PositionHistory search_history = history_;
        stop_requested_.store(
          false, std::memory_order_relaxed);

        // Search mutates private root-state copies. The command thread keeps
        // the session position available for read-only diagnostic commands.
        try {
            search_thread_ = std::jthread(
              [this,
               search_position =
                 std::move(search_position),
               search_history =
                 std::move(search_history),
               limits,
               fallback_move]() mutable {
                  [[maybe_unused]]
                  const PositionKey root_key =
                    search_position.key();
                  const IterativeResult result =
                    iterative_search(
                      search_position,
                      search_history,
                      limits,
                      *table_);
                  assert(
                    search_position.key()
                    == root_key);
                  assert(
                    search_history.current_key()
                    == root_key);

                  Move best_move = fallback_move;
                  Score score =
                    evaluate(search_position);
                  int depth = 0;
                  if (result.last_completed) {
                      depth =
                        result.last_completed
                          ->depth;
                      score =
                        result.last_completed
                          ->result.score;
                      if (result.last_completed
                            ->result.best_move
                            .is_board_move()) {
                          best_move =
                            result.last_completed
                              ->result.best_move;
                      }
                  }

                  output_.write(
                    [&](std::ostream& output) {
                        write_search_result(
                          output,
                          depth,
                          score,
                          result.total_nodes,
                          elapsed_milliseconds(
                            result.elapsed),
                          best_move);
                    });
              });
        } catch (const std::bad_alloc&) {
            write_search_error(
              "search thread allocation failed",
              fallback_move);
        } catch (const std::system_error&) {
            write_search_error(
              "search thread could not start",
              fallback_move);
        }
    }

    void write_fen() {
        if (!position_valid_) {
            output_.write(
              [](std::ostream& output) {
                  write_info_string(
                    output, "invalid position");
              });
        } else {
            const std::string fen =
              serialize_fen(position_);
            output_.write(
              [&](std::ostream& output) {
                  write_text(output, "fen ");
                  write_text(output, fen);
                  write_text(output, "\n");
              });
        }
    }

    void write_legal_moves() {
        if (!position_valid_) {
            output_.write(
              [](std::ostream& output) {
                  write_info_string(
                    output, "invalid position");
              });
            return;
        }

        MoveList moves;
        generate_legal_moves(position_, moves);
        output_.write(
          [&](std::ostream& output) {
              write_text(output, "legalmoves");
              for (const Move move : moves) {
                  write_text(output, " ");
                  write_text(
                    output,
                    serialize_move(move));
              }
              write_text(output, "\n");
          });
    }

    void write_game_result() {
        if (!position_valid_) {
            output_.write(
              [](std::ostream& output) {
                  write_text(
                    output,
                    "gameresult invalid\n");
              });
            return;
        }

        MoveList moves;
        generate_legal_moves(position_, moves);
        const PositionResult result =
          terminal_result(
            position_, history_, moves);
        output_.write(
          [&](std::ostream& output) {
              write_text(output, "gameresult ");
              write_text(
                output, result_name(result));
              write_text(output, "\n");
          });
    }

    void show() {
        if (!position_valid_) {
            output_.write(
              [](std::ostream& output) {
                  write_info_string(
                    output, "invalid position");
              });
        } else {
            const std::string position_text =
              serialize_position(position_);
            output_.write(
              [&](std::ostream& output) {
                  write_text(
                    output, position_text);
                  write_text(output, "\n");
              });
        }
    }

    void evaluate_position() {
        if (!position_valid_) {
            output_.write(
              [](std::ostream& output) {
                  write_info_string(
                    output, "invalid position");
              });
        } else {
            const Score score =
              evaluate(position_);
            output_.write(
              [score](std::ostream& output) {
                  write_text(
                    output,
                    "info string Evaluation ");
                  write_integer(output, score);
                  write_text(output, "\n");
              });
        }
    }

    void run_benchmark_command() {
        stop_search();
        const BenchmarkRunResult result =
          run_benchmark();
        output_.write(
          [&](std::ostream& output) {
              if (!result) {
                  write_info_string(
                    output,
                    "benchmark failed");
              } else {
                  write_text(
                    output,
                    format_benchmark(*result));
                  write_text(output, "\n");
              }
          });
    }

    void write_help() {
        output_.write(
          [](std::ostream& output) {
              write_text(
                output,
                "info string Commands: uci,"
                " isready, setoption,"
                " ucinewgame, position, go,"
                " stop, quit, fen,"
                " legalmoves, gameresult,"
                " eval, d, bench\n");
          });
    }

    void write_unknown(
      std::string_view line) {
        output_.write(
          [&](std::ostream& output) {
              write_text(
                output,
                "Unknown command: '");
              write_text(output, line);
              write_text(output, "'\n");
          });
    }

  private:
    void stop_search() {
        if (!search_thread_.joinable())
            return;

        // SearchBudget observes this flag at node entry. Joining also covers a
        // worker that finished before the stop command arrived.
        stop_requested_.store(
          true, std::memory_order_relaxed);
        search_thread_.join();
        stop_requested_.store(
          false, std::memory_order_relaxed);
    }

    void write_search_error(
      std::string_view message,
      Move fallback = Move::none()) {
        output_.write(
          [&](std::ostream& output) {
              write_info_string(output, message);
              write_text(output, "bestmove ");
              if (fallback.is_board_move()) {
                  write_text(
                    output,
                    serialize_move(fallback));
              } else {
                  write_text(output, "0000");
              }
              write_text(output, "\n");
          });
    }

    void invalidate_position(
      std::string_view message) {
        position_valid_ = false;
        output_.write(
          [&](std::ostream& output) {
              write_info_string(
                output, message);
          });
    }

    ProtocolOutput& output_;
    Position position_;
    PositionHistory history_;
    std::unique_ptr<TranspositionTable> table_;
    std::jthread search_thread_;
    std::atomic_bool stop_requested_{false};
    std::uint64_t move_overhead_ms_ =
      DEFAULT_MOVE_OVERHEAD_MS;
    bool position_valid_ = true;
};

}  // namespace

int run_uci(
  std::istream& input,
  std::ostream& output,
  std::ostream& errors) {
    ProtocolOutput protocol_output{output};
    UciSession session{protocol_output};
    std::string line;

    while (std::getline(input, line)) {
        TokenStream tokens{line};
        const auto command_token = tokens.next();
        if (!command_token)
            continue;

        const std::string command =
          lowercase(*command_token);
        if (command == "quit") {
            session.stop();
            return EXIT_SUCCESS;
        }
        if (command == "uci") {
            session.identify();
        } else if (command == "isready") {
            session.ready();
        } else if (command == "setoption") {
            session.set_option(tokens);
        } else if (command == "ucinewgame") {
            session.new_game();
        } else if (command == "position") {
            session.set_position(tokens);
        } else if (command == "go") {
            session.go(tokens);
        } else if (command == "stop"
                   || command == "ponderhit") {
            session.stop();
        } else if (command == "fen") {
            session.write_fen();
        } else if (command == "legalmoves") {
            session.write_legal_moves();
        } else if (command == "gameresult") {
            session.write_game_result();
        } else if (command == "eval") {
            session.evaluate_position();
        } else if (command == "d"
                   || command == "show") {
            session.show();
        } else if (command == "bench") {
            session.run_benchmark_command();
        } else if (command == "help") {
            session.write_help();
        } else if (!command.empty()
                   && command.front() != '#') {
            session.write_unknown(line);
        }
    }

    session.stop();
    if (input.bad() || !input.eof()) {
        write_text(
          errors,
          "error: input failure\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static_assert(DEFAULT_SEARCH_DEPTH >= 1);
static_assert(DEFAULT_SEARCH_DEPTH <= MAX_SEARCH_DEPTH);
static_assert(
  DEFAULT_HASH_MEGABYTES
  >= MIN_HASH_MEGABYTES);
static_assert(
  DEFAULT_HASH_MEGABYTES
  <= MAX_HASH_MEGABYTES);
static_assert(hash_bucket_count(1) > 0);
static_assert(CLOCK_ALLOCATION_DIVISOR > 0);

}  // namespace Mockingbird
