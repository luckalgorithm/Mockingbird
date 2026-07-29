#include "uci.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
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
    std::string_view score_kind;
    std::int64_t score = 0;
    std::uint64_t nodes = 0;
    std::uint64_t time_ms = 0;
    std::optional<std::string_view> pv;
};

[[nodiscard]] std::optional<SearchInfo>
parse_search_info(std::string_view line) {
    const std::vector<std::string_view> words =
      split_words(line);
    if ((words.size() != 10
         && words.size() != 12)
        || words[0] != "info"
        || words[1] != "depth"
        || words[3] != "score"
        || (words[4] != "cp"
            && words[4] != "mate")
        || words[6] != "nodes"
        || words[8] != "time"
        || (words.size() == 12
            && words[10] != "pv")) {
        return std::nullopt;
    }

    SearchInfo info;
    info.score_kind = words[4];
    if (!parse_integer(words[2], info.depth)
        || !parse_integer(words[5], info.score)
        || !parse_integer(words[7], info.nodes)
        || !parse_integer(words[9], info.time_ms)) {
        return std::nullopt;
    }

    if (words.size() == 12)
        info.pv = words[11];

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
        expect(info->pv
                 && *info->pv == expected_text,
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
        "setoption name UCI_Variant value 4pc\n"
        "isready\n"
        "quit\n");
    expect_success(
      result,
      "handshake and option commands return success",
      "handshake and option commands write no errors");

    constexpr std::array<std::string_view, 10>
      expected = {
        "id name Mockingbird",
        "id author Mockingbird contributors",
        "option name Threads type spin default 1 min 1 max 1",
        "option name Hash type spin default 16 min 1 max 1024",
        "option name Clear Hash type button",
        "option name MultiPV type spin default 1 min 1 max 1",
        "option name Move Overhead type spin default 10 min 0 max 5000",
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
        expect(info->pv && *info->pv == *best,
               "depth-one principal variation starts with bestmove");
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
        expect(!info->pv,
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

}  // namespace

int main() {
    test_handshake_options_and_ready();
    test_start_position_move_replay();
    test_strict_fen_position_input();
    test_invalid_position_behavior();
    test_legalmoves_compact_notation();
    test_go_depth_one();
    test_zero_limit_fallbacks();
    test_four_player_and_legacy_clocks();
    test_perft_command();
    test_terminal_bestmove();
    test_stop_and_quit();

    if (failures != 0) {
        std::cerr << failures
                  << " UCI test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All UCI tests passed\n";
    return EXIT_SUCCESS;
}
