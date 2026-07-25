#include "notation.h"

#include "perft.h"

#include <array>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <limits>
#include <string_view>
#include <system_error>

namespace Mockingbird {

namespace {

struct Field {
    std::string_view text;
    std::size_t offset = 0;
};

using Fields = std::array<Field, 4>;

inline constexpr std::array<char, COLOR_NB> COLOR_CHARACTERS = {
  'r',
  'b',
  'y',
  'g',
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

[[nodiscard]] constexpr Color parse_color(
  char character) noexcept {
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        if (COLOR_CHARACTERS[std::size_t(color_index)]
            == character)
            return Color(color_index);
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
            == character)
            return PieceType(type_index);
    }

    return NO_PIECE_TYPE;
}

[[nodiscard]] constexpr char color_character(
  Color color) noexcept {
    assert(is_ok(color));
    return COLOR_CHARACTERS[std::size_t(color)];
}

[[nodiscard]] constexpr char piece_type_character(
  PieceType piece_type) noexcept {
    assert(is_ok(piece_type));
    return PIECE_TYPE_CHARACTERS[
      std::size_t(piece_type)];
}

[[nodiscard]] std::unexpected<NotationFailure> fail(
  NotationError error,
  std::size_t offset) noexcept {
    return std::unexpected(
      NotationFailure{error, offset});
}

[[nodiscard]] std::expected<Fields, NotationFailure>
split_fields(std::string_view notation) noexcept {
    Fields fields;
    std::size_t cursor = 0;

    for (Field& field : fields) {
        while (cursor < notation.size()
               && is_ascii_whitespace(notation[cursor]))
            ++cursor;

        if (cursor == notation.size())
            return fail(NotationError::FIELD_COUNT, cursor);

        const std::size_t start = cursor;
        while (cursor < notation.size()
               && !is_ascii_whitespace(notation[cursor]))
            ++cursor;

        field = {notation.substr(start, cursor - start), start};
    }

    while (cursor < notation.size()
           && is_ascii_whitespace(notation[cursor]))
        ++cursor;

    if (cursor != notation.size())
        return fail(NotationError::FIELD_COUNT, cursor);

    return fields;
}

[[nodiscard]] std::expected<unsigned, NotationFailure>
parse_empty_run(
  std::string_view rank,
  std::size_t& cursor,
  std::size_t absolute_offset) noexcept {
    const std::size_t start = cursor;
    while (cursor < rank.size()
           && is_ascii_digit(rank[cursor]))
        ++cursor;

    const std::string_view digits =
      rank.substr(start, cursor - start);
    if (digits.empty() || digits.front() == '0')
        return fail(
          NotationError::EMPTY_RUN,
          absolute_offset + start);

    unsigned value = 0;
    const auto result = std::from_chars(
      digits.data(),
      digits.data() + digits.size(),
      value);
    if (result.ec != std::errc{}
        || result.ptr != digits.data() + digits.size()
        || value == 0
        || value > unsigned(BOARD_FILES)) {
        return fail(
          NotationError::EMPTY_RUN,
          absolute_offset + start);
    }

    return value;
}

[[nodiscard]] std::expected<void, NotationFailure>
parse_rank(
  std::string_view text,
  std::size_t absolute_offset,
  Rank rank,
  Position& position) noexcept {
    const bool outer =
      rank <= RANK_3 || rank >= RANK_12;
    const File first_file = outer ? FILE_D : FILE_A;
    const unsigned width =
      outer ? 8U : unsigned(BOARD_FILES);

    std::size_t cursor = 0;
    unsigned used = 0;

    while (cursor < text.size()) {
        if (is_ascii_digit(text[cursor])) {
            const std::size_t run_offset = cursor;
            auto run = parse_empty_run(
              text, cursor, absolute_offset);
            if (!run)
                return std::unexpected(run.error());
            if (*run > width - used) {
                return fail(
                  NotationError::RANK_WIDTH,
                  absolute_offset + run_offset);
            }

            used += *run;
            continue;
        }

        const std::size_t piece_offset = cursor;
        const Color color = parse_color(text[cursor]);
        if (!is_ok(color)) {
            return fail(
              NotationError::PIECE,
              absolute_offset + piece_offset);
        }
        if (cursor + 1 >= text.size()) {
            return fail(
              NotationError::PIECE,
              absolute_offset + cursor + 1);
        }

        const PieceType piece_type =
          parse_piece_type(text[cursor + 1]);
        if (!is_ok(piece_type)) {
            return fail(
              NotationError::PIECE,
              absolute_offset + cursor + 1);
        }

        if (used == width) {
            return fail(
              NotationError::RANK_WIDTH,
              absolute_offset + piece_offset);
        }

        const File file = File(
          int(first_file) + static_cast<int>(used));
        const Square square = make_square(file, rank);
        assert(is_ok(square));
        position.put_piece(
          make_piece(color, piece_type), square);

        ++used;
        cursor += 2;
    }

    if (used != width) {
        return fail(
          NotationError::RANK_WIDTH,
          absolute_offset + text.size());
    }

    return {};
}

[[nodiscard]] std::expected<void, NotationFailure>
parse_board(Field field, Position& position) noexcept {
    std::size_t cursor = 0;

    for (int rank_index = RANK_14;
         rank_index >= RANK_1;
         --rank_index) {
        const std::size_t separator =
          field.text.find('/', cursor);
        const bool last_rank = rank_index == RANK_1;

        if (!last_rank
            && separator == std::string_view::npos) {
            return fail(
              NotationError::RANK_COUNT,
              field.offset + field.text.size());
        }
        if (last_rank
            && separator != std::string_view::npos) {
            return fail(
              NotationError::RANK_COUNT,
              field.offset + separator);
        }

        const std::size_t end =
          separator == std::string_view::npos
            ? field.text.size()
            : separator;
        const auto result = parse_rank(
          field.text.substr(cursor, end - cursor),
          field.offset + cursor,
          Rank(rank_index),
          position);
        if (!result)
            return result;

        cursor = end + (last_rank ? 0U : 1U);
    }

    return {};
}

[[nodiscard]] std::expected<Color, NotationFailure>
parse_side(Field field) noexcept {
    if (field.text.size() != 1)
        return fail(NotationError::SIDE, field.offset);

    const Color color = parse_color(field.text.front());
    if (!is_ok(color))
        return fail(NotationError::SIDE, field.offset);

    return color;
}

[[nodiscard]] std::expected<void, NotationFailure>
parse_castling(
  Field field,
  Position& position) noexcept {
    if (field.text == "-")
        return {};

    if (field.text.size() % 2 != 0) {
        return fail(
          NotationError::CASTLING,
          field.offset + field.text.size() - 1);
    }

    for (std::size_t cursor = 0;
         cursor < field.text.size();
         cursor += 2) {
        const Color color =
          parse_color(field.text[cursor]);
        if (!is_ok(color)) {
            return fail(
              NotationError::CASTLING,
              field.offset + cursor);
        }

        const char side_character =
          field.text[cursor + 1];
        CastlingSide side;
        if (side_character == 'K')
            side = CastlingSide::KING_SIDE;
        else if (side_character == 'Q')
            side = CastlingSide::QUEEN_SIDE;
        else {
            return fail(
              NotationError::CASTLING,
              field.offset + cursor + 1);
        }

        if (position.has_castling_right(color, side)) {
            return fail(
              NotationError::DUPLICATE_CASTLING,
              field.offset + cursor);
        }
        position.set_castling_right(color, side);
    }

    return {};
}

[[nodiscard]] std::expected<Square, NotationFailure>
parse_coordinate(
  std::string_view text,
  std::size_t absolute_offset) noexcept {
    if (text.size() < 2 || text.size() > 3)
        return fail(NotationError::EN_PASSANT, absolute_offset);

    const char file_character = text.front();
    if (file_character < 'a' || file_character > 'n') {
        return fail(NotationError::EN_PASSANT, absolute_offset);
    }

    const std::string_view rank_text = text.substr(1);
    if (rank_text.front() == '0') {
        return fail(
          NotationError::EN_PASSANT,
          absolute_offset + 1);
    }
    for (std::size_t index = 0;
         index < rank_text.size();
         ++index) {
        if (!is_ascii_digit(rank_text[index])) {
            return fail(
              NotationError::EN_PASSANT,
              absolute_offset + 1 + index);
        }
    }

    int rank_value = 0;
    const auto result = std::from_chars(
      rank_text.data(),
      rank_text.data() + rank_text.size(),
      rank_value);
    if (result.ec != std::errc{}
        || result.ptr
             != rank_text.data() + rank_text.size()
        || rank_value < RANK_1
        || rank_value > RANK_14) {
        return fail(
          NotationError::EN_PASSANT,
          absolute_offset + 1);
    }

    const File file =
      File(file_character - 'a' + FILE_A);
    const Square square =
      make_square(file, Rank(rank_value));
    if (!is_ok(square))
        return fail(NotationError::EN_PASSANT, absolute_offset);

    return square;
}

[[nodiscard]] std::expected<void, NotationFailure>
parse_en_passant(
  Field field,
  Position& position) noexcept {
    if (field.text == "-")
        return {};

    std::size_t cursor = 0;
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const std::size_t separator =
          field.text.find(',', cursor);
        const bool last_color =
          color_index == COLOR_NB - 1;

        if (!last_color
            && separator == std::string_view::npos) {
            return fail(
              NotationError::EN_PASSANT,
              field.offset + field.text.size());
        }
        if (last_color
            && separator != std::string_view::npos) {
            return fail(
              NotationError::EN_PASSANT,
              field.offset + separator);
        }

        const std::size_t end =
          separator == std::string_view::npos
            ? field.text.size()
            : separator;
        const std::string_view target =
          field.text.substr(cursor, end - cursor);
        if (target.empty()) {
            return fail(
              NotationError::EN_PASSANT,
              field.offset + cursor);
        }

        if (target != "-") {
            auto square = parse_coordinate(
              target, field.offset + cursor);
            if (!square)
                return std::unexpected(square.error());
            position.set_en_passant_square(
              Color(color_index), *square);
        }

        cursor = end + (last_color ? 0U : 1U);
    }

    return {};
}

template<typename Integer>
void append_decimal(
  std::string& output, Integer value) {
    char buffer[
      std::numeric_limits<Integer>::digits10 + 2];
    const auto result = std::to_chars(
      buffer, buffer + sizeof(buffer), value);
    assert(result.ec == std::errc{});
    output.append(
      buffer,
      static_cast<std::size_t>(result.ptr - buffer));
}

void append_square(
  std::string& output,
  Square square) {
    assert(is_ok(square));
    output += char(
      'a' + file_of(square) - FILE_A);
    append_decimal(output, int(rank_of(square)));
}

void append_move(
  std::string& output, Move move) {
    if (move.is_none()) {
        output += "none";
        return;
    }
    if (move.is_null()) {
        output += "null";
        return;
    }

    assert(is_ok(move));
    append_square(output, move.from());
    output += '-';
    append_square(output, move.to());

    if (move.is_promotion()) {
        output += '=';
        output +=
          piece_type_character(move.promotion_type());
    }

    switch (move.type()) {
        case MoveType::NORMAL:
        case MoveType::PROMOTION:
            break;

        case MoveType::CASTLING:
            output += " (castling)";
            break;

        case MoveType::EN_PASSANT:
            output += " (en passant)";
            break;

        case MoveType::COUNT:
            assert(false);
            break;
    }
}

}  // namespace

PositionParseResult parse_position(
  std::string_view notation) noexcept {
    auto fields = split_fields(notation);
    if (!fields)
        return std::unexpected(fields.error());

    Position position;

    const auto board =
      parse_board((*fields)[0], position);
    if (!board)
        return std::unexpected(board.error());

    const auto side = parse_side((*fields)[1]);
    if (!side)
        return std::unexpected(side.error());
    position.set_side_to_move(*side);

    const auto castling =
      parse_castling((*fields)[2], position);
    if (!castling)
        return std::unexpected(castling.error());

    const auto en_passant =
      parse_en_passant((*fields)[3], position);
    if (!en_passant)
        return std::unexpected(en_passant.error());

    return position;
}

std::string serialize_position(
  const Position& position) {
    std::string notation;
    notation.reserve(384);

    for (int rank_index = RANK_14;
         rank_index >= RANK_1;
         --rank_index) {
        if (rank_index != RANK_14)
            notation += '/';

        const Rank rank = Rank(rank_index);
        const bool outer =
          rank <= RANK_3 || rank >= RANK_12;
        const File first_file =
          outer ? FILE_D : FILE_A;
        const File last_file =
          outer ? FILE_K : FILE_N;
        int empty_count = 0;

        for (int file_index = first_file;
             file_index <= last_file;
             ++file_index) {
            const Square square =
              make_square(File(file_index), rank);
            if (position.empty(square)) {
                ++empty_count;
                continue;
            }

            if (empty_count != 0) {
                append_decimal(notation, empty_count);
                empty_count = 0;
            }

            const Piece piece = position.piece_on(square);
            notation += color_character(color_of(piece));
            notation += piece_type_character(type_of(piece));
        }

        if (empty_count != 0)
            append_decimal(notation, empty_count);
    }

    notation += ' ';
    notation += color_character(position.side_to_move());
    notation += ' ';

    bool has_castling = false;
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        for (std::size_t side_index = 0;
             side_index < CASTLING_SIDE_NB;
             ++side_index) {
            const CastlingSide side =
              static_cast<CastlingSide>(side_index);
            if (!position.has_castling_right(color, side))
                continue;

            notation += color_character(color);
            notation +=
              side == CastlingSide::KING_SIDE ? 'K' : 'Q';
            has_castling = true;
        }
    }
    if (!has_castling)
        notation += '-';

    notation += ' ';
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

    if (!has_en_passant) {
        notation += '-';
        return notation;
    }

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        if (color_index != 0)
            notation += ',';

        const Square target =
          position.en_passant_square(
            Color(color_index));
        if (target == SQ_NONE)
            notation += '-';
        else
            append_square(notation, target);
    }

    return notation;
}

std::string serialize_move(Move move) {
    std::string notation;
    notation.reserve(24);
    append_move(notation, move);
    return notation;
}

std::string format_perft_divide(
  const PerftList& entries) {
    std::string output;
    output.reserve(entries.size() * 45 + 27);

    std::uint64_t total = 0;
    bool total_overflow = false;
    for (const PerftEntry& entry : entries) {
        append_move(output, entry.move);
        output += ": ";
        append_decimal(output, entry.nodes);
        output += '\n';

        if (!total_overflow) {
            const std::uint64_t available =
              std::numeric_limits<std::uint64_t>::max()
              - total;
            if (entry.nodes > available)
                total_overflow = true;
            else
                total += entry.nodes;
        }
    }

    output += "Total: ";
    if (total_overflow)
        output += "overflow";
    else
        append_decimal(output, total);
    return output;
}

}  // namespace Mockingbird
