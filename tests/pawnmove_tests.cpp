#include "movegen.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>
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

constexpr std::array<Mockingbird::PieceType, 4> PROMOTION_TYPES = {
  Mockingbird::QUEEN,
  Mockingbird::ROOK,
  Mockingbird::BISHOP,
  Mockingbird::KNIGHT,
};

struct Delta {
    int file;
    int rank;
};

constexpr std::array<std::array<Delta, 2>, Mockingbird::COLOR_NB>
  PAWN_CAPTURE_DELTAS = {{
    {{{-1, 1}, {1, 1}}},
    {{{1, 1}, {1, -1}}},
    {{{1, -1}, {-1, -1}}},
    {{{-1, -1}, {-1, 1}}},
  }};

[[nodiscard]] constexpr int forward_coordinate(
  Mockingbird::Color color, Mockingbird::Square square) {
    using namespace Mockingbird;

    return color == RED    ? rank_of(square)
         : color == BLUE   ? file_of(square)
         : color == YELLOW ? BOARD_RANKS + 1 - rank_of(square)
                           : BOARD_FILES + 1 - file_of(square);
}

[[nodiscard]] constexpr Mockingbird::Square coordinate_destination(
  Mockingbird::Square source, int file_delta, int rank_delta) {
    using namespace Mockingbird;

    const int file = file_of(source) + file_delta;
    const int rank = rank_of(source) + rank_delta;

    if (file < FILE_A || file > FILE_N || rank < RANK_1 || rank > RANK_14)
        return SQ_NONE;

    const Square destination = make_square(File(file), Rank(rank));
    return is_ok(destination) ? destination : SQ_NONE;
}

[[nodiscard]] constexpr Mockingbird::Square expected_push_destination(
  Mockingbird::Color color, Mockingbird::Square source, int distance) {
    using namespace Mockingbird;

    if (!is_ok(source))
        return SQ_NONE;
    if (distance == 2 && forward_coordinate(color, source) != 2)
        return SQ_NONE;

    const int file_step = color == BLUE ? 1 : color == GREEN ? -1 : 0;
    const int rank_step = color == RED ? 1 : color == YELLOW ? -1 : 0;
    Square destination = source;

    for (int step = 0; step < distance; ++step) {
        destination =
          coordinate_destination(destination, file_step, rank_step);
        if (destination == SQ_NONE)
            return SQ_NONE;
    }

    return destination;
}

[[nodiscard]] constexpr std::array<Mockingbird::Square, 2>
expected_capture_destinations(
  Mockingbird::Color color, Mockingbird::Square source) {
    using namespace Mockingbird;

    const auto& deltas = PAWN_CAPTURE_DELTAS[std::size_t(color)];
    std::array<Square, 2> destinations = {
      coordinate_destination(source, deltas[0].file, deltas[0].rank),
      coordinate_destination(source, deltas[1].file, deltas[1].rank),
    };

    if (destinations[1] != SQ_NONE
        && (destinations[0] == SQ_NONE || destinations[1] < destinations[0]))
        std::swap(destinations[0], destinations[1]);

    return destinations;
}

void append_expected_pawn_move(
  Mockingbird::Color color,
  Mockingbird::Square from,
  Mockingbird::Square to,
  Mockingbird::MoveList& moves) {
    using namespace Mockingbird;

    if (forward_coordinate(color, to) != 11) {
        moves.push_back(Move::normal(from, to));
        return;
    }

    for (const PieceType promotion_type : PROMOTION_TYPES)
        moves.push_back(Move::promotion(from, to, promotion_type));
}

void append_expected_en_passant_move(
  Mockingbird::Color color,
  Mockingbird::Square from,
  Mockingbird::Square to,
  Mockingbird::MoveList& moves) {
    using namespace Mockingbird;

    if (forward_coordinate(color, to) != 11) {
        moves.push_back(Move::en_passant(from, to));
        return;
    }

    for (const PieceType promotion_type : PROMOTION_TYPES)
        moves.push_back(
          Move::en_passant(from, to, promotion_type));
}

[[nodiscard]] bool move_lists_equal(
  const Mockingbird::MoveList& left, const Mockingbird::MoveList& right) {
    if (left.size() != right.size())
        return false;

    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index] != right[index])
            return false;
    }

    return true;
}

[[nodiscard]] bool contains_move(
  const Mockingbird::MoveList& moves, Mockingbird::Move target) {
    for (const Mockingbird::Move move : moves) {
        if (move == target)
            return true;
    }

    return false;
}

[[nodiscard]] constexpr Mockingbird::Square rotate_clockwise(
  Mockingbird::Square square) {
    using namespace Mockingbird;

    return make_square(
      File(rank_of(square)), Rank(BOARD_FILES + 1 - file_of(square)));
}

[[nodiscard]] Mockingbird::Position rotate_clockwise(
  const Mockingbird::Position& position) {
    using namespace Mockingbird;

    Position rotated;
    rotated.set_side_to_move(next_color(position.side_to_move()));

    for (int square_index = 0; square_index < SQUARE_NB; ++square_index) {
        const Square square = Square(square_index);
        if (!is_ok(square))
            continue;

        const Piece piece = position.piece_on(square);
        if (piece == NO_PIECE)
            continue;

        rotated.put_piece(
          make_piece(next_color(color_of(piece)), type_of(piece)),
          rotate_clockwise(square));
    }

    for (int owner_index = 0; owner_index < COLOR_NB; ++owner_index) {
        const Color owner = Color(owner_index);
        const Square target = position.en_passant_square(owner);
        if (target != SQ_NONE) {
            rotated.set_en_passant_square(
              next_color(owner), rotate_clockwise(target));
        }
    }

    return rotated;
}

[[nodiscard]] Mockingbird::Move rotate_clockwise(Mockingbird::Move move) {
    using namespace Mockingbird;

    const Square from = rotate_clockwise(move.from());
    const Square to = rotate_clockwise(move.to());

    switch (move.type()) {
        case MoveType::NORMAL:
            return Move::normal(from, to);
        case MoveType::PROMOTION:
            return Move::promotion(
              from, to, move.promotion_type());
        case MoveType::CASTLING:
            return Move::castling(from, to);
        case MoveType::EN_PASSANT:
            return move.is_promotion()
              ? Move::en_passant(
                  from, to, move.promotion_type())
              : Move::en_passant(from, to);
        case MoveType::COUNT:
            break;
    }

    std::unreachable();
}

[[nodiscard]] bool move_sets_are_rotations(
  const Mockingbird::MoveList& original,
  const Mockingbird::MoveList& rotated) {
    if (original.size() != rotated.size())
        return false;

    std::array<bool, Mockingbird::MoveList::CAPACITY> matched{};

    for (const Mockingbird::Move move : original) {
        const Mockingbird::Move expected =
          rotate_clockwise(move);
        bool found = false;

        for (std::size_t index = 0; index < rotated.size(); ++index) {
            if (!matched[index] && rotated[index] == expected) {
                matched[index] = true;
                found = true;
                break;
            }
        }

        if (!found)
            return false;
    }

    return true;
}

[[nodiscard]] constexpr bool constexpr_pawn_generation() {
    using namespace Mockingbird;

    constexpr Square d2 = make_square(FILE_D, RANK_2);
    constexpr Square d3 = make_square(FILE_D, RANK_3);
    constexpr Square d4 = make_square(FILE_D, RANK_4);

    Position position;
    position.put_piece(R_PAWN, d2);

    MoveList moves;
    generate_pawn_moves(position, moves);

    return moves.size() == 2 && moves[0] == Move::normal(d2, d3)
        && moves[1] == Move::normal(d2, d4);
}

static_assert(constexpr_pawn_generation());

[[nodiscard]] constexpr bool constexpr_en_passant_generation() {
    using namespace Mockingbird;

    constexpr Square d5 = make_square(FILE_D, RANK_5);
    constexpr Square c6 = make_square(FILE_C, RANK_6);
    constexpr Square d6 = make_square(FILE_D, RANK_6);

    Position ordinary;
    ordinary.put_piece(R_PAWN, d5);
    ordinary.put_piece(B_PAWN, d6);
    ordinary.set_en_passant_square(BLUE, c6);

    MoveList ordinary_moves;
    generate_pawn_moves(ordinary, ordinary_moves);
    if (ordinary_moves.size() != 1
        || ordinary_moves[0] != Move::en_passant(d5, c6))
        return false;

    constexpr Square b10 = make_square(FILE_B, RANK_10);
    constexpr Square b11 = make_square(FILE_B, RANK_11);
    constexpr Square c11 = make_square(FILE_C, RANK_11);
    constexpr Square d11 = make_square(FILE_D, RANK_11);

    Position promotion;
    promotion.put_piece(R_PAWN, b10);
    promotion.put_piece(Y_ROOK, b11);
    promotion.put_piece(B_PAWN, d11);
    promotion.set_en_passant_square(BLUE, c11);

    MoveList promotion_moves;
    generate_pawn_moves(promotion, promotion_moves);
    if (promotion_moves.size() != PROMOTION_TYPES.size())
        return false;

    for (std::size_t index = 0; index < PROMOTION_TYPES.size(); ++index) {
        if (promotion_moves[index]
            != Move::en_passant(
              b10, c11, PROMOTION_TYPES[index]))
            return false;
    }

    return true;
}

static_assert(constexpr_en_passant_generation());

void test_known_single_and_double_pushes() {
    using namespace Mockingbird;

    constexpr std::array<Square, COLOR_NB> sources = {
      make_square(FILE_D, RANK_2),
      make_square(FILE_B, RANK_4),
      make_square(FILE_D, RANK_13),
      make_square(FILE_M, RANK_4),
    };
    constexpr std::array<Square, COLOR_NB> single_destinations = {
      make_square(FILE_D, RANK_3),
      make_square(FILE_C, RANK_4),
      make_square(FILE_D, RANK_12),
      make_square(FILE_L, RANK_4),
    };
    constexpr std::array<Square, COLOR_NB> double_destinations = {
      make_square(FILE_D, RANK_4),
      make_square(FILE_D, RANK_4),
      make_square(FILE_D, RANK_11),
      make_square(FILE_K, RANK_4),
    };

    for (int color_index = 0; color_index < COLOR_NB; ++color_index) {
        const Color color = Color(color_index);

        Position position;
        position.set_side_to_move(color);
        position.put_piece(make_piece(color, PAWN), sources[std::size_t(color)]);

        MoveList moves;
        generate_pawn_moves(position, moves);

        expect(moves.size() == 2, "unblocked starting pawn has two quiet moves");
        expect(moves[0]
                 == Move::normal(
                   sources[std::size_t(color)],
                   single_destinations[std::size_t(color)]),
               "single push is generated first");
        expect(moves[1]
                 == Move::normal(
                   sources[std::size_t(color)],
                   double_destinations[std::size_t(color)]),
               "double push is generated second");
    }
}

void test_blocked_pushes() {
    using namespace Mockingbird;

    constexpr Square d2 = make_square(FILE_D, RANK_2);
    constexpr Square d3 = make_square(FILE_D, RANK_3);
    constexpr Square d4 = make_square(FILE_D, RANK_4);

    Position blocked_first;
    blocked_first.put_piece(R_PAWN, d2);
    blocked_first.put_piece(B_ROOK, d3);

    MoveList first_moves;
    generate_pawn_moves(blocked_first, first_moves);
    expect(first_moves.empty(), "occupied first square blocks both quiet pushes");

    Position blocked_second;
    blocked_second.put_piece(R_PAWN, d2);
    blocked_second.put_piece(B_ROOK, d4);

    MoveList second_moves;
    generate_pawn_moves(blocked_second, second_moves);
    expect(second_moves.size() == 1, "occupied second square blocks only double push");
    expect(second_moves[0] == Move::normal(d2, d3),
           "single push remains when the double destination is occupied");
}

void test_capture_team_rules() {
    using namespace Mockingbird;

    constexpr Square h8 = make_square(FILE_H, RANK_8);
    constexpr Square g9 = make_square(FILE_G, RANK_9);
    constexpr Square h9 = make_square(FILE_H, RANK_9);
    constexpr Square i9 = make_square(FILE_I, RANK_9);

    Position friendly;
    friendly.put_piece(R_PAWN, h8);
    friendly.put_piece(B_ROOK, h9);
    friendly.put_piece(R_ROOK, g9);
    friendly.put_piece(Y_ROOK, i9);

    MoveList friendly_moves;
    generate_pawn_moves(friendly, friendly_moves);
    expect(friendly_moves.empty(), "own and teammate pieces cannot be captured");

    Position enemies;
    enemies.put_piece(R_PAWN, h8);
    enemies.put_piece(R_ROOK, h9);
    enemies.put_piece(B_ROOK, g9);
    enemies.put_piece(G_ROOK, i9);

    MoveList enemy_moves;
    generate_pawn_moves(enemies, enemy_moves);
    expect(enemy_moves.size() == 2, "both opponents can be captured");
    expect(enemy_moves[0] == Move::normal(h8, g9),
           "lower mailbox capture is generated first");
    expect(enemy_moves[1] == Move::normal(h8, i9),
           "higher mailbox capture is generated second");
}

void test_quiet_promotions() {
    using namespace Mockingbird;

    constexpr Square h10 = make_square(FILE_H, RANK_10);
    constexpr Square h11 = make_square(FILE_H, RANK_11);

    Position position;
    position.put_piece(R_PAWN, h10);

    MoveList moves;
    generate_pawn_moves(position, moves);

    expect(moves.size() == PROMOTION_TYPES.size(),
           "quiet promotion emits four moves");

    for (std::size_t index = 0; index < PROMOTION_TYPES.size(); ++index) {
        expect(moves[index]
                 == Move::promotion(h10, h11, PROMOTION_TYPES[index]),
               "quiet promotion order is queen, rook, bishop, knight");
    }
}

void test_capture_promotions() {
    using namespace Mockingbird;

    constexpr Square h10 = make_square(FILE_H, RANK_10);
    constexpr Square g11 = make_square(FILE_G, RANK_11);
    constexpr Square h11 = make_square(FILE_H, RANK_11);
    constexpr Square i11 = make_square(FILE_I, RANK_11);

    Position position;
    position.put_piece(R_PAWN, h10);
    position.put_piece(R_ROOK, h11);
    position.put_piece(B_ROOK, g11);
    position.put_piece(G_ROOK, i11);

    MoveList moves;
    generate_pawn_moves(position, moves);

    expect(moves.size() == 2 * PROMOTION_TYPES.size(),
           "two promotion captures emit eight moves");

    for (std::size_t index = 0; index < PROMOTION_TYPES.size(); ++index) {
        expect(moves[index]
                 == Move::promotion(h10, g11, PROMOTION_TYPES[index]),
               "first capture emits all four promotion types");
        expect(moves[index + PROMOTION_TYPES.size()]
                 == Move::promotion(h10, i11, PROMOTION_TYPES[index]),
               "second capture emits all four promotion types");
    }
}

void test_every_en_passant_capture() {
    using namespace Mockingbird;

    std::size_t capture_arrangements = 0;
    std::size_t promotion_arrangements = 0;
    std::size_t generated_en_passant_moves = 0;

    for (int owner_index = 0; owner_index < COLOR_NB; ++owner_index) {
        const Color owner = Color(owner_index);

        for (int owner_source_index = 0;
             owner_source_index < SQUARE_NB;
             ++owner_source_index) {
            const Square owner_source = Square(owner_source_index);
            const Square target =
              expected_push_destination(owner, owner_source, 1);
            const Square victim =
              expected_push_destination(owner, owner_source, 2);
            if (target == SQ_NONE || victim == SQ_NONE)
                continue;

            for (int moving_color_index = 0;
                 moving_color_index < COLOR_NB;
                 ++moving_color_index) {
                const Color moving_color =
                  Color(moving_color_index);
                if (team_of(moving_color) == team_of(owner))
                    continue;

                for (int capturer_index = 0;
                     capturer_index < SQUARE_NB;
                     ++capturer_index) {
                    const Square capturer = Square(capturer_index);
                    if (!is_ok(capturer))
                        continue;

                    const auto captures =
                      expected_capture_destinations(
                        moving_color, capturer);
                    if (captures[0] != target
                        && captures[1] != target)
                        continue;

                    Position position;
                    position.set_side_to_move(moving_color);
                    position.put_piece(
                      make_piece(moving_color, PAWN), capturer);
                    position.put_piece(
                      make_piece(owner, PAWN), victim);
                    position.set_en_passant_square(owner, target);

                    MoveList expected;
                    append_expected_en_passant_move(
                      moving_color, capturer, target, expected);

                    MoveList generated;
                    generate_pawn_moves(position, generated);

                    MoveList actual;
                    for (const Move move : generated) {
                        if (move.type() == MoveType::EN_PASSANT)
                            actual.push_back(move);
                    }

                    expect(move_lists_equal(actual, expected),
                           "every geometrically valid en-passant capture is generated");
                    expect(position.en_passant_square(owner) == target,
                           "pawn generation preserves the en-passant target");
                    expect(position.piece_on(victim)
                             == make_piece(owner, PAWN),
                           "pawn generation preserves the en-passant victim");

                    ++capture_arrangements;
                    generated_en_passant_moves += actual.size();
                    if (expected.size() == PROMOTION_TYPES.size())
                        ++promotion_arrangements;
                }
            }
        }
    }

    expect(capture_arrangements == 120,
           "all 120 legal owner-capturer arrangements were tested");
    expect(promotion_arrangements == 16,
           "all 16 en-passant promotion arrangements were tested");
    expect(generated_en_passant_moves == 168,
           "all 168 en-passant move encodings were tested");
}

void test_en_passant_target_occupancy() {
    using namespace Mockingbird;

    constexpr Square d5 = make_square(FILE_D, RANK_5);
    constexpr Square c6 = make_square(FILE_C, RANK_6);
    constexpr Square d6 = make_square(FILE_D, RANK_6);
    constexpr std::array<Piece, 4> occupants = {
      R_ROOK,
      Y_ROOK,
      B_ROOK,
      G_ROOK,
    };

    for (const Piece occupant : occupants) {
        Position position;
        position.put_piece(R_PAWN, d5);
        position.put_piece(B_PAWN, d6);
        position.put_piece(occupant, c6);
        position.set_en_passant_square(BLUE, c6);

        MoveList moves;
        generate_pawn_moves(position, moves);

        const bool occupant_is_opponent =
          team_of(color_of(occupant)) != team_of(RED);
        expect(contains_move(
                 moves, Move::en_passant(d5, c6))
                 == occupant_is_opponent,
               "an opposing target occupant can be captured en passant");
        expect(!contains_move(
                 moves, Move::normal(d5, c6)),
               "an en-passant target does not also emit a normal capture");
        expect(moves.size()
                 == (occupant_is_opponent ? std::size_t{1}
                                          : std::size_t{0}),
               "occupied target generates exactly one applicable capture");
    }

    constexpr Square b10 = make_square(FILE_B, RANK_10);
    constexpr Square b11 = make_square(FILE_B, RANK_11);
    constexpr Square c11 = make_square(FILE_C, RANK_11);
    constexpr Square d11 = make_square(FILE_D, RANK_11);

    Position promotion;
    promotion.put_piece(R_PAWN, b10);
    promotion.put_piece(Y_ROOK, b11);
    promotion.put_piece(B_ROOK, c11);
    promotion.put_piece(B_PAWN, d11);
    promotion.set_en_passant_square(BLUE, c11);

    MoveList promotion_moves;
    generate_pawn_moves(promotion, promotion_moves);

    expect(promotion_moves.size() == PROMOTION_TYPES.size(),
           "occupied promotion target emits four en-passant captures");
    for (std::size_t index = 0; index < PROMOTION_TYPES.size(); ++index) {
        expect(promotion_moves[index]
                 == Move::en_passant(
                   b10, c11, PROMOTION_TYPES[index]),
               "occupied target uses an en-passant promotion encoding");
    }
}

void test_en_passant_owner_and_victim_validation() {
    using namespace Mockingbird;

    constexpr Square d5 = make_square(FILE_D, RANK_5);
    constexpr Square c6 = make_square(FILE_C, RANK_6);
    constexpr Square d6 = make_square(FILE_D, RANK_6);
    constexpr std::array<Piece, 5> invalid_victims = {
      NO_PIECE,
      B_ROOK,
      R_PAWN,
      Y_PAWN,
      G_PAWN,
    };

    for (const Piece victim : invalid_victims) {
        Position position;
        position.put_piece(R_PAWN, d5);
        if (victim != NO_PIECE)
            position.put_piece(victim, d6);
        position.set_en_passant_square(BLUE, c6);

        MoveList moves;
        generate_pawn_moves(position, moves);
        expect(!contains_move(
                 moves, Move::en_passant(d5, c6)),
               "en passant requires the owning pawn on the victim square");
    }

    Position stale_target;
    stale_target.put_piece(R_PAWN, d5);
    stale_target.put_piece(G_ROOK, c6);
    stale_target.set_en_passant_square(BLUE, c6);

    MoveList stale_target_moves;
    generate_pawn_moves(stale_target, stale_target_moves);
    expect(contains_move(
             stale_target_moves, Move::normal(d5, c6)),
           "invalid en-passant state does not suppress an ordinary capture");
    expect(!contains_move(
             stale_target_moves, Move::en_passant(d5, c6)),
           "invalid en-passant state does not emit an en-passant capture");

    constexpr Square e6 = make_square(FILE_E, RANK_6);
    constexpr Square f6 = make_square(FILE_F, RANK_6);

    Position invalid_geometry;
    invalid_geometry.put_piece(R_PAWN, d5);
    invalid_geometry.put_piece(B_PAWN, f6);
    invalid_geometry.put_piece(G_ROOK, e6);
    invalid_geometry.set_en_passant_square(BLUE, e6);

    MoveList invalid_geometry_moves;
    generate_pawn_moves(
      invalid_geometry, invalid_geometry_moves);
    expect(!contains_move(
             invalid_geometry_moves,
             Move::en_passant(d5, e6)),
           "target geometry must match the owner's double push");
    expect(contains_move(
             invalid_geometry_moves, Move::normal(d5, e6)),
           "invalid target geometry does not suppress an ordinary capture");

    constexpr Square e2 = make_square(FILE_E, RANK_2);
    constexpr Square d3 = make_square(FILE_D, RANK_3);
    constexpr Square d4 = make_square(FILE_D, RANK_4);

    Position own_target;
    own_target.put_piece(R_PAWN, e2);
    own_target.put_piece(R_PAWN, d4);
    own_target.set_en_passant_square(RED, d3);

    MoveList own_target_moves;
    generate_pawn_moves(own_target, own_target_moves);
    expect(!contains_move(
             own_target_moves,
             Move::en_passant(e2, d3)),
           "a pawn cannot capture its own en-passant target");

    constexpr Square c11 = make_square(FILE_C, RANK_11);
    constexpr Square d11 = make_square(FILE_D, RANK_11);
    constexpr Square d12 = make_square(FILE_D, RANK_12);

    Position teammate_target;
    teammate_target.put_piece(R_PAWN, c11);
    teammate_target.put_piece(Y_PAWN, d11);
    teammate_target.set_en_passant_square(YELLOW, d12);

    MoveList teammate_target_moves;
    generate_pawn_moves(
      teammate_target, teammate_target_moves);
    expect(!contains_move(
             teammate_target_moves,
             Move::en_passant(c11, d12)),
           "a pawn cannot capture its teammate's en-passant target");

    Position occupied_teammate_target;
    occupied_teammate_target.put_piece(R_PAWN, c11);
    occupied_teammate_target.put_piece(Y_PAWN, d11);
    occupied_teammate_target.put_piece(B_ROOK, d12);
    occupied_teammate_target.set_en_passant_square(
      YELLOW, d12);

    MoveList occupied_teammate_moves;
    generate_pawn_moves(
      occupied_teammate_target, occupied_teammate_moves);
    expect(occupied_teammate_moves.size() == 1
             && occupied_teammate_moves[0]
                  == Move::normal(c11, d12),
           "teammate-owned target follows ordinary capture rules");

    constexpr Square h5 = make_square(FILE_H, RANK_5);
    constexpr Square h6 = make_square(FILE_H, RANK_6);

    Position nonattacking;
    nonattacking.put_piece(R_PAWN, h5);
    nonattacking.put_piece(B_PAWN, d6);
    nonattacking.set_en_passant_square(BLUE, c6);

    MoveList nonattacking_moves;
    generate_pawn_moves(nonattacking, nonattacking_moves);
    expect(nonattacking_moves.size() == 1
             && nonattacking_moves[0] == Move::normal(h5, h6),
           "a pawn must attack the target to capture en passant");
}

void test_multiple_en_passant_targets_and_capturers() {
    using namespace Mockingbird;

    constexpr Square b5 = make_square(FILE_B, RANK_5);
    constexpr Square d5 = make_square(FILE_D, RANK_5);
    constexpr Square c6 = make_square(FILE_C, RANK_6);
    constexpr Square d6 = make_square(FILE_D, RANK_6);
    constexpr Square k7 = make_square(FILE_K, RANK_7);
    constexpr Square l8 = make_square(FILE_L, RANK_8);
    constexpr Square k8 = make_square(FILE_K, RANK_8);
    constexpr Move existing =
      Move::normal(
        make_square(FILE_E, RANK_1),
        make_square(FILE_F, RANK_1));

    Position position;
    position.put_piece(R_PAWN, b5);
    position.put_piece(R_PAWN, d5);
    position.put_piece(R_PAWN, k7);
    position.put_piece(B_PAWN, d6);
    position.put_piece(G_PAWN, k8);
    position.set_en_passant_square(BLUE, c6);
    position.set_en_passant_square(GREEN, l8);

    MoveList moves;
    moves.push_back(existing);
    generate_pawn_moves(position, moves);

    MoveList en_passant_moves;
    for (const Move move : moves) {
        if (move.type() == MoveType::EN_PASSANT)
            en_passant_moves.push_back(move);
    }

    MoveList expected;
    expected.push_back(Move::en_passant(b5, c6));
    expected.push_back(Move::en_passant(d5, c6));
    expected.push_back(Move::en_passant(k7, l8));

    expect(moves[0] == existing,
           "pawn generation preserves moves already in the list");
    expect(move_lists_equal(en_passant_moves, expected),
           "all opposing targets and attacking pawns generate independently");
    expect(position.en_passant_square(BLUE) == c6
             && position.en_passant_square(GREEN) == l8,
           "generation does not clear active en-passant targets");
}

void test_en_passant_promotion_order() {
    using namespace Mockingbird;

    constexpr Square b10 = make_square(FILE_B, RANK_10);
    constexpr Square d10 = make_square(FILE_D, RANK_10);
    constexpr Square b11 = make_square(FILE_B, RANK_11);
    constexpr Square c11 = make_square(FILE_C, RANK_11);
    constexpr Square d11 = make_square(FILE_D, RANK_11);

    Position position;
    position.put_piece(R_PAWN, b10);
    position.put_piece(R_PAWN, d10);
    position.put_piece(Y_ROOK, b11);
    position.put_piece(B_PAWN, d11);
    position.set_en_passant_square(BLUE, c11);

    MoveList moves;
    generate_pawn_moves(position, moves);

    expect(moves.size() == 2 * PROMOTION_TYPES.size(),
           "two en-passant capturers emit eight promotions");

    for (std::size_t index = 0; index < PROMOTION_TYPES.size(); ++index) {
        expect(moves[index]
                 == Move::en_passant(
                   b10, c11, PROMOTION_TYPES[index]),
               "first en-passant source emits queen, rook, bishop, knight");
        expect(moves[index + PROMOTION_TYPES.size()]
                 == Move::en_passant(
                   d10, c11, PROMOTION_TYPES[index]),
               "second en-passant source emits queen, rook, bishop, knight");
    }
}

void test_multiple_pawns_and_append_behavior() {
    using namespace Mockingbird;

    constexpr Square d2 = make_square(FILE_D, RANK_2);
    constexpr Square d3 = make_square(FILE_D, RANK_3);
    constexpr Square d4 = make_square(FILE_D, RANK_4);
    constexpr Square h10 = make_square(FILE_H, RANK_10);
    constexpr Square h11 = make_square(FILE_H, RANK_11);
    constexpr Move existing =
      Move::normal(make_square(FILE_E, RANK_1), make_square(FILE_F, RANK_1));

    Position position;
    position.put_piece(R_PAWN, d2);
    position.put_piece(R_PAWN, h10);

    MoveList expected;
    expected.push_back(existing);
    expected.push_back(Move::normal(d2, d3));
    expected.push_back(Move::normal(d2, d4));
    for (const PieceType promotion_type : PROMOTION_TYPES)
        expected.push_back(Move::promotion(h10, h11, promotion_type));

    MoveList actual;
    actual.push_back(existing);
    generate_pawn_moves(position, actual);

    expect(move_lists_equal(actual, expected),
           "multiple pawns are generated by source order and append to the list");
}

void test_every_local_occupancy_combination() {
    using namespace Mockingbird;

    // Each relevant destination is independently empty or occupied by the
    // moving player, the teammate, or either opponent.
    for (int moving_color_index = 0; moving_color_index < COLOR_NB;
         ++moving_color_index) {
        const Color moving_color = Color(moving_color_index);
        const std::array<Color, 5> state_colors = {
          moving_color,
          moving_color,
          Color((moving_color + 2) % COLOR_NB),
          next_color(moving_color),
          previous_color(moving_color),
        };

        for (int source_index = 0; source_index < SQUARE_NB; ++source_index) {
            const Square source = Square(source_index);
            if (!is_ok(source))
                continue;

            const Square single =
              expected_push_destination(moving_color, source, 1);
            const Square double_push =
              expected_push_destination(moving_color, source, 2);
            const auto captures =
              expected_capture_destinations(moving_color, source);

            std::array<Square, 4> relevant{};
            std::size_t relevant_count = 0;

            const auto add_relevant = [&](Square square) {
                if (square == SQ_NONE)
                    return;

                for (std::size_t index = 0; index < relevant_count; ++index) {
                    if (relevant[index] == square)
                        return;
                }

                relevant[relevant_count++] = square;
            };

            add_relevant(single);
            add_relevant(double_push);
            add_relevant(captures[0]);
            add_relevant(captures[1]);

            std::size_t case_count = 1;
            for (std::size_t index = 0; index < relevant_count; ++index)
                case_count *= state_colors.size();

            for (std::size_t occupancy_case = 0;
                 occupancy_case < case_count;
                 ++occupancy_case) {
                Position position;
                position.set_side_to_move(moving_color);
                position.put_piece(
                  make_piece(moving_color, PAWN), source);

                std::size_t encoded_states = occupancy_case;
                for (std::size_t index = 0; index < relevant_count; ++index) {
                    const std::size_t state =
                      encoded_states % state_colors.size();
                    encoded_states /= state_colors.size();

                    if (state != 0) {
                        position.put_piece(
                          make_piece(state_colors[state], ROOK),
                          relevant[index]);
                    }
                }

                MoveList expected;

                if (single != SQ_NONE && position.empty(single)) {
                    append_expected_pawn_move(
                      moving_color, source, single, expected);

                    if (double_push != SQ_NONE
                        && position.empty(double_push)) {
                        expected.push_back(
                          Move::normal(source, double_push));
                    }
                }

                for (const Square capture : captures) {
                    if (capture == SQ_NONE || position.empty(capture))
                        continue;

                    const Piece target = position.piece_on(capture);
                    if (team_of(color_of(target)) != team_of(moving_color)) {
                        append_expected_pawn_move(
                          moving_color, source, capture, expected);
                    }
                }

                MoveList actual;
                generate_pawn_moves(position, actual);

                expect(move_lists_equal(actual, expected),
                       "pawn moves match every local occupancy combination");
            }
        }
    }
}

void test_rotational_symmetry() {
    using namespace Mockingbird;

    Position position;
    position.put_piece(R_PAWN, make_square(FILE_D, RANK_2));
    position.put_piece(R_PAWN, make_square(FILE_D, RANK_5));
    position.put_piece(R_PAWN, make_square(FILE_J, RANK_6));
    position.put_piece(R_PAWN, make_square(FILE_H, RANK_10));
    position.put_piece(R_PAWN, make_square(FILE_K, RANK_10));
    position.put_piece(R_ROOK, make_square(FILE_D, RANK_3));
    position.put_piece(B_PAWN, make_square(FILE_D, RANK_6));
    position.put_piece(B_ROOK, make_square(FILE_K, RANK_7));
    position.put_piece(B_ROOK, make_square(FILE_G, RANK_11));
    position.put_piece(Y_ROOK, make_square(FILE_I, RANK_11));
    position.put_piece(G_PAWN, make_square(FILE_K, RANK_11));
    position.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_6));
    position.set_en_passant_square(
      GREEN, make_square(FILE_L, RANK_11));

    for (int rotation = 0; rotation < COLOR_NB; ++rotation) {
        MoveList original_moves;
        generate_pawn_moves(position, original_moves);

        const Position rotated_position = rotate_clockwise(position);
        MoveList rotated_moves;
        generate_pawn_moves(rotated_position, rotated_moves);

        expect(move_sets_are_rotations(original_moves, rotated_moves),
               "quarter-turn rotation preserves generated pawn moves");

        position = rotated_position;
    }
}

void test_generated_move_types() {
    using namespace Mockingbird;

    Position position;
    position.put_piece(R_PAWN, make_square(FILE_D, RANK_2));
    position.put_piece(R_PAWN, make_square(FILE_D, RANK_5));
    position.put_piece(R_PAWN, make_square(FILE_H, RANK_10));
    position.put_piece(R_PAWN, make_square(FILE_K, RANK_10));
    position.put_piece(B_PAWN, make_square(FILE_D, RANK_6));
    position.put_piece(B_ROOK, make_square(FILE_G, RANK_11));
    position.put_piece(G_PAWN, make_square(FILE_K, RANK_11));
    position.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_6));
    position.set_en_passant_square(
      GREEN, make_square(FILE_L, RANK_11));

    MoveList moves;
    generate_pawn_moves(position, moves);

    bool found_en_passant = false;
    bool found_en_passant_promotion = false;

    for (const Move move : moves) {
        expect(
          move.type() == MoveType::NORMAL
            || move.type() == MoveType::PROMOTION
            || move.type() == MoveType::EN_PASSANT,
          "pawn generation emits only normal, promotion, and en-passant moves");

        if (move.type() == MoveType::EN_PASSANT) {
            found_en_passant = true;
            found_en_passant_promotion |= move.is_promotion();
        }
    }

    expect(found_en_passant,
           "pawn generation emits ordinary en-passant moves");
    expect(found_en_passant_promotion,
           "pawn generation emits en-passant promotions");
}

}  // namespace

int main() {
    test_known_single_and_double_pushes();
    test_blocked_pushes();
    test_capture_team_rules();
    test_quiet_promotions();
    test_capture_promotions();
    test_every_en_passant_capture();
    test_en_passant_target_occupancy();
    test_en_passant_owner_and_victim_validation();
    test_multiple_en_passant_targets_and_capturers();
    test_en_passant_promotion_order();
    test_multiple_pawns_and_append_behavior();
    test_every_local_occupancy_combination();
    test_rotational_symmetry();
    test_generated_move_types();

    if (failures != 0) {
        std::cerr << failures << " pawn-move test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All pawn-move tests passed\n";
    return EXIT_SUCCESS;
}
