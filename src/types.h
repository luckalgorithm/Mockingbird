#pragma once

#include <cstddef>

namespace Mockingbird {

// -----------------------------------------------------------------------------
// Players and teams
// -----------------------------------------------------------------------------

// Numeric order matches turn order: Red -> Blue -> Yellow -> Green -> Red.
enum Color : int {
    RED,
    BLUE,
    YELLOW,
    GREEN,
    COLOR_NB
};

enum Team : int {
    RED_YELLOW,
    BLUE_GREEN,
    TEAM_NB
};

// COLOR_NB is a count/sentinel, not a playable color.
[[nodiscard]] constexpr bool is_ok(Color color) noexcept {
    return color >= RED && color < COLOR_NB;
}

// Return the player whose turn follows color.
[[nodiscard]] constexpr Color next_color(Color color) noexcept {
    return Color((color + 1) % COLOR_NB);
}

// Return the player whose turn precedes color.
[[nodiscard]] constexpr Color previous_color(Color color) noexcept {
    return Color((color + COLOR_NB - 1) % COLOR_NB);
}

// Opposite players form a team: Red with Yellow, and Blue with Green.
[[nodiscard]] constexpr Team team_of(Color color) noexcept {
    return color == RED || color == YELLOW ? RED_YELLOW : BLUE_GREEN;
}

// -----------------------------------------------------------------------------
// Pieces
// -----------------------------------------------------------------------------

// NO_PIECE_TYPE is 0. Real piece types occupy values 1 through 6.
enum PieceType : int {
    NO_PIECE_TYPE,
    PAWN,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING,
    PIECE_TYPE_NB
};

// A Piece combines its type and color into one integer:
//
//     bits 0..2: PieceType (three bits)
//     bits 3..4: Color     (two bits)
//
// For example, make_piece(BLUE, ROOK) is (1 << 3) | 4 == 12.
// Values 7, 8, 15, 16, 23, and 24 are unused.
enum Piece : int {
    NO_PIECE,

    R_PAWN = (RED << 3) | PAWN,
    R_KNIGHT,
    R_BISHOP,
    R_ROOK,
    R_QUEEN,
    R_KING,

    B_PAWN = (BLUE << 3) | PAWN,
    B_KNIGHT,
    B_BISHOP,
    B_ROOK,
    B_QUEEN,
    B_KING,

    Y_PAWN = (YELLOW << 3) | PAWN,
    Y_KNIGHT,
    Y_BISHOP,
    Y_ROOK,
    Y_QUEEN,
    Y_KING,

    G_PAWN = (GREEN << 3) | PAWN,
    G_KNIGHT,
    G_BISHOP,
    G_ROOK,
    G_QUEEN,
    G_KING,

    PIECE_NB = 32
};

// NO_PIECE_TYPE and PIECE_TYPE_NB are sentinels rather than real types.
[[nodiscard]] constexpr bool is_ok(PieceType piece_type) noexcept {
    return piece_type >= PAWN && piece_type <= KING;
}

// A valid encoded piece must be in the five-bit encoding range and contain a
// real PieceType. The type check also rejects the unused gaps in Piece.
[[nodiscard]] constexpr bool is_ok(Piece piece) noexcept {
    return piece != NO_PIECE && piece > NO_PIECE && piece < PIECE_NB
        && is_ok(PieceType(piece & 7));
}

// Combine a color and piece type using the bit layout documented above.
[[nodiscard]] constexpr Piece make_piece(Color color, PieceType piece_type) noexcept {
    return Piece((color << 3) | piece_type);
}

// The low three bits contain the piece type.
[[nodiscard]] constexpr PieceType type_of(Piece piece) noexcept {
    return PieceType(piece & 7);
}

// The color is stored above the three piece-type bits.
// Precondition: piece is a real piece, not NO_PIECE.
[[nodiscard]] constexpr Color color_of(Piece piece) noexcept {
    return Color(piece >> 3);
}

// -----------------------------------------------------------------------------
// Board geometry
// -----------------------------------------------------------------------------

// The board is a 14x14 cross with a 3x3 square removed from each corner.
// It is stored in a 16x16 mailbox. Files 0 and 15 and ranks 0 and 15 are
// padding. A square is encoded as:
//
//     square = rank * 16 + file
//
// The file occupies the low four bits. The rank occupies the high four bits.
inline constexpr int BOARD_FILES = 14;
inline constexpr int BOARD_RANKS = 14;
inline constexpr int PLAYABLE_SQUARE_NB = 160;
inline constexpr int MAILBOX_WIDTH = 16;

// Real files are one-based because mailbox file 0 is left-side padding.
enum File : int {
    FILE_A = 1,
    FILE_B,
    FILE_C,
    FILE_D,
    FILE_E,
    FILE_F,
    FILE_G,
    FILE_H,
    FILE_I,
    FILE_J,
    FILE_K,
    FILE_L,
    FILE_M,
    FILE_N,
    FILE_NB = MAILBOX_WIDTH
};

// Real ranks are likewise one-based because mailbox rank 0 is padding.
enum Rank : int {
    RANK_1 = 1,
    RANK_2,
    RANK_3,
    RANK_4,
    RANK_5,
    RANK_6,
    RANK_7,
    RANK_8,
    RANK_9,
    RANK_10,
    RANK_11,
    RANK_12,
    RANK_13,
    RANK_14,
    RANK_NB = MAILBOX_WIDTH
};

// Valid mailbox indices are 0..255. SQ_NONE sits immediately outside that
// range and represents "there is no square." SQUARE_NB is the array size, so
// it intentionally has the same numeric value as SQ_NONE.
enum Square : int {
    SQ_NONE = 256,
    SQUARE_NB = 256
};

// Direction values are offsets in the 16x16 mailbox. "North" is defined from
// Red's perspective; the other players' pawn directions rotate around it.
enum Direction : int {
    NORTH = MAILBOX_WIDTH,
    EAST = 1,
    SOUTH = -NORTH,
    WEST = -EAST,
    NORTH_EAST = NORTH + EAST,
    SOUTH_EAST = SOUTH + EAST,
    SOUTH_WEST = SOUTH + WEST,
    NORTH_WEST = NORTH + WEST
};

// Because the mailbox width is 16, its low four bits encode the file.
[[nodiscard]] constexpr File file_of(Square square) noexcept {
    return File(square & 0xF);
}

// Dividing by 16 (a four-bit shift) recovers the rank.
[[nodiscard]] constexpr Rank rank_of(Square square) noexcept {
    return Rank(square >> 4);
}

// Pack a one-based file and rank into a mailbox index.
[[nodiscard]] constexpr Square make_square(File file, Rank rank) noexcept {
    return Square((rank << 4) | file);
}

// Returns true for coordinates in the complete 14x14 grid, including the four
// cut-out 3x3 corners.
[[nodiscard]] constexpr bool is_board_coordinate(Square square) noexcept {
    // Conversion to unsigned maps negative values outside the 0..255 range.
    if (static_cast<unsigned>(square) >= SQUARE_NB)
        return false;

    const int file = file_of(square);
    const int rank = rank_of(square);
    return file >= FILE_A && file <= FILE_N && rank >= RANK_1 && rank <= RANK_14;
}

// True only for one of the 160 playable squares in the cross-shaped board.
[[nodiscard]] constexpr bool is_ok(Square square) noexcept {
    if (!is_board_coordinate(square))
        return false;

    const int file = file_of(square);
    const int rank = rank_of(square);

    // On the outer three ranks at either end, only files d through k exist.
    // Every file is playable on the middle eight ranks.
    const bool corner_rank = rank <= RANK_3 || rank >= RANK_12;
    return !corner_rank || (file >= FILE_D && file <= FILE_K);
}

// These operators apply a mailbox offset. They do not validate the result.
[[nodiscard]] constexpr Square operator+(Square square, Direction direction) noexcept {
    return Square(int(square) + int(direction));
}

[[nodiscard]] constexpr Square operator-(Square square, Direction direction) noexcept {
    return Square(int(square) - int(direction));
}

// Pawn directions are Red: North, Blue: East, Yellow: South, Green: West.
[[nodiscard]] constexpr Direction pawn_push(Color color) noexcept {
    return color == RED    ? NORTH
         : color == BLUE   ? EAST
         : color == YELLOW ? SOUTH
                           : WEST;
}

}  // namespace Mockingbird
