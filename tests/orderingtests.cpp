#include "ordering.h"
#include "search.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

int failures = 0;

using namespace Mockingbird;

inline constexpr std::array<Color, COLOR_NB> COLORS = {
  RED,
  BLUE,
  YELLOW,
  GREEN,
};

inline constexpr std::array<PieceType, 6> PIECE_TYPES = {
  PAWN,
  KNIGHT,
  BISHOP,
  ROOK,
  QUEEN,
  KING,
};

inline constexpr std::array<PieceType, 4> PROMOTION_TYPES = {
  QUEEN,
  ROOK,
  BISHOP,
  KNIGHT,
};

inline constexpr std::array<CastlingSide, CASTLING_SIDE_NB>
  CASTLING_SIDES = {
    CastlingSide::KING_SIDE,
    CastlingSide::QUEEN_SIDE,
};

// Records a failed condition and allows the remaining tests to run.
void expect(bool condition, std::string_view message) {
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

[[nodiscard]] constexpr bool contains_move(
  const MoveList& moves,
  Move expected) noexcept {
    for (const Move move : moves) {
        if (move == expected)
            return true;
    }

    return false;
}

[[nodiscard]] constexpr bool move_lists_equal(
  const MoveList& left,
  const MoveList& right) noexcept {
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

[[nodiscard]] constexpr bool same_move_multiset(
  const MoveList& left,
  const MoveList& right) noexcept {
    if (left.size() != right.size())
        return false;

    std::array<bool, MoveList::CAPACITY> matched{};
    for (const Move move : left) {
        bool found = false;

        for (std::size_t index = 0;
             index < right.size();
             ++index) {
            if (!matched[index]
                && right[index] == move) {
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

[[nodiscard]] constexpr bool positions_equal(
  const Position& left,
  const Position& right) noexcept {
    if (left.side_to_move() != right.side_to_move()
        || left.key() != right.key()
        || left.recompute_key() != right.recompute_key()
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

    for (const Color color : COLORS) {
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

        for (const Color color : COLORS) {
            if (left.pieces(color, piece_type)
                  != right.pieces(color, piece_type))
                return false;
        }
    }

    return true;
}

[[nodiscard]] constexpr Square rotate_clockwise(
  Square square) noexcept {
    return make_square(
      File(int(rank_of(square))),
      Rank(BOARD_FILES + 1 - int(file_of(square))));
}

[[nodiscard]] constexpr Move rotate_clockwise(
  Move move) noexcept {
    const Square from =
      rotate_clockwise(move.from());
    const Square to =
      rotate_clockwise(move.to());

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
                    from,
                    to,
                    move.promotion_type())
                : Move::en_passant(from, to);

        case MoveType::COUNT:
            break;
    }

    assert(false);
    return Move::none();
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

    for (const Color color : COLORS) {
        const Color rotated_color =
          next_color(color);

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
kings_only_position() noexcept {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      B_KING, make_square(FILE_D, RANK_8));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_13));
    position.put_piece(
      G_KING, make_square(FILE_K, RANK_8));
    return position;
}

[[nodiscard]] constexpr Square capture_source(
  PieceType attacker) noexcept {
    switch (attacker) {
        case PAWN:
            return make_square(FILE_H, RANK_7);

        case KNIGHT:
            return make_square(FILE_E, RANK_7);

        case BISHOP:
            return make_square(FILE_F, RANK_7);

        case ROOK:
        case QUEEN:
            return make_square(FILE_G, RANK_7);

        case KING:
            return make_square(FILE_H, RANK_7);

        case NO_PIECE_TYPE:
        case PIECE_TYPE_NB:
            break;
    }

    assert(false);
    return SQ_NONE;
}

struct CaptureFixture {
    Position position;
    Move move = Move::none();
};

[[nodiscard]] constexpr CaptureFixture
make_capture_fixture(
  PieceType attacker,
  PieceType victim) noexcept {
    constexpr Square red_king =
      make_square(FILE_H, RANK_5);
    constexpr Square blue_king =
      make_square(FILE_D, RANK_8);
    constexpr Square target =
      make_square(FILE_G, RANK_8);

    Position position = kings_only_position();
    if (attacker == KING)
        position.remove_piece(red_king);
    if (victim == KING)
        position.remove_piece(blue_king);

    const Square source =
      capture_source(attacker);
    position.put_piece(
      make_piece(RED, attacker), source);
    position.put_piece(
      make_piece(BLUE, victim), target);

    return {
      position,
      Move::normal(source, target),
    };
}

[[nodiscard]] constexpr Position
material_tactic_position() noexcept {
    Position position = kings_only_position();
    position.put_piece(
      R_ROOK, make_square(FILE_F, RANK_5));
    position.put_piece(
      B_QUEEN, make_square(FILE_F, RANK_8));
    return position;
}

[[nodiscard]] consteval bool
constexpr_ordering_works() {
    Position position = material_tactic_position();
    const Move quiet = Move::normal(
      make_square(FILE_H, RANK_5),
      make_square(FILE_H, RANK_6));
    const Move capture = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));

    MoveList moves;
    moves.push_back(quiet);
    moves.push_back(capture);
    order_moves(position, moves);

    return is_capture_move(position, capture)
        && is_tactical_move(position, capture)
        && !is_capture_move(position, quiet)
        && !is_tactical_move(position, quiet)
        && move_order_score(position, capture)
             > move_order_score(position, quiet)
        && moves.size() == 2
        && moves[0] == capture
        && moves[1] == quiet
        && position.key() == position.recompute_key();
}

static_assert(constexpr_ordering_works());
static_assert(
  std::is_same_v<
    decltype(move_order_score(
      std::declval<const Position&>(),
      std::declval<Move>())),
    MoveOrderScore>);
static_assert(noexcept(
  move_order_score(
    std::declval<const Position&>(),
    std::declval<Move>())));
static_assert(noexcept(
  order_moves(
    std::declval<const Position&>(),
    std::declval<MoveList&>())));

void test_every_attacker_and_victim() {
    for (int rotation = 0;
         rotation < COLOR_NB;
         ++rotation) {
        std::array<
          std::array<MoveOrderScore, PIECE_TYPES.size()>,
          PIECE_TYPES.size()> scores{};

        for (std::size_t attacker_index = 0;
             attacker_index < PIECE_TYPES.size();
             ++attacker_index) {
            for (std::size_t victim_index = 0;
                 victim_index < PIECE_TYPES.size();
                 ++victim_index) {
                const PieceType attacker =
                  PIECE_TYPES[attacker_index];
                const PieceType victim =
                  PIECE_TYPES[victim_index];
                CaptureFixture fixture =
                  make_capture_fixture(
                    attacker, victim);

                for (int step = 0;
                     step < rotation;
                     ++step) {
                    fixture.position =
                      rotate_clockwise(
                        fixture.position);
                    fixture.move =
                      rotate_clockwise(
                        fixture.move);
                }

                const Position original =
                  fixture.position;
                MoveList legal_moves;
                generate_legal_moves(
                  fixture.position, legal_moves);

                expect(
                  contains_move(
                    legal_moves, fixture.move),
                  "every rotated attacker-victim fixture is legal");
                expect(
                  is_capture_move(
                    fixture.position,
                    fixture.move)
                    && is_tactical_move(
                      fixture.position,
                      fixture.move),
                  "every attacker-victim move is classified as tactical");

                scores[attacker_index][victim_index] =
                  move_order_score(
                    fixture.position,
                    fixture.move);
                expect(
                  positions_equal(
                    fixture.position, original),
                  "capture scoring preserves every position field");
            }
        }

        for (std::size_t attacker = 0;
             attacker < PIECE_TYPES.size();
             ++attacker) {
            for (std::size_t victim = 1;
                 victim < PIECE_TYPES.size();
                 ++victim) {
                expect(
                  scores[attacker][victim - 1]
                    < scores[attacker][victim],
                  "more valuable victims receive higher scores");
            }
        }

        // King victims are excluded because every opposing-king capture has
        // the same terminal priority.
        for (std::size_t victim = 0;
             victim + 1 < PIECE_TYPES.size();
             ++victim) {
            for (std::size_t attacker = 1;
                 attacker < PIECE_TYPES.size();
                 ++attacker) {
                expect(
                  scores[attacker - 1][victim]
                    > scores[attacker][victim],
                  "lower-cost attackers receive higher equal-victim scores");
            }

            if (victim > 0) {
                expect(
                  scores.back()[victim]
                    > scores.front()[victim - 1],
                  "victim value precedes attacker cost");
            }
        }

        for (std::size_t attacker = 1;
             attacker < PIECE_TYPES.size();
             ++attacker) {
            expect(
              scores[attacker][std::size_t(KING - PAWN)]
                == scores[0][std::size_t(KING - PAWN)],
              "all opposing-king captures share terminal priority");
        }
    }
}

void test_priority_bands_and_promotion_order() {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      B_KING, make_square(FILE_D, RANK_8));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_13));
    position.put_piece(
      G_KING, make_square(FILE_F, RANK_8));
    position.put_piece(
      R_ROOK, make_square(FILE_F, RANK_5));
    position.put_piece(
      R_PAWN, make_square(FILE_H, RANK_10));
    position.put_piece(
      R_KNIGHT, make_square(FILE_E, RANK_7));
    position.put_piece(
      B_QUEEN, make_square(FILE_G, RANK_8));

    const Position original = position;
    const Move king_capture = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));
    const Move material_capture = Move::normal(
      make_square(FILE_E, RANK_7),
      make_square(FILE_G, RANK_8));
    const Move quiet = Move::normal(
      make_square(FILE_H, RANK_5),
      make_square(FILE_H, RANK_6));

    const std::array scrambled_promotions = {
      Move::promotion(
        make_square(FILE_H, RANK_10),
        make_square(FILE_H, RANK_11),
        KNIGHT),
      Move::promotion(
        make_square(FILE_H, RANK_10),
        make_square(FILE_H, RANK_11),
        QUEEN),
      Move::promotion(
        make_square(FILE_H, RANK_10),
        make_square(FILE_H, RANK_11),
        BISHOP),
      Move::promotion(
        make_square(FILE_H, RANK_10),
        make_square(FILE_H, RANK_11),
        ROOK),
    };

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    expect(
      contains_move(legal_moves, king_capture)
        && contains_move(
             legal_moves, material_capture)
        && contains_move(legal_moves, quiet),
      "the priority fixture contains its capture and quiet moves");
    for (const Move promotion : scrambled_promotions) {
        expect(
          contains_move(legal_moves, promotion),
          "the priority fixture contains every promotion type");
        expect(
          !is_capture_move(position, promotion)
            && is_tactical_move(position, promotion),
          "quiet promotions are tactical without being captures");
    }

    MoveList moves;
    moves.push_back(quiet);
    moves.push_back(scrambled_promotions[0]);
    moves.push_back(material_capture);
    moves.push_back(scrambled_promotions[1]);
    moves.push_back(scrambled_promotions[2]);
    moves.push_back(king_capture);
    moves.push_back(scrambled_promotions[3]);
    const MoveList original_moves = moves;

    MoveOrderingBuffer buffer;
    order_moves(position, moves, buffer);

    const std::array expected = {
      king_capture,
      scrambled_promotions[1],
      scrambled_promotions[3],
      scrambled_promotions[2],
      scrambled_promotions[0],
      material_capture,
      quiet,
    };
    expect(
      moves.size() == expected.size(),
      "priority ordering preserves the move count");
    for (std::size_t index = 0;
         index < expected.size();
         ++index) {
        expect(
          moves[index] == expected[index],
          "king capture, promotions, capture, and quiet use distinct bands");
    }

    expect(
      same_move_multiset(moves, original_moves),
      "priority ordering preserves every move");
    expect(
      positions_equal(position, original),
      "priority ordering preserves every position field");

    const MoveList once_ordered = moves;
    order_moves(position, moves, buffer);
    expect(
      move_lists_equal(moves, once_ordered),
      "ordering is idempotent with a reused buffer");
}

void test_castling_is_stable_quiet_move() {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_1));
    position.put_piece(
      R_ROOK, make_square(FILE_D, RANK_1));
    position.put_piece(
      R_ROOK, make_square(FILE_K, RANK_1));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    position.put_piece(
      Y_KING, make_square(FILE_G, RANK_14));
    position.put_piece(
      G_KING, make_square(FILE_N, RANK_8));
    position.set_castling_right(
      RED, CastlingSide::KING_SIDE);
    position.set_castling_right(
      RED, CastlingSide::QUEEN_SIDE);
    const Position original = position;

    const CastlingGeometry& kingside =
      castling_geometry(
        RED, CastlingSide::KING_SIDE);
    const CastlingGeometry& queenside =
      castling_geometry(
        RED, CastlingSide::QUEEN_SIDE);
    const Move king_castle = Move::castling(
      kingside.king_source,
      kingside.king_destination);
    const Move queen_castle = Move::castling(
      queenside.king_source,
      queenside.king_destination);

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    expect(
      contains_move(legal_moves, king_castle)
        && contains_move(
             legal_moves, queen_castle),
      "the castling fixture contains both castling moves");
    expect(
      move_order_score(position, king_castle) == 0
        && move_order_score(
             position, queen_castle) == 0
        && !is_tactical_move(
             position, king_castle)
        && !is_tactical_move(
             position, queen_castle),
      "castling moves use the quiet ordering band");

    MoveList castling_moves;
    castling_moves.push_back(queen_castle);
    castling_moves.push_back(king_castle);
    order_moves(position, castling_moves);
    expect(
      castling_moves[0] == queen_castle
        && castling_moves[1] == king_castle,
      "equal castling scores retain supplied order");
    expect(
      positions_equal(position, original),
      "castling scoring and ordering preserve every position field");
}

void test_score_band_boundaries() {
    Position maximum_capture =
      kings_only_position();
    maximum_capture.put_piece(
      R_PAWN, make_square(FILE_D, RANK_5));
    maximum_capture.put_piece(
      B_PAWN, make_square(FILE_D, RANK_6));
    maximum_capture.put_piece(
      G_QUEEN, make_square(FILE_C, RANK_6));
    maximum_capture.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_6));
    const Move capture = Move::en_passant(
      make_square(FILE_D, RANK_5),
      make_square(FILE_C, RANK_6));

    Position quiet_promotion =
      kings_only_position();
    quiet_promotion.put_piece(
      R_PAWN, make_square(FILE_H, RANK_10));
    const Move knight_promotion =
      Move::promotion(
        make_square(FILE_H, RANK_10),
        make_square(FILE_H, RANK_11),
        KNIGHT);
    const Move bishop_promotion =
      Move::promotion(
        make_square(FILE_H, RANK_10),
        make_square(FILE_H, RANK_11),
        BISHOP);

    Position maximum_promotion =
      kings_only_position();
    maximum_promotion.put_piece(
      R_PAWN, make_square(FILE_B, RANK_10));
    maximum_promotion.put_piece(
      B_PAWN, make_square(FILE_D, RANK_11));
    maximum_promotion.put_piece(
      G_QUEEN, make_square(FILE_C, RANK_11));
    maximum_promotion.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_11));
    const Move knight_capture_promotion =
      Move::en_passant(
        make_square(FILE_B, RANK_10),
        make_square(FILE_C, RANK_11),
        KNIGHT);
    const Move queen_capture_promotion =
      Move::en_passant(
        make_square(FILE_B, RANK_10),
        make_square(FILE_C, RANK_11),
        QUEEN);

    const CaptureFixture king_fixture =
      make_capture_fixture(PAWN, KING);

    MoveList maximum_capture_moves;
    generate_legal_moves(
      maximum_capture, maximum_capture_moves);
    MoveList quiet_promotion_moves;
    generate_legal_moves(
      quiet_promotion, quiet_promotion_moves);
    MoveList maximum_promotion_moves;
    generate_legal_moves(
      maximum_promotion, maximum_promotion_moves);
    MoveList king_capture_moves;
    Position king_position =
      king_fixture.position;
    generate_legal_moves(
      king_position, king_capture_moves);

    expect(
      contains_move(
        maximum_capture_moves, capture)
        && contains_move(
             quiet_promotion_moves,
             knight_promotion)
        && contains_move(
             quiet_promotion_moves,
             bishop_promotion)
        && contains_move(
             maximum_promotion_moves,
             knight_capture_promotion)
        && contains_move(
             maximum_promotion_moves,
             queen_capture_promotion)
        && contains_move(
             king_capture_moves,
             king_fixture.move),
      "every score-band boundary move is legal");
    expect(
      move_order_score(
        quiet_promotion, knight_promotion)
        > move_order_score(
            maximum_capture, capture),
      "the lowest promotion exceeds the highest non-king capture");
    expect(
      move_order_score(
        quiet_promotion, bishop_promotion)
        > move_order_score(
            maximum_promotion,
            knight_capture_promotion),
      "promotion piece value exceeds maximum captured-material detail");
    expect(
      move_order_score(
        king_fixture.position,
        king_fixture.move)
        > move_order_score(
            maximum_promotion,
            queen_capture_promotion),
      "king capture exceeds the highest promotion score");
}

void test_every_move_list_size() {
    Position position =
      material_tactic_position();
    const Position original = position;
    const Move capture = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    std::array<Move, 2> quiets = {
      Move::none(),
      Move::none(),
    };
    std::size_t quiet_count = 0;
    for (const Move move : legal_moves) {
        if (quiet_count < quiets.size()
            && !is_tactical_move(position, move))
            quiets[quiet_count++] = move;
    }

    expect(
      contains_move(legal_moves, capture)
        && quiet_count == quiets.size(),
      "the list-size fixture contains one capture and two quiet moves");
    if (quiet_count != quiets.size())
        return;

    MoveOrderingBuffer buffer;
    for (std::size_t size = 0;
         size <= MoveList::CAPACITY;
         ++size) {
        MoveList moves;
        for (std::size_t index = 0;
             index < size;
             ++index) {
            const std::size_t pattern = index % 3;
            moves.push_back(
              pattern == 0 ? quiets[0]
              : pattern == 1 ? capture
                             : quiets[1]);
        }

        MoveList expected;
        for (const Move move : moves) {
            if (move == capture)
                expected.push_back(move);
        }
        for (const Move move : moves) {
            if (move != capture)
                expected.push_back(move);
        }

        order_moves(position, moves, buffer);
        expect(
          move_lists_equal(moves, expected),
          "stable merge ordering handles every supported list size");
    }

    expect(
      positions_equal(position, original),
      "all list-size ordering passes preserve every position field");
}

void test_stable_equal_scores() {
    {
        Position position = kings_only_position();
        const Position original = position;
        MoveList quiet_moves;
        generate_legal_moves(position, quiet_moves);
        const MoveList generated = quiet_moves;

        order_moves(position, quiet_moves);

        expect(
          move_lists_equal(quiet_moves, generated),
          "all-quiet ordering retains complete generation order");
        expect(
          positions_equal(position, original),
          "all-quiet ordering preserves every position field");
    }

    {
        Position position = kings_only_position();
        position.put_piece(
          R_KNIGHT, make_square(FILE_H, RANK_8));
        position.put_piece(
          B_PAWN, make_square(FILE_F, RANK_7));
        position.put_piece(
          B_PAWN, make_square(FILE_F, RANK_9));
        const Position original = position;
        const Move first = Move::normal(
          make_square(FILE_H, RANK_8),
          make_square(FILE_F, RANK_9));
        const Move second = Move::normal(
          make_square(FILE_H, RANK_8),
          make_square(FILE_F, RANK_7));

        MoveList legal_moves;
        generate_legal_moves(position, legal_moves);
        expect(
          contains_move(legal_moves, first)
            && contains_move(legal_moves, second),
          "the equal-capture fixture contains both knight captures");
        expect(
          move_order_score(position, first)
            == move_order_score(position, second),
          "equal attacker-victim captures receive equal scores");

        MoveList tied;
        tied.push_back(first);
        tied.push_back(second);
        order_moves(position, tied);

        expect(
          tied[0] == first && tied[1] == second,
          "stable ordering retains the supplied equal-capture order");
        expect(
          positions_equal(position, original),
          "equal-capture ordering preserves every position field");
    }
}

void test_en_passant_capture_values() {
    {
        Position position = kings_only_position();
        position.put_piece(
          R_PAWN, make_square(FILE_D, RANK_5));
        position.put_piece(
          B_PAWN, make_square(FILE_D, RANK_6));
        position.put_piece(
          G_ROOK, make_square(FILE_C, RANK_6));
        position.put_piece(
          B_ROOK, make_square(FILE_E, RANK_6));
        position.set_en_passant_square(
          BLUE, make_square(FILE_C, RANK_6));
        Move en_passant = Move::en_passant(
          make_square(FILE_D, RANK_5),
          make_square(FILE_C, RANK_6));
        Move ordinary = Move::normal(
          make_square(FILE_D, RANK_5),
          make_square(FILE_E, RANK_6));

        for (int rotation = 0;
             rotation < COLOR_NB;
             ++rotation) {
            const Position original = position;
            MoveList legal_moves;
            generate_legal_moves(
              position, legal_moves);
            expect(
              contains_move(legal_moves, en_passant)
                && contains_move(
                     legal_moves, ordinary),
              "the rotated double-capture fixture contains both captures");
            expect(
              move_order_score(position, en_passant)
                > move_order_score(position, ordinary),
              "en passant adds its off-destination pawn victim");

            MoveList moves;
            moves.push_back(ordinary);
            moves.push_back(en_passant);
            order_moves(position, moves);
            expect(
              moves[0] == en_passant
                && moves[1] == ordinary,
              "double-capture en passant precedes an equal destination capture");
            expect(
              positions_equal(position, original),
              "double-capture ordering preserves every position field");

            position = rotate_clockwise(position);
            en_passant =
              rotate_clockwise(en_passant);
            ordinary =
              rotate_clockwise(ordinary);
        }
    }

    {
        Position position = kings_only_position();
        position.put_piece(
          R_PAWN, make_square(FILE_D, RANK_5));
        position.put_piece(
          B_PAWN, make_square(FILE_D, RANK_6));
        position.put_piece(
          B_PAWN, make_square(FILE_E, RANK_6));
        position.set_en_passant_square(
          BLUE, make_square(FILE_C, RANK_6));
        Move en_passant = Move::en_passant(
          make_square(FILE_D, RANK_5),
          make_square(FILE_C, RANK_6));
        Move ordinary = Move::normal(
          make_square(FILE_D, RANK_5),
          make_square(FILE_E, RANK_6));

        for (int rotation = 0;
             rotation < COLOR_NB;
             ++rotation) {
            const Position original = position;
            MoveList legal_moves;
            generate_legal_moves(
              position, legal_moves);
            expect(
              contains_move(legal_moves, en_passant)
                && contains_move(
                     legal_moves, ordinary),
              "the rotated empty-target fixture contains both captures");
            expect(
              is_capture_move(position, en_passant)
                && position.empty(en_passant.to()),
              "empty-target en passant is classified as a capture");
            expect(
              move_order_score(position, en_passant)
                == move_order_score(position, ordinary),
              "empty-target en passant equals an ordinary pawn capture");

            MoveList tied;
            tied.push_back(ordinary);
            tied.push_back(en_passant);
            order_moves(position, tied);
            expect(
              tied[0] == ordinary
                && tied[1] == en_passant,
              "equal empty-target en passant retains supplied order");
            expect(
              positions_equal(position, original),
              "empty-target ordering preserves every position field");

            position = rotate_clockwise(position);
            en_passant =
              rotate_clockwise(en_passant);
            ordinary =
              rotate_clockwise(ordinary);
        }
    }

    {
        Position position = kings_only_position();
        position.remove_piece(
          make_square(FILE_K, RANK_8));
        position.put_piece(
          G_KING, make_square(FILE_C, RANK_6));
        position.put_piece(
          R_PAWN, make_square(FILE_D, RANK_5));
        position.put_piece(
          B_PAWN, make_square(FILE_D, RANK_6));
        position.put_piece(
          B_QUEEN, make_square(FILE_E, RANK_6));
        position.set_en_passant_square(
          BLUE, make_square(FILE_C, RANK_6));
        Move king_capture = Move::en_passant(
          make_square(FILE_D, RANK_5),
          make_square(FILE_C, RANK_6));
        Move queen_capture = Move::normal(
          make_square(FILE_D, RANK_5),
          make_square(FILE_E, RANK_6));

        for (int rotation = 0;
             rotation < COLOR_NB;
             ++rotation) {
            const Position original = position;
            MoveList legal_moves;
            generate_legal_moves(
              position, legal_moves);
            expect(
              contains_move(legal_moves, king_capture)
                && contains_move(
                     legal_moves, queen_capture),
              "rotated occupied-target en passant can capture a king");
            expect(
              move_order_score(position, king_capture)
                > move_order_score(position, queen_capture),
              "an en-passant king capture has terminal ordering priority");
            expect(
              positions_equal(position, original),
              "king-capturing en-passant scoring preserves the position");

            position = rotate_clockwise(position);
            king_capture =
              rotate_clockwise(king_capture);
            queen_capture =
              rotate_clockwise(queen_capture);
        }
    }
}

void test_en_passant_promotions() {
    Position position = kings_only_position();
    position.put_piece(
      R_PAWN, make_square(FILE_B, RANK_10));
    position.put_piece(
      B_ROOK, make_square(FILE_A, RANK_11));
    position.put_piece(
      G_ROOK, make_square(FILE_C, RANK_11));
    position.put_piece(
      B_PAWN, make_square(FILE_D, RANK_11));
    position.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_11));

    for (int rotation = 0;
         rotation < COLOR_NB;
         ++rotation) {
        const Position original = position;
        MoveList legal_moves;
        generate_legal_moves(position, legal_moves);

        for (const PieceType promotion :
             PROMOTION_TYPES) {
            Move en_passant = Move::en_passant(
              make_square(FILE_B, RANK_10),
              make_square(FILE_C, RANK_11),
              promotion);
            Move ordinary = Move::promotion(
              make_square(FILE_B, RANK_10),
              make_square(FILE_A, RANK_11),
              promotion);

            for (int step = 0;
                 step < rotation;
                 ++step) {
                en_passant =
                  rotate_clockwise(en_passant);
                ordinary =
                  rotate_clockwise(ordinary);
            }

            expect(
              contains_move(legal_moves, en_passant)
                && contains_move(
                     legal_moves, ordinary),
              "every rotated en-passant promotion fixture is legal");
            expect(
              move_order_score(position, en_passant)
                > move_order_score(position, ordinary),
              "en-passant promotion includes both captured pieces");
        }

        expect(
          positions_equal(position, original),
          "en-passant promotion scoring preserves every position field");
        position = rotate_clockwise(position);
    }
}

struct ReferenceResult {
    Move best_move = Move::none();
    Score score = DRAW_SCORE;
    std::uint64_t nodes = 0;
};

[[nodiscard]] ReferenceResult unordered_alpha_beta(
  Position& position,
  PositionHistory& history,
  int depth,
  int ply,
  Score alpha,
  Score beta) {
    ReferenceResult result;
    result.nodes = 1;

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    const PositionResult position_result =
      terminal_result(position, history, legal_moves);

    assert(position_result.is_valid());
    if (position_result.is_terminal()) {
        result.score =
          SearchDetail::terminal_score(
            position_result,
            team_of(position.side_to_move()),
            ply);
        return result;
    }

    if (depth == 0) {
        result.score = evaluate(position);
        return result;
    }

    result.score = -INFINITE_SCORE;
    for (const Move move : legal_moves) {
        UndoState undo;
        do_move(position, move, undo);
        const PositionKey child_key =
          position.key();
        history.push(child_key);

        const ReferenceResult child =
          unordered_alpha_beta(
            position,
            history,
            depth - 1,
            ply + 1,
            -beta,
            -alpha);
        const Score candidate = -child.score;
        result.nodes += child.nodes;

        history.pop(child_key);
        undo_move(position, move, undo);

        if (candidate > result.score) {
            result.score = candidate;
            result.best_move = move;
        }
        if (candidate > alpha)
            alpha = candidate;
        if (alpha >= beta)
            break;
    }

    return result;
}

[[nodiscard]] ReferenceResult unordered_search(
  Position& position,
  const PositionHistory& history,
  int depth) {
    PositionHistory working_history{history};
    return unordered_alpha_beta(
      position,
      working_history,
      depth,
      0,
      -INFINITE_SCORE,
      INFINITE_SCORE);
}

void test_search_integration() {
    Position position =
      material_tactic_position();
    Move expected = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));
    std::uint64_t ordered_nodes = 0;
    std::uint64_t unordered_nodes = 0;

    for (int rotation = 0;
         rotation < COLOR_NB;
         ++rotation) {
        const Position original = position;
        const PositionHistory history{
          position.key()};

        const ReferenceResult unordered =
          unordered_search(
            position, history, 3);
        expect(
          positions_equal(position, original),
          "generation-order reference search restores the position");

        const SearchResult ordered =
          search(position, history, 3);
        expect(
          ordered.best_move == expected
            && unordered.best_move == expected
            && ordered.score == unordered.score
            && ordered.score == ROOK_VALUE,
          "ordered and generation-order alpha-beta return the same tactic");
        expect(
          positions_equal(position, original),
          "ordered alpha-beta restores every position field");

        if (rotation == 0) {
            expect(
              ordered.nodes < unordered.nodes,
              "material ordering reduces the base tactical search");
        }

        ordered_nodes += ordered.nodes;
        unordered_nodes += unordered.nodes;
        position = rotate_clockwise(position);
        expected = rotate_clockwise(expected);
    }

    expect(
      ordered_nodes < unordered_nodes,
      "material ordering reduces aggregate nodes across all colors");
}

}  // namespace

int main() {
    test_every_attacker_and_victim();
    test_priority_bands_and_promotion_order();
    test_castling_is_stable_quiet_move();
    test_score_band_boundaries();
    test_every_move_list_size();
    test_stable_equal_scores();
    test_en_passant_capture_values();
    test_en_passant_promotions();
    test_search_integration();

    if (failures != 0) {
        std::cerr << failures
                  << " ordering test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All ordering tests passed\n";
    return EXIT_SUCCESS;
}
