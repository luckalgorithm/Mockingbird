#include "diagnostic.h"

#include <array>
#include <charconv>
#include <concepts>
#include <cstdlib>
#include <istream>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <system_error>

#include "notation.h"
#include "perft.h"
#include "setup.h"

namespace Mockingbird {

namespace {

inline constexpr std::string_view HELP_TEXT =
  "Commands:\n"
  "  position start\n"
  "  position <notation>\n"
  "  show\n"
  "  perft <depth>\n"
  "  divide <depth>\n"
  "  help\n"
  "  quit\n";

[[nodiscard]] constexpr bool is_ascii_whitespace(
  char character) noexcept {
    return character == ' '
        || character == '\t'
        || character == '\n'
        || character == '\r'
        || character == '\f'
        || character == '\v';
}

[[nodiscard]] constexpr std::string_view trim_ascii_whitespace(
  std::string_view text) noexcept {
    while (!text.empty()
           && is_ascii_whitespace(text.front()))
        text.remove_prefix(1);

    while (!text.empty()
           && is_ascii_whitespace(text.back()))
        text.remove_suffix(1);

    return text;
}

struct Command {
    std::string_view name;
    std::string_view arguments;
};

[[nodiscard]] constexpr Command split_command(
  std::string_view line) noexcept {
    line = trim_ascii_whitespace(line);

    std::size_t cursor = 0;
    while (cursor < line.size()
           && !is_ascii_whitespace(line[cursor]))
        ++cursor;

    return {
      line.substr(0, cursor),
      trim_ascii_whitespace(line.substr(cursor)),
    };
}

[[nodiscard]] constexpr std::string_view notation_error_name(
  NotationError error) noexcept {
    switch (error) {
        case NotationError::FIELD_COUNT:
            return "field-count";
        case NotationError::RANK_COUNT:
            return "rank-count";
        case NotationError::RANK_WIDTH:
            return "rank-width";
        case NotationError::EMPTY_RUN:
            return "empty-run";
        case NotationError::PIECE:
            return "piece";
        case NotationError::SIDE:
            return "side";
        case NotationError::CASTLING:
            return "castling";
        case NotationError::DUPLICATE_CASTLING:
            return "duplicate-castling";
        case NotationError::EN_PASSANT:
            return "en-passant";
    }

    return "unknown";
}

[[nodiscard]] bool parse_depth(
  std::string_view text, int& depth) noexcept {
    if (text.empty())
        return false;

    for (const char character : text) {
        if (character < '0' || character > '9')
            return false;
    }

    const auto result = std::from_chars(
      text.data(), text.data() + text.size(), depth);
    return result.ec == std::errc{}
        && result.ptr == text.data() + text.size();
}

void write_text(
  std::ostream& stream,
  std::string_view text) {
    stream.write(
      text.data(),
      static_cast<std::streamsize>(text.size()));
}

template<std::unsigned_integral Integer>
void write_decimal(
  std::ostream& stream,
  Integer value) {
    std::array<
      char,
      static_cast<std::size_t>(
        std::numeric_limits<Integer>::digits10 + 1)>
      buffer{};
    const auto result = std::to_chars(
      buffer.data(), buffer.data() + buffer.size(), value);

    write_text(
      stream,
      std::string_view(
        buffer.data(),
        static_cast<std::size_t>(
          result.ptr - buffer.data())));
}

void write_position_error(
  std::ostream& errors,
  const NotationFailure& failure) {
    write_text(errors, "error: position ");
    write_text(errors, notation_error_name(failure.code));
    write_text(errors, " at offset ");
    write_decimal(errors, failure.offset);
    write_text(errors, "\n");
}

void write_no_argument_error(
  std::ostream& errors,
  std::string_view command) {
    write_text(errors, "error: ");
    write_text(errors, command);
    write_text(errors, " takes no arguments\n");
}

}  // namespace

int run_diagnostic(
  std::istream& input,
  std::ostream& output,
  std::ostream& errors) {
    Position position = make_starting_position();
    std::string line;

    while (std::getline(input, line)) {
        const Command command = split_command(line);
        if (command.name.empty())
            continue;

        if (command.name == "position") {
            if (command.arguments.empty()) {
                write_text(
                  errors,
                  "error: position requires an argument\n");
                continue;
            }

            if (command.arguments == "start") {
                position = make_starting_position();
                write_text(output, "Position loaded\n");
                continue;
            }

            const PositionParseResult parsed =
              parse_position(command.arguments);
            if (!parsed) {
                write_position_error(errors, parsed.error());
                continue;
            }

            position = *parsed;
            write_text(output, "Position loaded\n");
            continue;
        }

        if (command.name == "show") {
            if (!command.arguments.empty()) {
                write_no_argument_error(errors, "show");
                continue;
            }

            write_text(output, serialize_position(position));
            write_text(output, "\n");
            continue;
        }

        if (command.name == "perft") {
            int depth = 0;
            if (!parse_depth(command.arguments, depth)) {
                write_text(
                  errors,
                  "error: invalid perft depth\n");
                continue;
            }

            write_text(output, "Nodes: ");
            write_decimal(output, perft(position, depth));
            write_text(output, "\n");
            continue;
        }

        if (command.name == "divide") {
            int depth = 0;
            if (!parse_depth(command.arguments, depth)) {
                write_text(
                  errors,
                  "error: invalid divide depth\n");
                continue;
            }

            const PerftList entries =
              perft_divide(position, depth);
            write_text(output, format_perft_divide(entries));
            write_text(output, "\n");
            continue;
        }

        if (command.name == "help") {
            if (!command.arguments.empty()) {
                write_no_argument_error(errors, "help");
                continue;
            }

            write_text(output, HELP_TEXT);
            continue;
        }

        if (command.name == "quit") {
            if (!command.arguments.empty()) {
                write_no_argument_error(errors, "quit");
                continue;
            }

            return EXIT_SUCCESS;
        }

        write_text(errors, "error: unknown command\n");
    }

    if (input.bad() || !input.eof()) {
        write_text(errors, "error: input failure\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

}  // namespace Mockingbird
