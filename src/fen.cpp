#include "fen.h"

#include <array>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace Mockingbird {

namespace {

struct Field {
    std::string_view text;
    std::size_t offset = 0;
};

struct TopFields {
    std::array<Field, 8> values{};
    std::size_t count = 0;
};

inline constexpr std::array<char, COLOR_NB>
  LOWER_COLOR_CHARACTERS = {
    'r',
    'b',
    'y',
    'g',
};

inline constexpr std::array<char, COLOR_NB>
  UPPER_COLOR_CHARACTERS = {
    'R',
    'B',
    'Y',
    'G',
};

inline constexpr std::array<char, PIECE_TYPE_NB>
  PIECE_TYPE_CHARACTERS = {
    '\0',
    'P',
    'N',
    'B',
    'R',
    'Q',
    'K',
};

[[nodiscard]] constexpr bool is_ascii_whitespace(
  char character) noexcept {
    return character == ' '
        || character == '\t'
        || character == '\n'
        || character == '\r'
        || character == '\f'
        || character == '\v';
}

[[nodiscard]] constexpr bool is_ascii_digit(
  char character) noexcept {
    return character >= '0' && character <= '9';
}

[[nodiscard]] constexpr Field trim_field(
  Field field) noexcept {
    while (!field.text.empty()
           && is_ascii_whitespace(field.text.front())) {
        field.text.remove_prefix(1);
        ++field.offset;
    }

    while (!field.text.empty()
           && is_ascii_whitespace(field.text.back()))
        field.text.remove_suffix(1);

    return field;
}

[[nodiscard]] std::unexpected<FenFailure> fail(
  FenError error,
  std::size_t offset) noexcept {
    return std::unexpected(FenFailure{error, offset});
}

[[nodiscard]] std::expected<TopFields, FenFailure>
split_top_fields(std::string_view fen) noexcept {
    TopFields result;
    std::size_t start = 0;

    while (true) {
        if (result.count == result.values.size()) {
            const std::size_t offset =
              start == 0 ? 0 : start - 1;
            return fail(FenError::FIELD_COUNT, offset);
        }

        const std::size_t separator =
          fen.find('-', start);
        const std::size_t end =
          separator == std::string_view::npos
            ? fen.size()
            : separator;

        result.values[result.count] = trim_field(
          Field{fen.substr(start, end - start), start});
        ++result.count;

        if (separator == std::string_view::npos)
            break;

        start = separator + 1;
    }

    if (result.count != 7 && result.count != 8)
        return fail(FenError::FIELD_COUNT, fen.size());

    return result;
}

template<std::size_t Count>
[[nodiscard]] std::expected<std::array<Field, Count>, FenFailure>
split_exact(
  Field field,
  char separator_character,
  FenError error) noexcept {
    std::array<Field, Count> result{};
    std::size_t start = 0;

    for (std::size_t index = 0;
         index < Count;
         ++index) {
        const std::size_t separator =
          field.text.find(separator_character, start);
        const bool last = index + 1 == Count;

        if (!last && separator == std::string_view::npos)
            return fail(error, field.offset + field.text.size());
        if (last && separator != std::string_view::npos)
            return fail(error, field.offset + separator);

        const std::size_t end =
          separator == std::string_view::npos
            ? field.text.size()
            : separator;
        result[index] = {
          field.text.substr(start, end - start),
          field.offset + start,
        };
        start = end + (last ? 0U : 1U);
    }

    return result;
}

[[nodiscard]] constexpr Color parse_upper_color(
  char character) noexcept {
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        if (UPPER_COLOR_CHARACTERS[
              std::size_t(color_index)]
            == character) {
            return Color(color_index);
        }
    }

    return COLOR_NB;
}

[[nodiscard]] constexpr Color parse_lower_color(
  char character) noexcept {
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        if (LOWER_COLOR_CHARACTERS[
              std::size_t(color_index)]
            == character) {
            return Color(color_index);
        }
    }

    return COLOR_NB;
}

[[nodiscard]] constexpr PieceType parse_piece_type(
  char character) noexcept {
    for (int type_index = PAWN;
         type_index <= KING;
         ++type_index) {
        if (PIECE_TYPE_CHARACTERS[
              std::size_t(type_index)]
            == character) {
            return PieceType(type_index);
        }
    }

    return NO_PIECE_TYPE;
}

[[nodiscard]] std::expected<Color, FenFailure>
parse_side(Field field) noexcept {
    if (field.text.size() != 1)
        return fail(FenError::SIDE, field.offset);

    const Color color =
      parse_upper_color(field.text.front());
    if (!is_ok(color))
        return fail(FenError::SIDE, field.offset);

    return color;
}

[[nodiscard]] std::expected<
  std::array<bool, COLOR_NB>,
  FenFailure>
parse_binary_flags(
  Field field,
  FenError error) noexcept {
    const auto fields =
      split_exact<COLOR_NB>(field, ',', error);
    if (!fields)
        return std::unexpected(fields.error());

    std::array<bool, COLOR_NB> flags{};
    for (std::size_t index = 0;
         index < fields->size();
         ++index) {
        const Field token = (*fields)[index];
        if (token.text == "0") {
            flags[index] = false;
        } else if (token.text == "1") {
            flags[index] = true;
        } else {
            return fail(error, token.offset);
        }
    }

    return flags;
}

[[nodiscard]] std::expected<std::uint64_t, FenFailure>
parse_unsigned_decimal(
  Field field,
  FenError error) noexcept {
    if (field.text.empty())
        return fail(error, field.offset);
    if (field.text.size() > 1
        && field.text.front() == '0') {
        return fail(error, field.offset);
    }

    for (std::size_t index = 0;
         index < field.text.size();
         ++index) {
        if (!is_ascii_digit(field.text[index]))
            return fail(error, field.offset + index);
    }

    std::uint64_t value = 0;
    const auto conversion = std::from_chars(
      field.text.data(),
      field.text.data() + field.text.size(),
      value);
    if (conversion.ec != std::errc{}
        || conversion.ptr
             != field.text.data() + field.text.size()) {
        return fail(error, field.offset);
    }

    return value;
}

[[nodiscard]] std::expected<void, FenFailure>
parse_dead_flags(Field field) noexcept {
    const auto flags =
      parse_binary_flags(field, FenError::DEAD_FLAGS);
    if (!flags)
        return std::unexpected(flags.error());

    const auto fields =
      split_exact<COLOR_NB>(
        field, ',', FenError::DEAD_FLAGS);
    assert(fields.has_value());

    for (std::size_t index = 0;
         index < flags->size();
         ++index) {
        if ((*flags)[index]) {
            return fail(
              FenError::UNSUPPORTED_DEAD_PLAYER,
              (*fields)[index].offset);
        }
    }

    return {};
}

[[nodiscard]] std::expected<void, FenFailure>
parse_points(Field field) noexcept {
    const auto fields =
      split_exact<COLOR_NB>(
        field, ',', FenError::POINTS);
    if (!fields)
        return std::unexpected(fields.error());

    for (const Field token : *fields) {
        const auto value =
          parse_unsigned_decimal(token, FenError::POINTS);
        if (!value)
            return std::unexpected(value.error());
    }

    return {};
}

[[nodiscard]] std::expected<unsigned, FenFailure>
parse_empty_run(Field field) noexcept {
    if (field.text.empty()
        || field.text.front() == '0') {
        return fail(FenError::EMPTY_RUN, field.offset);
    }

    for (std::size_t index = 0;
         index < field.text.size();
         ++index) {
        if (!is_ascii_digit(field.text[index])) {
            return fail(
              FenError::EMPTY_RUN,
              field.offset + index);
        }
    }

    unsigned value = 0;
    const auto conversion = std::from_chars(
      field.text.data(),
      field.text.data() + field.text.size(),
      value);
    if (conversion.ec != std::errc{}
        || conversion.ptr
             != field.text.data() + field.text.size()
        || value == 0) {
        return fail(FenError::EMPTY_RUN, field.offset);
    }

    return value;
}

[[nodiscard]] std::expected<void, FenFailure>
parse_rank(
  Field field,
  Rank rank,
  Position& position) noexcept {
    std::size_t cursor = 0;
    int file_index = FILE_A;

    while (cursor <= field.text.size()) {
        const std::size_t separator =
          field.text.find(',', cursor);
        const std::size_t end =
          separator == std::string_view::npos
            ? field.text.size()
            : separator;
        const Field token = {
          field.text.substr(cursor, end - cursor),
          field.offset + cursor,
        };

        if (token.text.empty())
            return fail(FenError::RANK_TOKEN, token.offset);
        if (file_index > FILE_N)
            return fail(FenError::RANK_WIDTH, token.offset);

        const File file = File(file_index);
        const Square square = make_square(file, rank);

        if (token.text == "x") {
            if (is_ok(square))
                return fail(FenError::CUTOUT, token.offset);
            ++file_index;
        } else if (!is_ok(square)) {
            return fail(FenError::CUTOUT, token.offset);
        } else if (is_ascii_digit(token.text.front())) {
            const auto run = parse_empty_run(token);
            if (!run)
                return std::unexpected(run.error());

            if (*run
                > static_cast<unsigned>(
                    FILE_N - file_index + 1)) {
                return fail(
                  FenError::RANK_WIDTH,
                  token.offset);
            }

            for (unsigned step = 0;
                 step < *run;
                 ++step) {
                const File empty_file = File(
                  file_index + static_cast<int>(step));
                if (!is_ok(make_square(empty_file, rank))) {
                    return fail(
                      FenError::CUTOUT,
                      token.offset);
                }
            }

            file_index += static_cast<int>(*run);
        } else {
            if (token.text.front() == 'x')
                return fail(FenError::RANK_TOKEN, token.offset);
            if (token.text.size() != 2)
                return fail(FenError::PIECE, token.offset);

            const Color color =
              parse_lower_color(token.text.front());
            if (!is_ok(color))
                return fail(FenError::PIECE, token.offset);

            const PieceType piece_type =
              parse_piece_type(token.text[1]);
            if (!is_ok(piece_type)) {
                return fail(
                  FenError::PIECE,
                  token.offset + 1);
            }

            position.put_piece(
              make_piece(color, piece_type), square);
            ++file_index;
        }

        if (separator == std::string_view::npos)
            break;

        cursor = separator + 1;
    }

    if (file_index != FILE_N + 1) {
        return fail(
          FenError::RANK_WIDTH,
          field.offset + field.text.size());
    }

    return {};
}

[[nodiscard]] std::expected<void, FenFailure>
parse_board(
  Field field,
  Position& position) noexcept {
    const auto ranks =
      split_exact<BOARD_RANKS>(
        field, '/', FenError::RANK_COUNT);
    if (!ranks)
        return std::unexpected(ranks.error());

    for (std::size_t index = 0;
         index < ranks->size();
         ++index) {
        const Rank rank = Rank(
          RANK_14 - static_cast<int>(index));
        const auto result =
          parse_rank((*ranks)[index], rank, position);
        if (!result)
            return result;
    }

    return {};
}

[[nodiscard]] std::expected<Square, FenFailure>
parse_fen_square(Field field) noexcept {
    if (field.text.size() < 2
        || field.text.size() > 3) {
        return fail(
          FenError::EN_PASSANT_COORDINATE,
          field.offset);
    }

    const char file_character = field.text.front();
    if (file_character < 'a'
        || file_character > 'n') {
        return fail(
          FenError::EN_PASSANT_COORDINATE,
          field.offset);
    }

    const Field rank_field = {
      field.text.substr(1),
      field.offset + 1,
    };
    const auto rank =
      parse_unsigned_decimal(
        rank_field,
        FenError::EN_PASSANT_COORDINATE);
    if (!rank
        || *rank < std::uint64_t(RANK_1)
        || *rank > std::uint64_t(RANK_14)) {
        if (!rank)
            return std::unexpected(rank.error());
        return fail(
          FenError::EN_PASSANT_COORDINATE,
          rank_field.offset);
    }

    const File file =
      File(file_character - 'a' + FILE_A);
    const Square square =
      make_square(file, Rank(static_cast<int>(*rank)));
    if (!is_ok(square)) {
        return fail(
          FenError::EN_PASSANT_COORDINATE,
          field.offset);
    }

    return square;
}

[[nodiscard]] std::expected<void, FenFailure>
parse_en_passant(
  Field field,
  Position& position) noexcept {
    constexpr std::string_view PREFIX =
      "{'enPassant':(";
    constexpr std::string_view SUFFIX = ")}";

    if (!field.text.starts_with(PREFIX)
        || !field.text.ends_with(SUFFIX)
        || field.text.size()
             < PREFIX.size() + SUFFIX.size()) {
        return fail(FenError::EN_PASSANT, field.offset);
    }

    const Field entries_field = {
      field.text.substr(
        PREFIX.size(),
        field.text.size()
          - PREFIX.size()
          - SUFFIX.size()),
      field.offset + PREFIX.size(),
    };
    const auto entries =
      split_exact<COLOR_NB>(
        entries_field, ',', FenError::EN_PASSANT);
    if (!entries)
        return std::unexpected(entries.error());

    for (std::size_t index = 0;
         index < entries->size();
         ++index) {
        const Field entry = (*entries)[index];
        if (entry.text == "''")
            continue;

        if (entry.text.size() < 5
            || entry.text.front() != '\''
            || entry.text.back() != '\'') {
            return fail(FenError::EN_PASSANT, entry.offset);
        }

        const Field pair = {
          entry.text.substr(1, entry.text.size() - 2),
          entry.offset + 1,
        };
        const std::size_t colon = pair.text.find(':');
        if (colon == std::string_view::npos
            || pair.text.find(':', colon + 1)
                 != std::string_view::npos) {
            return fail(
              FenError::EN_PASSANT,
              pair.offset);
        }

        const Field target_field = {
          pair.text.substr(0, colon),
          pair.offset,
        };
        const Field victim_field = {
          pair.text.substr(colon + 1),
          pair.offset + colon + 1,
        };
        const auto target =
          parse_fen_square(target_field);
        if (!target)
            return std::unexpected(target.error());

        const auto victim =
          parse_fen_square(victim_field);
        if (!victim)
            return std::unexpected(victim.error());

        const Color color = Color(index);
        const Square expected_victim =
          *target + pawn_push(color);
        if (!is_ok(expected_victim)
            || *victim != expected_victim) {
            return fail(
              FenError::EN_PASSANT_VICTIM,
              victim_field.offset);
        }

        position.set_en_passant_square(color, *target);
    }

    return {};
}

[[nodiscard]] constexpr char upper_color_character(
  Color color) noexcept {
    assert(is_ok(color));
    return UPPER_COLOR_CHARACTERS[std::size_t(color)];
}

[[nodiscard]] constexpr char lower_color_character(
  Color color) noexcept {
    assert(is_ok(color));
    return LOWER_COLOR_CHARACTERS[std::size_t(color)];
}

[[nodiscard]] constexpr char piece_type_character(
  PieceType piece_type) noexcept {
    assert(is_ok(piece_type));
    return PIECE_TYPE_CHARACTERS[
      std::size_t(piece_type)];
}

void append_unsigned(
  std::string& output,
  unsigned value) {
    std::array<char, 10> buffer{};
    const auto conversion = std::to_chars(
      buffer.data(),
      buffer.data() + buffer.size(),
      value);
    assert(conversion.ec == std::errc{});
    output.append(buffer.data(), conversion.ptr);
}

void append_square(
  std::string& output,
  Square square) {
    assert(is_board_coordinate(square));
    output += static_cast<char>(
      'a' + int(file_of(square)) - int(FILE_A));
    append_unsigned(
      output,
      static_cast<unsigned>(rank_of(square)));
}

void append_comma(
  std::string& output,
  bool& first_token) {
    if (!first_token)
        output += ',';
    first_token = false;
}

void append_board(
  std::string& output,
  const Position& position) {
    for (int rank_index = RANK_14;
         rank_index >= RANK_1;
         --rank_index) {
        if (rank_index != RANK_14)
            output += '/';

        bool first_token = true;
        unsigned empty_run = 0;

        const auto flush_empty_run = [&] {
            if (empty_run == 0)
                return;

            append_comma(output, first_token);
            append_unsigned(output, empty_run);
            empty_run = 0;
        };

        for (int file_index = FILE_A;
             file_index <= FILE_N;
             ++file_index) {
            const Square square =
              make_square(
                File(file_index), Rank(rank_index));

            if (!is_ok(square)) {
                flush_empty_run();
                append_comma(output, first_token);
                output += 'x';
                continue;
            }

            const Piece piece = position.piece_on(square);
            if (piece == NO_PIECE) {
                ++empty_run;
                continue;
            }

            flush_empty_run();
            append_comma(output, first_token);
            output += lower_color_character(
              color_of(piece));
            output += piece_type_character(
              type_of(piece));
        }

        flush_empty_run();
    }
}

}  // namespace

FenParseResult parse_fen(
  std::string_view fen) noexcept {
    const auto fields = split_top_fields(fen);
    if (!fields)
        return std::unexpected(fields.error());

    const auto side = parse_side(fields->values[0]);
    if (!side)
        return std::unexpected(side.error());

    const auto dead =
      parse_dead_flags(fields->values[1]);
    if (!dead)
        return std::unexpected(dead.error());

    const auto kingside = parse_binary_flags(
      fields->values[2],
      FenError::KINGSIDE_FLAGS);
    if (!kingside)
        return std::unexpected(kingside.error());

    const auto queenside = parse_binary_flags(
      fields->values[3],
      FenError::QUEENSIDE_FLAGS);
    if (!queenside)
        return std::unexpected(queenside.error());

    const auto points =
      parse_points(fields->values[4]);
    if (!points)
        return std::unexpected(points.error());

    const auto halfmove =
      parse_unsigned_decimal(
        fields->values[5],
        FenError::HALFMOVE);
    if (!halfmove)
        return std::unexpected(halfmove.error());

    Position position;
    position.set_side_to_move(*side);

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        if ((*kingside)[std::size_t(color)]) {
            position.set_castling_right(
              color, CastlingSide::KING_SIDE);
        }
        if ((*queenside)[std::size_t(color)]) {
            position.set_castling_right(
              color, CastlingSide::QUEEN_SIDE);
        }
    }

    const bool has_en_passant = fields->count == 8;
    if (has_en_passant) {
        const auto en_passant =
          parse_en_passant(
            fields->values[6], position);
        if (!en_passant)
            return std::unexpected(en_passant.error());
    }

    const std::size_t board_index =
      has_en_passant ? 7U : 6U;
    const auto board =
      parse_board(
        fields->values[board_index], position);
    if (!board)
        return std::unexpected(board.error());

    assert(position.key() == position.recompute_key());
    return position;
}

std::string serialize_fen(
  const Position& position) {
    std::string output;
    output.reserve(512);

    output += upper_color_character(
      position.side_to_move());
    output += "-0,0,0,0-";

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        if (color_index != 0)
            output += ',';
        output += position.has_castling_right(
          Color(color_index),
          CastlingSide::KING_SIDE)
          ? '1'
          : '0';
    }

    output += '-';
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        if (color_index != 0)
            output += ',';
        output += position.has_castling_right(
          Color(color_index),
          CastlingSide::QUEEN_SIDE)
          ? '1'
          : '0';
    }

    output += "-0,0,0,0-0-";

    bool has_en_passant = false;
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        if (position.en_passant_square(
              Color(color_index))
            != SQ_NONE) {
            has_en_passant = true;
            break;
        }
    }

    if (has_en_passant) {
        output += "{'enPassant':(";
        for (int color_index = 0;
             color_index < COLOR_NB;
             ++color_index) {
            if (color_index != 0)
                output += ',';

            const Color color = Color(color_index);
            const Square target =
              position.en_passant_square(color);
            if (target == SQ_NONE) {
                output += "''";
                continue;
            }

            const Square victim =
              target + pawn_push(color);
            assert(is_ok(victim));

            output += '\'';
            append_square(output, target);
            output += ':';
            append_square(output, victim);
            output += '\'';
        }
        output += ")}-";
    }

    append_board(output, position);
    return output;
}

}  // namespace Mockingbird
