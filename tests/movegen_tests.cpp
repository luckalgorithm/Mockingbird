#include "movegen.h"

#include <array>
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

[[nodiscard]] constexpr bool constexpr_slider_generation() {
    using namespace Mockingbird;

    constexpr Square d1 = make_square(FILE_D, RANK_1);

    Position position;
    position.put_piece(R_ROOK, d1);

    MoveList moves;
    generate_rook_moves(position, moves);
    return moves.size() == 20;
}

static_assert(constexpr_slider_generation());

[[nodiscard]] constexpr bool constexpr_king_generation() {
    using namespace Mockingbird;

    constexpr Square d1 = make_square(FILE_D, RANK_1);

    Position position;
    position.put_piece(R_KING, d1);

    MoveList moves;
    generate_king_moves(position, moves);
    return moves.size() == 3;
}

static_assert(constexpr_king_generation());

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

[[nodiscard]] bool matches_single_source(
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

constexpr std::array<Mockingbird::PieceType, 3> SLIDER_TYPES = {
  Mockingbird::BISHOP,
  Mockingbird::ROOK,
  Mockingbird::QUEEN,
};

[[nodiscard]] Mockingbird::Bitboard slider_attacks(
  Mockingbird::PieceType piece_type,
  Mockingbird::Square source,
  const Mockingbird::Bitboard& occupied) {
    using namespace Mockingbird;

    if (piece_type == BISHOP)
        return bishop_attacks(source, occupied);
    if (piece_type == ROOK)
        return rook_attacks(source, occupied);

    return queen_attacks(source, occupied);
}

void generate_slider_moves(
  Mockingbird::PieceType piece_type,
  const Mockingbird::Position& position,
  Mockingbird::MoveList& moves) {
    using namespace Mockingbird;

    if (piece_type == BISHOP)
        generate_bishop_moves(position, moves);
    else if (piece_type == ROOK)
        generate_rook_moves(position, moves);
    else
        generate_queen_moves(position, moves);
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

                    expect(matches_single_source(moves, source, expected),
                           "knight moves match every single-blocker position");
                }
            }
        }
    }
}

void test_empty_and_inactive_kings() {
    using namespace Mockingbird;

    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square h8 = make_square(FILE_H, RANK_8);

    Position position;
    MoveList moves;
    generate_king_moves(position, moves);
    expect(moves.empty(), "an empty position has no king moves");

    position.put_piece(Y_KING, d1);
    position.put_piece(B_KING, h8);
    generate_king_moves(position, moves);
    expect(moves.empty(), "teammate and opponent kings do not move on Red's turn");
}

void test_known_king_destinations() {
    using namespace Mockingbird;

    constexpr Square h8 = make_square(FILE_H, RANK_8);
    constexpr Square g7 = make_square(FILE_G, RANK_7);
    constexpr Square g8 = make_square(FILE_G, RANK_8);
    constexpr Square g9 = make_square(FILE_G, RANK_9);
    constexpr Square h7 = make_square(FILE_H, RANK_7);

    Position position;
    position.put_piece(R_KING, h8);
    position.put_piece(R_PAWN, g7);
    position.put_piece(Y_ROOK, g8);
    position.put_piece(B_QUEEN, g9);
    position.put_piece(G_BISHOP, h7);

    MoveList moves;
    generate_king_moves(position, moves);

    const Bitboard destinations = destinations_from(moves, h8);
    expect(moves.size() == 6, "two friendly king destinations are excluded");
    expect(!destinations.test(g7), "own piece blocks a king destination");
    expect(!destinations.test(g8), "teammate piece blocks a king destination");
    expect(destinations.test(g9), "Blue piece is a capturable king destination");
    expect(destinations.test(h7), "Green piece is a capturable king destination");

    for (const Move move : moves)
        expect(move.type() == MoveType::NORMAL,
               "pseudo-legal king generation emits only normal moves");
}

void test_attacked_king_destination_is_not_filtered() {
    using namespace Mockingbird;

    constexpr Square h8 = make_square(FILE_H, RANK_8);
    constexpr Square h9 = make_square(FILE_H, RANK_9);
    constexpr Square h14 = make_square(FILE_H, RANK_14);

    Position position;
    position.put_piece(R_KING, h8);
    position.put_piece(B_ROOK, h14);

    MoveList moves;
    generate_king_moves(position, moves);

    expect(destinations_from(moves, h8).test(h9),
           "pseudo-legal king generation retains an attacked destination");
}

void test_king_append_behavior() {
    using namespace Mockingbird;

    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square e1 = make_square(FILE_E, RANK_1);

    Position position;
    position.put_piece(R_KING, d1);

    MoveList moves;
    const Move existing = Move::normal(d1, e1);
    moves.push_back(existing);
    generate_king_moves(position, moves);

    expect(moves[0] == existing, "king generation appends to an existing move list");
    expect(moves.size()
             == 1 + static_cast<std::size_t>(king_attacks(d1).popcount()),
           "appended king moves preserve the existing size");
}

void test_every_single_king_blocker() {
    using namespace Mockingbird;

    // For every player and king source, places each color on every other
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
                    position.put_piece(make_piece(moving_color, KING), source);
                    position.put_piece(make_piece(blocker_color, PAWN), blocker);

                    Bitboard expected = king_attacks(source);
                    if (team_of(blocker_color) == team_of(moving_color))
                        expected.clear(blocker);

                    MoveList moves;
                    generate_king_moves(position, moves);

                    expect(matches_single_source(moves, source, expected),
                           "king moves match every single-blocker position");
                }
            }
        }
    }
}

void test_empty_and_inactive_sliders() {
    using namespace Mockingbird;

    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square h8 = make_square(FILE_H, RANK_8);
    constexpr Square n4 = make_square(FILE_N, RANK_4);

    Position position;
    MoveList moves;
    generate_sliding_moves(position, moves);
    expect(moves.empty(), "an empty position has no sliding moves");

    position.put_piece(Y_BISHOP, d1);
    position.put_piece(B_ROOK, h8);
    position.put_piece(G_QUEEN, n4);
    generate_sliding_moves(position, moves);
    expect(moves.empty(), "inactive sliders do not move on Red's turn");
}

void test_known_rook_blockers() {
    using namespace Mockingbird;

    constexpr Square h8 = make_square(FILE_H, RANK_8);
    constexpr Square h10 = make_square(FILE_H, RANK_10);
    constexpr Square h11 = make_square(FILE_H, RANK_11);
    constexpr Square f8 = make_square(FILE_F, RANK_8);
    constexpr Square e8 = make_square(FILE_E, RANK_8);
    constexpr Square h6 = make_square(FILE_H, RANK_6);
    constexpr Square h5 = make_square(FILE_H, RANK_5);
    constexpr Square j8 = make_square(FILE_J, RANK_8);
    constexpr Square k8 = make_square(FILE_K, RANK_8);

    Position position;
    position.put_piece(R_ROOK, h8);
    position.put_piece(R_PAWN, h10);
    position.put_piece(Y_PAWN, f8);
    position.put_piece(B_PAWN, h6);
    position.put_piece(G_PAWN, j8);

    MoveList moves;
    generate_rook_moves(position, moves);

    const Bitboard destinations = destinations_from(moves, h8);
    expect(!destinations.test(h10), "own piece is excluded from a rook ray");
    expect(!destinations.test(h11), "own piece hides later rook destinations");
    expect(!destinations.test(f8), "teammate piece is excluded from a rook ray");
    expect(!destinations.test(e8), "teammate piece hides later rook destinations");
    expect(destinations.test(h6), "Blue piece is a capturable rook destination");
    expect(!destinations.test(h5), "Blue piece hides later rook destinations");
    expect(destinations.test(j8), "Green piece is a capturable rook destination");
    expect(!destinations.test(k8), "Green piece hides later rook destinations");
}

void test_known_bishop_blockers() {
    using namespace Mockingbird;

    constexpr Square h8 = make_square(FILE_H, RANK_8);
    constexpr Square j10 = make_square(FILE_J, RANK_10);
    constexpr Square k11 = make_square(FILE_K, RANK_11);
    constexpr Square f10 = make_square(FILE_F, RANK_10);
    constexpr Square e11 = make_square(FILE_E, RANK_11);
    constexpr Square j6 = make_square(FILE_J, RANK_6);
    constexpr Square k5 = make_square(FILE_K, RANK_5);
    constexpr Square f6 = make_square(FILE_F, RANK_6);
    constexpr Square e5 = make_square(FILE_E, RANK_5);

    Position position;
    position.put_piece(R_BISHOP, h8);
    position.put_piece(R_PAWN, j10);
    position.put_piece(Y_PAWN, f10);
    position.put_piece(B_PAWN, j6);
    position.put_piece(G_PAWN, f6);

    MoveList moves;
    generate_bishop_moves(position, moves);

    const Bitboard destinations = destinations_from(moves, h8);
    expect(!destinations.test(j10), "own piece is excluded from a bishop ray");
    expect(!destinations.test(k11), "own piece hides later bishop destinations");
    expect(!destinations.test(f10), "teammate piece is excluded from a bishop ray");
    expect(!destinations.test(e11), "teammate piece hides later bishop destinations");
    expect(destinations.test(j6), "Blue piece is a capturable bishop destination");
    expect(!destinations.test(k5), "Blue piece hides later bishop destinations");
    expect(destinations.test(f6), "Green piece is a capturable bishop destination");
    expect(!destinations.test(e5), "Green piece hides later bishop destinations");
}

void test_queen_combines_rook_and_bishop_moves() {
    using namespace Mockingbird;

    constexpr Square h8 = make_square(FILE_H, RANK_8);

    Position position;
    position.put_piece(R_QUEEN, h8);
    position.put_piece(R_PAWN, make_square(FILE_H, RANK_10));
    position.put_piece(Y_PAWN, make_square(FILE_F, RANK_10));
    position.put_piece(B_PAWN, make_square(FILE_H, RANK_6));
    position.put_piece(G_PAWN, make_square(FILE_J, RANK_6));

    MoveList moves;
    generate_queen_moves(position, moves);

    const Bitboard expected =
      queen_attacks(h8, position.occupied()) & ~position.pieces(RED_YELLOW);
    expect(matches_single_source(moves, h8, expected),
           "queen generation combines blocked rook and bishop rays");
}

void test_multiple_sliders_and_append_behavior() {
    using namespace Mockingbird;

    constexpr Square d1 = make_square(FILE_D, RANK_1);
    constexpr Square h8 = make_square(FILE_H, RANK_8);
    constexpr Square n4 = make_square(FILE_N, RANK_4);
    constexpr Square d14 = make_square(FILE_D, RANK_14);
    constexpr Square e1 = make_square(FILE_E, RANK_1);

    Position position;
    position.put_piece(R_BISHOP, d1);
    position.put_piece(R_ROOK, h8);
    position.put_piece(R_QUEEN, n4);
    position.put_piece(Y_QUEEN, d14);

    MoveList moves;
    const Move existing = Move::normal(d1, e1);
    moves.push_back(existing);
    generate_sliding_moves(position, moves);
    expect(moves[0] == existing, "sliding generation appends to an existing move list");

    MoveList generated;
    generate_sliding_moves(position, generated);

    const Bitboard friendly = position.pieces(RED_YELLOW);
    const Bitboard bishop =
      bishop_attacks(d1, position.occupied()) & ~friendly;
    const Bitboard rook =
      rook_attacks(h8, position.occupied()) & ~friendly;
    const Bitboard queen =
      queen_attacks(n4, position.occupied()) & ~friendly;

    expect(destinations_from(generated, d1) == bishop,
           "active bishop contributes its blocked destinations");
    expect(destinations_from(generated, h8) == rook,
           "active rook contributes its blocked destinations");
    expect(destinations_from(generated, n4) == queen,
           "active queen contributes its blocked destinations");
    expect(destinations_from(generated, d14).empty(),
           "teammate queen contributes no moves");
    expect(generated.size()
             == static_cast<std::size_t>(
               bishop.popcount() + rook.popcount() + queen.popcount()),
           "combined slider size equals each active piece's destinations");
}

void test_every_single_slider_blocker() {
    using namespace Mockingbird;

    // For every slider type, player, and source, places each color on every
    // other playable square.
    for (const PieceType piece_type : SLIDER_TYPES) {
        for (int moving_color_index = 0; moving_color_index < COLOR_NB;
             ++moving_color_index) {
            const Color moving_color = Color(moving_color_index);

            for (int source_index = 0; source_index < SQUARE_NB; ++source_index) {
                const Square source = Square(source_index);
                if (!is_ok(source))
                    continue;

                for (int blocker_index = 0; blocker_index < SQUARE_NB;
                     ++blocker_index) {
                    const Square blocker = Square(blocker_index);
                    if (!is_ok(blocker) || blocker == source)
                        continue;

                    for (int blocker_color_index = 0;
                         blocker_color_index < COLOR_NB;
                         ++blocker_color_index) {
                        const Color blocker_color = Color(blocker_color_index);

                        Position position;
                        position.set_side_to_move(moving_color);
                        position.put_piece(
                          make_piece(moving_color, piece_type), source);
                        position.put_piece(
                          make_piece(blocker_color, PAWN), blocker);

                        const Bitboard expected =
                          slider_attacks(piece_type, source, position.occupied())
                          & ~position.pieces(team_of(moving_color));

                        MoveList moves;
                        generate_slider_moves(piece_type, position, moves);

                        expect(matches_single_source(moves, source, expected),
                               "slider moves match every single-blocker position");
                    }
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
    test_empty_and_inactive_kings();
    test_known_king_destinations();
    test_attacked_king_destination_is_not_filtered();
    test_king_append_behavior();
    test_every_single_king_blocker();
    test_empty_and_inactive_sliders();
    test_known_rook_blockers();
    test_known_bishop_blockers();
    test_queen_combines_rook_and_bishop_moves();
    test_multiple_sliders_and_append_behavior();
    test_every_single_slider_blocker();

    if (failures != 0) {
        std::cerr << failures << " move-generation test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All move-generation tests passed\n";
    return EXIT_SUCCESS;
}
