#include "uci.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include "fen.h"
#include "legal.h"
#include "notation.h"
#include "perft.h"
#include "setup.h"
#include "transition.h"

namespace {

int failures = 0;

using namespace Mockingbird;

// Records a failed condition and allows the remaining tests to run.
void expect(bool condition, std::string_view message) {
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

struct UciResult {
    int status = EXIT_FAILURE;
    std::string output;
    std::string errors;
};

// Each segment after the first remains unavailable until the test releases
// it. This lets protocol tests inspect output at exact command boundaries.
class StagedInputBuffer final : public std::streambuf {
  public:
    explicit StagedInputBuffer(
      std::vector<std::string> segments)
        : segments_(std::move(segments)) {
        assert(!segments_.empty());
        assert(!segments_.front().empty());
        select_segment(0);
    }

    [[nodiscard]] bool wait_until_blocked_for(
      std::size_t segment,
      std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex_};
        return condition_.wait_for(
          lock,
          timeout,
          [&] {
              return waiting_for_segment_
                  == segment;
          });
    }

    void release(std::size_t segment) {
        {
            const std::scoped_lock lock{mutex_};
            if (released_through_ < segment)
                released_through_ = segment;
        }
        condition_.notify_all();
    }

  private:
    void select_segment(std::size_t index) noexcept {
        current_segment_ = index;
        std::string& segment = segments_[index];
        setg(
          segment.data(),
          segment.data(),
          segment.data() + segment.size());
    }

    [[nodiscard]] int_type underflow() override {
        std::unique_lock lock{mutex_};
        const std::size_t next =
          current_segment_ + 1;
        if (next >= segments_.size())
            return traits_type::eof();

        waiting_for_segment_ = next;
        condition_.notify_all();
        condition_.wait(
          lock,
          [&] {
              return released_through_ >= next;
          });
        select_segment(next);
        waiting_for_segment_ =
          std::numeric_limits<std::size_t>::max();
        return traits_type::to_int_type(*gptr());
    }

    std::vector<std::string> segments_;
    std::size_t current_segment_ = 0;
    std::size_t released_through_ = 0;
    std::size_t waiting_for_segment_ =
      std::numeric_limits<std::size_t>::max();
    std::mutex mutex_;
    std::condition_variable condition_;
};

// ProtocolOutput can write from the command and search threads. This buffer
// provides race-free snapshots while both threads remain active.
class SynchronizedOutputBuffer final : public std::streambuf {
  public:
    [[nodiscard]] std::string snapshot() const {
        const std::scoped_lock lock{mutex_};
        return text_;
    }

    [[nodiscard]] bool wait_for_text(
      std::string_view expected,
      std::chrono::milliseconds timeout) {
        std::unique_lock lock{mutex_};
        return condition_.wait_for(
          lock,
          timeout,
          [&] {
              return text_.find(expected)
                  != std::string::npos;
          });
    }

  private:
    [[nodiscard]] int_type overflow(
      int_type character) override {
        if (traits_type::eq_int_type(
              character, traits_type::eof())) {
            return traits_type::not_eof(character);
        }

        {
            const std::scoped_lock lock{mutex_};
            text_ += traits_type::to_char_type(character);
        }
        condition_.notify_all();
        return character;
    }

    std::streamsize xsputn(
      const char* text,
      std::streamsize size) override {
        {
            const std::scoped_lock lock{mutex_};
            text_.append(
              text,
              static_cast<std::size_t>(size));
        }
        condition_.notify_all();
        return size;
    }

    int sync() override {
        return 0;
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::string text_;
};

struct StagedUciResult {
    int status = EXIT_FAILURE;
    bool command_boundary_reached = false;
    bool response_before_quit = false;
    std::string before_action;
    std::string output;
    std::string errors;
};

[[nodiscard]] StagedUciResult run_staged_search(
  std::string first_segment,
  std::string action) {
    using namespace std::chrono_literals;

    StagedInputBuffer input_buffer{{
      std::move(first_segment),
      std::move(action),
      "quit\n",
    }};
    SynchronizedOutputBuffer output_buffer;
    std::istream input{&input_buffer};
    std::ostream output{&output_buffer};
    std::ostringstream errors;
    int status = EXIT_FAILURE;
    std::jthread command_thread{
      [&] {
          status = run_uci(input, output, errors);
      }};

    const bool command_boundary_reached =
      input_buffer.wait_until_blocked_for(1, 5s);
    // The finite zero-node search completes on its worker thread. Settling at
    // the blocked command boundary makes a premature worker write observable
    // before the releasing command is supplied.
    if (command_boundary_reached)
        std::this_thread::sleep_for(50ms);
    const std::string before_action =
      output_buffer.snapshot();
    input_buffer.release(1);
    const bool response_before_quit =
      output_buffer.wait_for_text("bestmove ", 10s);
    input_buffer.release(2);
    command_thread.join();

    return {
      status,
      command_boundary_reached,
      response_before_quit,
      before_action,
      output_buffer.snapshot(),
      errors.str(),
    };
}

[[nodiscard]] UciResult run(
  std::string_view commands) {
    std::istringstream input{std::string(commands)};
    std::ostringstream output;
    std::ostringstream errors;

    const int status =
      run_uci(input, output, errors);
    return {
      status,
      output.str(),
      errors.str(),
    };
}

void expect_success(
  const UciResult& result,
  std::string_view status_message,
  std::string_view error_message) {
    expect(result.status == EXIT_SUCCESS,
           status_message);
    expect(result.errors.empty(),
           error_message);
}

[[nodiscard]] std::vector<std::string_view>
split_lines(const std::string& text) {
    std::vector<std::string_view> lines;
    std::size_t begin = 0;

    while (begin < text.size()) {
        const std::size_t end =
          text.find('\n', begin);
        if (end == std::string::npos) {
            lines.emplace_back(text.data() + begin,
                               text.size() - begin);
            break;
        }

        lines.emplace_back(text.data() + begin,
                           end - begin);
        begin = end + 1;
    }

    return lines;
}

[[nodiscard]] constexpr bool
is_ascii_whitespace(char character) noexcept {
    return character == ' '
        || character == '\t'
        || character == '\n'
        || character == '\r'
        || character == '\f'
        || character == '\v';
}

[[nodiscard]] std::vector<std::string_view>
split_words(std::string_view text) {
    std::vector<std::string_view> words;
    std::size_t begin = 0;

    while (begin < text.size()) {
        while (begin < text.size()
               && is_ascii_whitespace(text[begin])) {
            ++begin;
        }

        if (begin == text.size())
            break;

        std::size_t end = begin;
        while (end < text.size()
               && !is_ascii_whitespace(text[end])) {
            ++end;
        }

        words.push_back(
          text.substr(begin, end - begin));
        begin = end;
    }

    return words;
}

[[nodiscard]] std::optional<std::string_view>
unique_line_with_prefix(
  const std::vector<std::string_view>& lines,
  std::string_view prefix,
  std::string_view message) {
    std::optional<std::string_view> found;
    std::size_t count = 0;

    for (const std::string_view line : lines) {
        if (!line.starts_with(prefix))
            continue;

        found = line;
        ++count;
    }

    expect(count == 1, message);
    return count == 1
        ? found
        : std::nullopt;
}

template<typename Integer>
[[nodiscard]] bool parse_integer(
  std::string_view text,
  Integer& value) noexcept {
    if (text.empty())
        return false;

    const auto result =
      std::from_chars(
        text.data(),
        text.data() + text.size(),
        value);
    return result.ec == std::errc{}
        && result.ptr
             == text.data() + text.size();
}

struct SearchInfo {
    std::uint64_t depth = 0;
    std::uint64_t selective_depth = 0;
    std::uint64_t multipv = 0;
    std::string_view score_kind;
    std::int64_t score = 0;
    std::uint64_t nodes = 0;
    std::uint64_t nps = 0;
    std::uint64_t hashfull = 0;
    std::uint64_t time_ms = 0;
    std::vector<std::string_view> principal_variation;
};

[[nodiscard]] std::optional<SearchInfo>
parse_search_info(std::string_view line) {
    const std::vector<std::string_view> words =
      split_words(line);
    if (words.size() < 18
        || words[0] != "info"
        || words[1] != "depth"
        || words[3] != "seldepth"
        || words[5] != "multipv"
        || words[7] != "score"
        || (words[8] != "cp"
            && words[8] != "mate")
        || words[10] != "nodes"
        || words[12] != "nps"
        || words[14] != "hashfull"
        || words[16] != "time"
        || (words.size() != 18
            && (words.size() < 20
                || words[18] != "pv"))) {
        return std::nullopt;
    }

    SearchInfo info;
    info.score_kind = words[8];
    if (!parse_integer(words[2], info.depth)
        || !parse_integer(
             words[4], info.selective_depth)
        || !parse_integer(words[6], info.multipv)
        || !parse_integer(words[9], info.score)
        || !parse_integer(words[11], info.nodes)
        || !parse_integer(words[13], info.nps)
        || !parse_integer(words[15], info.hashfull)
        || !parse_integer(words[17], info.time_ms)) {
        return std::nullopt;
    }

    if (words.size() >= 20) {
        info.principal_variation.assign(
          words.begin() + 19,
          words.end());
    }

    return info;
}

[[nodiscard]] std::vector<SearchInfo>
search_infos(
  const std::vector<std::string_view>& lines,
  std::string_view syntax_message) {
    std::vector<SearchInfo> infos;

    for (const std::string_view line : lines) {
        if (!line.starts_with("info depth "))
            continue;

        const std::optional<SearchInfo> info =
          parse_search_info(line);
        expect(info.has_value(), syntax_message);
        if (info)
            infos.push_back(*info);
    }

    return infos;
}

[[nodiscard]] std::optional<SearchInfo>
find_search_info(
  const std::vector<SearchInfo>& infos,
  std::uint64_t depth,
  std::optional<std::uint64_t> nodes =
    std::nullopt) {
    for (auto iterator = infos.rbegin();
         iterator != infos.rend();
         ++iterator) {
        if (iterator->depth == depth
            && (!nodes
                || iterator->nodes == *nodes)) {
            return *iterator;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<std::string_view>
bestmove_text(
  const std::vector<std::string_view>& lines,
  std::string_view message) {
    const auto line =
      unique_line_with_prefix(
        lines, "bestmove ", message);
    if (!line)
        return std::nullopt;

    return line->substr(
      std::string_view("bestmove ").size());
}

[[nodiscard]] Position replay_position() {
    Position position =
      make_starting_position();
    constexpr std::array<Move, 4> moves = {
      Move::normal(
        make_square(FILE_D, RANK_2),
        make_square(FILE_D, RANK_4)),
      Move::normal(
        make_square(FILE_B, RANK_7),
        make_square(FILE_D, RANK_7)),
      Move::normal(
        make_square(FILE_E, RANK_13),
        make_square(FILE_E, RANK_11)),
      Move::normal(
        make_square(FILE_M, RANK_8),
        make_square(FILE_K, RANK_8)),
    };

    for (const Move move : moves) {
        UndoState undo;
        do_move(position, move, undo);
    }

    return position;
}

[[nodiscard]] Move first_legal_move(
  const Position& source) noexcept {
    Position position = source;
    MoveList moves;
    generate_legal_moves(position, moves);
    return moves.empty()
        ? Move::none()
        : moves[0];
}

[[nodiscard]] bool legal_principal_variation(
  Position position,
  const std::vector<std::string_view>& variation) {
    for (const std::string_view text : variation) {
        const MoveParseResult move =
          parse_move(position, text);
        if (!move || !move->is_board_move())
            return false;

        UndoState undo;
        do_move(position, *move, undo);
    }

    return true;
}

void expect_fallback_search(
  const UciResult& result,
  const Position& position,
  std::string_view status_message,
  std::string_view error_message,
  std::string_view info_message,
  std::string_view bestmove_message) {
    expect_success(
      result, status_message, error_message);
    const std::vector<std::string_view> lines =
      split_lines(result.output);
    const std::vector<SearchInfo> infos =
      search_infos(
        lines,
        "fallback search emits valid info syntax");
    const std::optional<SearchInfo> info =
      find_search_info(infos, 0, 0);
    expect(info.has_value(), info_message);

    const Move expected_move =
      first_legal_move(position);
    expect(expected_move.is_board_move(),
           "fallback fixture has a legal move");
    const std::string expected_text =
      serialize_move(expected_move);
    const auto best =
      bestmove_text(lines, bestmove_message);
    expect(best && *best == expected_text,
           "fallback search returns the first legal move");

    if (info) {
        expect(info->score_kind == "cp",
               "fallback search reports a centipawn score");
        expect(info->selective_depth == 0
                 && info->multipv == 1
                 && info->hashfull <= 1000,
               "fallback search reports bounded protocol metadata");
        expect(info->principal_variation.size() == 1
                 && info->principal_variation.front()
                      == expected_text,
               "fallback principal variation matches bestmove");
    }
}

[[nodiscard]] constexpr Position
blocked_corner_checkmate() noexcept {
    Position position;
    position.set_side_to_move(RED);
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
    position.put_piece(
      B_KNIGHT, make_square(FILE_F, RANK_2));
    return position;
}

void test_handshake_options_and_ready() {
    const UciResult result =
      run(
        "uci\n"
        "setoption name Threads value 1\n"
        "setoption name Hash value 1\n"
        "setoption name Clear Hash\n"
        "setoption name MultiPV value 1\n"
        "setoption name Move Overhead value 0\n"
        "setoption name Ponder value true\n"
        "setoption name Ponder value false\n"
        "setoption name UCI_Variant value 4pc\n"
        "isready\n"
        "quit\n");
    expect_success(
      result,
      "handshake and option commands return success",
      "handshake and option commands write no errors");

    constexpr std::array<std::string_view, 11>
      expected = {
        "id name Mockingbird",
        "id author Mockingbird contributors",
        "option name Threads type spin default 1 min 1 max 1",
        "option name Hash type spin default 16 min 1 max 1024",
        "option name Clear Hash type button",
        "option name MultiPV type spin default 1 min 1 max 1",
        "option name Move Overhead type spin default 10 min 0 max 5000",
        "option name Ponder type check default false",
        "option name UCI_Variant type combo default 4pc var 4pc",
        "uciok",
        "readyok",
      };
    const std::vector<std::string_view> lines =
      split_lines(result.output);
    expect(lines.size() == expected.size(),
           "handshake emits every declaration exactly once");

    const std::size_t common_size =
      std::min(lines.size(), expected.size());
    for (std::size_t index = 0;
         index < common_size;
         ++index) {
        expect(
          lines[index] == expected[index],
          "handshake declarations preserve protocol order and spelling");
    }
}

void test_start_position_move_replay() {
    const Position expected =
      replay_position();
    const UciResult result =
      run(
        "position startpos moves"
        " d2d4 b7d7 e13e11 m8k8\n"
        "fen\n"
        "quit\n");
    expect_success(
      result,
      "starting-position move replay returns success",
      "starting-position move replay writes no errors");
    expect(
      result.output
        == "fen " + serialize_fen(expected) + "\n",
      "compact move replay reaches the independently constructed position");
}

void test_strict_fen_position_input() {
    const Position expected =
      replay_position();
    const std::string fen =
      serialize_fen(expected);
    const UciResult valid =
      run(
        "position fen " + fen
        + "\nfen\nquit\n");
    expect_success(
      valid,
      "valid FEN position command returns success",
      "valid FEN position command writes no errors");
    expect(valid.output == "fen " + fen + "\n",
           "valid FEN position round-trips canonically");

    const std::string starting_fen =
      serialize_fen(make_starting_position());
    const UciResult suffix =
      run(
        "position fen " + fen
        + " unexpected\n"
          "fen\n"
          "position startpos\n"
          "fen\n"
          "quit\n");
    expect_success(
      suffix,
      "invalid FEN suffix does not terminate the command loop",
      "invalid FEN suffix writes no standard error output");
    expect(
      suffix.output
        == "info string invalid position FEN\n"
           "info string invalid position\n"
           "fen " + starting_fen + "\n",
      "FEN input rejects trailing data and a valid position recovers");

    const std::string position_notation =
      serialize_position(
        make_starting_position());
    const UciResult wrong_notation =
      run(
        "position fen " + position_notation
        + "\nfen\nquit\n");
    expect_success(
      wrong_notation,
      "non-FEN position text does not terminate the command loop",
      "non-FEN position text writes no standard error output");
    expect(
      wrong_notation.output
        == "info string invalid position FEN\n"
           "info string invalid position\n",
      "position fen accepts only the FEN4 grammar");
}

void test_invalid_position_behavior() {
    const std::string starting_fen =
      serialize_fen(make_starting_position());
    const UciResult replay_error =
      run(
        "position startpos moves d2d5\n"
        "fen\n"
        "legalmoves\n"
        "gameresult\n"
        "eval\n"
        "d\n"
        "go depth 1\n"
        "position startpos\n"
        "fen\n"
        "quit\n");
    expect_success(
      replay_error,
      "illegal replay move does not terminate the command loop",
      "illegal replay move writes no standard error output");
    expect(
      replay_error.output
        == "info string Illegal move ignored: 'd2d5'\n"
           "info string invalid position\n"
           "info string invalid position\n"
           "gameresult invalid\n"
           "info string invalid position\n"
           "info string invalid position\n"
           "info string invalid position\n"
           "bestmove 0000\n"
           "fen " + starting_fen + "\n",
      "invalid-position commands report stable results until recovery");

    Position empty;
    empty.set_side_to_move(RED);
    const UciResult invalid_layout =
      run(
        "position fen "
        + serialize_fen(empty)
        + "\ngo depth 1\nquit\n");
    expect_success(
      invalid_layout,
      "invalid king layout does not terminate the command loop",
      "invalid king layout writes no standard error output");
    expect(
      invalid_layout.output
        == "info string invalid position\n"
           "bestmove 0000\n",
      "search rejects a parsed position without four kings");
}

[[nodiscard]] constexpr bool
is_compact_move_text(
  std::string_view text) noexcept {
    if (text.size() < 4 || text.size() > 7)
        return false;

    for (const char character : text) {
        if ((character < 'a'
             || character > 'n')
            && (character < '0'
                || character > '9')) {
            return false;
        }
    }

    return true;
}

void test_legalmoves_compact_notation() {
    const UciResult result =
      run(
        "position startpos\n"
        "legalmoves\n"
        "quit\n");
    expect_success(
      result,
      "legalmoves command returns success",
      "legalmoves command writes no errors");

    const std::vector<std::string_view> lines =
      split_lines(result.output);
    const auto line =
      unique_line_with_prefix(
        lines,
        "legalmoves",
        "legalmoves emits one result line");
    if (!line)
        return;

    const std::vector<std::string_view> words =
      split_words(*line);
    expect(!words.empty()
             && words[0] == "legalmoves",
           "legalmoves line starts with its command name");

    Position expected_position =
      make_starting_position();
    MoveList expected_moves;
    generate_legal_moves(
      expected_position, expected_moves);
    expect(
      words.size() == expected_moves.size() + 1,
      "legalmoves emits every legal starting move");

    std::vector<std::uint32_t> actual_raw;
    for (std::size_t index = 1;
         index < words.size();
         ++index) {
        const std::string_view text = words[index];
        expect(
          is_compact_move_text(text),
          "legalmoves uses lowercase compact coordinate text");

        const MoveParseResult parsed =
          parse_move(expected_position, text);
        expect(
          parsed.has_value()
            && parsed->is_board_move(),
          "every legalmoves entry parses as a legal board move");
        if (parsed && parsed->is_board_move())
            actual_raw.push_back(parsed->raw());
    }

    std::vector<std::uint32_t> expected_raw;
    expected_raw.reserve(expected_moves.size());
    for (const Move move : expected_moves)
        expected_raw.push_back(move.raw());

    std::ranges::sort(actual_raw);
    std::ranges::sort(expected_raw);
    expect(
      std::adjacent_find(
        actual_raw.begin(),
        actual_raw.end())
        == actual_raw.end(),
      "legalmoves does not emit duplicate moves");
    expect(actual_raw == expected_raw,
           "legalmoves entries equal the generated legal move set");
}

void test_go_depth_one() {
    const UciResult result =
      run(
        "position startpos\n"
        "go depth 1\n"
        "quit\n");
    expect_success(
      result,
      "depth-one search returns success",
      "depth-one search writes no errors");

    const std::vector<std::string_view> lines =
      split_lines(result.output);
    const std::vector<SearchInfo> infos =
      search_infos(
        lines,
        "depth-one search emits valid info syntax");
    const std::optional<SearchInfo> info =
      infos.empty()
        ? std::nullopt
        : std::optional<SearchInfo>{
            infos.back()};
    expect(info.has_value(),
           "depth-limited search reports its final search state");
    if (info) {
        expect(info->depth <= 1,
               "depth-limited search does not exceed depth one");
        expect(info->score_kind == "cp",
               "ongoing depth-one search reports a centipawn score");
    }

    const auto best =
      bestmove_text(
        lines,
        "depth-one search emits one bestmove");
    if (!best)
        return;

    Position position =
      make_starting_position();
    const MoveParseResult parsed =
      parse_move(position, *best);
    expect(
      parsed.has_value()
        && parsed->is_board_move(),
      "depth-one bestmove is legal in the root position");
    if (info) {
        expect(!info->principal_variation.empty()
                 && info->principal_variation.front()
                      == *best,
               "depth-one principal variation starts with bestmove");
    }
}

void test_iterative_info_reporting() {
    const StagedUciResult result =
      run_staged_search(
        "position startpos\n"
        "go depth 3\n",
        "isready\n");
    expect(result.command_boundary_reached,
           "iterative info test reaches the command boundary");
    expect(result.response_before_quit,
           "depth-three search completes before quit");
    expect(result.status == EXIT_SUCCESS,
           "depth-three search returns success");
    expect(result.errors.empty(),
           "depth-three search writes no errors");

    const std::vector<std::string_view> lines =
      split_lines(result.output);
    const std::vector<SearchInfo> infos =
      search_infos(
        lines,
        "iterative search emits valid info syntax");
    expect(infos.size() == 3,
           "depth-three search emits one info line per depth");

    for (std::size_t index = 0;
         index < infos.size();
         ++index) {
        const SearchInfo& info = infos[index];
        expect(
          info.depth
            == static_cast<std::uint64_t>(index + 1),
          "iterative info depths are consecutive");
        expect(info.score_kind == "cp",
               "starting-position iterations report centipawn scores");
        expect(info.nodes > 0,
               "completed iterations report searched nodes");
        expect(info.nps > 0,
               "completed iterations report positive throughput");
        expect(info.selective_depth >= info.depth,
               "completed iterations report their deepest entered ply");
        expect(info.multipv == 1,
               "completed iterations identify the supported principal line");
        expect(info.hashfull <= 1000,
               "completed iterations report per-mille hash occupancy");
        expect(!info.principal_variation.empty(),
               "completed iterations report a root principal variation");
        expect(
          info.principal_variation.size()
              <= info.selective_depth
            && legal_principal_variation(
                 make_starting_position(),
                 info.principal_variation),
          "reported principal variations are bounded legal move sequences");

        if (index == 0)
            continue;

        expect(info.nodes > infos[index - 1].nodes,
               "iterative node counts are cumulative");
        expect(info.time_ms >= infos[index - 1].time_ms,
               "iterative elapsed times are monotonic");
    }

    expect(
      result.output.find("currmove")
        == std::string::npos,
      "search output omits current-move fields");
    expect(
      result.output.find("currmovenumber")
        == std::string::npos,
      "search output omits current-move-number fields");

    const auto best =
      bestmove_text(
        lines,
        "depth-three search emits one bestmove");
    if (best && !infos.empty()) {
        expect(
          !infos.back().principal_variation.empty()
            && infos.back().principal_variation.front()
                 == *best,
          "final principal variation starts with bestmove");
        expect(
          infos.back().principal_variation.size() >= 2,
          "a completed deeper iteration reports more than its root move");
    }
}

void test_zero_limit_fallbacks() {
    const Position starting =
      make_starting_position();
    const UciResult nodes =
      run(
        "position startpos\n"
        "go nodes 0\n"
        "quit\n");
    expect_fallback_search(
      nodes,
      starting,
      "zero-node search returns success",
      "zero-node search writes no errors",
      "zero-node search reports depth zero and zero nodes",
      "zero-node search emits one bestmove");

    const UciResult movetime =
      run(
        "position startpos\n"
        "go movetime 0\n"
        "quit\n");
    expect_fallback_search(
      movetime,
      starting,
      "zero-time search returns success",
      "zero-time search writes no errors",
      "zero-time search reports depth zero and zero nodes",
      "zero-time search emits one bestmove");
}

void test_four_player_and_legacy_clocks() {
    Position blue_to_move =
      make_starting_position();
    UndoState undo;
    do_move(
      blue_to_move,
      Move::normal(
        make_square(FILE_D, RANK_2),
        make_square(FILE_D, RANK_4)),
      undo);

    const UciResult four_player =
      run(
        "setoption name Move Overhead value 0\n"
        "position startpos moves d2d4\n"
        "go rtime 1000 btime 0"
        " ytime 1000 gtime 1000"
        " rinc 4 binc 5 yinc 6 ginc 7"
        " movestogo 10\n"
        "quit\n");
    expect_fallback_search(
      four_player,
      blue_to_move,
      "four-clock search returns success",
      "four-clock search writes no errors",
      "four-clock search uses Blue's zero remaining time",
      "four-clock search emits one bestmove");

    const UciResult legacy =
      run(
        "setoption name Move Overhead value 0\n"
        "position startpos moves d2d4\n"
        "go wtime 1000 btime 0"
        " winc 4 binc 5"
        " movestogo 10\n"
        "quit\n");
    expect_fallback_search(
      legacy,
      blue_to_move,
      "legacy-clock search returns success",
      "legacy-clock search writes no errors",
      "legacy clocks map the black clock to Blue",
      "legacy-clock search emits one bestmove");
}

void test_perft_command() {
    Position position =
      make_starting_position();
    const std::uint64_t depth_one =
      perft(position, 1);
    const std::uint64_t depth_two =
      perft(position, 2);
    const std::string expected =
      "Nodes searched: "
      + std::to_string(depth_one)
      + "\nNodes searched: "
      + std::to_string(depth_two)
      + "\nfen "
      + serialize_fen(position)
      + "\n";

    const UciResult result =
      run(
        "position startpos\n"
        "go perft 1\n"
        "go perft 2\n"
        "fen\n"
        "quit\n");
    expect_success(
      result,
      "perft commands return success",
      "perft commands write no errors");
    expect(result.output == expected,
           "perft commands match core counts and preserve the position");
    expect(
      result.output.find("bestmove ")
        == std::string::npos,
      "perft commands do not emit bestmove");
}

void test_terminal_bestmove() {
    const Position mate =
      blocked_corner_checkmate();
    const UciResult result =
      run(
        "position fen "
        + serialize_fen(mate)
        + "\ngo depth 1\nquit\n");
    expect_success(
      result,
      "terminal search returns success",
      "terminal search writes no errors");

    const std::vector<std::string_view> lines =
      split_lines(result.output);
    const std::vector<SearchInfo> infos =
      search_infos(
        lines,
        "terminal search emits valid info syntax");
    const std::optional<SearchInfo> info =
      find_search_info(infos, 0, 0);
    expect(info.has_value(),
           "terminal search reports depth zero and zero nodes");
    if (info) {
        expect(info->score_kind == "mate",
               "checkmate reports a mate score");
        expect(info->selective_depth == 0
                 && info->multipv == 1
                 && info->hashfull == 0,
               "terminal search reports zero-depth protocol metadata");
        expect(info->principal_variation.empty(),
               "terminal search does not emit a principal variation");
    }

    const auto best =
      bestmove_text(
        lines,
        "terminal search emits one bestmove");
    expect(best && *best == "0000",
           "terminal search emits the null bestmove token");
}

void test_stop_and_quit() {
    const UciResult stop_only =
      run("stop\n");
    expect_success(
      stop_only,
      "stop at end of input returns success",
      "stop at end of input writes no errors");
    expect(stop_only.output.empty(),
           "stop emits no protocol output without an active search");

    const UciResult active =
      run(
        "position startpos\n"
        "go infinite\n"
        "stop\n"
        "quit\n");
    expect_success(
      active,
      "stop interrupts an active search",
      "interrupted search writes no errors");
    const std::vector<std::string_view> active_lines =
      split_lines(active.output);
    const auto stopped_move =
      bestmove_text(
        active_lines,
        "interrupted search emits exactly one bestmove");
    if (stopped_move) {
        Position position =
          make_starting_position();
        const MoveParseResult parsed =
          parse_move(position, *stopped_move);
        expect(
          parsed.has_value()
            && parsed->is_board_move(),
          "interrupted search preserves a legal fallback move");
    }

    const UciResult quit =
      run(
        "stop\n"
        "isready\n"
        "quit\n"
        "uci\n");
    expect_success(
      quit,
      "quit returns success",
      "stop and quit write no errors");
    expect(quit.output == "readyok\n",
           "stop is silent and quit suppresses later commands");
}

void test_ponder_lifecycle() {
    const StagedUciResult completed =
      run_staged_search(
        "position startpos\n"
        "go ponder nodes 0\n",
        "ponderhit\n");
    expect(
      completed.command_boundary_reached,
      "ponder completion test reaches the ponderhit boundary");
    expect(
      completed.before_action.empty(),
      "a completed finite ponder search emits nothing before ponderhit");
    expect(
      completed.response_before_quit,
      "ponderhit releases the completed result without waiting for quit");
    expect(
      completed.status == EXIT_SUCCESS
        && completed.errors.empty(),
      "completed ponder search exits successfully");

    const std::vector<std::string_view> completed_lines =
      split_lines(completed.output);
    const std::vector<SearchInfo> completed_infos =
      search_infos(
        completed_lines,
        "completed ponder search emits valid info syntax");
    const std::optional<SearchInfo> completed_info =
      find_search_info(completed_infos, 0, 0);
    expect(
      completed_info.has_value(),
      "zero-node ponder search retains its completed fallback result");
    static_cast<void>(bestmove_text(
      completed_lines,
      "ponderhit emits exactly one bestmove"));

    const StagedUciResult searched =
      run_staged_search(
        "position startpos\n"
        "go ponder depth 3\n",
        "ponderhit\n");
    expect(
      searched.command_boundary_reached
        && searched.before_action.empty(),
      "completed ponder depths remain buffered before ponderhit");
    expect(
      searched.response_before_quit
        && searched.status == EXIT_SUCCESS
        && searched.errors.empty(),
      "ponderhit releases a completed depth-three ponder search");
    const std::vector<std::string_view> searched_lines =
      split_lines(searched.output);
    const std::vector<SearchInfo> searched_infos =
      search_infos(
        searched_lines,
        "released ponder search emits valid info syntax");
    expect(searched_infos.size() == 3,
           "ponderhit releases every buffered completed depth");
    for (std::size_t index = 0;
         index < searched_infos.size();
         ++index) {
        expect(
          searched_infos[index].depth
            == static_cast<std::uint64_t>(index + 1),
          "released ponder depths remain consecutive");
    }
    static_cast<void>(bestmove_text(
      searched_lines,
      "released ponder search emits exactly one bestmove"));

    const StagedUciResult timed =
      run_staged_search(
        "setoption name Move Overhead value 0\n"
        "position startpos\n"
        "go ponder movetime 0\n",
        "ponderhit\n");
    expect(
      timed.command_boundary_reached
        && timed.before_action.empty(),
      "a ponder clock remains dormant before ponderhit");
    expect(
      timed.response_before_quit
        && timed.status == EXIT_SUCCESS
        && timed.errors.empty(),
      "ponderhit activates the clock and completes before quit");
    const std::vector<std::string_view> timed_lines =
      split_lines(timed.output);
    const auto timed_move =
      bestmove_text(
        timed_lines,
        "timed ponder search emits exactly one bestmove");
    if (timed_move) {
        Position position = make_starting_position();
        const MoveParseResult parsed =
          parse_move(position, *timed_move);
        expect(
          parsed && parsed->is_board_move(),
          "timed ponder search preserves a legal fallback move");
    }

    const StagedUciResult stopped =
      run_staged_search(
        "position startpos\n"
        "go ponder nodes 0\n",
        "stop\n");
    expect(
      stopped.command_boundary_reached
        && stopped.before_action.empty(),
      "a ponder result remains withheld before stop");
    expect(
      stopped.response_before_quit
        && stopped.status == EXIT_SUCCESS
        && stopped.errors.empty(),
      "stop releases and joins a ponder search before quit");
    static_cast<void>(bestmove_text(
      split_lines(stopped.output),
      "stop during ponder emits exactly one bestmove"));

    const UciResult stray =
      run(
        "ponderhit\n"
        "isready\n"
        "quit\n");
    expect_success(
      stray,
      "stray ponderhit returns success",
      "stray ponderhit writes no errors");
    expect(
      stray.output == "readyok\n",
      "stray ponderhit is silent and leaves the session responsive");
}

}  // namespace

int main() {
    test_handshake_options_and_ready();
    test_start_position_move_replay();
    test_strict_fen_position_input();
    test_invalid_position_behavior();
    test_legalmoves_compact_notation();
    test_go_depth_one();
    test_iterative_info_reporting();
    test_zero_limit_fallbacks();
    test_four_player_and_legacy_clocks();
    test_perft_command();
    test_terminal_bestmove();
    test_stop_and_quit();
    test_ponder_lifecycle();

    if (failures != 0) {
        std::cerr << failures
                  << " UCI test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All UCI tests passed\n";
    return EXIT_SUCCESS;
}
