#include "fen.h"
#include "setup.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using namespace Mockingbird;

int failures = 0;

inline constexpr std::array<CastlingSide, CASTLING_SIDE_NB>
  CASTLING_SIDES = {
    CastlingSide::KING_SIDE,
    CastlingSide::QUEEN_SIDE,
};

inline constexpr std::string_view EMPTY_BOARD =
  "x,x,x,8,x,x,x/"
  "x,x,x,8,x,x,x/"
  "x,x,x,8,x,x,x/"
  "14/14/14/14/14/14/14/14/"
  "x,x,x,8,x,x,x/"
  "x,x,x,8,x,x,x/"
  "x,x,x,8,x,x,x";

inline constexpr std::string_view EMPTY_FEN =
  "R-0,0,0,0-0,0,0,0-0,0,0,0-0,0,0,0-0-"
  "x,x,x,8,x,x,x/"
  "x,x,x,8,x,x,x/"
  "x,x,x,8,x,x,x/"
  "14/14/14/14/14/14/14/14/"
  "x,x,x,8,x,x,x/"
  "x,x,x,8,x,x,x/"
  "x,x,x,8,x,x,x";

inline constexpr std::string_view START_FEN =
  "R-0,0,0,0-1,1,1,1-1,1,1,1-0,0,0,0-0-"
  "x,x,x,yR,yN,yB,yK,yQ,yB,yN,yR,x,x,x/"
  "x,x,x,yP,yP,yP,yP,yP,yP,yP,yP,x,x,x/"
  "x,x,x,8,x,x,x/"
  "bR,bP,10,gP,gR/"
  "bN,bP,10,gP,gN/"
  "bB,bP,10,gP,gB/"
  "bQ,bP,10,gP,gK/"
  "bK,bP,10,gP,gQ/"
  "bB,bP,10,gP,gB/"
  "bN,bP,10,gP,gN/"
  "bR,bP,10,gP,gR/"
  "x,x,x,8,x,x,x/"
  "x,x,x,rP,rP,rP,rP,rP,rP,rP,rP,x,x,x/"
  "x,x,x,rR,rN,rB,rQ,rK,rB,rN,rR,x,x,x";

// Records a failed condition and permits the remaining tests to run.
void expect(bool condition, std::string_view message) {
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

[[nodiscard]] constexpr bool positions_equal(
  const Position& left,
  const Position& right) noexcept {
    if (left.side_to_move() != right.side_to_move()
        || left.key() != right.key()
        || left.occupied() != right.occupied())
        return false;

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

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        if (left.pieces(color) != right.pieces(color)
            || left.en_passant_square(color)
                 != right.en_passant_square(color)) {
            return false;
        }

        for (const CastlingSide side : CASTLING_SIDES) {
            if (left.has_castling_right(color, side)
                != right.has_castling_right(color, side)) {
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
    }

    return left.key() == left.recompute_key()
        && right.key() == right.recompute_key();
}

[[nodiscard]] std::string make_fen(
  std::string_view side,
  std::string_view dead,
  std::string_view kingside,
  std::string_view queenside,
  std::string_view points,
  std::string_view halfmove,
  std::string_view board,
  std::string_view en_passant = {}) {
    std::string result;
    result.reserve(
      side.size() + dead.size() + kingside.size()
      + queenside.size() + points.size()
      + halfmove.size() + board.size()
      + en_passant.size() + 8);
    result += side;
    result += '-';
    result += dead;
    result += '-';
    result += kingside;
    result += '-';
    result += queenside;
    result += '-';
    result += points;
    result += '-';
    result += halfmove;
    result += '-';
    if (!en_passant.empty()) {
        result += en_passant;
        result += '-';
    }
    result += board;
    return result;
}

[[nodiscard]] std::string standard_fen(
  std::string_view board,
  std::string_view en_passant = {}) {
    return make_fen(
      "R",
      "0,0,0,0",
      "0,0,0,0",
      "0,0,0,0",
      "0,0,0,0",
      "0",
      board,
      en_passant);
}

[[nodiscard]] std::string board_with_rank(
  std::size_t rank_from_top,
  std::string_view replacement) {
    std::array<std::string_view, BOARD_RANKS> ranks{};
    std::size_t start = 0;

    for (std::size_t index = 0;
         index < ranks.size();
         ++index) {
        const std::size_t separator =
          EMPTY_BOARD.find('/', start);
        const std::size_t end =
          separator == std::string_view::npos
            ? EMPTY_BOARD.size()
            : separator;
        ranks[index] =
          EMPTY_BOARD.substr(start, end - start);
        start = end + 1;
    }

    std::string result;
    for (std::size_t index = 0;
         index < ranks.size();
         ++index) {
        if (index != 0)
            result += '/';
        result += index == rank_from_top
          ? replacement
          : ranks[index];
    }
    return result;
}

void expect_failure(
  std::string_view fen,
  FenError error,
  std::string_view message) {
    const FenParseResult result = parse_fen(fen);
    expect(
      !result
        && result.error().code == error
        && result.error().offset <= fen.size(),
      message);
}

void expect_failure_at(
  std::string_view fen,
  FenError error,
  std::size_t offset,
  std::string_view message) {
    const FenParseResult result = parse_fen(fen);
    expect(
      !result
        && result.error()
             == FenFailure{error, offset},
      message);
}

[[nodiscard]] Position rich_position() {
    Position position;
    position.set_side_to_move(GREEN);

    position.put_piece(
      Y_KING, make_square(FILE_D, RANK_14));
    position.put_piece(
      G_QUEEN, make_square(FILE_K, RANK_14));
    position.put_piece(
      B_ROOK, make_square(FILE_A, RANK_11));
    position.put_piece(
      R_BISHOP, make_square(FILE_N, RANK_11));
    position.put_piece(
      R_KNIGHT, make_square(FILE_A, RANK_4));
    position.put_piece(
      B_PAWN, make_square(FILE_N, RANK_4));
    position.put_piece(
      G_KING, make_square(FILE_D, RANK_1));
    position.put_piece(
      Y_QUEEN, make_square(FILE_K, RANK_1));

    position.set_castling_right(
      RED, CastlingSide::KING_SIDE);
    position.set_castling_right(
      BLUE, CastlingSide::QUEEN_SIDE);
    position.set_castling_right(
      YELLOW, CastlingSide::KING_SIDE);
    position.set_castling_right(
      GREEN, CastlingSide::QUEEN_SIDE);

    position.set_en_passant_square(
      RED, make_square(FILE_H, RANK_3));
    position.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_7));
    position.set_en_passant_square(
      YELLOW, make_square(FILE_I, RANK_12));
    position.set_en_passant_square(
      GREEN, make_square(FILE_L, RANK_8));
    return position;
}

void test_empty_position() {
    const Position empty;
    expect(
      serialize_fen(empty) == EMPTY_FEN,
      "an empty position has the exact canonical FEN4 spelling");

    const FenParseResult parsed = parse_fen(EMPTY_FEN);
    expect(parsed.has_value(), "canonical empty FEN4 parses");
    if (parsed) {
        expect(
          positions_equal(*parsed, empty),
          "canonical empty FEN4 represents the empty position");
    }
}

void test_starting_position() {
    const Position starting = make_starting_position();
    expect(
      serialize_fen(starting) == START_FEN,
      "the starting position has the expected independent FEN4");

    const FenParseResult parsed = parse_fen(START_FEN);
    expect(
      parsed.has_value(),
      "the independently written starting FEN4 parses");
    if (parsed) {
        expect(
          positions_equal(*parsed, starting),
          "the starting FEN4 maps every piece and rule field");
    }
}

void test_rich_round_trip() {
    const Position original = rich_position();
    const Position before = original;
    const std::string serialized =
      serialize_fen(original);

    expect(
      positions_equal(original, before),
      "serialization does not mutate the position");
    expect(
      serialized.find(
        "{'enPassant':('h3:h4','c7:d7',"
        "'i12:i11','l8:k8')}-")
        != std::string::npos,
      "serialization records every en-passant orientation");

    const FenParseResult parsed =
      parse_fen(serialized);
    expect(
      parsed.has_value(),
      "a position with pieces, rights, and targets parses");
    if (parsed) {
        expect(
          positions_equal(*parsed, original),
          "all Position state survives the FEN4 round trip");
        expect(
          serialize_fen(*parsed) == serialized,
          "rich FEN4 serialization is deterministic");
    }
}

void test_metadata_normalization() {
    const std::string input = make_fen(
      "B",
      "0,0,0,0",
      "1,0,1,0",
      "0,1,0,1",
      "2,3,5,8",
      "27",
      EMPTY_BOARD);
    const FenParseResult parsed = parse_fen(input);

    expect(
      parsed.has_value(),
      "nonzero points and halfmove metadata parse");
    if (!parsed)
        return;

    expect(
      parsed->side_to_move() == BLUE,
      "the uppercase side field selects the active color");
    expect(
      parsed->has_castling_right(
        RED, CastlingSide::KING_SIDE)
        && parsed->has_castling_right(
          YELLOW, CastlingSide::KING_SIDE)
        && parsed->has_castling_right(
          BLUE, CastlingSide::QUEEN_SIDE)
        && parsed->has_castling_right(
          GREEN, CastlingSide::QUEEN_SIDE),
      "the two castling lists preserve their color order");

    const std::string canonical =
      serialize_fen(*parsed);
    expect(
      canonical.starts_with(
        "B-0,0,0,0-1,0,1,0-0,1,0,1-"
        "0,0,0,0-0-"),
      "serialization normalizes points and halfmove metadata");
}

void test_optional_en_passant_object() {
    const std::string explicit_empty = standard_fen(
      EMPTY_BOARD,
      "{'enPassant':('','','','')}");
    const FenParseResult parsed =
      parse_fen(explicit_empty);

    expect(
      parsed.has_value(),
      "an explicit object with four absent targets parses");
    if (parsed) {
        expect(
          serialize_fen(*parsed) == EMPTY_FEN,
          "serialization omits an empty en-passant object");
    }
}

void test_surrounding_whitespace() {
    std::string spaced =
      " \tR \n- 0,0,0,0 - 0,0,0,0 "
      "- 0,0,0,0 - 0,0,0,0 - 0 - \r\n";
    spaced += EMPTY_BOARD;
    spaced += "\t ";

    const FenParseResult parsed = parse_fen(spaced);
    expect(
      parsed.has_value(),
      "ASCII whitespace around top-level fields is accepted");
    if (parsed) {
        expect(
          serialize_fen(*parsed) == EMPTY_FEN,
          "field-boundary whitespace is not serialized");
    }
}

void test_metadata_failures() {
    expect_failure(
      "",
      FenError::FIELD_COUNT,
      "too few top-level fields are rejected");

    const std::string bad_side = make_fen(
      "r", "0,0,0,0", "0,0,0,0", "0,0,0,0",
      "0,0,0,0", "0", EMPTY_BOARD);
    expect_failure_at(
      bad_side,
      FenError::SIDE,
      0,
      "the side field is uppercase and reports its offset");

    expect_failure(
      make_fen(
        "R", "0,0,0", "0,0,0,0", "0,0,0,0",
        "0,0,0,0", "0", EMPTY_BOARD),
      FenError::DEAD_FLAGS,
      "the dead-player field requires four flags");

    const std::string dead_player = make_fen(
      "R", "0,1,0,0", "0,0,0,0", "0,0,0,0",
      "0,0,0,0", "0", EMPTY_BOARD);
    expect_failure_at(
      dead_player,
      FenError::UNSUPPORTED_DEAD_PLAYER,
      4,
      "a dead player is rejected at the active flag");

    expect_failure(
      make_fen(
        "R", "0,0,0,0", "0,2,0,0", "0,0,0,0",
        "0,0,0,0", "0", EMPTY_BOARD),
      FenError::KINGSIDE_FLAGS,
      "kingside castling flags are binary");
    expect_failure(
      make_fen(
        "R", "0,0,0,0", "0,0,0,0", "0,0,0",
        "0,0,0,0", "0", EMPTY_BOARD),
      FenError::QUEENSIDE_FLAGS,
      "the queenside castling field requires four flags");
    expect_failure(
      make_fen(
        "R", "0,0,0,0", "0,0,0,0", "0,0,0,0",
        "0,seven,0,0", "0", EMPTY_BOARD),
      FenError::POINTS,
      "point values contain decimal digits only");
    expect_failure(
      make_fen(
        "R", "0,0,0,0", "0,0,0,0", "0,0,0,0",
        "0,0,0,0", "01", EMPTY_BOARD),
      FenError::HALFMOVE,
      "the halfmove value has no leading zero");
    expect_failure(
      make_fen(
        "R", "0,0,0,0", "0,0,0,0", "0,0,0,0",
        "18446744073709551616,0,0,0", "0", EMPTY_BOARD),
      FenError::POINTS,
      "overflowing point values are rejected");
}

void test_en_passant_failures() {
    expect_failure(
      standard_fen(
        EMPTY_BOARD,
        "{'enpassant':('','','','')}"),
      FenError::EN_PASSANT,
      "the en-passant property spelling is exact");
    expect_failure(
      standard_fen(
        EMPTY_BOARD,
        "{'enPassant':('','','')}"),
      FenError::EN_PASSANT,
      "the en-passant object requires four entries");
    expect_failure(
      standard_fen(
        EMPTY_BOARD,
        "{'enPassant':(h3:h4,'','','')}"),
      FenError::EN_PASSANT,
      "present en-passant entries require quotes");
    expect_failure(
      standard_fen(
        EMPTY_BOARD,
        "{'enPassant':('H3:h4','','','')}"),
      FenError::EN_PASSANT_COORDINATE,
      "en-passant coordinates use lowercase files");
    expect_failure(
      standard_fen(
        EMPTY_BOARD,
        "{'enPassant':('a1:a2','','','')}"),
      FenError::EN_PASSANT_COORDINATE,
      "en-passant targets must be playable");

    const std::string wrong_victim = standard_fen(
      EMPTY_BOARD,
      "{'enPassant':('h3:h5','','','')}");
    const std::size_t victim_offset =
      wrong_victim.find("h5");
    expect_failure_at(
      wrong_victim,
      FenError::EN_PASSANT_VICTIM,
      victim_offset,
      "the victim square must follow the owner's pawn direction");
}

void test_board_failures() {
    expect_failure(
      standard_fen("14"),
      FenError::RANK_COUNT,
      "the board requires fourteen ranks");
    expect_failure(
      standard_fen(
        std::string(EMPTY_BOARD) + "/14"),
      FenError::RANK_COUNT,
      "an extra board rank is rejected");

    const std::string playable_cutout =
      standard_fen(board_with_rank(3, "x,13"));
    expect_failure_at(
      playable_cutout,
      FenError::CUTOUT,
      playable_cutout.find("x,13"),
      "x is rejected on a playable square");

    expect_failure(
      standard_fen(
        board_with_rank(0, "3,8,x,x,x")),
      FenError::CUTOUT,
      "an empty run cannot replace cut-out squares");
    expect_failure(
      standard_fen(
        board_with_rank(3, "01,13")),
      FenError::EMPTY_RUN,
      "empty runs have no leading zero");
    expect_failure(
      standard_fen(
        board_with_rank(3, "15")),
      FenError::RANK_WIDTH,
      "a rank cannot exceed fourteen files");
    expect_failure(
      standard_fen(
        board_with_rank(3, "13")),
      FenError::RANK_WIDTH,
      "a rank cannot contain fewer than fourteen files");
    expect_failure(
      standard_fen(
        board_with_rank(3, "7,,7")),
      FenError::RANK_TOKEN,
      "empty comma-separated rank tokens are rejected");
    expect_failure(
      standard_fen(
        board_with_rank(3, "zP,13")),
      FenError::PIECE,
      "piece colors are restricted to the four lowercase codes");
    expect_failure(
      standard_fen(
        board_with_rank(3, "rX,13")),
      FenError::PIECE,
      "piece types are restricted to the six uppercase codes");
    expect_failure(
      standard_fen(
        board_with_rank(0, "x,x,x,9,x,x")),
      FenError::CUTOUT,
      "empty runs cannot cross into a cut-out corner");
}

}  // namespace

int main() {
    test_empty_position();
    test_starting_position();
    test_rich_round_trip();
    test_metadata_normalization();
    test_optional_en_passant_object();
    test_surrounding_whitespace();
    test_metadata_failures();
    test_en_passant_failures();
    test_board_failures();

    if (failures != 0) {
        std::cerr << failures
                  << " FEN4 test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All FEN4 tests passed\n";
    return EXIT_SUCCESS;
}
