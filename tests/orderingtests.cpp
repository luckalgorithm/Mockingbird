#include "ordering.h"
#include "search.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
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

[[nodiscard]] constexpr std::size_t move_index(
  const MoveList& moves,
  Move expected) noexcept {
    for (std::size_t index = 0;
         index < moves.size();
         ++index) {
        if (moves[index] == expected)
            return index;
    }

    return moves.size();
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

[[nodiscard]] constexpr Position
quiet_history_cutoff_position() noexcept {
    Position position = kings_only_position();
    position.put_piece(
      Y_BISHOP, make_square(FILE_N, RANK_4));
    position.put_piece(
      B_PAWN, make_square(FILE_J, RANK_14));
    position.set_side_to_move(YELLOW);
    return position;
}

[[nodiscard]] constexpr Position
quiet_history_alias_position() noexcept {
    Position position = kings_only_position();
    position.put_piece(
      R_KNIGHT, make_square(FILE_E, RANK_4));
    position.put_piece(
      R_KNIGHT, make_square(FILE_G, RANK_4));
    position.put_piece(
      B_ROOK, make_square(FILE_G, RANK_8));
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
static_assert(
  QuietHistory::depth_bonus(1)
  == QuietHistory::DEPTH_BONUS_SCALE);
static_assert(
  QuietHistory::depth_bonus(3)
  == HistoryScore{288});
static_assert(
  QuietHistory::depth_bonus(8)
  == QuietHistory::MAX_DEPTH_BONUS);
static_assert(
  QuietHistory::depth_bonus(MAX_SEARCH_DEPTH)
  == QuietHistory::MAX_DEPTH_BONUS);

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

void test_quiet_history_table() {
    constexpr Square destination =
      make_square(FILE_G, RANK_11);
    constexpr Square other_destination =
      make_square(FILE_H, RANK_11);
    constexpr HistoryScore depth_three_bonus =
      QuietHistory::depth_bonus(3);

    QuietHistory history;
    expect(
      history.score(Y_BISHOP, destination) == 0
        && history.score(
             B_BISHOP, destination) == 0
        && history.score(
             Y_ROOK, destination) == 0
        && history.score(
             Y_BISHOP, other_destination) == 0,
      "quiet history starts at zero for independent table indexes");

    history.reward(
      Y_BISHOP, destination, 3);
    expect(
      history.score(Y_BISHOP, destination)
          == depth_three_bonus
        && history.score(
             B_BISHOP, destination) == 0
        && history.score(
             Y_ROOK, destination) == 0
        && history.score(
             Y_BISHOP, other_destination) == 0,
      "quiet history separates player, piece type, and destination");

    history.reward(
      Y_BISHOP, destination, 3);
    expect(
      history.score(Y_BISHOP, destination)
        == HistoryScore{571},
      "repeated quiet-history rewards use bounded gravity");

    QuietHistory penalties;
    penalties.penalize(
      Y_BISHOP, destination, 3);
    expect(
      penalties.score(Y_BISHOP, destination)
        == -depth_three_bonus,
      "quiet-history penalties use the negative depth bonus");

    QuietHistory bounds;
    bounds.update(
      Y_BISHOP,
      destination,
      std::numeric_limits<HistoryScore>::max());
    bounds.update(
      Y_BISHOP,
      destination,
      std::numeric_limits<HistoryScore>::max());
    expect(
      bounds.score(Y_BISHOP, destination)
        == QuietHistory::LIMIT,
      "positive gravity updates remain at the history limit");

    bounds.update(
      Y_BISHOP,
      destination,
      std::numeric_limits<HistoryScore>::lowest());
    bounds.update(
      Y_BISHOP,
      destination,
      std::numeric_limits<HistoryScore>::lowest());
    expect(
      bounds.score(Y_BISHOP, destination)
        == -QuietHistory::LIMIT,
      "negative gravity updates remain at the history limit");

    Position alias_position =
      kings_only_position();
    constexpr Move left_alias = Move::normal(
      make_square(FILE_F, RANK_9),
      make_square(FILE_G, RANK_10));
    constexpr Move right_alias = Move::normal(
      make_square(FILE_H, RANK_9),
      make_square(FILE_G, RANK_10));
    alias_position.put_piece(
      R_BISHOP, left_alias.from());
    alias_position.put_piece(
      R_BISHOP, right_alias.from());

    QuietHistory aliases;
    aliases.update(
      alias_position.piece_on(
        left_alias.from()),
      left_alias.to(),
      HistoryScore{123});
    expect(
      aliases.score(
        alias_position.piece_on(
          right_alias.from()),
        right_alias.to())
        == HistoryScore{123},
      "same-player same-piece moves to one destination share a history index");

    history.clear();
    bounds.clear();
    aliases.clear();
    expect(
      history.score(Y_BISHOP, destination) == 0
        && bounds.score(
             Y_BISHOP, destination) == 0
        && aliases.score(
             R_BISHOP, left_alias.to()) == 0,
      "clearing quiet history restores every observed entry to zero");
}

void test_quiet_history_ordering() {
    Position position =
      material_tactic_position();
    const Position original = position;
    const Move capture = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    std::array<Move, 3> quiets{};
    std::size_t quiet_count = 0;
    for (const Move move : legal_moves) {
        if (is_tactical_move(position, move)
            || quiet_count == quiets.size()) {
            continue;
        }

        quiets[quiet_count++] = move;
    }

    expect(
      contains_move(legal_moves, capture)
        && quiet_count == quiets.size(),
      "the history-ordering fixture contains one capture and "
      "three quiet moves");
    if (!contains_move(legal_moves, capture)
        || quiet_count != quiets.size()) {
        return;
    }

    QuietHistory history;
    history.update(
      position.piece_on(quiets[2].from()),
      quiets[2].to(),
      QuietHistory::LIMIT);

    MoveList precedence;
    precedence.push_back(quiets[0]);
    precedence.push_back(quiets[2]);
    precedence.push_back(capture);
    precedence.push_back(quiets[1]);
    MoveOrderingBuffer buffer;
    order_moves(
      position,
      precedence,
      buffer,
      history);
    expect(
      precedence[0] == capture
        && precedence[1] == quiets[2]
        && precedence[2] == quiets[0]
        && precedence[3] == quiets[1],
      "a tactical move precedes a quiet move at maximum history");

    history.clear();
    constexpr HistoryScore tied_score = 512;
    history.update(
      position.piece_on(quiets[0].from()),
      quiets[0].to(),
      tied_score);
    history.update(
      position.piece_on(quiets[1].from()),
      quiets[1].to(),
      tied_score);
    history.update(
      position.piece_on(quiets[2].from()),
      quiets[2].to(),
      -tied_score);

    MoveList stable;
    stable.push_back(quiets[1]);
    stable.push_back(quiets[2]);
    stable.push_back(capture);
    stable.push_back(quiets[0]);
    order_moves(
      position,
      stable,
      buffer,
      history);
    expect(
      stable[0] == capture
        && stable[1] == quiets[1]
        && stable[2] == quiets[0]
        && stable[3] == quiets[2],
      "equal quiet-history scores retain their existing relative order");

    MoveList preferred;
    preferred.push_back(quiets[1]);
    preferred.push_back(quiets[2]);
    preferred.push_back(capture);
    preferred.push_back(quiets[0]);
    order_moves(
      position,
      preferred,
      buffer,
      history,
      quiets[2]);
    expect(
      preferred[0] == quiets[2]
        && preferred[1] == capture
        && preferred[2] == quiets[1]
        && preferred[3] == quiets[0],
      "a validated preferred quiet move precedes tactical and history order");
    expect(
      positions_equal(position, original),
      "quiet-history ordering preserves every position field");
}

void test_killer_move_pair() {
    constexpr Move first = Move::normal(
      make_square(FILE_H, RANK_5),
      make_square(FILE_H, RANK_6));
    constexpr Move second = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_6));
    constexpr Move third = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_G, RANK_5));
    constexpr Move alternate_encoding =
      Move::castling(
        first.from(), first.to());

    KillerMoves killers;
    expect(
      killers.primary().is_none()
        && killers.secondary().is_none()
        && killers.priority(first) == 0
        && killers.priority(Move::none()) == 0
        && killers.priority(Move::null()) == 0,
      "a killer pair starts empty with zero move priorities");

    killers.record(first);
    expect(
      killers.primary() == first
        && killers.secondary().is_none()
        && killers.priority(first)
             == KillerMoves::PRIMARY_PRIORITY
        && killers.priority(
             alternate_encoding) == 0,
      "killer matching includes the complete encoded move type");

    killers.record(first);
    expect(
      killers.primary() == first
        && killers.secondary().is_none(),
      "recording the primary killer is idempotent");

    killers.record(second);
    expect(
      killers.primary() == second
        && killers.secondary() == first
        && killers.priority(second)
             == KillerMoves::PRIMARY_PRIORITY
        && killers.priority(first)
             == KillerMoves::SECONDARY_PRIORITY,
      "a distinct killer shifts the prior primary to the secondary slot");

    killers.record(first);
    expect(
      killers.primary() == first
        && killers.secondary() == second,
      "recording the secondary killer promotes it and retains the old primary");

    killers.record(third);
    expect(
      killers.primary() == third
        && killers.secondary() == first
        && killers.priority(second) == 0,
      "a third distinct killer replaces the old secondary slot");

    killers.clear();
    expect(
      killers.primary().is_none()
        && killers.secondary().is_none()
        && killers.priority(third) == 0,
      "clearing a killer pair resets both slots");
}

void test_killer_move_ordering() {
    Position position =
      material_tactic_position();
    const Position original = position;
    const Move capture = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    std::array<Move, 4> quiets{};
    std::size_t quiet_count = 0;
    for (const Move move : legal_moves) {
        if (is_tactical_move(position, move)
            || quiet_count == quiets.size()) {
            continue;
        }

        quiets[quiet_count++] = move;
    }

    expect(
      contains_move(legal_moves, capture)
        && quiet_count == quiets.size(),
      "the killer-ordering fixture contains one capture and four quiet moves");
    if (!contains_move(legal_moves, capture)
        || quiet_count != quiets.size()) {
        return;
    }

    QuietHistory history;
    history.update(
      position.piece_on(quiets[3].from()),
      quiets[3].to(),
      QuietHistory::LIMIT);
    history.update(
      position.piece_on(quiets[1].from()),
      quiets[1].to(),
      -QuietHistory::LIMIT);
    history.update(
      position.piece_on(quiets[2].from()),
      quiets[2].to(),
      -QuietHistory::LIMIT);

    KillerMoves killers;
    killers.record(quiets[2]);
    killers.record(quiets[1]);

    MoveList ordered;
    ordered.push_back(quiets[0]);
    ordered.push_back(quiets[2]);
    ordered.push_back(quiets[3]);
    ordered.push_back(capture);
    ordered.push_back(quiets[1]);
    MoveOrderingBuffer buffer;
    order_moves(
      position,
      ordered,
      buffer,
      history,
      killers,
      Move::none());
    expect(
      ordered[0] == capture
        && ordered[1] == quiets[1]
        && ordered[2] == quiets[2]
        && ordered[3] == quiets[3]
        && ordered[4] == quiets[0],
      "tactical, killer-slot, and quiet-history priorities form distinct bands");

    MoveList preferred;
    preferred.push_back(quiets[0]);
    preferred.push_back(quiets[2]);
    preferred.push_back(quiets[3]);
    preferred.push_back(capture);
    preferred.push_back(quiets[1]);
    order_moves(
      position,
      preferred,
      buffer,
      history,
      killers,
      quiets[0]);
    expect(
      preferred[0] == quiets[0]
        && preferred[1] == capture
        && preferred[2] == quiets[1]
        && preferred[3] == quiets[2]
        && preferred[4] == quiets[3],
      "a preferred quiet move precedes tactical and killer ordering");

    const Move absent = Move::normal(
      make_square(FILE_D, RANK_1),
      make_square(FILE_D, RANK_2));
    killers.record(absent);
    MoveList absent_primary;
    absent_primary.push_back(quiets[0]);
    absent_primary.push_back(quiets[2]);
    absent_primary.push_back(quiets[3]);
    absent_primary.push_back(capture);
    absent_primary.push_back(quiets[1]);
    order_moves(
      position,
      absent_primary,
      buffer,
      history,
      killers,
      Move::none());
    expect(
      absent_primary[0] == capture
        && absent_primary[1] == quiets[1]
        && absent_primary[2] == quiets[3]
        && absent_primary[3] == quiets[0]
        && absent_primary[4] == quiets[2],
      "an absent primary killer leaves a present secondary ahead of history");

    Position quiet_position =
      kings_only_position();
    quiet_position.put_piece(
      R_ROOK, make_square(FILE_G, RANK_5));
    const Move stale_killer = Move::normal(
      make_square(FILE_G, RANK_5),
      make_square(FILE_G, RANK_8));
    MoveList quiet_legal;
    generate_legal_moves(
      quiet_position, quiet_legal);

    Position capture_position = quiet_position;
    capture_position.put_piece(
      B_PAWN, stale_killer.to());
    capture_position.put_piece(
      R_PAWN, make_square(FILE_H, RANK_7));
    capture_position.put_piece(
      B_QUEEN, make_square(FILE_I, RANK_8));
    const Move queen_capture = Move::normal(
      make_square(FILE_H, RANK_7),
      make_square(FILE_I, RANK_8));
    MoveList capture_legal;
    generate_legal_moves(
      capture_position, capture_legal);

    expect(
      contains_move(quiet_legal, stale_killer)
        && !is_tactical_move(
             quiet_position, stale_killer)
        && contains_move(
             capture_legal, stale_killer)
        && contains_move(
             capture_legal, queen_capture)
        && is_tactical_move(
             capture_position, stale_killer)
        && move_order_score(
             capture_position, queen_capture)
             > move_order_score(
                 capture_position,
                 stale_killer),
      "a previously quiet killer can be a lower-priority capture in another node");

    if (contains_move(capture_legal, stale_killer)
        && contains_move(
             capture_legal, queen_capture)) {
        KillerMoves stale;
        stale.record(stale_killer);
        QuietHistory empty_history;
        MoveList captures;
        captures.push_back(stale_killer);
        captures.push_back(queen_capture);
        order_moves(
          capture_position,
          captures,
          buffer,
          empty_history,
          stale,
          Move::none());
        expect(
          move_index(captures, queen_capture)
            < move_index(captures, stale_killer),
          "a stale killer that is now tactical retains material ordering");
    }

    PositionHistory search_history{
      position.key()};
    SearchDetail::SearchState tactical_state;
    const auto tactical_cutoff =
      SearchDetail::alpha_beta(
        position,
        search_history,
        1,
        0,
        -INFINITE_SCORE,
        DRAW_SCORE,
        tactical_state);
    expect(
      tactical_cutoff
        && tactical_cutoff->best_move == capture
        && tactical_state.killer_moves(0)
             .primary().is_none()
        && tactical_state.killer_moves(0)
             .secondary().is_none(),
      "a tactical beta cutoff does not update either killer slot");
    expect(
      positions_equal(position, original)
        && search_history.size() == 1
        && search_history.current_key()
             == position.key(),
      "killer-ordering checks preserve position and history");
}

void test_killer_ply_isolation() {
    Position position =
      material_tactic_position();
    const Move capture = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));
    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);

    std::array<Move, 4> quiets{};
    std::size_t quiet_count = 0;
    for (const Move move : legal_moves) {
        if (!is_tactical_move(position, move)
            && quiet_count < quiets.size()) {
            quiets[quiet_count++] = move;
        }
    }

    expect(
      contains_move(legal_moves, capture)
        && quiet_count == quiets.size(),
      "the ply-isolation fixture contains every required move");
    if (!contains_move(legal_moves, capture)
        || quiet_count != quiets.size()) {
        return;
    }

    SearchDetail::SearchState state;
    state.killer_moves(4).record(quiets[1]);
    state.killer_moves(5).record(quiets[2]);
    state.killer_moves(
      MAX_SEARCH_DEPTH - 1).record(quiets[3]);
    state.quiet_history.update(
      position.piece_on(quiets[0].from()),
      quiets[0].to(),
      QuietHistory::LIMIT);

    MoveList ply_four;
    ply_four.push_back(quiets[2]);
    ply_four.push_back(quiets[0]);
    ply_four.push_back(capture);
    ply_four.push_back(quiets[1]);
    order_moves(
      position,
      ply_four,
      state.ordering_buffer,
      state.quiet_history,
      state.killer_moves(4),
      Move::none());

    MoveList ply_five;
    ply_five.push_back(quiets[1]);
    ply_five.push_back(quiets[0]);
    ply_five.push_back(capture);
    ply_five.push_back(quiets[2]);
    order_moves(
      position,
      ply_five,
      state.ordering_buffer,
      state.quiet_history,
      state.killer_moves(5),
      Move::none());

    expect(
      ply_four[0] == capture
        && ply_four[1] == quiets[1]
        && ply_four[2] == quiets[0]
        && ply_five[0] == capture
        && ply_five[1] == quiets[2]
        && ply_five[2] == quiets[0],
      "killer moves affect only the main-search ply where they were recorded");
    expect(
      state.killer_moves(3).primary().is_none()
        && state.killer_moves(4).primary()
             == quiets[1]
        && state.killer_moves(5).primary()
             == quiets[2]
        && state.killer_moves(
             MAX_SEARCH_DEPTH - 1)
             .primary() == quiets[3],
      "untouched, adjacent, and final killer-table indexes are independent");

    state.killer_moves(4).clear();
    expect(
      state.killer_moves(4).primary().is_none()
        && state.killer_moves(5).primary()
             == quiets[2],
      "clearing one ply does not alter an adjacent killer pair");
}

void test_preferred_move_ordering() {
    Position position =
      material_tactic_position();
    const Position original = position;
    const Move capture = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    Move first_quiet = Move::none();
    Move second_quiet = Move::none();
    for (const Move move : legal_moves) {
        if (is_tactical_move(position, move))
            continue;

        if (!first_quiet.is_board_move())
            first_quiet = move;
        else {
            second_quiet = move;
            break;
        }
    }

    expect(
      contains_move(legal_moves, capture)
        && first_quiet.is_board_move()
        && second_quiet.is_board_move(),
      "the preferred-order fixture contains every tested move");
    if (!contains_move(legal_moves, capture)
        || !first_quiet.is_board_move()
        || !second_quiet.is_board_move())
        return;

    MoveList baseline;
    baseline.push_back(first_quiet);
    baseline.push_back(capture);
    baseline.push_back(second_quiet);
    order_moves(position, baseline);
    expect(
      baseline[0] == capture
        && baseline[1] == first_quiet
        && baseline[2] == second_quiet,
      "material ordering establishes the preferred-order baseline");

    MoveList preferred;
    preferred.push_back(first_quiet);
    preferred.push_back(capture);
    preferred.push_back(second_quiet);
    order_moves(
      position, preferred, second_quiet);
    expect(
      preferred[0] == second_quiet
        && preferred[1] == capture
        && preferred[2] == first_quiet,
      "the preferred move leads while the sorted tail remains stable");

    const Move absent = Move::normal(
      make_square(FILE_D, RANK_1),
      make_square(FILE_D, RANK_2));
    for (const Move hint :
         {Move::none(), Move::null(), absent}) {
        MoveList unchanged;
        unchanged.push_back(first_quiet);
        unchanged.push_back(capture);
        unchanged.push_back(second_quiet);
        order_moves(position, unchanged, hint);

        expect(
          unchanged[0] == capture
            && unchanged[1] == first_quiet
            && unchanged[2] == second_quiet,
          "an absent or non-board hint leaves material order unchanged");
    }

    order_moves(
      position, preferred, second_quiet);
    expect(
      preferred[0] == second_quiet
        && preferred[1] == capture
        && preferred[2] == first_quiet,
      "reapplying an existing first preference is idempotent");
    expect(
      positions_equal(position, original),
      "preferred move ordering preserves every position field");
}

void test_aliased_quiet_cutoff_history() {
    Position position =
      quiet_history_alias_position();
    const Position original = position;
    PositionHistory history{position.key()};
    constexpr Move first_alias = Move::normal(
      make_square(FILE_E, RANK_4),
      make_square(FILE_F, RANK_6));
    constexpr Move cutoff_alias = Move::normal(
      make_square(FILE_G, RANK_4),
      make_square(FILE_F, RANK_6));
    constexpr Square shared_destination =
      make_square(FILE_F, RANK_6);

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    expect(
      contains_move(legal_moves, first_alias)
        && contains_move(
             legal_moves, cutoff_alias)
        && !is_tactical_move(
             position, first_alias)
        && !is_tactical_move(
             position, cutoff_alias),
      "the alias fixture contains both quiet knight moves");
    if (!contains_move(legal_moves, first_alias)
        || !contains_move(
             legal_moves, cutoff_alias)) {
        return;
    }

    SearchDetail::SearchState state;
    for (const Move move : legal_moves) {
        if (is_tactical_move(position, move)) {
            continue;
        }

        const Piece piece =
          position.piece_on(move.from());
        if (piece == R_KNIGHT
            && move.to() == shared_destination) {
            continue;
        }

        state.quiet_history.update(
          piece,
          move.to(),
          -QuietHistory::LIMIT);
    }

    MoveList ordered = legal_moves;
    order_moves(
      position,
      ordered,
      state.ordering_buffer,
      state.quiet_history,
      first_alias);
    expect(
      ordered.size() >= 2
        && ordered[0] == first_alias
        && ordered[1] == cutoff_alias,
      "the preferred aliased quiet precedes the unpenalized cutoff alias");

    const auto result =
      SearchDetail::alpha_beta(
        position,
        history,
        1,
        0,
        -INFINITE_SCORE,
        DRAW_SCORE,
        state,
        first_alias);
    expect(
      result
        && result->score == Score{140}
        && result->best_move == cutoff_alias
        && state.nodes == 4,
      "the second aliased quiet move produces the depth-one beta cutoff");
    expect(
      state.quiet_history.score(
        R_KNIGHT, shared_destination)
        == QuietHistory::depth_bonus(1),
      "an earlier aliased quiet does not penalize the cutoff history entry");
    expect(
      state.killer_moves(0).primary()
          == cutoff_alias
        && state.killer_moves(0)
             .secondary().is_none(),
      "the exact aliased move, rather than its shared history entry, "
      "occupies the primary killer slot");
    expect(
      positions_equal(position, original)
        && history.size() == 1
        && history.current_key()
             == position.key(),
      "aliased cutoff search restores position and history");
}

void test_quiet_history_cutoff_training() {
    constexpr std::array<std::size_t, COLOR_NB>
      TARGET_INDEXES = {6, 6, 1, 1};
    constexpr std::array<std::uint64_t, COLOR_NB>
      FIRST_SEARCH_NODES = {134, 142, 68, 68};
    constexpr std::uint64_t TRAINED_SEARCH_NODES = 50;
    constexpr HistoryScore DEPTH_THREE_BONUS =
      QuietHistory::depth_bonus(3);

    Position position =
      quiet_history_cutoff_position();
    Move target = Move::normal(
      make_square(FILE_N, RANK_4),
      make_square(FILE_G, RANK_11));

    for (int rotation = 0;
         rotation < COLOR_NB;
         ++rotation) {
        const std::size_t rotation_index =
          static_cast<std::size_t>(rotation);
        const Position original = position;
        PositionHistory history{position.key()};
        const Piece target_piece =
          position.piece_on(target.from());

        MoveList generated;
        generate_legal_moves(position, generated);
        MoveList initial_order = generated;
        order_moves(position, initial_order);

        std::size_t target_index =
          initial_order.size();
        bool all_quiet = true;
        for (std::size_t index = 0;
             index < initial_order.size();
             ++index) {
            const Move move = initial_order[index];
            if (is_tactical_move(position, move))
                all_quiet = false;
            if (move == target)
                target_index = index;
        }

        expect(
          initial_order.size() == 16
            && all_quiet
            && target_index
                 == TARGET_INDEXES[rotation_index],
          "the rotated cutoff fixture has the expected initial quiet order");

        const SearchResult exact =
          search(position, history, 3);
        expect(
          exact.score == BISHOP_VALUE
            && exact.best_move == target,
          "the rotated quiet cutoff move is the depth-three exact best move");

        SearchDetail::SearchState state;
        const auto first =
          SearchDetail::alpha_beta(
            position,
            history,
            3,
            0,
            Score{-1},
            DRAW_SCORE,
            state);
        expect(
          first
            && first->score
                 == BISHOP_VALUE - PAWN_VALUE
            && first->best_move == target
            && state.nodes
                 == FIRST_SEARCH_NODES[
                      rotation_index],
          "a later quiet move produces the expected null-window beta cutoff");
        if (!first)
            return;
        expect(
          state.killer_moves(0).primary()
              == target
            && state.killer_moves(0)
                 .secondary().is_none(),
          "a completed rotated quiet cutoff records its exact move as "
          "the primary root killer");

        bool root_updates_match = true;
        for (std::size_t index = 0;
             index < initial_order.size();
             ++index) {
            const Move move = initial_order[index];
            const HistoryScore expected =
              index < target_index
                ? -DEPTH_THREE_BONUS
              : index == target_index
                ? DEPTH_THREE_BONUS
                : HistoryScore{0};
            if (state.quiet_history.score(
                  position.piece_on(
                    move.from()),
                  move.to())
                != expected) {
                root_updates_match = false;
            }
        }
        expect(
          root_updates_match,
          "a quiet cutoff rewards its move and penalizes only "
          "earlier quiet siblings");

        MoveList trained_order = generated;
        order_moves(
          position,
          trained_order,
          state.ordering_buffer,
          state.quiet_history);
        expect(
          !trained_order.empty()
            && trained_order[0] == target,
          "the rewarded quiet cutoff leads the next root ordering");

        QuietHistory empty_history;
        MoveList killer_order = generated;
        order_moves(
          position,
          killer_order,
          state.ordering_buffer,
          empty_history,
          state.killer_moves(0),
          Move::none());
        expect(
          !killer_order.empty()
            && killer_order[0] == target,
          "the recorded killer leads root quiet moves without history scores");

        const std::uint64_t first_nodes =
          state.nodes;
        const auto trained =
          SearchDetail::alpha_beta(
            position,
            history,
            3,
            0,
            Score{-1},
            DRAW_SCORE,
            state);
        const std::uint64_t trained_nodes =
          state.nodes - first_nodes;
        expect(
          trained
            && trained->score == first->score
            && trained->best_move == target
            && trained_nodes
                 == TRAINED_SEARCH_NODES
            && trained_nodes < first_nodes,
          "trained quiet ordering reduces the repeated null-window search");
        expect(
          state.quiet_history.score(
            target_piece, target.to())
            == HistoryScore{571},
          "a repeated quiet cutoff applies a second gravity reward");
        expect(
          state.killer_moves(0).primary()
              == target
            && state.killer_moves(0)
                 .secondary().is_none(),
          "repeating the primary killer leaves the root pair unchanged");

        const std::uint64_t interrupted_limit =
          FIRST_SEARCH_NODES[rotation_index]
          - 1;
        SearchDetail::SearchBudget budget{
          interrupted_limit,
          std::nullopt};
        SearchDetail::LimitedSearchState
          interrupted_state{
            std::move(budget)};
        PositionHistory interrupted_history{
          position.key()};
        const auto interrupted =
          SearchDetail::alpha_beta(
            position,
            interrupted_history,
            3,
            0,
            Score{-1},
            DRAW_SCORE,
            interrupted_state);

        bool interrupted_root_history_zero = true;
        for (const Move move : generated) {
            if (interrupted_state
                  .quiet_history.score(
                    position.piece_on(
                      move.from()),
                    move.to())
                != 0) {
                interrupted_root_history_zero =
                  false;
            }
        }
        expect(
          !interrupted
            && interrupted.error()
                 == SearchStopReason::NODE_LIMIT
            && interrupted_state.nodes
                 == interrupted_limit
            && interrupted_root_history_zero
            && interrupted_state.killer_moves(0)
                 .primary().is_none()
            && interrupted_state.killer_moves(0)
                 .secondary().is_none(),
          "an interrupted cutoff child cannot update root-player "
          "history or root killers");

        SearchDetail::SearchBudget exact_budget{
          FIRST_SEARCH_NODES[rotation_index],
          std::nullopt};
        SearchDetail::LimitedSearchState
          exact_state{
            std::move(exact_budget)};
        PositionHistory exact_history{
          position.key()};
        const auto exact_limit =
          SearchDetail::alpha_beta(
            position,
            exact_history,
            3,
            0,
            Score{-1},
            DRAW_SCORE,
            exact_state);
        expect(
          exact_limit
            && exact_state.nodes
                 == FIRST_SEARCH_NODES[
                      rotation_index]
            && exact_state.quiet_history.score(
                 target_piece, target.to())
                 == DEPTH_THREE_BONUS
            && exact_state.killer_moves(0)
                 .primary() == target
            && exact_state.killer_moves(0)
                 .secondary().is_none(),
          "the exact node boundary completes and trains both quiet heuristics");
        expect(
          positions_equal(position, original)
            && history.size() == 1
            && history.current_key()
                 == position.key()
            && interrupted_history.size() == 1
            && interrupted_history.current_key()
                 == position.key()
            && exact_history.size() == 1
            && exact_history.current_key()
                 == position.key(),
          "completed and interrupted quiet-history searches restore "
          "all root state");

        position = rotate_clockwise(position);
        target = rotate_clockwise(target);
    }
}

void test_killer_training_after_pvs_research() {
    Position position =
      quiet_history_cutoff_position();
    const Position original = position;
    const Move target = Move::normal(
      make_square(FILE_N, RANK_4),
      make_square(FILE_G, RANK_11));
    const Piece target_piece =
      position.piece_on(target.from());
    const PositionHistory history{
      position.key()};

    Position target_child = position;
    UndoState target_undo;
    do_move(
      target_child, target, target_undo);
    PositionHistory target_child_history{
      history};
    target_child_history.push(
      target_child.key());

    TranspositionTable interrupted_table;
    interrupted_table.new_search();
    SearchDetail::SearchBudget budget{
      std::uint64_t{183},
      std::nullopt};
    SearchDetail::LimitedSearchState
      interrupted_state{
        std::move(budget),
        &interrupted_table};
    PositionHistory interrupted_history{
      history};
    const auto interrupted =
      SearchDetail::alpha_beta(
        position,
        interrupted_history,
        3,
        0,
        -INFINITE_SCORE,
        Score{300},
        interrupted_state);
    const TranspositionEntry* scout_entry =
      interrupted_table.find(
        target_child.key(),
        target_child_history.context());

    expect(
      !interrupted
        && interrupted.error()
             == SearchStopReason::NODE_LIMIT
        && interrupted_state.nodes == 183
        && scout_entry
        && scout_entry->depth == 2
        && scout_entry->bound
             == TranspositionBound::UPPER
        && scout_entry->score == Score{-230},
      "the PVS scout completes before the finite-window re-search is interrupted");
    expect(
      interrupted_state.killer_moves(0)
          .primary().is_none()
        && interrupted_state.killer_moves(0)
             .secondary().is_none()
        && interrupted_state.quiet_history.score(
             target_piece, target.to()) == 0
        && !interrupted_table.find(
             position.key(),
             history.context()),
      "an interior scout result cannot train or publish the interrupted root");

    TranspositionTable completed_table;
    completed_table.new_search();
    SearchDetail::SearchState completed_state{
      SearchDetail::UnlimitedBudget{},
      &completed_table};
    PositionHistory completed_history{
      history};
    const auto completed =
      SearchDetail::alpha_beta(
        position,
        completed_history,
        3,
        0,
        -INFINITE_SCORE,
        Score{300},
        completed_state);
    const TranspositionEntry* root_entry =
      completed_table.find(
        position.key(),
        history.context());

    expect(
      completed
        && completed->score == BISHOP_VALUE
        && completed->best_move == target
        && completed_state.nodes == 268
        && root_entry
        && root_entry->depth == 3
        && root_entry->bound
             == TranspositionBound::LOWER
        && root_entry->score == BISHOP_VALUE,
      "the completed PVS re-search returns the exact fail-soft cutoff score");
    expect(
      completed_state.killer_moves(0)
          .primary() == target
        && completed_state.killer_moves(0)
             .secondary().is_none()
        && completed_state.quiet_history.score(
             target_piece,
             target.to())
             == QuietHistory::depth_bonus(3),
      "the quiet move is trained only after its full-window re-search completes");
    expect(
      positions_equal(position, original)
        && interrupted_history.size() == 1
        && interrupted_history.current_key()
             == position.key()
        && completed_history.size() == 1
        && completed_history.current_key()
             == position.key(),
      "interrupted and completed PVS killer training restores all root state");
}

struct ReferenceResult {
    Move best_move = Move::none();
    Score score = DRAW_SCORE;
    std::uint64_t nodes = 0;
};

// This reference follows generation order in both fixed-depth and
// quiescence nodes.
[[nodiscard]] ReferenceResult unordered_quiescence(
  Position& position,
  PositionHistory& history,
  int ply,
  int quiescence_ply,
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

    if (quiescence_ply == MAX_QUIESCENCE_PLY) {
        result.score = evaluate(position);
        return result;
    }

    const bool checked = in_check(position);
    result.score =
      checked ? -INFINITE_SCORE : evaluate(position);

    if (!checked) {
        if (result.score >= beta)
            return result;
        if (result.score > alpha)
            alpha = result.score;
    }

    for (const Move move : legal_moves) {
        if (!checked
            && !is_tactical_move(position, move))
            continue;

        UndoState undo;
        do_move(position, move, undo);
        const PositionKey child_key =
          position.key();
        history.push(child_key);

        const ReferenceResult child =
          unordered_quiescence(
            position,
            history,
            ply + 1,
            quiescence_ply + 1,
            -beta,
            -alpha);
        const Score candidate = -child.score;
        result.nodes += child.nodes;

        history.pop(child_key);
        undo_move(position, move, undo);

        if (candidate > result.score)
            result.score = candidate;
        if (candidate > alpha)
            alpha = candidate;
        if (alpha >= beta)
            break;
    }

    return result;
}

[[nodiscard]] ReferenceResult unordered_alpha_beta(
  Position& position,
  PositionHistory& history,
  int depth,
  int ply,
  Score alpha,
  Score beta) {
    if (depth == 0) {
        return unordered_quiescence(
          position,
          history,
          ply,
          0,
          alpha,
          beta);
    }

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
    test_quiet_history_table();
    test_quiet_history_ordering();
    test_killer_move_pair();
    test_killer_move_ordering();
    test_killer_ply_isolation();
    test_preferred_move_ordering();
    test_aliased_quiet_cutoff_history();
    test_quiet_history_cutoff_training();
    test_killer_training_after_pvs_research();
    test_search_integration();

    if (failures != 0) {
        std::cerr << failures
                  << " ordering test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All ordering tests passed\n";
    return EXIT_SUCCESS;
}
