#include "movegen.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

// Records a failed condition and allows the remaining tests to run.
void expect(bool condition, std::string_view message) {
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

template<typename AttackFunction>
[[nodiscard]] consteval std::size_t maximum_empty_board_attacks(
  AttackFunction attacks) {
    using namespace Mockingbird;

    std::size_t maximum = 0;

    for (int square_index = 0; square_index < SQUARE_NB; ++square_index) {
        const Square square = Square(square_index);
        if (!is_ok(square))
            continue;

        const std::size_t count =
          static_cast<std::size_t>(attacks(square).popcount());
        if (count > maximum)
            maximum = count;
    }

    return maximum;
}

inline constexpr std::size_t MAX_KNIGHT_ATTACKS =
  maximum_empty_board_attacks(
    [](Mockingbird::Square square) {
        return Mockingbird::knight_attacks(square);
    });

inline constexpr std::size_t MAX_BISHOP_ATTACKS =
  maximum_empty_board_attacks(
    [](Mockingbird::Square square) {
        return Mockingbird::bishop_attacks(square);
    });

inline constexpr std::size_t MAX_ROOK_ATTACKS =
  maximum_empty_board_attacks(
    [](Mockingbird::Square square) {
        return Mockingbird::rook_attacks(square);
    });

inline constexpr std::size_t MAX_QUEEN_ATTACKS =
  maximum_empty_board_attacks(
    [](Mockingbird::Square square) {
        return Mockingbird::queen_attacks(square);
    });

inline constexpr std::size_t MAX_KING_ATTACKS =
  maximum_empty_board_attacks(
    [](Mockingbird::Square square) {
        return Mockingbird::king_attacks(square);
    });

static_assert(MAX_KNIGHT_ATTACKS == 8);
static_assert(MAX_BISHOP_ATTACKS == 19);
static_assert(MAX_ROOK_ATTACKS == 26);
static_assert(MAX_QUEEN_ATTACKS == 45);
static_assert(MAX_KING_ATTACKS == 8);

// This loose pawn bound expands one push, two ordinary captures, and both
// opponent-owned en-passant records into four promotion choices apiece.
inline constexpr std::size_t PAWN_MOVE_UPPER_BOUND = 5 * 4;

static_assert(PAWN_MOVE_UPPER_BOUND < MAX_QUEEN_ATTACKS);

// Each starting pawn either remains a pawn or becomes one promoted piece.
// Treating all eight as queens gives a conservative per-piece upper bound.
inline constexpr std::size_t CALCULATED_MOVE_UPPER_BOUND =
  9 * MAX_QUEEN_ATTACKS
  + 2 * MAX_ROOK_ATTACKS
  + 2 * MAX_BISHOP_ATTACKS
  + 2 * MAX_KNIGHT_ATTACKS
  + MAX_KING_ATTACKS
  + 2;

static_assert(CALCULATED_MOVE_UPPER_BOUND == 521);
static_assert(
  CALCULATED_MOVE_UPPER_BOUND
  == Mockingbird::STANDARD_INVENTORY_MOVE_UPPER_BOUND);
static_assert(
  Mockingbird::MoveList::capacity()
  >= CALCULATED_MOVE_UPPER_BOUND);

inline constexpr std::array<Mockingbird::Square, 9> QUEEN_SQUARES = {
  Mockingbird::make_square(Mockingbird::FILE_E, Mockingbird::RANK_4),
  Mockingbird::make_square(Mockingbird::FILE_H, Mockingbird::RANK_9),
  Mockingbird::make_square(Mockingbird::FILE_G, Mockingbird::RANK_11),
  Mockingbird::make_square(Mockingbird::FILE_K, Mockingbird::RANK_5),
  Mockingbird::make_square(Mockingbird::FILE_C, Mockingbird::RANK_10),
  Mockingbird::make_square(Mockingbird::FILE_J, Mockingbird::RANK_2),
  Mockingbird::make_square(Mockingbird::FILE_F, Mockingbird::RANK_8),
  Mockingbird::make_square(Mockingbird::FILE_I, Mockingbird::RANK_6),
  Mockingbird::make_square(Mockingbird::FILE_D, Mockingbird::RANK_7),
};

inline constexpr std::array<Mockingbird::Square, 2> ROOK_SQUARES = {
  Mockingbird::make_square(Mockingbird::FILE_J, Mockingbird::RANK_14),
  Mockingbird::make_square(Mockingbird::FILE_N, Mockingbird::RANK_10),
};

inline constexpr std::array<Mockingbird::Square, 2> BISHOP_SQUARES = {
  Mockingbird::make_square(Mockingbird::FILE_M, Mockingbird::RANK_8),
  Mockingbird::make_square(Mockingbird::FILE_G, Mockingbird::RANK_12),
};

inline constexpr std::array<Mockingbird::Square, 2> KNIGHT_SQUARES = {
  Mockingbird::make_square(Mockingbird::FILE_L, Mockingbird::RANK_8),
  Mockingbird::make_square(Mockingbird::FILE_G, Mockingbird::RANK_13),
};

inline constexpr Mockingbird::Square KING_SQUARE =
  Mockingbird::make_square(Mockingbird::FILE_B, Mockingbird::RANK_8);

// This inventory contains the original non-pawn army and eight queens produced
// by promoting all eight pawns.
[[nodiscard]] constexpr Mockingbird::Position make_high_mobility_position() {
    using namespace Mockingbird;

    Position position;

    for (const Square square : QUEEN_SQUARES)
        position.put_piece(R_QUEEN, square);
    for (const Square square : ROOK_SQUARES)
        position.put_piece(R_ROOK, square);
    for (const Square square : BISHOP_SQUARES)
        position.put_piece(R_BISHOP, square);
    for (const Square square : KNIGHT_SQUARES)
        position.put_piece(R_KNIGHT, square);

    position.put_piece(R_KING, KING_SQUARE);
    return position;
}

[[nodiscard]] constexpr std::size_t high_mobility_move_count() {
    Mockingbird::MoveList moves;
    Mockingbird::generate_moves(
      make_high_mobility_position(), moves);
    return moves.size();
}

static_assert(
  make_high_mobility_position().pieces(Mockingbird::RED).popcount() == 16);
static_assert(
  make_high_mobility_position()
      .pieces(Mockingbird::RED, Mockingbird::QUEEN)
      .popcount()
  == 9);
static_assert(high_mobility_move_count() == 449);
static_assert(high_mobility_move_count() > 256);

void test_capacity_bound() {
    using namespace Mockingbird;

    expect(MoveList::capacity() == 528,
           "move-list capacity is 528");
    expect(MoveList::capacity()
             >= STANDARD_INVENTORY_MOVE_UPPER_BOUND,
           "move-list capacity covers the standard-inventory upper bound");
}

void test_high_mobility_position() {
    using namespace Mockingbird;

    const Position position = make_high_mobility_position();
    MoveList moves;
    generate_moves(position, moves);

    expect(moves.size() == 449,
           "promotion-heavy position generates 449 moves");
    expect(moves.size() > 256,
           "promotion-heavy position exceeds the former capacity");

    for (std::size_t first = 0; first < moves.size(); ++first) {
        for (std::size_t second = first + 1;
             second < moves.size();
             ++second)
            expect(moves[first] != moves[second],
                   "promotion-heavy position has no duplicate moves");
    }
}

}  // namespace

int main() {
    test_capacity_bound();
    test_high_mobility_position();

    if (failures != 0) {
        std::cerr << failures << " capacity test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All capacity tests passed\n";
    return EXIT_SUCCESS;
}
