#include "movegen.h"

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

[[nodiscard]] constexpr bool constexpr_move_list_operations() {
    using namespace Mockingbird;

    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square d2 = make_square(FILE_D, RANK_2);

    MoveList moves;
    moves.push_back(Move::normal(d1, d2));

    return moves.size() == 1 && !moves.empty() && !moves.full()
        && moves[0] == Move::normal(d1, d2) && moves.begin() + 1 == moves.end();
}

static_assert(constexpr_move_list_operations());

[[nodiscard]] Mockingbird::Bitboard destinations_from(
  const Mockingbird::MoveList& moves, Mockingbird::Square source) {
    using namespace Mockingbird;

    Bitboard destinations;

    for (const Move move : moves) {
        if (move.from() == source)
            destinations.set(move.to());
    }

    return destinations;
}

[[nodiscard]] bool matches_single_knight(
  const Mockingbird::MoveList& moves,
  Mockingbird::Square source,
  const Mockingbird::Bitboard& expected_destinations) {
    using namespace Mockingbird;

    if (moves.size() != static_cast<std::size_t>(expected_destinations.popcount()))
        return false;

    Bitboard actual_destinations;

    for (const Move move : moves) {
        if (move.type() != MoveType::NORMAL || move.from() != source
            || actual_destinations.test(move.to()))
            return false;

        actual_destinations.set(move.to());
    }

    return actual_destinations == expected_destinations;
}

void test_move_list() {
    using namespace Mockingbird;

    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square d2 = make_square(FILE_D, RANK_2);
    constexpr Square d3 = make_square(FILE_D, RANK_3);
    constexpr Move first = Move::normal(d1, d2);
    constexpr Move second = Move::normal(d2, d3);

    MoveList moves;
    expect(moves.empty(), "default move list is empty");
    expect(moves.size() == 0, "default move list has size zero");
    expect(moves.capacity() == 256, "move list has fixed capacity 256");

    moves.push_back(first);
    moves.push_back(second);

    expect(moves.size() == 2, "push_back increases the move-list size");
    expect(moves[0] == first, "indexing returns the first move");
    expect(moves[1] == second, "indexing returns the second move");
    expect(moves.begin() + 2 == moves.end(), "iteration spans the stored moves");

    moves.clear();
    expect(moves.empty(), "clear resets the move-list size");

    for (std::size_t index = 0; index < moves.capacity(); ++index)
        moves.push_back(first);

    expect(moves.full(), "a move list is full at its fixed capacity");
    expect(moves.size() == moves.capacity(), "full move list reports its capacity");
}

void test_empty_and_inactive_knights() {
    using namespace Mockingbird;

    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square h8 = make_square(FILE_H, RANK_8);

    Position position;
    MoveList moves;
    generate_knight_moves(position, moves);
    expect(moves.empty(), "an empty position has no knight moves");

    position.put_piece(Y_KNIGHT, d1);
    position.put_piece(B_KNIGHT, h8);
    generate_knight_moves(position, moves);
    expect(moves.empty(), "teammate and opponent knights do not move on Red's turn");
}

void test_known_friendly_and_enemy_destinations() {
    using namespace Mockingbird;

    constexpr Square h8 = make_square(FILE_H, RANK_8);
    constexpr Square f7 = make_square(FILE_F, RANK_7);
    constexpr Square f9 = make_square(FILE_F, RANK_9);
    constexpr Square g6 = make_square(FILE_G, RANK_6);
    constexpr Square g10 = make_square(FILE_G, RANK_10);

    Position position;
    position.put_piece(R_KNIGHT, h8);
    position.put_piece(R_PAWN, f7);
    position.put_piece(Y_ROOK, f9);
    position.put_piece(B_QUEEN, g6);
    position.put_piece(G_BISHOP, g10);

    MoveList moves;
    generate_knight_moves(position, moves);

    const Bitboard destinations = destinations_from(moves, h8);
    expect(moves.size() == 6, "two friendly destinations are excluded");
    expect(!destinations.test(f7), "own piece blocks a knight destination");
    expect(!destinations.test(f9), "teammate piece blocks a knight destination");
    expect(destinations.test(g6), "Blue piece is a capturable knight destination");
    expect(destinations.test(g10), "Green piece is a capturable knight destination");
}

void test_multiple_active_knights_and_append_behavior() {
    using namespace Mockingbird;

    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square h8 = make_square(FILE_H, RANK_8);
    constexpr Square n4 = make_square(FILE_N, RANK_4);
    constexpr Square d14 = make_square(FILE_D, RANK_14);
    constexpr Square e1 = make_square(FILE_E, RANK_1);

    Position position;
    position.put_piece(R_KNIGHT, d1);
    position.put_piece(R_KNIGHT, h8);
    position.put_piece(Y_KNIGHT, n4);
    position.put_piece(B_KNIGHT, d14);

    MoveList moves;
    const Move existing = Move::normal(d1, e1);
    moves.push_back(existing);
    generate_knight_moves(position, moves);

    expect(moves[0] == existing, "knight generation appends to an existing move list");

    MoveList generated;
    generate_knight_moves(position, generated);

    expect(destinations_from(generated, d1) == knight_attacks(d1),
           "first Red knight contributes all empty-board destinations");
    expect(destinations_from(generated, h8) == knight_attacks(h8),
           "second Red knight contributes all empty-board destinations");
    expect(destinations_from(generated, n4).empty(),
           "Yellow teammate knight contributes no moves");
    expect(destinations_from(generated, d14).empty(),
           "Blue opponent knight contributes no moves");
    expect(generated.size()
             == static_cast<std::size_t>(
               knight_attacks(d1).popcount() + knight_attacks(h8).popcount()),
           "generated size equals both active knights' destinations");
}

void test_every_single_blocker() {
    using namespace Mockingbird;

    // For every player and knight source, places each color on every other
    // playable square. This covers all own, teammate, and opponent blockers.
    for (int moving_color_index = 0; moving_color_index < COLOR_NB;
         ++moving_color_index) {
        const Color moving_color = Color(moving_color_index);

        for (int source_index = 0; source_index < SQUARE_NB; ++source_index) {
            const Square source = Square(source_index);
            if (!is_ok(source))
                continue;

            for (int blocker_index = 0; blocker_index < SQUARE_NB; ++blocker_index) {
                const Square blocker = Square(blocker_index);
                if (!is_ok(blocker) || blocker == source)
                    continue;

                for (int blocker_color_index = 0; blocker_color_index < COLOR_NB;
                     ++blocker_color_index) {
                    const Color blocker_color = Color(blocker_color_index);

                    Position position;
                    position.set_side_to_move(moving_color);
                    position.put_piece(make_piece(moving_color, KNIGHT), source);
                    position.put_piece(make_piece(blocker_color, PAWN), blocker);

                    Bitboard expected = knight_attacks(source);
                    if (team_of(blocker_color) == team_of(moving_color))
                        expected.clear(blocker);

                    MoveList moves;
                    generate_knight_moves(position, moves);

                    expect(matches_single_knight(moves, source, expected),
                           "knight moves match every single-blocker position");
                }
            }
        }
    }
}

}  // namespace

int main() {
    test_move_list();
    test_empty_and_inactive_knights();
    test_known_friendly_and_enemy_destinations();
    test_multiple_active_knights_and_append_behavior();
    test_every_single_blocker();

    if (failures != 0) {
        std::cerr << failures << " move-generation test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All move-generation tests passed\n";
    return EXIT_SUCCESS;
}
