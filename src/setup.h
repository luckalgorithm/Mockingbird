#pragma once

#include <array>
#include <cstddef>

#include "position.h"

namespace Mockingbird {

// Constructs the chess.com "modern" four-player starting position.
[[nodiscard]] constexpr Position make_starting_position() noexcept {
    constexpr std::array<PieceType, 8> queen_first_back_rank = {
      ROOK,
      KNIGHT,
      BISHOP,
      QUEEN,
      KING,
      BISHOP,
      KNIGHT,
      ROOK,
    };
    constexpr std::array<PieceType, 8> king_first_back_rank = {
      ROOK,
      KNIGHT,
      BISHOP,
      KING,
      QUEEN,
      BISHOP,
      KNIGHT,
      ROOK,
    };

    Position position;

    for (std::size_t index = 0;
         index < queen_first_back_rank.size();
         ++index) {
        const File file =
          File(FILE_D + static_cast<int>(index));
        const Rank rank =
          Rank(RANK_4 + static_cast<int>(index));

        position.put_piece(
          make_piece(RED, queen_first_back_rank[index]),
          make_square(file, RANK_1));
        position.put_piece(
          make_piece(RED, PAWN),
          make_square(file, RANK_2));

        position.put_piece(
          make_piece(YELLOW, king_first_back_rank[index]),
          make_square(file, RANK_14));
        position.put_piece(
          make_piece(YELLOW, PAWN),
          make_square(file, RANK_13));

        position.put_piece(
          make_piece(BLUE, king_first_back_rank[index]),
          make_square(FILE_A, rank));
        position.put_piece(
          make_piece(BLUE, PAWN),
          make_square(FILE_B, rank));

        position.put_piece(
          make_piece(GREEN, queen_first_back_rank[index]),
          make_square(FILE_N, rank));
        position.put_piece(
          make_piece(GREEN, PAWN),
          make_square(FILE_M, rank));
    }

    position.set_side_to_move(RED);
    position.clear_en_passant_squares();

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);

        for (std::size_t side_index = 0;
             side_index < CASTLING_SIDE_NB;
             ++side_index) {
            position.set_castling_right(
              color, static_cast<CastlingSide>(side_index));
        }
    }

    return position;
}

static_assert(
  make_starting_position().occupied().popcount() == 64);
static_assert(
  make_starting_position().side_to_move() == RED);

}  // namespace Mockingbird
