#include "notation.h"
#include "perft.h"
#include "setup.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

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

inline constexpr std::array<Color, COLOR_NB>
  ROTATION_COLORS = {
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

inline constexpr std::string_view EMPTY_BOARD =
  "8/8/8/14/14/14/14/14/14/14/14/8/8/8";
inline constexpr std::string_view EMPTY_NOTATION =
  "8/8/8/14/14/14/14/14/14/14/14/8/8/8 r - -";

[[nodiscard]] constexpr bool positions_equal(
  const Position& left,
  const Position& right) noexcept {
    if (left.side_to_move() != right.side_to_move()
        || left.occupied() != right.occupied())
        return false;

    for (int square_index = 0;
         square_index < SQUARE_NB;
         ++square_index) {
        const Square square = Square(square_index);
        if (is_ok(square)
            && left.piece_on(square)
                 != right.piece_on(square))
            return false;
    }

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        if (left.pieces(color) != right.pieces(color)
            || left.en_passant_square(color)
                 != right.en_passant_square(color))
            return false;

        for (const CastlingSide side : CASTLING_SIDES) {
            if (left.has_castling_right(color, side)
                != right.has_castling_right(color, side))
                return false;
        }
    }

    for (int type_index = PAWN;
         type_index <= KING;
         ++type_index) {
        const PieceType piece_type =
          PieceType(type_index);
        if (left.pieces(piece_type)
              != right.pieces(piece_type))
            return false;

        for (int color_index = 0;
             color_index < COLOR_NB;
             ++color_index) {
            const Color color = Color(color_index);
            if (left.pieces(color, piece_type)
                  != right.pieces(color, piece_type))
                return false;
        }
    }

    return true;
}

[[nodiscard]] constexpr bool perft_lists_equal(
  const PerftList& left,
  const PerftList& right) noexcept {
    if (left.size() != right.size())
        return false;

    for (std::size_t index = 0;
         index < left.size();
         ++index) {
        if (left[index] != right[index])
            return false;
    }

    return true;
}

[[nodiscard]] constexpr std::uint64_t perft_sum(
  const PerftList& entries) noexcept {
    std::uint64_t nodes = 0;
    for (const PerftEntry& entry : entries)
        nodes += entry.nodes;

    return nodes;
}

[[nodiscard]] std::string reference_square(
  Square square) {
    std::string result;
    result += char(
      'a' + int(file_of(square)) - int(FILE_A));
    result += std::to_string(int(rank_of(square)));
    return result;
}

[[nodiscard]] std::string make_notation(
  std::string_view board,
  std::string_view side = "r",
  std::string_view castling = "-",
  std::string_view en_passant = "-") {
    std::string text(board);
    text += ' ';
    text += side;
    text += ' ';
    text += castling;
    text += ' ';
    text += en_passant;
    return text;
}

void expect_parse_failure(
  std::string_view text,
  NotationError expected_error,
  std::string_view message) {
    const auto parsed = parse_position(text);
    const bool matches =
      !parsed
      && parsed.error().code == expected_error
      && parsed.error().offset <= text.size();
    expect(matches, message);
}

void expect_parse_failure_at(
  std::string_view text,
  NotationError expected_error,
  std::size_t expected_offset,
  std::string_view message) {
    const auto parsed = parse_position(text);
    const bool matches =
      !parsed
      && parsed.error().code == expected_error
      && parsed.error().offset == expected_offset;
    expect(matches, message);
}

void expect_round_trip(
  const Position& position,
  std::string_view message) {
    const std::string serialized =
      serialize_position(position);
    const auto parsed =
      parse_position(serialized);

    if (!parsed) {
        expect(false, message);
        return;
    }

    expect(positions_equal(*parsed, position), message);
    expect(serialize_position(*parsed) == serialized,
           "round-trip serialization is deterministic");
}

void expect_move_failure_at(
  const Position& position,
  std::string_view text,
  MoveNotationError expected_error,
  std::size_t expected_offset,
  std::string_view message) {
    const Position original = position;
    const PositionKey original_key = position.key();
    const MoveParseResult parsed =
      parse_move(position, text);

    expect(
      !parsed
        && parsed.error().code == expected_error
        && parsed.error().offset == expected_offset,
      message);
    expect(
      positions_equal(position, original)
        && position.key() == original_key
        && position.key() == position.recompute_key(),
      "failed move parsing preserves the complete position");
}

[[nodiscard]] std::string uppercase_ascii(
  std::string text) {
    for (char& character : text) {
        if (character >= 'a' && character <= 'z') {
            character =
              static_cast<char>(
                character - 'a' + 'A');
        }
    }

    return text;
}

[[nodiscard]] constexpr Square rotate_clockwise(
  Square square) noexcept {
    return make_square(
      File(int(rank_of(square))),
      Rank(BOARD_FILES + 1 - int(file_of(square))));
}

[[nodiscard]] constexpr Position rotate_clockwise(
  const Position& position) noexcept {
    Position rotated;
    rotated.set_side_to_move(
      next_color(position.side_to_move()));

    for (int square_index = 0;
         square_index < SQUARE_NB;
         ++square_index) {
        const Square square = Square(square_index);
        if (!is_ok(square) || position.empty(square))
            continue;

        const Piece piece = position.piece_on(square);
        rotated.put_piece(
          make_piece(
            next_color(color_of(piece)),
            type_of(piece)),
          rotate_clockwise(square));
    }

    for (const Color color : ROTATION_COLORS) {
        const Color rotated_color = next_color(color);

        for (const CastlingSide side : CASTLING_SIDES) {
            if (position.has_castling_right(color, side)) {
                rotated.set_castling_right(
                  rotated_color, side);
            }
        }

        const Square target =
          position.en_passant_square(color);
        if (target != SQ_NONE) {
            rotated.set_en_passant_square(
              rotated_color,
              rotate_clockwise(target));
        }
    }

    return rotated;
}

[[nodiscard]] constexpr Position
special_move_position() noexcept {
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
en_passant_promotion_position() noexcept {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_13));
    position.put_piece(
      G_KING, make_square(FILE_K, RANK_8));
    position.put_piece(
      R_PAWN, make_square(FILE_B, RANK_10));
    position.put_piece(
      B_PAWN, make_square(FILE_D, RANK_11));
    position.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_11));
    return position;
}

[[nodiscard]] constexpr Position
boundary_rook_position(Square source) noexcept {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_13));
    position.put_piece(
      G_KING, make_square(FILE_K, RANK_8));
    position.put_piece(R_ROOK, source);
    return position;
}

[[nodiscard]] Position edge_position() {
    Position position;
    position.set_side_to_move(GREEN);

    position.put_piece(
      R_QUEEN, make_square(FILE_D, RANK_14));
    position.put_piece(
      G_KNIGHT, make_square(FILE_K, RANK_14));
    position.put_piece(
      B_BISHOP, make_square(FILE_A, RANK_11));
    position.put_piece(
      Y_ROOK, make_square(FILE_N, RANK_11));
    position.put_piece(
      G_PAWN, make_square(FILE_A, RANK_4));
    position.put_piece(
      R_KING, make_square(FILE_N, RANK_4));
    position.put_piece(
      B_QUEEN, make_square(FILE_D, RANK_1));
    position.put_piece(
      Y_KING, make_square(FILE_K, RANK_1));

    position.set_castling_right(
      RED, CastlingSide::KING_SIDE);
    position.set_castling_right(
      BLUE, CastlingSide::KING_SIDE);
    position.set_castling_right(
      YELLOW, CastlingSide::QUEEN_SIDE);
    position.set_castling_right(
      GREEN, CastlingSide::QUEEN_SIDE);

    position.set_en_passant_square(
      RED, make_square(FILE_D, RANK_4));
    position.set_en_passant_square(
      YELLOW, make_square(FILE_K, RANK_10));
    position.set_en_passant_square(
      GREEN, make_square(FILE_N, RANK_11));
    return position;
}

[[nodiscard]] Position all_piece_position() {
    Position position;
    position.set_side_to_move(YELLOW);

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);

        for (int type_index = PAWN;
             type_index <= KING;
             ++type_index) {
            const int piece_index =
              color_index * 6 + type_index - PAWN;
            const File file =
              File(int(FILE_D) + piece_index % 8);
            const Rank rank =
              Rank(int(RANK_4) + piece_index / 8);
            position.put_piece(
              make_piece(color, PieceType(type_index)),
              make_square(file, rank));
        }
    }

    position.set_castling_right(
      RED, CastlingSide::QUEEN_SIDE);
    position.set_castling_right(
      BLUE, CastlingSide::KING_SIDE);
    position.set_castling_right(
      GREEN, CastlingSide::KING_SIDE);
    position.set_castling_right(
      GREEN, CastlingSide::QUEEN_SIDE);

    position.set_en_passant_square(
      RED, make_square(FILE_D, RANK_4));
    position.set_en_passant_square(
      BLUE, make_square(FILE_K, RANK_10));
    position.set_en_passant_square(
      YELLOW, make_square(FILE_N, RANK_11));
    position.set_en_passant_square(
      GREEN, make_square(FILE_E, RANK_14));
    return position;
}

void test_empty_canonical_notation() {
    const Position empty;
    expect(serialize_position(empty) == EMPTY_NOTATION,
           "empty position has the canonical notation");

    const auto parsed = parse_position(EMPTY_NOTATION);
    expect(parsed.has_value(),
           "canonical empty notation parses");
    if (parsed) {
        expect(positions_equal(*parsed, empty),
               "canonical empty notation represents an empty position");
    }
}

void test_independent_edge_notation() {
    constexpr std::string_view EDGE_NOTATION =
      "rQ6gN/8/8/bB12yR/14/14/14/14/14/14/"
      "gP12rK/8/8/bQ6yK g rKbKyQgQ d4,-,k10,n11";

    const Position expected = edge_position();
    const auto parsed = parse_position(EDGE_NOTATION);

    expect(parsed.has_value(),
           "hand-authored edge notation parses");
    if (parsed) {
        expect(positions_equal(*parsed, expected),
               "hand-authored notation maps ranks and files exactly");
    }
    expect(serialize_position(expected) == EDGE_NOTATION,
           "edge position serializes to the exact canonical notation");
}

void test_starting_and_rich_round_trips() {
    expect_round_trip(
      make_starting_position(),
      "starting position round-trips exactly");
    expect_round_trip(
      all_piece_position(),
      "position containing every color and piece type round-trips exactly");
}

void test_every_piece_token() {
    constexpr std::array<Color, COLOR_NB> COLORS = {
      RED,
      BLUE,
      YELLOW,
      GREEN,
    };
    constexpr std::array<char, COLOR_NB> COLOR_CHARS = {
      'r',
      'b',
      'y',
      'g',
    };
    constexpr std::array<PieceType, 6> PIECE_TYPES = {
      PAWN,
      KNIGHT,
      BISHOP,
      ROOK,
      QUEEN,
      KING,
    };
    constexpr std::array<char, 6> PIECE_CHARS = {
      'P',
      'N',
      'B',
      'R',
      'Q',
      'K',
    };
    constexpr Square h8 =
      make_square(FILE_H, RANK_8);

    for (std::size_t color_index = 0;
         color_index < COLORS.size();
         ++color_index) {
        for (std::size_t type_index = 0;
             type_index < PIECE_TYPES.size();
             ++type_index) {
            std::string token;
            token += COLOR_CHARS[color_index];
            token += PIECE_CHARS[type_index];

            const std::string board =
              "8/8/8/14/14/14/7" + token
              + "6/14/14/14/14/8/8/8";
            const std::string text =
              make_notation(board);
            const auto parsed = parse_position(text);

            const bool exact =
              parsed
              && parsed->occupied().popcount() == 1
              && parsed->piece_on(h8)
                   == make_piece(
                     COLORS[color_index],
                     PIECE_TYPES[type_index]);
            expect(exact,
                   "piece token maps to its exact color and type");
            if (parsed) {
                expect(serialize_position(*parsed) == text,
                       "piece token serializes canonically");
            }
        }
    }
}

void test_side_values() {
    constexpr std::array<Color, COLOR_NB> COLORS = {
      RED,
      BLUE,
      YELLOW,
      GREEN,
    };
    constexpr std::array<std::string_view, COLOR_NB> TOKENS = {
      "r",
      "b",
      "y",
      "g",
    };

    for (std::size_t index = 0;
         index < COLORS.size();
         ++index) {
        Position position;
        position.set_side_to_move(COLORS[index]);
        const std::string expected =
          make_notation(EMPTY_BOARD, TOKENS[index]);

        expect(serialize_position(position) == expected,
               "side serializes to its canonical token");
        const auto parsed = parse_position(expected);
        expect(parsed
                 && parsed->side_to_move() == COLORS[index],
               "side token parses to its exact color");
    }
}

void test_castling_subsets_and_order() {
    constexpr std::array<std::string_view, 8> TOKENS = {
      "rK",
      "rQ",
      "bK",
      "bQ",
      "yK",
      "yQ",
      "gK",
      "gQ",
    };

    for (unsigned mask = 0; mask < 256; ++mask) {
        Position position;
        std::string expected_field;

        for (std::size_t bit = 0;
             bit < TOKENS.size();
             ++bit) {
            if ((mask & (1U << bit)) == 0)
                continue;

            const Color color =
              Color(int(bit / CASTLING_SIDE_NB));
            const CastlingSide side =
              static_cast<CastlingSide>(
                bit % CASTLING_SIDE_NB);
            position.set_castling_right(color, side);
            expected_field += TOKENS[bit];
        }

        if (expected_field.empty())
            expected_field = "-";

        const std::string expected =
          make_notation(
            EMPTY_BOARD, "r", expected_field);
        expect(serialize_position(position) == expected,
               "castling subset uses canonical token order");

        const auto parsed = parse_position(expected);
        expect(parsed
                 && positions_equal(*parsed, position),
               "castling subset parses exactly");
    }

    const std::string unordered =
      make_notation(
        EMPTY_BOARD, "r", "gQrQbKrK");
    const auto parsed = parse_position(unordered);
    expect(parsed.has_value(),
           "castling tokens may be parsed in noncanonical order");
    if (parsed) {
        expect(
          serialize_position(*parsed)
            == make_notation(
                 EMPTY_BOARD, "r", "rKrQbKgQ"),
          "unordered castling tokens serialize canonically");
    }
}

void test_en_passant_slots_and_squares() {
    constexpr std::array<Square, COLOR_NB> TARGETS = {
      make_square(FILE_D, RANK_4),
      make_square(FILE_K, RANK_10),
      make_square(FILE_N, RANK_11),
      make_square(FILE_E, RANK_14),
    };

    Position combined;
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        combined.set_en_passant_square(
          Color(color_index),
          TARGETS[std::size_t(color_index)]);
    }

    constexpr std::string_view COMBINED_FIELD =
      "d4,k10,n11,e14";
    const std::string combined_notation =
      make_notation(
        EMPTY_BOARD, "r", "-", COMBINED_FIELD);
    expect(
      serialize_position(combined) == combined_notation,
      "all en-passant slots serialize in Red, Blue, Yellow, Green order");
    const auto combined_parsed =
      parse_position(combined_notation);
    expect(combined_parsed
             && positions_equal(*combined_parsed, combined),
           "all en-passant slots parse exactly");

    const std::string expanded_absent =
      make_notation(
        EMPTY_BOARD, "r", "-", "-,-,-,-");
    const auto absent_parsed =
      parse_position(expanded_absent);
    expect(absent_parsed
             && positions_equal(*absent_parsed, Position{}),
           "four absent en-passant slots parse");
    if (absent_parsed) {
        expect(serialize_position(*absent_parsed)
                 == EMPTY_NOTATION,
               "four absent en-passant slots serialize as one hyphen");
    }

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);

        for (int square_index = 0;
             square_index < SQUARE_NB;
             ++square_index) {
            const Square square = Square(square_index);
            if (!is_ok(square))
                continue;

            Position position;
            position.set_en_passant_square(color, square);
            const std::string serialized =
              serialize_position(position);
            const auto parsed =
              parse_position(serialized);
            expect(
              parsed
                && positions_equal(*parsed, position),
              "every playable square round-trips in every en-passant slot");
        }
    }
}

void test_publicly_representable_game_states() {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_D, RANK_4));
    position.put_piece(
      R_KING, make_square(FILE_E, RANK_4));
    position.put_piece(
      B_PAWN, make_square(FILE_K, RANK_11));
    position.set_side_to_move(BLUE);
    position.set_castling_right(
      GREEN, CastlingSide::QUEEN_SIDE);
    position.set_en_passant_square(
      YELLOW, make_square(FILE_H, RANK_8));

    expect_round_trip(
      position,
      "publicly representable terminal state round-trips");
}

void test_ascii_whitespace_policy() {
    constexpr std::array<std::string_view, 6>
      ASCII_WHITESPACE = {
        " ",
        "\t",
        "\n",
        "\v",
        "\f",
        "\r",
    };
    const Position empty;

    for (const std::string_view whitespace :
         ASCII_WHITESPACE) {
        std::string text(whitespace);
        text += EMPTY_BOARD;
        text += whitespace;
        text += "r";
        text += whitespace;
        text += "-";
        text += whitespace;
        text += "-";
        text += whitespace;

        const auto parsed = parse_position(text);
        expect(parsed
                 && positions_equal(*parsed, empty),
               "each ASCII whitespace byte separates fields");
        if (parsed) {
            expect(serialize_position(*parsed)
                     == EMPTY_NOTATION,
                   "accepted whitespace serializes as single spaces");
        }
    }

    const std::string mixed =
      "\t \n" + std::string(EMPTY_BOARD)
      + "\r\n r\v-\f-\t ";
    const auto mixed_parsed = parse_position(mixed);
    expect(mixed_parsed
             && positions_equal(*mixed_parsed, empty),
           "mixed repeated ASCII whitespace is accepted");
}

void test_malformed_field_and_board_inputs() {
    expect_parse_failure(
      "", NotationError::FIELD_COUNT,
      "empty input has an invalid field count");
    expect_parse_failure(
      EMPTY_BOARD, NotationError::FIELD_COUNT,
      "board-only input has an invalid field count");
    expect_parse_failure(
      std::string(EMPTY_BOARD) + " r -",
      NotationError::FIELD_COUNT,
      "three fields are rejected");
    expect_parse_failure(
      std::string(EMPTY_NOTATION) + " extra",
      NotationError::FIELD_COUNT,
      "five fields are rejected");

    expect_parse_failure(
      make_notation(
        "8/8/8/14/14/14/14/14/14/14/14/8/8"),
      NotationError::RANK_COUNT,
      "thirteen board ranks are rejected");
    expect_parse_failure(
      make_notation(
        "8/8/8/14/14/14/14/14/14/14/14/8/8/8/8"),
      NotationError::RANK_COUNT,
      "fifteen board ranks are rejected");
    expect_parse_failure(
      make_notation(
        "8/8/8/14/14/14/14//14/14/14/8/8/8"),
      NotationError::RANK_WIDTH,
      "an empty rank is rejected");

    expect_parse_failure(
      make_notation(
        "7/8/8/14/14/14/14/14/14/14/14/8/8/8"),
      NotationError::RANK_WIDTH,
      "an undersized outer rank is rejected");
    expect_parse_failure(
      make_notation(
        "9/8/8/14/14/14/14/14/14/14/14/8/8/8"),
      NotationError::RANK_WIDTH,
      "an oversized outer rank is rejected");
    expect_parse_failure(
      make_notation(
        "8/8/8/13/14/14/14/14/14/14/14/8/8/8"),
      NotationError::RANK_WIDTH,
      "an undersized middle rank is rejected");
    expect_parse_failure(
      make_notation(
        "8/8/8/14rK/14/14/14/14/14/14/14/8/8/8"),
      NotationError::RANK_WIDTH,
      "an oversized middle rank is rejected");
    expect_parse_failure(
      make_notation(
        "0rK7/8/8/14/14/14/14/14/14/14/14/8/8/8"),
      NotationError::EMPTY_RUN,
      "a zero-length empty run is rejected");
    expect_parse_failure(
      make_notation(
        "999999999999999999999/8/8/14/14/14/14/"
        "14/14/14/14/8/8/8"),
      NotationError::EMPTY_RUN,
      "an overflowing empty run is rejected");

    const std::string board_error =
      make_notation(
        "xK7/8/8/14/14/14/14/14/14/14/14/8/8/8");
    expect_parse_failure_at(
      board_error,
      NotationError::PIECE,
      0,
      "board failure reports the first invalid piece byte");

    const std::string piece_type_error =
      make_notation(
        "rX7/8/8/14/14/14/14/14/14/14/14/8/8/8");
    expect_parse_failure_at(
      piece_type_error,
      NotationError::PIECE,
      1,
      "piece failure reports the invalid type byte");
}

void test_malformed_piece_side_and_castling_inputs() {
    constexpr std::array<std::string_view, 5>
      INVALID_PIECE_RANKS = {
        "xK7",
        "rX7",
        "rk7",
        "RK7",
        "r7",
    };

    for (const std::string_view rank :
         INVALID_PIECE_RANKS) {
        const std::string board =
          std::string(rank)
          + "/8/8/14/14/14/14/14/14/14/14/8/8/8";
        expect_parse_failure(
          make_notation(board),
          NotationError::PIECE,
          "invalid piece token is rejected");
    }

    for (const std::string_view side :
         std::array<std::string_view, 4>{
           "x",
           "R",
           "rr",
           "-",
         }) {
        expect_parse_failure(
          make_notation(EMPTY_BOARD, side),
          NotationError::SIDE,
          "invalid side token is rejected");
    }

    const std::string side_error =
      make_notation(EMPTY_BOARD, "R");
    expect_parse_failure_at(
      side_error,
      NotationError::SIDE,
      EMPTY_BOARD.size() + 1,
      "side failure reports the side-field byte");

    const std::string duplicate_castling =
      make_notation(
        EMPTY_BOARD, "r", "rKrK");
    expect_parse_failure(
      duplicate_castling,
      NotationError::DUPLICATE_CASTLING,
      "duplicate castling right is rejected");
    expect_parse_failure_at(
      duplicate_castling,
      NotationError::DUPLICATE_CASTLING,
      EMPTY_BOARD.size() + 5,
      "duplicate castling failure reports the repeated pair");

    for (const std::string_view castling :
         std::array<std::string_view, 6>{
           "rR",
           "RK",
           "r",
           "xK",
           "rK-",
           "--",
         }) {
        expect_parse_failure(
          make_notation(
            EMPTY_BOARD, "r", castling),
          NotationError::CASTLING,
          "invalid or duplicate castling token is rejected");
    }
}

void test_malformed_en_passant_inputs() {
    constexpr std::array<std::string_view, 14>
      INVALID_FIELDS = {
        "d4",
        "d4,-",
        "d4,-,-",
        "d4,-,-,-,-",
        "d4,,--,-",
        "D4,-,-,-",
        "d04,-,-,-",
        "a1,-,-,-",
        "o4,-,-,-",
        "d15,-,-,-",
        "d0,-,-,-",
        "d1x,-,-,-",
        "--,-,-,-",
        "d4,-,-,",
    };

    for (const std::string_view en_passant :
         INVALID_FIELDS) {
        expect_parse_failure(
          make_notation(
            EMPTY_BOARD, "r", "-", en_passant),
          NotationError::EN_PASSANT,
          "invalid en-passant field is rejected");
    }

    const std::string uppercase_file =
      make_notation(
        EMPTY_BOARD, "r", "-", "D4,-,-,-");
    expect_parse_failure_at(
      uppercase_file,
      NotationError::EN_PASSANT,
      EMPTY_BOARD.size() + 5,
      "en-passant failure reports the uppercase file");

    const std::string invalid_rank_byte =
      make_notation(
        EMPTY_BOARD, "r", "-", "d1x,-,-,-");
    expect_parse_failure_at(
      invalid_rank_byte,
      NotationError::EN_PASSANT,
      EMPTY_BOARD.size() + 7,
      "en-passant failure reports the invalid rank byte");

    const std::string extra_field =
      std::string(EMPTY_NOTATION) + " extra";
    expect_parse_failure_at(
      extra_field,
      NotationError::FIELD_COUNT,
      EMPTY_NOTATION.size() + 1,
      "extra-field failure reports the fifth field");
}

void test_deterministic_parse_serialize_parse() {
    const std::array<std::string, 4> inputs = {
      std::string(EMPTY_NOTATION),
      serialize_position(make_starting_position()),
      serialize_position(edge_position()),
      serialize_position(all_piece_position()),
    };

    for (const std::string& input : inputs) {
        const auto first = parse_position(input);
        if (!first) {
            expect(false,
                   "determinism input parses initially");
            continue;
        }

        const std::string canonical =
          serialize_position(*first);
        const auto second =
          parse_position(canonical);
        expect(
          second
            && positions_equal(*first, *second),
          "parse-serialize-parse preserves exact position state");
        if (second) {
            expect(serialize_position(*second)
                     == canonical,
                   "canonical serialization remains stable");
        }
    }
}

inline constexpr std::size_t MOVE_TYPE_COUNT =
  std::to_underlying(MoveType::COUNT);

using MoveTypeSeen =
  std::array<bool, MOVE_TYPE_COUNT>;
using PromotionTypeSeen =
  std::array<bool, PIECE_TYPE_NB>;

void expect_all_legal_move_round_trips(
  const Position& position,
  MoveTypeSeen& move_types,
  PromotionTypeSeen& promotion_types,
  std::size_t& move_count) {
    const Position original = position;
    const PositionKey original_key = position.key();
    Position working = position;
    MoveList legal_moves;
    generate_legal_moves(
      working, legal_moves);

    expect(
      !legal_moves.empty(),
      "a move round-trip fixture has legal moves");
    for (const Move move : legal_moves) {
        const std::string text =
          serialize_move(move);
        const MoveParseResult parsed =
          parse_move(position, text);
        const MoveParseResult uppercase =
          parse_move(
            position,
            uppercase_ascii(text));

        expect(
          parsed && *parsed == move,
          "serialized legal move text resolves to the original move");
        expect(
          uppercase && *uppercase == move,
          "ASCII uppercase legal move text resolves to the original move");
        if (parsed) {
            expect(
              serialize_move(*parsed) == text,
              "parsed legal move text serializes canonically");
        }

        move_types[
          std::to_underlying(move.type())] = true;
        if (move.is_promotion()) {
            promotion_types[
              std::size_t(
                move.promotion_type())] = true;
        }
        ++move_count;
    }

    expect(
      positions_equal(position, original)
        && positions_equal(working, original)
        && position.key() == original_key
        && working.key() == original_key
        && position.key() == position.recompute_key()
        && working.key() == working.recompute_key(),
      "legal move generation and parsing preserve the complete root");
}

static_assert(
  std::is_same_v<
    decltype(parse_move(
      std::declval<const Position&>(),
      std::declval<std::string_view>())),
    MoveParseResult>);
static_assert(noexcept(
  parse_move(
    std::declval<const Position&>(),
    std::declval<std::string_view>())));

void test_move_sentinel_parsing() {
    const Position position =
      make_starting_position();
    const Position original = position;

    const MoveParseResult none =
      parse_move(position, "none");
    const MoveParseResult uppercase_none =
      parse_move(position, "NONE");
    const MoveParseResult null =
      parse_move(position, "0000");

    expect(
      none && none->is_none()
        && uppercase_none
        && uppercase_none->is_none()
        && null && null->is_null(),
      "sentinel move text parses to its exact move token");
    expect(
      serialize_move(Move::none()) == "none"
        && serialize_move(Move::null()) == "0000",
      "sentinel moves use their canonical text");
    expect(
      positions_equal(position, original),
      "sentinel move parsing preserves the position");
}

void test_exhaustive_legal_move_round_trips() {
    MoveTypeSeen move_types{};
    PromotionTypeSeen promotion_types{};
    std::size_t move_count = 0;

    expect_all_legal_move_round_trips(
      make_starting_position(),
      move_types,
      promotion_types,
      move_count);

    Position special =
      special_move_position();
    Position en_passant_promotion =
      en_passant_promotion_position();
    for (int rotation = 0;
         rotation < COLOR_NB;
         ++rotation) {
        expect_all_legal_move_round_trips(
          special,
          move_types,
          promotion_types,
          move_count);
        expect_all_legal_move_round_trips(
          en_passant_promotion,
          move_types,
          promotion_types,
          move_count);
        special = rotate_clockwise(special);
        en_passant_promotion =
          rotate_clockwise(
            en_passant_promotion);
    }

    for (std::size_t type_index = 0;
         type_index < move_types.size();
         ++type_index) {
        expect(
          move_types[type_index],
          "round-trip fixtures contain every board move type");
    }
    for (int type_index = KNIGHT;
         type_index <= QUEEN;
         ++type_index) {
        expect(
          promotion_types[
            std::size_t(type_index)],
          "round-trip fixtures contain every promotion type");
    }
    expect(
      move_count > 0,
      "exhaustive move round trips test generated legal moves");
}

void test_special_move_type_inference() {
    const Position special =
      special_move_position();
    const Position special_original = special;
    const MoveParseResult castling =
      parse_move(special, "h1j1");
    const MoveParseResult en_passant =
      parse_move(special, "d5c6");
    const MoveParseResult promotion =
      parse_move(special, "b10c11q");

    expect(
      castling
        && *castling
             == Move::castling(
                  make_square(FILE_H, RANK_1),
                  make_square(FILE_J, RANK_1)),
      "castling coordinates resolve to a castling move");
    expect(
      en_passant
        && *en_passant
             == Move::en_passant(
                  make_square(FILE_D, RANK_5),
                  make_square(FILE_C, RANK_6)),
      "en-passant coordinates resolve to an en-passant move");
    expect(
      promotion
        && *promotion
             == Move::promotion(
                  make_square(FILE_B, RANK_10),
                  make_square(FILE_C, RANK_11),
                  QUEEN),
      "promotion coordinates resolve to a promotion move");

    const Position promotion_en_passant =
      en_passant_promotion_position();
    const Position promotion_en_passant_original =
      promotion_en_passant;
    const MoveParseResult combined =
      parse_move(
        promotion_en_passant,
        "B10C11Q");
    expect(
      combined
        && *combined
             == Move::en_passant(
                  make_square(FILE_B, RANK_10),
                  make_square(FILE_C, RANK_11),
                  QUEEN),
      "promotion en passant coordinates infer both internal properties");

    expect(
      positions_equal(special, special_original)
        && positions_equal(
             promotion_en_passant,
             promotion_en_passant_original),
      "special move parsing preserves both positions");
}

void test_move_coordinate_boundaries() {
    struct BoundaryCase {
        Square source;
        Square destination;
        std::string_view text;
    };

    constexpr std::array<BoundaryCase, 4> CASES = {{
      {
        make_square(FILE_A, RANK_4),
        make_square(FILE_N, RANK_4),
        "a4n4",
      },
      {
        make_square(FILE_N, RANK_11),
        make_square(FILE_A, RANK_11),
        "n11a11",
      },
      {
        make_square(FILE_D, RANK_1),
        make_square(FILE_K, RANK_1),
        "d1k1",
      },
      {
        make_square(FILE_K, RANK_14),
        make_square(FILE_D, RANK_14),
        "k14d14",
      },
    }};

    for (const BoundaryCase& test_case :
         CASES) {
        const Position position =
          boundary_rook_position(
            test_case.source);
        const Position original = position;
        const MoveParseResult parsed =
          parse_move(
            position, test_case.text);

        expect(
          parsed
            && *parsed
                 == Move::normal(
                      test_case.source,
                      test_case.destination),
          "boundary coordinate text resolves to the exact legal move");
        expect(
          positions_equal(position, original),
          "boundary move parsing preserves the position");
    }
}

void test_malformed_move_text() {
    const Position position =
      make_starting_position();

    struct FailureCase {
        std::string_view text;
        MoveNotationError error;
        std::size_t offset;
    };

    constexpr std::array<FailureCase, 20> CASES = {{
      {"", MoveNotationError::EMPTY, 0},
      {"2d4", MoveNotationError::SOURCE_FILE, 0},
      {"o2d4", MoveNotationError::SOURCE_FILE, 0},
      {"dd4", MoveNotationError::SOURCE_RANK, 1},
      {"d0d4", MoveNotationError::SOURCE_RANK, 1},
      {"d01d4", MoveNotationError::SOURCE_RANK, 1},
      {"d15d4", MoveNotationError::SOURCE_RANK, 1},
      {
        "d999999999999999999999d4",
        MoveNotationError::SOURCE_RANK,
        1,
      },
      {"a1d4", MoveNotationError::SOURCE_SQUARE, 0},
      {"d2", MoveNotationError::DESTINATION_FILE, 2},
      {"d2o4", MoveNotationError::DESTINATION_FILE, 2},
      {"d2dd", MoveNotationError::DESTINATION_RANK, 3},
      {"d2d0", MoveNotationError::DESTINATION_RANK, 3},
      {"d2d15", MoveNotationError::DESTINATION_RANK, 3},
      {"d2a1", MoveNotationError::DESTINATION_SQUARE, 2},
      {"d2d4x", MoveNotationError::PROMOTION, 4},
      {"d2d4=", MoveNotationError::PROMOTION, 4},
      {
        "d2d4qx",
        MoveNotationError::TRAILING_CHARACTERS,
        5,
      },
      {"d2-d4", MoveNotationError::DESTINATION_FILE, 2},
      {" d2d4", MoveNotationError::SOURCE_FILE, 0},
    }};

    for (const FailureCase& test_case : CASES) {
        expect_move_failure_at(
          position,
          test_case.text,
          test_case.error,
          test_case.offset,
          "malformed move text reports its exact category and offset");
    }
}

void test_illegal_move_text() {
    const Position position =
      make_starting_position();

    struct FailureCase {
        std::string_view text;
        std::size_t offset;
    };
    constexpr std::array<FailureCase, 5> CASES = {{
      {"d2d5", 0},
      {"d2d2", 0},
      {"a7a6", 0},
      {"d2d4q", 4},
      {"d2d4n", 4},
    }};

    for (const FailureCase& test_case : CASES) {
        expect_move_failure_at(
          position,
          test_case.text,
          MoveNotationError::ILLEGAL,
          test_case.offset,
          "structurally valid illegal move text is rejected");
    }

    const Position promotion =
      special_move_position();
    expect_move_failure_at(
      promotion,
      "b10b11",
      MoveNotationError::ILLEGAL,
      0,
      "promotion text requires a promotion suffix");
}

void test_exact_move_serialization() {
    expect(
      serialize_move(Move::normal(
        make_square(FILE_D, RANK_2),
        make_square(FILE_D, RANK_4)))
        == "d2d4",
      "normal move has exact notation");
    expect(
      serialize_move(Move::promotion(
        make_square(FILE_B, RANK_10),
        make_square(FILE_B, RANK_11),
        QUEEN))
        == "b10b11q",
      "promotion has exact notation");
    expect(
      serialize_move(Move::castling(
        make_square(FILE_H, RANK_1),
        make_square(FILE_J, RANK_1)))
        == "h1j1",
      "castling move has exact notation");
    expect(
      serialize_move(Move::en_passant(
        make_square(FILE_D, RANK_5),
        make_square(FILE_C, RANK_6)))
        == "d5c6",
      "en-passant move has exact notation");
    expect(
      serialize_move(Move::en_passant(
        make_square(FILE_B, RANK_10),
        make_square(FILE_C, RANK_11),
        QUEEN))
        == "b10c11q",
      "en-passant promotion has exact notation");
    expect(serialize_move(Move::none()) == "none",
           "none move has exact notation");
    expect(serialize_move(Move::null()) == "0000",
           "null move has exact notation");
}

void test_all_promotion_serialization() {
    constexpr std::array<PieceType, 4>
      PROMOTION_TYPES = {
        QUEEN,
        ROOK,
        BISHOP,
        KNIGHT,
    };
    constexpr std::array<char, 4>
      PROMOTION_CHARACTERS = {
        'q',
        'r',
        'b',
        'n',
    };
    constexpr Square b10 =
      make_square(FILE_B, RANK_10);
    constexpr Square b11 =
      make_square(FILE_B, RANK_11);
    constexpr Square c11 =
      make_square(FILE_C, RANK_11);

    for (std::size_t index = 0;
         index < PROMOTION_TYPES.size();
         ++index) {
        std::string promotion = "b10b11";
        promotion += PROMOTION_CHARACTERS[index];
        expect(
          serialize_move(Move::promotion(
            b10, b11, PROMOTION_TYPES[index]))
            == promotion,
          "each promotion type has its exact suffix");

        std::string en_passant = "b10c11";
        en_passant += PROMOTION_CHARACTERS[index];
        expect(
          serialize_move(Move::en_passant(
            b10, c11, PROMOTION_TYPES[index]))
            == en_passant,
          "each en-passant promotion type has its exact suffix");
    }
}

void test_castling_move_serialization() {
    struct CastlingCase {
        Color color;
        CastlingSide side;
        std::string_view expected;
    };

    constexpr std::array<CastlingCase, 8> CASES = {{
      {
        RED,
        CastlingSide::KING_SIDE,
        "h1j1",
      },
      {
        RED,
        CastlingSide::QUEEN_SIDE,
        "h1f1",
      },
      {
        BLUE,
        CastlingSide::KING_SIDE,
        "a7a5",
      },
      {
        BLUE,
        CastlingSide::QUEEN_SIDE,
        "a7a9",
      },
      {
        YELLOW,
        CastlingSide::KING_SIDE,
        "g14e14",
      },
      {
        YELLOW,
        CastlingSide::QUEEN_SIDE,
        "g14i14",
      },
      {
        GREEN,
        CastlingSide::KING_SIDE,
        "n8n10",
      },
      {
        GREEN,
        CastlingSide::QUEEN_SIDE,
        "n8n6",
      },
    }};

    for (const CastlingCase& test_case : CASES) {
        const CastlingGeometry& geometry =
          castling_geometry(
            test_case.color, test_case.side);
        expect(
          serialize_move(Move::castling(
            geometry.king_source,
            geometry.king_destination))
            == test_case.expected,
          "each color and side has exact castling notation");
    }
}

void test_all_normal_move_boundaries() {
    bool exact = true;
    std::size_t tested = 0;

    for (int from_index = 0;
         from_index < SQUARE_NB;
         ++from_index) {
        const Square from = Square(from_index);
        if (!is_ok(from))
            continue;

        for (int to_index = 0;
             to_index < SQUARE_NB;
             ++to_index) {
            const Square to = Square(to_index);
            if (!is_ok(to) || from == to)
                continue;

            const std::string expected =
              reference_square(from)
              + reference_square(to);
            const Move move =
              Move::normal(from, to);
            const std::string first =
              serialize_move(move);
            const std::string second =
              serialize_move(move);

            exact &=
              first == expected
              && second == first;
            ++tested;
        }
    }

    expect(exact,
           "every playable normal source and destination serializes exactly");
    expect(tested == 160U * 159U,
           "all distinct playable square pairs are tested");
}

void test_handcrafted_perft_divide_format() {
    PerftList entries;
    entries.push_back({
      Move::normal(
        make_square(FILE_D, RANK_2),
        make_square(FILE_D, RANK_4)),
      12,
    });
    entries.push_back({
      Move::promotion(
        make_square(FILE_B, RANK_10),
        make_square(FILE_B, RANK_11),
        QUEEN),
      0,
    });
    entries.push_back({
      Move::castling(
        make_square(FILE_H, RANK_1),
        make_square(FILE_J, RANK_1)),
      3,
    });
    entries.push_back({
      Move::en_passant(
        make_square(FILE_D, RANK_5),
        make_square(FILE_C, RANK_6)),
      4,
    });
    const PerftList original = entries;

    constexpr std::string_view EXPECTED =
      "d2d4: 12\n"
      "b10b11q: 0\n"
      "h1j1: 3\n"
      "d5c6: 4\n"
      "Total: 19";
    const std::string formatted =
      format_perft_divide(entries);

    expect(formatted == EXPECTED,
           "ordered perft divide has exact formatting and sum");
    expect(!formatted.empty()
             && formatted.back() != '\n',
           "perft divide has no trailing newline");
    expect(perft_lists_equal(entries, original),
           "formatting preserves every perft entry");

    PerftList empty;
    expect(format_perft_divide(empty) == "Total: 0",
           "empty perft divide has a zero total");

    PerftList maximum;
    maximum.push_back({
      Move::normal(
        make_square(FILE_D, RANK_2),
        make_square(FILE_D, RANK_4)),
      std::numeric_limits<std::uint64_t>::max(),
    });
    constexpr std::string_view MAXIMUM_EXPECTED =
      "d2d4: 18446744073709551615\n"
      "Total: 18446744073709551615";
    expect(
      format_perft_divide(maximum)
        == MAXIMUM_EXPECTED,
      "maximum uint64 count formats without truncation");

    PerftList overflow;
    overflow.push_back({
      Move::normal(
        make_square(FILE_D, RANK_2),
        make_square(FILE_D, RANK_4)),
      std::numeric_limits<std::uint64_t>::max(),
    });
    overflow.push_back({
      Move::normal(
        make_square(FILE_E, RANK_2),
        make_square(FILE_E, RANK_4)),
      1,
    });
    constexpr std::string_view OVERFLOW_EXPECTED =
      "d2d4: 18446744073709551615\n"
      "e2e4: 1\n"
      "Total: overflow";
    expect(
      format_perft_divide(overflow)
        == OVERFLOW_EXPECTED,
      "an unrepresentable total is reported as overflow");
}

void test_actual_perft_divide_format() {
    Position position = make_starting_position();
    const Position original_position = position;
    const PerftList entries =
      perft_divide(position, 2);
    const PerftList original_entries = entries;

    std::string expected;
    for (const PerftEntry& entry : entries) {
        expected += serialize_move(entry.move);
        expected += ": ";
        expected += std::to_string(entry.nodes);
        expected += '\n';
    }
    expected += "Total: ";
    expected += std::to_string(perft_sum(entries));

    const std::string first =
      format_perft_divide(entries);
    const std::string second =
      format_perft_divide(entries);

    expect(first == expected,
           "actual divide lines preserve generation order and counts");
    expect(second == first,
           "repeated divide formatting is deterministic");
    expect(
      perft_sum(entries) == perft(position, 2),
      "actual divide total equals perft");
    expect(positions_equal(position, original_position),
           "divide generation and perft preserve the position");
    expect(perft_lists_equal(entries, original_entries),
           "actual divide formatting preserves its input list");
}

}  // namespace

int main() {
    test_empty_canonical_notation();
    test_independent_edge_notation();
    test_starting_and_rich_round_trips();
    test_every_piece_token();
    test_side_values();
    test_castling_subsets_and_order();
    test_en_passant_slots_and_squares();
    test_publicly_representable_game_states();
    test_ascii_whitespace_policy();
    test_malformed_field_and_board_inputs();
    test_malformed_piece_side_and_castling_inputs();
    test_malformed_en_passant_inputs();
    test_deterministic_parse_serialize_parse();
    test_move_sentinel_parsing();
    test_exhaustive_legal_move_round_trips();
    test_special_move_type_inference();
    test_move_coordinate_boundaries();
    test_malformed_move_text();
    test_illegal_move_text();
    test_exact_move_serialization();
    test_all_promotion_serialization();
    test_castling_move_serialization();
    test_all_normal_move_boundaries();
    test_handcrafted_perft_divide_format();
    test_actual_perft_divide_format();

    if (failures != 0) {
        std::cerr << failures
                  << " notation test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All notation tests passed\n";
    return EXIT_SUCCESS;
}
