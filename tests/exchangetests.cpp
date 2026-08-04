#include "exchange.h"
#include "ordering.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
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

inline constexpr std::array<CastlingSide, CASTLING_SIDE_NB>
  CASTLING_SIDES = {
    CastlingSide::KING_SIDE,
    CastlingSide::QUEEN_SIDE,
};

void expect(
  bool condition,
  std::string_view case_name,
  std::string_view requirement) {
    if (condition)
        return;

    std::cerr << "FAIL: " << case_name
              << ": " << requirement << '\n';
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

[[nodiscard]] constexpr bool positions_equal(
  const Position& left,
  const Position& right) noexcept {
    if (left.side_to_move() != right.side_to_move()
        || left.key() != right.key()
        || left.recompute_key() != right.recompute_key()
        || left.occupied() != right.occupied()) {
        return false;
    }

    for (int square_index = 0;
         square_index < SQUARE_NB;
         ++square_index) {
        const Square square = Square(square_index);
        if (is_ok(square)
            && left.piece_on(square)
                 != right.piece_on(square)) {
            return false;
        }
    }

    for (const Color color : COLORS) {
        if (left.pieces(color) != right.pieces(color)
            || left.en_passant_square(color)
                 != right.en_passant_square(color)) {
            return false;
        }

        for (const CastlingSide side : CASTLING_SIDES) {
            if (left.has_castling_right(color, side)
                != right.has_castling_right(color, side)) {
                return false;
            }
        }
    }

    for (int type_index = PAWN;
         type_index <= KING;
         ++type_index) {
        const PieceType piece_type =
          PieceType(type_index);
        if (left.pieces(piece_type)
              != right.pieces(piece_type)) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] constexpr Position base_position() noexcept {
    Position position;
    position.put_piece(
      R_KING, make_square(FILE_E, RANK_4));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_14));
    position.put_piece(
      G_KING, make_square(FILE_N, RANK_8));
    return position;
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

    std::unreachable();
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

        const Piece piece =
          position.piece_on(square);
        rotated.put_piece(
          make_piece(
            next_color(color_of(piece)),
            type_of(piece)),
          rotate_clockwise(square));
    }

    for (const Color color : COLORS) {
        const Color rotated_color =
          next_color(color);
        const Square target =
          position.en_passant_square(color);
        if (target != SQ_NONE) {
            rotated.set_en_passant_square(
              rotated_color,
              rotate_clockwise(target));
        }

        for (const CastlingSide side : CASTLING_SIDES) {
            if (position.has_castling_right(
                  color, side)) {
                rotated.set_castling_right(
                  rotated_color, side);
            }
        }
    }

    return rotated;
}

namespace Oracle {

[[nodiscard]] constexpr Team opposing_team(
  Team team) noexcept {
    assert(is_ok(team));
    return team == RED_YELLOW
        ? BLUE_GREEN
        : RED_YELLOW;
}

constexpr void pass_turn(
  Position& position) noexcept {
    const Color color =
      position.side_to_move();
    position.clear_en_passant_square(color);
    position.set_side_to_move(
      next_color(color));
}

[[nodiscard]] constexpr Score continuation(
  const Position& position,
  Square target,
  Team root_team,
  Score baseline) noexcept {
    assert(!position.empty(target));

    const Team capturing_team =
      opposing_team(
        team_of(color_of(
          position.piece_on(target))));
    const Score current =
      material_balance(position, root_team)
      - baseline;
    Score best = current;

    for (int turn_offset = 0;
         turn_offset < COLOR_NB;
         ++turn_offset) {
        Position branch = position;
        for (int skipped_turn = 0;
             skipped_turn < turn_offset;
             ++skipped_turn) {
            pass_turn(branch);
        }

        if (team_of(branch.side_to_move())
            != capturing_team) {
            continue;
        }

        MoveList pseudo_moves;
        generate_moves(branch, pseudo_moves);
        for (const Move move : pseudo_moves) {
            if (move.to() != target
                || move.type() == MoveType::CASTLING
                || !is_legal_move(branch, move)) {
                continue;
            }

            const Piece captured =
              branch.piece_on(target);
            Score result = 0;
            if (type_of(captured) == KING) {
                result =
                  capturing_team == root_team
                    ? EXCHANGE_KING_SCORE
                    : -EXCHANGE_KING_SCORE;
            } else {
                UndoState undo;
                do_move(branch, move, undo);
                result = continuation(
                  branch,
                  target,
                  root_team,
                  baseline);
                undo_move(branch, move, undo);
            }

            if (capturing_team == root_team) {
                if (result > best)
                    best = result;
            } else if (result < best) {
                best = result;
            }
        }
    }

    return best;
}

[[nodiscard]] constexpr Score evaluate(
  const Position& position,
  Move move) noexcept {
    const Team root_team =
      team_of(position.side_to_move());
    const Score baseline =
      material_balance(position, root_team);
    if (!position.empty(move.to())
        && type_of(position.piece_on(move.to()))
             == KING) {
        return EXCHANGE_KING_SCORE;
    }

    Position branch = position;
    UndoState undo;
    do_move(branch, move, undo);
    const Score result =
      continuation(
        branch,
        move.to(),
        root_team,
        baseline);
    undo_move(branch, move, undo);
    return result;
}

}  // namespace Oracle

struct ExchangeCase {
    Position position;
    Move move = Move::none();
    Score expected = 0;
    std::string_view name;
};

void verify_case(
  ExchangeCase exchange_case,
  bool rotate = true) {
    const int rotation_count =
      rotate ? COLOR_NB : 1;

    for (int rotation = 0;
         rotation < rotation_count;
         ++rotation) {
        const Position original =
          exchange_case.position;
        MoveList legal_moves;
        generate_legal_moves(
          exchange_case.position, legal_moves);

        expect(
          contains_move(
            legal_moves, exchange_case.move),
          exchange_case.name,
          "the forced move is legal");
        if (!contains_move(
              legal_moves, exchange_case.move)) {
            return;
        }

        const Score first =
          static_exchange_evaluation(
            exchange_case.position,
            exchange_case.move);
        const Score second =
          static_exchange_evaluation(
            exchange_case.position,
            exchange_case.move);
        const Score oracle =
          Oracle::evaluate(
            exchange_case.position,
            exchange_case.move);

        expect(
          first == exchange_case.expected,
          exchange_case.name,
          "the exchange score matches the exact regression");
        expect(
          first == oracle,
          exchange_case.name,
          "the exchange score matches the material-delta oracle");
        expect(
          second == first,
          exchange_case.name,
          "repeated evaluation is deterministic");
        expect(
          static_exchange_at_least(
            exchange_case.position,
            exchange_case.move,
            exchange_case.expected),
          exchange_case.name,
          "the exact threshold succeeds");
        expect(
          static_exchange_at_least(
            exchange_case.position,
            exchange_case.move,
            exchange_case.expected - 1),
          exchange_case.name,
          "the next lesser threshold succeeds");
        expect(
          !static_exchange_at_least(
            exchange_case.position,
            exchange_case.move,
            exchange_case.expected + 1),
          exchange_case.name,
          "the next greater threshold fails");
        expect(
          positions_equal(
            exchange_case.position, original),
          exchange_case.name,
          "evaluation restores every position field");

        exchange_case.position =
          rotate_clockwise(
            exchange_case.position);
        exchange_case.move =
          rotate_clockwise(exchange_case.move);
    }
}

void verify_fast_case(
  ExchangeCase exchange_case) {
    for (int rotation = 0;
         rotation < COLOR_NB;
         ++rotation) {
        const Position original =
          exchange_case.position;
        MoveList legal_moves;
        generate_legal_moves(
          exchange_case.position, legal_moves);

        const Score oracle = Oracle::evaluate(
          exchange_case.position,
          exchange_case.move);
        const FastExchangeResult exact_threshold =
          fast_static_exchange_at_least(
            exchange_case.position,
            exchange_case.move,
            oracle);
        const FastExchangeResult greater_threshold =
          fast_static_exchange_at_least(
            exchange_case.position,
            exchange_case.move,
            oracle + 1);

        expect(
          contains_move(
            legal_moves, exchange_case.move),
          exchange_case.name,
          "the fast exchange move is legal");
        expect(
          oracle == exchange_case.expected,
          exchange_case.name,
          "the fast exchange regression matches the oracle");
        expect(
          exact_threshold
            == FastExchangeResult::AT_LEAST,
          exchange_case.name,
          "the fast exchange accepts the exact oracle threshold");
        expect(
          greater_threshold
            == FastExchangeResult::BELOW,
          exchange_case.name,
          "the fast exchange rejects the next greater threshold");
        expect(
          fast_static_exchange_is_not_proven_below(
            exchange_case.position,
            exchange_case.move,
            oracle)
          && !fast_static_exchange_is_not_proven_below(
            exchange_case.position,
            exchange_case.move,
            oracle + 1),
          exchange_case.name,
          "the fast conservative wrapper preserves proven results");
        expect(
          positions_equal(
            exchange_case.position, original),
          exchange_case.name,
          "the fast exchange leaves every position field unchanged");

        exchange_case.position =
          rotate_clockwise(
            exchange_case.position);
        exchange_case.move =
          rotate_clockwise(exchange_case.move);
    }
}

void verify_fast_unknown(
  Position position,
  Move move,
  std::string_view case_name) {
    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    const Position original = position;

    expect(
      contains_move(legal_moves, move),
      case_name,
      "the unsupported fast exchange move is legal");
    expect(
      fast_static_exchange_at_least(
        position, move, 0)
        == FastExchangeResult::UNKNOWN,
      case_name,
      "the occupancy-only exchange reports unknown");
    expect(
      fast_static_exchange_is_not_proven_below(
        position, move, 0),
      case_name,
      "unknown passes through the conservative wrapper");
    expect(
      positions_equal(position, original),
      case_name,
      "unknown classification leaves every position field unchanged");
}

[[nodiscard]] constexpr ExchangeCase
undefended_capture_case() noexcept {
    Position position = base_position();
    position.put_piece(
      R_PAWN, make_square(FILE_G, RANK_7));
    position.put_piece(
      B_ROOK, make_square(FILE_H, RANK_8));
    return {
      position,
      Move::normal(
        make_square(FILE_G, RANK_7),
        make_square(FILE_H, RANK_8)),
      ROOK_VALUE,
      "undefended capture",
    };
}

[[nodiscard]] consteval bool
constexpr_exchange_works() {
    const ExchangeCase exchange_case =
      undefended_capture_case();
    return static_exchange_evaluation(
             exchange_case.position,
             exchange_case.move)
             == ROOK_VALUE
        && static_exchange_at_least(
             exchange_case.position,
             exchange_case.move,
             std::numeric_limits<Score>::lowest())
        && static_exchange_at_least(
             exchange_case.position,
             exchange_case.move,
             -EXCHANGE_KING_SCORE)
        && !static_exchange_at_least(
             exchange_case.position,
             exchange_case.move,
             EXCHANGE_KING_SCORE)
        && !static_exchange_at_least(
             exchange_case.position,
             exchange_case.move,
             EXCHANGE_KING_SCORE + 1)
        && !static_exchange_at_least(
             exchange_case.position,
             exchange_case.move,
             std::numeric_limits<Score>::max())
        && fast_static_exchange_at_least(
             exchange_case.position,
             exchange_case.move,
             ROOK_VALUE)
             == FastExchangeResult::AT_LEAST
        && fast_static_exchange_at_least(
             exchange_case.position,
             exchange_case.move,
             ROOK_VALUE + 1)
             == FastExchangeResult::BELOW;
}

static_assert(constexpr_exchange_works());
static_assert(
  std::is_same_v<
    decltype(static_exchange_evaluation(
      std::declval<const Position&>(),
      std::declval<Move>())),
    Score>);
static_assert(noexcept(
  static_exchange_evaluation(
    std::declval<const Position&>(),
    std::declval<Move>())));
static_assert(noexcept(
  static_exchange_at_least(
    std::declval<const Position&>(),
    std::declval<Move>(),
    std::declval<Score>())));
static_assert(noexcept(
  static_exchange_is_not_proven_losing(
    std::declval<const Position&>(),
    std::declval<Move>())));
static_assert(noexcept(
  fast_static_exchange_at_least(
    std::declval<const Position&>(),
    std::declval<Move>(),
    std::declval<Score>())));
static_assert(noexcept(
  fast_static_exchange_is_not_proven_below(
    std::declval<const Position&>(),
    std::declval<Move>(),
    std::declval<Score>())));

void test_fast_exchange_threshold() {
    verify_fast_case(undefended_capture_case());

    {
        Position position = base_position();
        position.put_piece(
          R_QUEEN, make_square(FILE_H, RANK_6));
        position.put_piece(
          B_PAWN, make_square(FILE_H, RANK_8));
        position.put_piece(
          G_ROOK, make_square(FILE_H, RANK_10));
        verify_fast_case({
          position,
          Move::normal(
            make_square(FILE_H, RANK_6),
            make_square(FILE_H, RANK_8)),
          PAWN_VALUE - QUEEN_VALUE,
          "fast losing defended capture",
        });
    }

    {
        Position position = base_position();
        position.put_piece(
          R_QUEEN, make_square(FILE_G, RANK_7));
        position.put_piece(
          B_PAWN, make_square(FILE_H, RANK_8));
        position.put_piece(
          G_ROOK, make_square(FILE_H, RANK_10));
        position.put_piece(
          Y_BISHOP, make_square(FILE_F, RANK_6));
        position.put_piece(
          B_BISHOP, make_square(FILE_E, RANK_5));
        position.put_piece(
          R_BISHOP, make_square(FILE_D, RANK_4));
        verify_fast_case({
          position,
          Move::normal(
            make_square(FILE_G, RANK_7),
            make_square(FILE_H, RANK_8)),
          -300,
          "fast four-reply x-ray sequence",
        });
    }

    {
        Position position = base_position();
        position.remove_piece(
          make_square(FILE_A, RANK_7));
        position.put_piece(
          B_KING, make_square(FILE_H, RANK_4));
        position.put_piece(
          R_ROOK, make_square(FILE_H, RANK_10));
        position.put_piece(
          B_ROOK, make_square(FILE_H, RANK_7));
        position.put_piece(
          R_PAWN, make_square(FILE_H, RANK_6));
        position.put_piece(
          B_PAWN, make_square(FILE_I, RANK_7));
        verify_fast_case({
          position,
          Move::normal(
            make_square(FILE_H, RANK_6),
            make_square(FILE_I, RANK_7)),
          PAWN_VALUE,
          "fast pinned illegal recapture",
        });
    }

    {
        Position position = base_position();
        position.put_piece(
          R_ROOK, make_square(FILE_H, RANK_6));
        position.put_piece(
          B_PAWN, make_square(FILE_H, RANK_8));
        position.put_piece(
          G_PAWN, make_square(FILE_I, RANK_9));
        position.put_piece(
          Y_PAWN, make_square(FILE_G, RANK_9));
        verify_fast_case({
          position,
          Move::normal(
            make_square(FILE_H, RANK_6),
            make_square(FILE_H, RANK_8)),
          PAWN_VALUE - ROOK_VALUE + PAWN_VALUE,
          "fast teammate recapture",
        });
    }

    {
        Position position = base_position();
        position.put_piece(
          R_KNIGHT, make_square(FILE_F, RANK_11));
        position.put_piece(
          B_PAWN, make_square(FILE_H, RANK_12));
        position.put_piece(
          Y_PAWN, make_square(FILE_H, RANK_11));
        position.put_piece(
          B_PAWN, make_square(FILE_G, RANK_11));
        position.set_en_passant_square(
          YELLOW, make_square(FILE_H, RANK_12));
        verify_fast_unknown(
          position,
          Move::normal(
            make_square(FILE_F, RANK_11),
            make_square(FILE_H, RANK_12)),
          "fast en-passant reply pass-through");
    }

    {
        Position position = base_position();
        position.put_piece(
          R_PAWN, make_square(FILE_H, RANK_10));
        verify_fast_unknown(
          position,
          Move::promotion(
            make_square(FILE_H, RANK_10),
            make_square(FILE_H, RANK_11),
            QUEEN),
          "fast promotion pass-through");
    }

    {
        Position position = base_position();
        position.put_piece(
          R_QUEEN, make_square(FILE_K, RANK_8));
        position.put_piece(
          B_PAWN, make_square(FILE_K, RANK_10));
        position.put_piece(
          B_PAWN, make_square(FILE_J, RANK_9));
        verify_fast_unknown(
          position,
          Move::normal(
            make_square(FILE_K, RANK_8),
            make_square(FILE_K, RANK_10)),
          "fast promotion-recapture pass-through");
    }
}

void test_capture_sequences() {
    verify_case(undefended_capture_case());

    {
        Position position = base_position();
        position.put_piece(
          R_QUEEN, make_square(FILE_H, RANK_6));
        position.put_piece(
          B_PAWN, make_square(FILE_H, RANK_8));
        position.put_piece(
          G_ROOK, make_square(FILE_H, RANK_10));
        verify_case({
          position,
          Move::normal(
            make_square(FILE_H, RANK_6),
            make_square(FILE_H, RANK_8)),
          PAWN_VALUE - QUEEN_VALUE,
          "losing defended capture",
        });
    }

    {
        Position position = base_position();
        position.put_piece(
          R_PAWN, make_square(FILE_G, RANK_7));
        position.put_piece(
          B_ROOK, make_square(FILE_H, RANK_8));
        position.put_piece(
          B_BISHOP, make_square(FILE_F, RANK_6));
        verify_case({
          position,
          Move::normal(
            make_square(FILE_G, RANK_7),
            make_square(FILE_H, RANK_8)),
          ROOK_VALUE - PAWN_VALUE,
          "discovered bishop recapture",
        });
    }

    {
        Position position = base_position();
        position.remove_piece(
          make_square(FILE_A, RANK_7));
        position.put_piece(
          B_KING, make_square(FILE_H, RANK_4));
        position.put_piece(
          R_ROOK, make_square(FILE_H, RANK_10));
        position.put_piece(
          B_ROOK, make_square(FILE_H, RANK_7));
        position.put_piece(
          R_PAWN, make_square(FILE_H, RANK_6));
        position.put_piece(
          B_PAWN, make_square(FILE_I, RANK_7));
        verify_case({
          position,
          Move::normal(
            make_square(FILE_H, RANK_6),
            make_square(FILE_I, RANK_7)),
          PAWN_VALUE,
          "pinned illegal recapture",
        });
    }

    {
        Position position = base_position();
        position.remove_piece(
          make_square(FILE_A, RANK_7));
        position.put_piece(
          B_KING, make_square(FILE_H, RANK_4));
        position.put_piece(
          R_ROOK, make_square(FILE_H, RANK_10));
        position.put_piece(
          B_ROOK, make_square(FILE_H, RANK_7));
        position.put_piece(
          R_QUEEN, make_square(FILE_G, RANK_7));
        position.put_piece(
          B_PAWN, make_square(FILE_H, RANK_8));
        verify_case({
          position,
          Move::normal(
            make_square(FILE_G, RANK_7),
            make_square(FILE_H, RANK_8)),
          PAWN_VALUE - QUEEN_VALUE
            + ROOK_VALUE,
          "legal recapture along pin line",
        });
    }
}

void test_declined_and_deep_sequences() {
    {
        Position position = base_position();
        position.put_piece(
          R_PAWN, make_square(FILE_G, RANK_7));
        position.put_piece(
          B_ROOK, make_square(FILE_H, RANK_8));
        position.put_piece(
          G_ROOK, make_square(FILE_H, RANK_10));
        position.put_piece(
          Y_BISHOP, make_square(FILE_F, RANK_6));
        verify_case({
          position,
          Move::normal(
            make_square(FILE_G, RANK_7),
            make_square(FILE_H, RANK_8)),
          ROOK_VALUE,
          "unfavorable recapture is declined",
        });
    }

    {
        Position position = base_position();
        position.put_piece(
          R_QUEEN, make_square(FILE_G, RANK_7));
        position.put_piece(
          B_PAWN, make_square(FILE_H, RANK_8));
        position.put_piece(
          G_ROOK, make_square(FILE_H, RANK_10));
        position.put_piece(
          Y_BISHOP, make_square(FILE_F, RANK_6));
        position.put_piece(
          B_BISHOP, make_square(FILE_E, RANK_5));
        position.put_piece(
          R_BISHOP, make_square(FILE_D, RANK_4));
        verify_case({
          position,
          Move::normal(
            make_square(FILE_G, RANK_7),
            make_square(FILE_H, RANK_8)),
          -300,
          "four-reply x-ray sequence",
        });
    }
}

void test_four_player_participation() {
    {
        Position position = base_position();
        position.put_piece(
          R_QUEEN, make_square(FILE_H, RANK_6));
        position.put_piece(
          B_PAWN, make_square(FILE_H, RANK_8));
        position.put_piece(
          G_PAWN, make_square(FILE_I, RANK_9));
        verify_case({
          position,
          Move::normal(
            make_square(FILE_H, RANK_6),
            make_square(FILE_H, RANK_8)),
          PAWN_VALUE - QUEEN_VALUE,
          "delayed Green recapture",
        });
    }

    {
        Position position = base_position();
        position.put_piece(
          R_ROOK, make_square(FILE_H, RANK_6));
        position.put_piece(
          B_PAWN, make_square(FILE_H, RANK_8));
        position.put_piece(
          G_PAWN, make_square(FILE_I, RANK_9));
        position.put_piece(
          Y_PAWN, make_square(FILE_G, RANK_9));
        verify_case({
          position,
          Move::normal(
            make_square(FILE_H, RANK_6),
            make_square(FILE_H, RANK_8)),
          PAWN_VALUE - ROOK_VALUE + PAWN_VALUE,
          "teammate recapture",
        });

        position.remove_piece(
          make_square(FILE_I, RANK_9));
        verify_case({
          position,
          Move::normal(
            make_square(FILE_H, RANK_6),
            make_square(FILE_H, RANK_8)),
          PAWN_VALUE,
          "teammate cannot capture friendly occupant",
        });
    }
}

void test_special_replies() {
    {
        Position position = base_position();
        position.put_piece(
          R_KNIGHT, make_square(FILE_F, RANK_11));
        position.put_piece(
          B_PAWN, make_square(FILE_H, RANK_12));
        position.put_piece(
          Y_PAWN, make_square(FILE_H, RANK_11));
        position.put_piece(
          B_PAWN, make_square(FILE_G, RANK_11));
        position.set_en_passant_square(
          YELLOW, make_square(FILE_H, RANK_12));
        verify_case({
          position,
          Move::normal(
            make_square(FILE_F, RANK_11),
            make_square(FILE_H, RANK_12)),
          PAWN_VALUE - KNIGHT_VALUE
            - PAWN_VALUE,
          "en-passant recapture",
        });
    }

    {
        Position position = base_position();
        position.put_piece(
          R_KNIGHT, make_square(FILE_F, RANK_11));
        position.put_piece(
          B_PAWN, make_square(FILE_H, RANK_12));
        position.put_piece(
          Y_PAWN, make_square(FILE_H, RANK_11));
        position.put_piece(
          G_PAWN, make_square(FILE_I, RANK_13));
        position.set_en_passant_square(
          YELLOW, make_square(FILE_H, RANK_12));
        verify_case({
          position,
          Move::normal(
            make_square(FILE_F, RANK_11),
            make_square(FILE_H, RANK_12)),
          PAWN_VALUE - KNIGHT_VALUE,
          "compressed-turn en-passant expiry",
        });
    }

    {
        Position position = base_position();
        position.put_piece(
          R_QUEEN, make_square(FILE_K, RANK_8));
        position.put_piece(
          B_PAWN, make_square(FILE_K, RANK_10));
        position.put_piece(
          B_PAWN, make_square(FILE_J, RANK_9));
        verify_case({
          position,
          Move::normal(
            make_square(FILE_K, RANK_8),
            make_square(FILE_K, RANK_10)),
          PAWN_VALUE - QUEEN_VALUE
            - (QUEEN_VALUE - PAWN_VALUE),
          "promotion recapture",
        });
    }
}

void test_king_recaptures() {
    {
        Position position = base_position();
        position.remove_piece(
          make_square(FILE_A, RANK_7));
        position.put_piece(
          B_KING, make_square(FILE_G, RANK_8));
        position.put_piece(
          R_ROOK, make_square(FILE_H, RANK_6));
        position.put_piece(
          B_PAWN, make_square(FILE_H, RANK_8));
        verify_case({
          position,
          Move::normal(
            make_square(FILE_H, RANK_6),
            make_square(FILE_H, RANK_8)),
          PAWN_VALUE - ROOK_VALUE,
          "legal king recapture",
        });

        position.put_piece(
          Y_ROOK, make_square(FILE_H, RANK_11));
        verify_case({
          position,
          Move::normal(
            make_square(FILE_H, RANK_6),
            make_square(FILE_H, RANK_8)),
          PAWN_VALUE,
          "attacked king recapture",
        });
    }

    {
        Position position = base_position();
        position.remove_piece(
          make_square(FILE_A, RANK_7));
        position.put_piece(
          B_KING, make_square(FILE_H, RANK_8));
        position.put_piece(
          R_ROOK, make_square(FILE_H, RANK_6));
        const Move capture = Move::normal(
          make_square(FILE_H, RANK_6),
          make_square(FILE_H, RANK_8));
        verify_case({
          position,
          capture,
          EXCHANGE_KING_SCORE,
          "direct king capture",
        });
        expect(
          !static_exchange_is_not_proven_below(
            position,
            capture,
            EXCHANGE_KING_SCORE + 1,
            1),
          "direct king capture",
          "the immediate shortcut respects thresholds above the terminal score");
    }

    {
        Position position = base_position();
        position.remove_piece(
          make_square(FILE_A, RANK_7));
        position.put_piece(
          B_KING, make_square(FILE_G, RANK_11));
        position.put_piece(
          R_PAWN, make_square(FILE_H, RANK_10));
        verify_case({
          position,
          Move::promotion(
            make_square(FILE_H, RANK_10),
            make_square(FILE_G, RANK_11),
            QUEEN),
          EXCHANGE_KING_SCORE,
          "promotion captures king",
        });
    }

    {
        Position position = base_position();
        position.remove_piece(
          make_square(FILE_N, RANK_8));
        position.put_piece(
          G_KING, make_square(FILE_C, RANK_6));
        position.put_piece(
          R_PAWN, make_square(FILE_D, RANK_5));
        position.put_piece(
          B_PAWN, make_square(FILE_D, RANK_6));
        position.set_en_passant_square(
          BLUE, make_square(FILE_C, RANK_6));
        verify_case({
          position,
          Move::en_passant(
            make_square(FILE_D, RANK_5),
            make_square(FILE_C, RANK_6)),
          EXCHANGE_KING_SCORE,
          "occupied en passant captures king",
        });
    }
}

void test_promotions() {
    constexpr std::array<PieceType, 4>
      promotion_types = {
        QUEEN,
        ROOK,
        BISHOP,
        KNIGHT,
    };

    for (const PieceType promotion :
         promotion_types) {
        Position position = base_position();
        position.put_piece(
          R_PAWN, make_square(FILE_H, RANK_10));
        verify_case({
          position,
          Move::promotion(
            make_square(FILE_H, RANK_10),
            make_square(FILE_H, RANK_11),
            promotion),
          piece_value(promotion) - PAWN_VALUE,
          "quiet promotion",
        });
    }

    {
        Position position = base_position();
        position.put_piece(
          R_PAWN, make_square(FILE_H, RANK_10));
        position.put_piece(
          B_ROOK, make_square(FILE_G, RANK_11));
        const Move move = Move::promotion(
          make_square(FILE_H, RANK_10),
          make_square(FILE_G, RANK_11),
          QUEEN);
        verify_case({
          position,
          move,
          ROOK_VALUE + QUEEN_VALUE
            - PAWN_VALUE,
          "capture promotion",
        });

        position.put_piece(
          G_ROOK, make_square(FILE_G, RANK_13));
        verify_case({
          position,
          move,
          ROOK_VALUE - PAWN_VALUE,
          "defended capture promotion",
        });
    }
}

void test_en_passant() {
    {
        Position position = base_position();
        position.put_piece(
          R_PAWN, make_square(FILE_D, RANK_5));
        position.put_piece(
          B_PAWN, make_square(FILE_D, RANK_6));
        position.set_en_passant_square(
          BLUE, make_square(FILE_C, RANK_6));
        const Move move = Move::en_passant(
          make_square(FILE_D, RANK_5),
          make_square(FILE_C, RANK_6));

        verify_case({
          position,
          move,
          PAWN_VALUE,
          "empty-target en passant",
        });

        position.put_piece(
          G_ROOK, make_square(FILE_C, RANK_6));
        verify_case({
          position,
          move,
          ROOK_VALUE + PAWN_VALUE,
          "occupied-target en passant",
        });

        position.remove_piece(
          make_square(FILE_E, RANK_4));
        position.put_piece(
          R_KING, make_square(FILE_F, RANK_4));
        position.put_piece(
          B_ROOK, make_square(FILE_E, RANK_6));
        verify_case({
          position,
          move,
          ROOK_VALUE,
          "en passant exposes a recapturing rook",
        });
    }

    {
        Position position = base_position();
        position.put_piece(
          R_PAWN, make_square(FILE_B, RANK_10));
        position.put_piece(
          B_PAWN, make_square(FILE_D, RANK_11));
        position.put_piece(
          G_ROOK, make_square(FILE_C, RANK_11));
        position.set_en_passant_square(
          BLUE, make_square(FILE_C, RANK_11));
        const Move move = Move::en_passant(
          make_square(FILE_B, RANK_10),
          make_square(FILE_C, RANK_11),
          QUEEN);

        verify_case({
          position,
          move,
          ROOK_VALUE + PAWN_VALUE
            + QUEEN_VALUE - PAWN_VALUE,
          "occupied en-passant promotion",
        });

        position.put_piece(
          B_ROOK, make_square(FILE_E, RANK_11));
        position.remove_piece(
          make_square(FILE_E, RANK_4));
        position.put_piece(
          R_KING, make_square(FILE_F, RANK_4));
        verify_case({
          position,
          move,
          ROOK_VALUE,
          "defended en-passant promotion",
        });
    }
}

void test_cutout_boundary() {
    Position position = base_position();
    position.remove_piece(
      make_square(FILE_E, RANK_4));
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_4));
    position.put_piece(
      R_PAWN, make_square(FILE_E, RANK_2));
    position.put_piece(
      B_PAWN, make_square(FILE_D, RANK_3));
    position.put_piece(
      B_BISHOP, make_square(FILE_C, RANK_4));
    verify_case({
      position,
      Move::normal(
        make_square(FILE_E, RANK_2),
        make_square(FILE_D, RANK_3)),
      0,
      "cutout-adjacent exchange",
    });
}

void test_quiet_exchange() {
    Position position = base_position();
    position.put_piece(
      R_QUEEN, make_square(FILE_H, RANK_6));
    position.put_piece(
      G_ROOK, make_square(FILE_H, RANK_10));
    verify_case({
      position,
      Move::normal(
        make_square(FILE_H, RANK_6),
        make_square(FILE_H, RANK_8)),
      -QUEEN_VALUE,
      "hanging quiet queen move",
    });
}

void test_immediate_gain_shortcut() {
    {
        Position position = base_position();
        position.put_piece(
          R_PAWN, make_square(FILE_G, RANK_7));
        position.put_piece(
          B_ROOK, make_square(FILE_H, RANK_8));
        const Move capture = Move::normal(
          make_square(FILE_G, RANK_7),
          make_square(FILE_H, RANK_8));
        MoveList legal_moves;
        generate_legal_moves(position, legal_moves);

        expect(
          contains_move(legal_moves, capture)
            && ExchangeDetail::post_move_piece_value(
                 position, capture) == PAWN_VALUE
            && ExchangeDetail::maximum_first_reply_gain(
                 position, capture) == PAWN_VALUE
            && ExchangeDetail::immediate_gain_guarantees(
                 position, capture, 0)
            && static_exchange_is_not_proven_below(
                 position, capture, 0, 1),
          "immediate-gain shortcut",
          "a rook capture by a pawn needs no recursive exchange nodes");
    }

    {
        Position position = base_position();
        position.put_piece(
          R_ROOK, make_square(FILE_K, RANK_6));
        position.put_piece(
          B_ROOK, make_square(FILE_K, RANK_8));
        const Move capture = Move::normal(
          make_square(FILE_K, RANK_6),
          make_square(FILE_K, RANK_8));
        MoveList legal_moves;
        generate_legal_moves(position, legal_moves);

        expect(
          contains_move(legal_moves, capture)
            && ExchangeDetail::maximum_first_reply_gain(
                 position, capture)
                 == ROOK_VALUE
                    + QUEEN_VALUE
                    - PAWN_VALUE
            && !ExchangeDetail::immediate_gain_guarantees(
                 position, capture, 0),
          "immediate-gain shortcut",
          "a possible promotion reply remains in recursive classification");
    }

    {
        Position position = base_position();
        position.put_piece(
          R_ROOK, make_square(FILE_C, RANK_6));
        position.put_piece(
          B_ROOK, make_square(FILE_C, RANK_8));
        position.put_piece(
          B_PAWN, make_square(FILE_D, RANK_8));
        position.set_en_passant_square(
          BLUE, make_square(FILE_C, RANK_8));
        const Move capture = Move::normal(
          make_square(FILE_C, RANK_6),
          make_square(FILE_C, RANK_8));
        MoveList legal_moves;
        generate_legal_moves(position, legal_moves);

        expect(
          contains_move(legal_moves, capture)
            && ExchangeDetail::maximum_first_reply_gain(
                 position, capture)
                 == ROOK_VALUE + PAWN_VALUE
            && !ExchangeDetail::immediate_gain_guarantees(
                 position, capture, 0),
          "immediate-gain shortcut",
          "a possible en-passant reply includes its additional pawn");
    }
}

void test_ordering_bands() {
    Position position = base_position();
    position.put_piece(
      R_PAWN, make_square(FILE_G, RANK_7));
    position.put_piece(
      B_ROOK, make_square(FILE_H, RANK_8));
    position.put_piece(
      R_QUEEN, make_square(FILE_F, RANK_6));
    position.put_piece(
      B_PAWN, make_square(FILE_F, RANK_8));
    position.put_piece(
      G_ROOK, make_square(FILE_F, RANK_10));

    const Move winning = Move::normal(
      make_square(FILE_G, RANK_7),
      make_square(FILE_H, RANK_8));
    const Move losing = Move::normal(
      make_square(FILE_F, RANK_6),
      make_square(FILE_F, RANK_8));
    const Move quiet = Move::normal(
      make_square(FILE_E, RANK_4),
      make_square(FILE_E, RANK_5));

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    expect(
      contains_move(legal_moves, winning)
        && contains_move(legal_moves, losing)
        && contains_move(legal_moves, quiet),
      "exchange ordering bands",
      "all supplied moves are legal");

    MoveList moves;
    moves.push_back(losing);
    moves.push_back(quiet);
    moves.push_back(winning);
    const Position original = position;
    QuietHistory history;
    KillerMoves killers;
    killers.record(quiet);
    MoveOrderingBuffer buffer;

    order_moves(
      position,
      moves,
      buffer,
      history,
      killers,
      Move::none());
    expect(
      moves[0] == winning
        && moves[1] == quiet
        && moves[2] == losing,
      "exchange ordering bands",
      "non-losing tactics precede quiets and losing tactics");

    killers.clear();
    killers.record(losing);
    moves[0] = losing;
    moves[1] = quiet;
    moves[2] = winning;
    order_moves(
      position,
      moves,
      buffer,
      history,
      killers,
      Move::none());
    expect(
      moves[0] == winning
        && moves[1] == quiet
        && moves[2] == losing,
      "exchange ordering bands",
      "a tactical killer cannot enter the quiet range");

    moves[0] = losing;
    moves[1] = quiet;
    moves[2] = winning;
    order_moves(
      position,
      moves,
      buffer,
      history,
      killers,
      losing);
    expect(
      moves[0] == losing
        && moves[1] == winning
        && moves[2] == quiet,
      "exchange ordering bands",
      "the preferred move remains first");
    expect(
      positions_equal(position, original),
      "exchange ordering bands",
      "ordering restores every position field");

    Position zero_position = base_position();
    zero_position.put_piece(
      R_PAWN, make_square(FILE_G, RANK_7));
    zero_position.put_piece(
      B_PAWN, make_square(FILE_H, RANK_8));
    zero_position.put_piece(
      G_PAWN, make_square(FILE_I, RANK_9));
    const Move equal_exchange = Move::normal(
      make_square(FILE_G, RANK_7),
      make_square(FILE_H, RANK_8));
    const Move zero_quiet = Move::normal(
      make_square(FILE_E, RANK_4),
      make_square(FILE_E, RANK_5));
    MoveList zero_moves;
    zero_moves.push_back(zero_quiet);
    zero_moves.push_back(equal_exchange);
    order_moves(
      zero_position, zero_moves, buffer);
    expect(
      static_exchange_evaluation(
        zero_position, equal_exchange)
          == 0
        && zero_moves[0] == equal_exchange
        && zero_moves[1] == zero_quiet,
      "exchange ordering bands",
      "an equal exchange remains in the non-losing band");
}

class Generator {
  public:
    constexpr explicit Generator(
      std::uint64_t seed) noexcept
        : state_(seed) {}

    [[nodiscard]] constexpr std::uint64_t
    next() noexcept {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return state_;
    }

  private:
    std::uint64_t state_;
};

[[nodiscard]] Position sparse_position(
  std::uint64_t seed) {
    Position position = base_position();
    Generator generator(seed);
    position.set_side_to_move(
      Color(generator.next() % COLOR_NB));

    constexpr int EXTRA_PIECES = 8;
    int placed = 0;
    while (placed < EXTRA_PIECES) {
        const Square square =
          Square(generator.next() % SQUARE_NB);
        if (!is_ok(square)
            || !position.empty(square)) {
            continue;
        }

        const Color color =
          Color(generator.next() % COLOR_NB);
        const PieceType piece_type =
          PieceType(
            PAWN + generator.next() % 5);
        position.put_piece(
          make_piece(color, piece_type),
          square);
        ++placed;
    }

    return position;
}

void test_sparse_oracle_agreement() {
    constexpr int POSITION_NB = 12;
    std::size_t tactical_count = 0;

    for (int index = 0;
         index < POSITION_NB;
         ++index) {
        Position position = sparse_position(
          UINT64_C(0x6A09E667F3BCC909)
          + static_cast<std::uint64_t>(index));

        for (int rotation = 0;
             rotation < COLOR_NB;
             ++rotation) {
            const Position original = position;
            MoveList legal_moves;
            generate_legal_moves(
              position, legal_moves);

            for (const Move move : legal_moves) {
                if (!is_tactical_move(position, move))
                    continue;

                ++tactical_count;
                const Score oracle =
                  Oracle::evaluate(position, move);
                expect(
                  static_exchange_evaluation(
                    position, move)
                    == oracle,
                  "sparse oracle agreement",
                  "every legal tactical move matches the independent oracle");
                if (move.type() == MoveType::NORMAL) {
                    const FastExchangeResult at_value =
                      fast_static_exchange_at_least(
                        position, move, oracle);
                    const FastExchangeResult above_value =
                      fast_static_exchange_at_least(
                        position, move, oracle + 1);
                    expect(
                      (at_value
                         == FastExchangeResult::AT_LEAST
                       || at_value
                            == FastExchangeResult::UNKNOWN)
                      && (above_value
                            == FastExchangeResult::BELOW
                          || above_value
                               == FastExchangeResult::UNKNOWN),
                      "sparse fast exchange agreement",
                      "classified normal captures bracket the oracle value");
                }
                expect(
                  positions_equal(
                    position, original),
                  "sparse oracle agreement",
                  "every comparison restores the position");
            }

            position =
              rotate_clockwise(position);
        }
    }

    expect(
      tactical_count >= 16,
      "sparse oracle agreement",
      "the deterministic sample contains tactical moves");
}

void test_dense_threshold_search() {
    Position position = base_position();
    position.remove_piece(
      make_square(FILE_N, RANK_8));
    position.put_piece(
      G_KING, make_square(FILE_N, RANK_7));
    position.put_piece(
      R_PAWN, make_square(FILE_G, RANK_7));
    position.put_piece(
      B_PAWN, make_square(FILE_H, RANK_8));

    const std::array pieces = {
      std::pair{B_ROOK, make_square(FILE_H, RANK_5)},
      std::pair{G_ROOK, make_square(FILE_H, RANK_11)},
      std::pair{B_BISHOP, make_square(FILE_F, RANK_6)},
      std::pair{G_BISHOP, make_square(FILE_J, RANK_6)},
      std::pair{B_KNIGHT, make_square(FILE_F, RANK_7)},
      std::pair{G_KNIGHT, make_square(FILE_J, RANK_7)},
      std::pair{Y_ROOK, make_square(FILE_E, RANK_8)},
      std::pair{R_ROOK, make_square(FILE_K, RANK_8)},
      std::pair{Y_BISHOP, make_square(FILE_F, RANK_10)},
      std::pair{R_BISHOP, make_square(FILE_J, RANK_10)},
      std::pair{Y_KNIGHT, make_square(FILE_F, RANK_9)},
      std::pair{R_KNIGHT, make_square(FILE_J, RANK_9)},
      std::pair{B_KNIGHT, make_square(FILE_G, RANK_6)},
      std::pair{G_KNIGHT, make_square(FILE_I, RANK_6)},
      std::pair{Y_KNIGHT, make_square(FILE_G, RANK_10)},
      std::pair{R_KNIGHT, make_square(FILE_I, RANK_10)},
      std::pair{R_ROOK, make_square(FILE_H, RANK_4)},
      std::pair{B_ROOK, make_square(FILE_H, RANK_3)},
      std::pair{Y_ROOK, make_square(FILE_H, RANK_12)},
      std::pair{G_ROOK, make_square(FILE_H, RANK_13)},
      std::pair{B_ROOK, make_square(FILE_D, RANK_8)},
      std::pair{R_ROOK, make_square(FILE_C, RANK_8)},
      std::pair{G_ROOK, make_square(FILE_L, RANK_8)},
      std::pair{Y_ROOK, make_square(FILE_M, RANK_8)},
      std::pair{Y_BISHOP, make_square(FILE_E, RANK_5)},
      std::pair{B_BISHOP, make_square(FILE_D, RANK_4)},
      std::pair{R_BISHOP, make_square(FILE_K, RANK_5)},
      std::pair{G_BISHOP, make_square(FILE_L, RANK_4)},
      std::pair{B_BISHOP, make_square(FILE_E, RANK_11)},
      std::pair{R_BISHOP, make_square(FILE_D, RANK_12)},
      std::pair{G_BISHOP, make_square(FILE_K, RANK_11)},
    };
    for (const auto& [piece, square] : pieces)
        position.put_piece(piece, square);

    const Move move = Move::normal(
      make_square(FILE_G, RANK_7),
      make_square(FILE_H, RANK_8));
    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    expect(
      contains_move(legal_moves, move),
      "dense threshold search",
      "the forced move is legal");

    const Position original = position;
    std::size_t remaining_reply_nodes =
      MAX_ORDERING_EXCHANGE_NODES;
    const ExchangeDetail::ThresholdResult
      bounded_result =
        ExchangeDetail::move_at_least(
          position,
          move,
          1,
          &remaining_reply_nodes);
    expect(
      static_exchange_at_least(position, move)
        && static_exchange_is_not_proven_losing(
             position, move)
        && bounded_result
             == ExchangeDetail::ThresholdResult::UNKNOWN
        && remaining_reply_nodes == 0,
      "dense threshold search",
      "the exact cutoff and bounded recursive cutoff are distinguished");
    expect(
      positions_equal(position, original),
      "dense threshold search",
      "the threshold search restores every position field");
}

}  // namespace

int main() {
    test_fast_exchange_threshold();
    test_capture_sequences();
    test_declined_and_deep_sequences();
    test_four_player_participation();
    test_special_replies();
    test_king_recaptures();
    test_promotions();
    test_en_passant();
    test_cutout_boundary();
    test_quiet_exchange();
    test_immediate_gain_shortcut();
    test_ordering_bands();
    test_sparse_oracle_agreement();
    test_dense_threshold_search();

    if (failures != 0) {
        std::cerr << failures
                  << " exchange test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All exchange tests passed\n";
    return EXIT_SUCCESS;
}
