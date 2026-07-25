#include "diagnostic.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <ios>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#include "notation.h"
#include "perft.h"
#include "setup.h"

namespace {

int failures = 0;

// Records a failed condition and allows the remaining tests to run.
void expect(bool condition, std::string_view message) {
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

using namespace Mockingbird;

inline constexpr std::string_view HELP_TEXT =
  "Commands:\n"
  "  position start\n"
  "  position <notation>\n"
  "  show\n"
  "  perft <depth>\n"
  "  divide <depth>\n"
  "  help\n"
  "  quit\n";

inline constexpr std::string_view EMPTY_BOARD =
  "8/8/8/14/14/14/14/14/14/14/14/8/8/8";

struct DiagnosticResult {
    int status = EXIT_FAILURE;
    std::string output;
    std::string errors;
};

[[nodiscard]] DiagnosticResult run(
  std::string_view commands) {
    std::istringstream input{std::string(commands)};
    std::ostringstream output;
    std::ostringstream errors;

    const int status =
      run_diagnostic(input, output, errors);
    return {
      status,
      output.str(),
      errors.str(),
    };
}

[[nodiscard]] Position custom_position() {
    Position position = make_starting_position();
    position.remove_piece(
      make_square(FILE_D, RANK_2));
    position.set_side_to_move(GREEN);
    position.clear_castling_right(
      RED, CastlingSide::KING_SIDE);
    position.clear_castling_right(
      BLUE, CastlingSide::QUEEN_SIDE);
    position.set_en_passant_square(
      GREEN, make_square(FILE_H, RANK_8));
    return position;
}

void test_initial_show_and_eof() {
    const DiagnosticResult result =
      run("show\n");
    const std::string expected =
      serialize_position(make_starting_position())
      + "\n";

    expect(result.status == EXIT_SUCCESS,
           "end of input returns success");
    expect(result.output == expected,
           "initial show prints the production starting position");
    expect(result.errors.empty(),
           "initial show writes no error output");
}

void test_custom_load_and_show() {
    const std::string notation =
      serialize_position(custom_position());
    const DiagnosticResult result =
      run(
        "position " + notation
        + "\nshow\nquit\n");

    expect(result.status == EXIT_SUCCESS,
           "custom position sequence returns success");
    expect(
      result.output
        == "Position loaded\n" + notation + "\n",
      "custom position load acknowledges and shows exact state");
    expect(result.errors.empty(),
           "successful custom load writes no errors");
}

void test_start_reset() {
    const std::string custom =
      serialize_position(custom_position());
    const std::string starting =
      serialize_position(make_starting_position());
    const DiagnosticResult result =
      run(
        "position " + custom
        + "\nposition start\nshow\n");

    expect(result.status == EXIT_SUCCESS,
           "starting-position reset sequence returns success");
    expect(
      result.output
        == "Position loaded\n"
           "Position loaded\n"
           + starting + "\n",
      "position start acknowledges and restores production setup");
    expect(result.errors.empty(),
           "starting-position reset writes no errors");
}

void test_malformed_load_preserves_state() {
    const std::string custom =
      serialize_position(custom_position());
    const std::string malformed =
      std::string(EMPTY_BOARD) + " R - -";
    const std::string commands =
      "position " + custom
      + "\nposition " + malformed
      + "\nshow\n";
    const DiagnosticResult result = run(commands);
    const std::string expected_error =
      "error: position side at offset "
      + std::to_string(EMPTY_BOARD.size() + 1)
      + "\n";

    expect(result.status == EXIT_SUCCESS,
           "notation error does not terminate the command loop");
    expect(
      result.output
        == "Position loaded\n" + custom + "\n",
      "malformed load preserves the previously loaded state");
    expect(result.errors == expected_error,
           "malformed load reports category and notation offset");
}

void test_perft_depths() {
    Position position = make_starting_position();
    const std::string original =
      serialize_position(position);
    const std::uint64_t depth_zero =
      perft(position, 0);
    const std::uint64_t depth_one =
      perft(position, 1);
    const DiagnosticResult result =
      run("perft 0\nperft 1\nquit\n");

    const std::string expected =
      "Nodes: " + std::to_string(depth_zero)
      + "\nNodes: " + std::to_string(depth_one)
      + "\n";
    expect(result.status == EXIT_SUCCESS,
           "perft commands return success");
    expect(result.output == expected,
           "perft depth zero and one match core perft");
    expect(result.errors.empty(),
           "valid perft commands write no errors");
    expect(serialize_position(position) == original,
           "core perft expectation preserves its position");
}

void test_divide_output() {
    Position position = make_starting_position();
    const std::string original =
      serialize_position(position);
    const PerftList entries =
      perft_divide(position, 1);
    const std::string expected =
      format_perft_divide(entries) + "\n";
    const DiagnosticResult result =
      run("divide 1\nquit\n");

    expect(result.status == EXIT_SUCCESS,
           "divide command returns success");
    expect(result.output == expected,
           "divide command exactly matches core formatting");
    expect(result.errors.empty(),
           "valid divide command writes no errors");
    expect(serialize_position(position) == original,
           "core divide expectation preserves its position");
}

void test_analysis_preserves_loaded_state() {
    Position position = custom_position();
    const std::string notation =
      serialize_position(position);

    Position perft_position = position;
    const std::uint64_t nodes =
      perft(perft_position, 1);
    Position divide_position = position;
    const PerftList entries =
      perft_divide(divide_position, 1);

    const DiagnosticResult result =
      run(
        "position " + notation
        + "\nperft 1\ndivide 1\nshow\nquit\n");
    const std::string expected =
      "Position loaded\nNodes: "
      + std::to_string(nodes) + "\n"
      + format_perft_divide(entries) + "\n"
      + notation + "\n";

    expect(result.status == EXIT_SUCCESS,
           "loaded-position analysis returns success");
    expect(result.output == expected,
           "perft and divide preserve the loaded position");
    expect(result.errors.empty(),
           "loaded-position analysis writes no errors");
    expect(
      serialize_position(perft_position) == notation
        && serialize_position(divide_position) == notation,
      "core analysis expectations preserve their positions");
}

void test_whitespace_and_blank_lines() {
    const std::string starting =
      serialize_position(make_starting_position());
    const DiagnosticResult result =
      run(
        "\n"
        " \t\r\n"
        "\vshow\f \r\n"
        "\tperft\t0\v\n"
        "  quit  \n");

    expect(result.status == EXIT_SUCCESS,
           "whitespace command sequence returns success");
    expect(
      result.output
        == starting + "\nNodes: 1\n",
      "ASCII whitespace and blank lines are handled consistently");
    expect(result.errors.empty(),
           "whitespace and blank lines write no errors");
}

void test_help() {
    const DiagnosticResult result =
      run("help\n");

    expect(result.status == EXIT_SUCCESS,
           "help followed by EOF returns success");
    expect(result.output == HELP_TEXT,
           "help output matches the exact command list");
    expect(result.errors.empty(),
           "help writes no errors");
}

void test_quit_suppresses_later_commands() {
    const DiagnosticResult result =
      run("quit\nshow\nunknown\n");

    expect(result.status == EXIT_SUCCESS,
           "quit returns success");
    expect(result.output.empty(),
           "quit suppresses later standard output");
    expect(result.errors.empty(),
           "quit suppresses later errors");
}

void test_unknown_and_missing_position_commands() {
    const std::string starting =
      serialize_position(make_starting_position());
    const DiagnosticResult result =
      run("unknown\nposition\nshow\n");

    expect(result.status == EXIT_SUCCESS,
           "command errors continue until EOF");
    expect(result.output == starting + "\n",
           "valid command after errors still executes");
    expect(
      result.errors
        == "error: unknown command\n"
           "error: position requires an argument\n",
      "unknown and missing-position errors are exact");
}

void test_forbidden_trailing_arguments() {
    const std::string starting =
      serialize_position(make_starting_position());
    const DiagnosticResult result =
      run(
        "show extra\n"
        "help extra\n"
        "quit extra\n"
        "show\n");

    expect(result.status == EXIT_SUCCESS,
           "trailing-argument errors do not terminate input");
    expect(result.output == starting + "\n",
           "commands with trailing arguments produce no standard output");
    expect(
      result.errors
        == "error: show takes no arguments\n"
           "error: help takes no arguments\n"
           "error: quit takes no arguments\n",
      "forbidden trailing-argument errors are exact");
}

void test_invalid_depth_arguments() {
    constexpr std::array<std::string_view, 7>
      INVALID_ARGUMENTS = {
        "",
        "-1",
        "+1",
        "one",
        "1x",
        "1 2",
        "999999999999999999999999999999999999",
    };
    std::string commands;
    std::string expected_errors;

    for (const std::string_view argument :
         INVALID_ARGUMENTS) {
        commands += "perft";
        if (!argument.empty()) {
            commands += ' ';
            commands += argument;
        }
        commands += '\n';
        expected_errors +=
          "error: invalid perft depth\n";
    }

    for (const std::string_view argument :
         INVALID_ARGUMENTS) {
        commands += "divide";
        if (!argument.empty()) {
            commands += ' ';
            commands += argument;
        }
        commands += '\n';
        expected_errors +=
          "error: invalid divide depth\n";
    }

    commands += "show\n";
    const DiagnosticResult result = run(commands);
    const std::string starting =
      serialize_position(make_starting_position());

    expect(result.status == EXIT_SUCCESS,
           "invalid depths do not terminate the command loop");
    expect(result.output == starting + "\n",
           "invalid depths produce no standard output");
    expect(result.errors == expected_errors,
           "every invalid depth form reports its command-specific error");
}

void test_input_failure() {
    for (const std::ios::iostate state :
         std::array{
           std::ios::badbit,
           std::ios::failbit,
           std::ios::badbit | std::ios::eofbit,
         }) {
        std::istringstream input;
        input.setstate(state);
        std::ostringstream output;
        std::ostringstream errors;

        const int status =
          run_diagnostic(input, output, errors);
        expect(status == EXIT_FAILURE,
               "non-EOF input failure returns failure");
        expect(output.str().empty(),
               "failed input stream writes no standard output");
        expect(errors.str() == "error: input failure\n",
               "failed input stream reports the exact error");
    }
}

void test_output_ignores_stream_formatting() {
    const std::string malformed =
      std::string(EMPTY_BOARD) + " R - -";
    std::istringstream input{
      "perft 1\nposition " + malformed + "\n"};
    std::ostringstream output;
    std::ostringstream errors;
    output << std::hex << std::showbase;
    errors << std::hex << std::showbase;

    Position position = make_starting_position();
    const std::string expected_output =
      "Nodes: " + std::to_string(perft(position, 1)) + "\n";
    const std::string expected_errors =
      "error: position side at offset "
      + std::to_string(EMPTY_BOARD.size() + 1) + "\n";

    const int status =
      run_diagnostic(input, output, errors);
    expect(status == EXIT_SUCCESS,
           "formatted output streams do not change status");
    expect(output.str() == expected_output,
           "node counts are always written in decimal");
    expect(errors.str() == expected_errors,
           "notation offsets are always written in decimal");
}

}  // namespace

int main() {
    test_initial_show_and_eof();
    test_custom_load_and_show();
    test_start_reset();
    test_malformed_load_preserves_state();
    test_perft_depths();
    test_divide_output();
    test_analysis_preserves_loaded_state();
    test_whitespace_and_blank_lines();
    test_help();
    test_quit_suppresses_later_commands();
    test_unknown_and_missing_position_commands();
    test_forbidden_trailing_arguments();
    test_invalid_depth_arguments();
    test_input_failure();
    test_output_ignores_stream_formatting();

    if (failures != 0) {
        std::cerr << failures
                  << " diagnostic test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All diagnostic tests passed\n";
    return EXIT_SUCCESS;
}
