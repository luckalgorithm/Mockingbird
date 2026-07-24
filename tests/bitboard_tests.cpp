#include "bitboard.h"

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

void test_layout() {
    using namespace Mockingbird;

    static_assert(sizeof(Bitboard) == 4 * sizeof(std::uint64_t));
    static_assert(std::is_trivially_copyable_v<Bitboard>);
    static_assert(Bitboard::LIMB_NB == 4);
    static_assert(Bitboard::BITS_PER_LIMB == 64);
    static_assert(Bitboard::BIT_NB == 256);
}

void test_set_clear_and_test() {
    using namespace Mockingbird;

    constexpr Square boundaries[] = {
      Square(0),   Square(63),  Square(64),  Square(127),
      Square(128), Square(191), Square(192), Square(255)
    };

    Bitboard bitboard;
    expect(bitboard.empty(), "a default bitboard is empty");
    expect(!bitboard, "an empty bitboard converts to false");

    for (const Square square : boundaries) {
        bitboard.set(square);
        expect(bitboard.test(square), "set marks the selected bit");
    }

    expect(bitboard.popcount() == 8, "eight boundary bits are set");
    expect(bool(bitboard), "a non-empty bitboard converts to true");

    for (const Square square : boundaries) {
        bitboard.clear(square);
        expect(!bitboard.test(square), "clear resets the selected bit");
    }

    expect(bitboard.empty(), "clearing every set bit produces an empty bitboard");

    bitboard.set(Square(42));
    bitboard.clear();
    expect(bitboard.empty(), "clear without a square resets all bits");
}

void test_bitwise_operations() {
    using namespace Mockingbird;

    Bitboard left;
    left.set(Square(1));
    left.set(Square(64));

    Bitboard right;
    right.set(Square(64));
    right.set(Square(130));

    const Bitboard intersection = left & right;
    expect(intersection.popcount() == 1, "intersection contains one bit");
    expect(intersection.test(Square(64)), "intersection contains the shared bit");

    const Bitboard union_bits = left | right;
    expect(union_bits.popcount() == 3, "union contains three bits");
    expect(union_bits.test(Square(1)), "union contains the left-only bit");
    expect(union_bits.test(Square(64)), "union contains the shared bit");
    expect(union_bits.test(Square(130)), "union contains the right-only bit");

    const Bitboard difference = left ^ right;
    expect(difference.popcount() == 2, "exclusive-or removes the shared bit");
    expect(difference.test(Square(1)), "exclusive-or contains the left-only bit");
    expect(difference.test(Square(130)), "exclusive-or contains the right-only bit");

    const Bitboard complement = ~left;
    expect(complement.popcount() == 254, "complement reverses all 256 bits");
    expect(!complement.test(Square(1)), "complement clears a previously set bit");
    expect(complement.test(Square(2)), "complement sets a previously clear bit");
}

void test_shifts() {
    using namespace Mockingbird;

    expect((Bitboard::from_square(Square(63)) << 1).test(Square(64)),
           "left shift crosses the first limb boundary");
    expect((Bitboard::from_square(Square(127)) << 1).test(Square(128)),
           "left shift crosses the second limb boundary");
    expect((Bitboard::from_square(Square(191)) << 1).test(Square(192)),
           "left shift crosses the third limb boundary");

    expect((Bitboard::from_square(Square(64)) >> 1).test(Square(63)),
           "right shift crosses the first limb boundary");
    expect((Bitboard::from_square(Square(128)) >> 1).test(Square(127)),
           "right shift crosses the second limb boundary");
    expect((Bitboard::from_square(Square(192)) >> 1).test(Square(191)),
           "right shift crosses the third limb boundary");

    expect((Bitboard::from_square(Square(0)) << 255).test(Square(255)),
           "left shift supports a 255-bit distance");
    expect((Bitboard::from_square(Square(255)) >> 255).test(Square(0)),
           "right shift supports a 255-bit distance");
    expect((Bitboard::from_square(Square(255)) << 1).empty(),
           "left shift discards bits above 255");
    expect((Bitboard::from_square(Square(0)) >> 1).empty(),
           "right shift discards bits below zero");
    expect((Bitboard::from_square(Square(0)) << 256).empty(),
           "left shift by the bit width produces zero");
    expect((Bitboard::from_square(Square(255)) >> 256).empty(),
           "right shift by the bit width produces zero");

    Bitboard two_bits;
    two_bits.set(Square(62));
    two_bits.set(Square(63));
    const Bitboard shifted = two_bits << 2;
    expect(shifted.test(Square(64)), "multi-bit shift preserves the first bit");
    expect(shifted.test(Square(65)), "multi-bit shift preserves the second bit");
    expect(shifted.popcount() == 2, "multi-bit shift preserves the bit count");

    // Checks every source bit with every shift distance from 0 through 256.
    for (unsigned source = 0; source < Bitboard::BIT_NB; ++source) {
        const Bitboard source_bit = Bitboard::from_square(Square(source));

        for (unsigned distance = 0; distance <= Bitboard::BIT_NB; ++distance) {
            const Bitboard shifted_left = source_bit << distance;
            const Bitboard shifted_right = source_bit >> distance;

            if (source + distance < Bitboard::BIT_NB) {
                expect(shifted_left.popcount() == 1,
                       "left shift preserves an in-range source bit");
                expect(shifted_left.test(Square(source + distance)),
                       "left shift places the bit at source plus distance");
            } else {
                expect(shifted_left.empty(), "left shift discards an out-of-range bit");
            }

            if (distance <= source) {
                expect(shifted_right.popcount() == 1,
                       "right shift preserves an in-range source bit");
                expect(shifted_right.test(Square(source - distance)),
                       "right shift places the bit at source minus distance");
            } else {
                expect(shifted_right.empty(), "right shift discards an out-of-range bit");
            }
        }
    }
}

void test_lsb_operations() {
    using namespace Mockingbird;

    Bitboard bitboard;
    bitboard.set(Square(192));
    bitboard.set(Square(64));
    bitboard.set(Square(7));

    expect(bitboard.lsb() == Square(7), "lsb returns the lowest set bit");
    expect(bitboard.pop_lsb() == Square(7), "pop_lsb returns the first set bit");
    expect(bitboard.pop_lsb() == Square(64), "pop_lsb crosses a limb boundary");
    expect(bitboard.pop_lsb() == Square(192), "pop_lsb returns the final set bit");
    expect(bitboard.empty(), "pop_lsb clears each returned bit");
}

void test_msb_operation() {
    using namespace Mockingbird;

    Bitboard bitboard;
    bitboard.set(Square(7));
    bitboard.set(Square(64));
    bitboard.set(Square(192));

    expect(bitboard.msb() == Square(192), "msb returns the highest set bit");

    bitboard.clear(Square(192));
    expect(bitboard.msb() == Square(64), "msb crosses a limb boundary");

    bitboard.clear(Square(64));
    expect(bitboard.msb() == Square(7), "msb returns the final set bit");
}

void test_playable_square_mask() {
    using namespace Mockingbird;

    static_assert(PLAYABLE_SQUARES.popcount() == PLAYABLE_SQUARE_NB);

    expect(PLAYABLE_SQUARES.test(make_square(FILE_D, RANK_1)),
           "playable mask contains d1");
    expect(PLAYABLE_SQUARES.test(make_square(FILE_A, RANK_4)),
           "playable mask contains a4");
    expect(PLAYABLE_SQUARES.test(make_square(FILE_N, RANK_11)),
           "playable mask contains n11");
    expect(PLAYABLE_SQUARES.test(make_square(FILE_K, RANK_14)),
           "playable mask contains k14");

    expect(!PLAYABLE_SQUARES.test(make_square(FILE_A, RANK_1)),
           "playable mask excludes a1");
    expect(!PLAYABLE_SQUARES.test(make_square(FILE_N, RANK_14)),
           "playable mask excludes n14");
    expect((PLAYABLE_SQUARES & ~PLAYABLE_SQUARES).empty(),
           "playable mask and its complement do not intersect");

    // Compares every mask bit with the board-geometry predicate.
    for (int index = 0; index < SQUARE_NB; ++index) {
        const Square square = Square(index);
        expect(PLAYABLE_SQUARES.test(square) == is_ok(square),
               "playable mask matches is_ok");
    }
}

}  // namespace

int main() {
    test_layout();
    test_set_clear_and_test();
    test_bitwise_operations();
    test_shifts();
    test_lsb_operations();
    test_msb_operation();
    test_playable_square_mask();

    if (failures != 0) {
        std::cerr << failures << " bitboard test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All bitboard tests passed\n";
    return EXIT_SUCCESS;
}
