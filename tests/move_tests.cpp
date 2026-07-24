#include "move.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <type_traits>

namespace {

int failures = 0;

// Records a failed condition and allows the remaining tests to run.
void expect(bool condition, std::string_view message) {
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

constexpr std::array<Mockingbird::PieceType, 4> PROMOTION_TYPES = {
  Mockingbird::KNIGHT,
  Mockingbird::BISHOP,
  Mockingbird::ROOK,
  Mockingbird::QUEEN,
};

[[nodiscard]] constexpr bool constexpr_move_operations() {
    using namespace Mockingbird;

    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square d2 = make_square(FILE_D, RANK_2);
    constexpr Move move = Move::promotion(d1, d2, QUEEN);

    return move.is_board_move() && !move.is_none() && !move.is_null()
        && move.from() == d1 && move.to() == d2
        && move.type() == MoveType::PROMOTION && move.promotion_type() == QUEEN;
}

static_assert(constexpr_move_operations());

void test_layout() {
    using namespace Mockingbird;

    static_assert(sizeof(Move) == sizeof(std::uint32_t));
    static_assert(std::is_trivially_copyable_v<Move>);
    static_assert(std::is_standard_layout_v<Move>);
}

void test_move_types() {
    using namespace Mockingbird;

    expect(is_ok(MoveType::NORMAL), "NORMAL is a valid move type");
    expect(is_ok(MoveType::PROMOTION), "PROMOTION is a valid move type");
    expect(is_ok(MoveType::CASTLING), "CASTLING is a valid move type");
    expect(is_ok(MoveType::EN_PASSANT), "EN_PASSANT is a valid move type");
    expect(!is_ok(MoveType::COUNT), "COUNT is not a valid move type");
}

void test_sentinels() {
    using namespace Mockingbird;

    constexpr Move default_move;
    constexpr Move none = Move::none();
    constexpr Move null = Move::null();

    expect(default_move == none, "default construction produces no move");
    expect(none.is_none(), "none identifies itself");
    expect(!none.is_null(), "none is distinct from null");
    expect(!none.is_board_move(), "none is not a board move");
    expect(!is_ok(none), "none is not a valid board move");
    expect(none.raw() == 0, "none uses the zero encoding");

    expect(null.is_null(), "null identifies itself");
    expect(!null.is_none(), "null is distinct from none");
    expect(!null.is_board_move(), "null is not a board move");
    expect(!is_ok(null), "null is not a valid board move");
    expect(null.raw() == (std::uint32_t{1} << 31), "null uses bit 31");
    expect(null != none, "null and none have different encodings");
}

void expect_common_fields(
  Mockingbird::Move move,
  Mockingbird::Square from,
  Mockingbird::Square to,
  Mockingbird::MoveType move_type) {
    using namespace Mockingbird;

    expect(move.is_board_move(), "encoded move is a board move");
    expect(!move.is_none(), "encoded move is not none");
    expect(!move.is_null(), "encoded move is not null");
    expect(is_ok(move), "encoded move is valid");
    expect(move.from() == from, "source square round-trips");
    expect(move.to() == to, "destination square round-trips");
    expect(move.type() == move_type, "move type round-trips");

    // Board moves use only the low 21 bits.
    expect((move.raw() >> 21) == 0, "board move leaves the high eleven bits clear");
}

void test_exhaustive_round_trips() {
    using namespace Mockingbird;

    int source_destination_pairs = 0;

    // Exercises every ordered pair of distinct playable squares.
    for (int from_index = 0; from_index < SQUARE_NB; ++from_index) {
        const Square from = Square(from_index);
        if (!is_ok(from))
            continue;

        for (int to_index = 0; to_index < SQUARE_NB; ++to_index) {
            const Square to = Square(to_index);
            if (!is_ok(to) || from == to)
                continue;

            ++source_destination_pairs;

            const Move normal = Move::normal(from, to);
            const Move castling = Move::castling(from, to);
            const Move en_passant = Move::en_passant(from, to);

            expect_common_fields(normal, from, to, MoveType::NORMAL);
            expect_common_fields(castling, from, to, MoveType::CASTLING);
            expect_common_fields(en_passant, from, to, MoveType::EN_PASSANT);

            expect(normal != castling, "normal and castling encodings differ");
            expect(normal != en_passant, "normal and en-passant encodings differ");
            expect(castling != en_passant, "castling and en-passant encodings differ");

            for (const PieceType promotion_type : PROMOTION_TYPES) {
                const Move promotion = Move::promotion(from, to, promotion_type);

                expect_common_fields(promotion, from, to, MoveType::PROMOTION);
                expect(promotion.promotion_type() == promotion_type,
                       "promotion piece type round-trips");
                expect(promotion != normal, "promotion and normal encodings differ");
                expect(promotion != castling, "promotion and castling encodings differ");
                expect(promotion != en_passant,
                       "promotion and en-passant encodings differ");
            }
        }
    }

    expect(source_destination_pairs
             == PLAYABLE_SQUARE_NB * (PLAYABLE_SQUARE_NB - 1),
           "all ordered pairs of distinct playable squares were tested");
}

void test_promotion_encodings_are_distinct() {
    using namespace Mockingbird;

    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square d2 = make_square(FILE_D, RANK_2);

    std::array<Move, PROMOTION_TYPES.size()> promotions{};
    for (std::size_t index = 0; index < PROMOTION_TYPES.size(); ++index)
        promotions[index] = Move::promotion(d1, d2, PROMOTION_TYPES[index]);

    for (std::size_t left = 0; left < promotions.size(); ++left) {
        for (std::size_t right = left + 1; right < promotions.size(); ++right)
            expect(promotions[left] != promotions[right],
                   "different promotion pieces have different encodings");
    }
}

}  // namespace

int main() {
    test_layout();
    test_move_types();
    test_sentinels();
    test_exhaustive_round_trips();
    test_promotion_encodings_are_distinct();

    if (failures != 0) {
        std::cerr << failures << " move test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All move tests passed\n";
    return EXIT_SUCCESS;
}
