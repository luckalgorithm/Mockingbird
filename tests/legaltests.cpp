#include "legal.h"
#include "setup.h"

#include <array>
#include <cstddef>
#include <cstdint>
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

using namespace Mockingbird;

inline constexpr std::array<CastlingSide, CASTLING_SIDE_NB>
  CASTLING_SIDES = {
    CastlingSide::KING_SIDE,
    CastlingSide::QUEEN_SIDE,
};

inline constexpr std::array<PieceType, 4> PROMOTION_TYPES = {
  QUEEN,
  ROOK,
  BISHOP,
  KNIGHT,
};

inline constexpr std::array<Square, COLOR_NB>
  DEFAULT_KING_SQUARES = {
    make_square(FILE_H, RANK_5),
    make_square(FILE_D, RANK_8),
    make_square(FILE_E, RANK_13),
    make_square(FILE_K, RANK_8),
};

[[nodiscard]] constexpr bool positions_equal(
  const Position& left,
  const Position& right) noexcept {
    if (left.side_to_move() != right.side_to_move()
        || left.occupied() != right.occupied()
        || left.key() != right.key()
        || left.key() != left.recompute_key()
        || right.key() != right.recompute_key())
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

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
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

        for (int color_index = 0;
             color_index < COLOR_NB;
             ++color_index) {
            const Color color = Color(color_index);
            if (left.pieces(color, piece_type)
                  != right.pieces(color, piece_type))
                return false;
        }
    }

    return true;
}

[[nodiscard]] constexpr bool contains_move(
  const MoveList& moves,
  Move target) noexcept {
    for (const Move move : moves) {
        if (move == target)
            return true;
    }

    return false;
}

[[nodiscard]] constexpr std::size_t move_index(
  const MoveList& moves,
  Move target) noexcept {
    for (std::size_t index = 0;
         index < moves.size();
         ++index) {
        if (moves[index] == target)
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

constexpr void add_missing_kings(Position& position) noexcept {
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        if (!position.pieces(color, KING).empty())
            continue;

        position.put_piece(
          make_piece(color, KING),
          DEFAULT_KING_SQUARES[std::size_t(color)]);
    }
}

[[nodiscard]] constexpr bool reference_complete_king_set(
  const Position& position) noexcept {
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        if (position.pieces(
              Color(color_index), KING)
              .popcount()
            != 1)
            return false;
    }

    return true;
}

// The copied position is not restored because it is discarded by the caller.
[[nodiscard]] constexpr bool reference_legal_on_copy(
  Position position,
  Move move) noexcept {
    if (!reference_complete_king_set(position))
        return false;

    const Color moving_color =
      position.side_to_move();
    const Piece destination_piece =
      position.empty(move.to())
        ? NO_PIECE
        : position.piece_on(move.to());
    const bool captures_opposing_king =
      destination_piece != NO_PIECE
      && type_of(destination_piece) == KING
      && team_of(color_of(destination_piece))
           != team_of(moving_color);

    UndoState undo;
    do_move(position, move, undo);
    return captures_opposing_king
        || checkers(position, moving_color).empty();
}

constexpr void reference_generate_legal_moves(
  const Position& position,
  MoveList& moves) noexcept {
    if (!reference_complete_king_set(position))
        return;

    MoveList pseudo_moves;
    generate_moves(position, pseudo_moves);

    for (const Move move : pseudo_moves) {
        if (reference_legal_on_copy(position, move))
            moves.push_back(move);
    }
}

[[nodiscard]] bool filter_matches_copy_oracle(
  Position position) {
    const Position original = position;
    MoveList pseudo_moves;
    generate_moves(position, pseudo_moves);

    MoveList expected;
    bool individual_results = true;
    bool individual_restoration = true;

    for (const Move move : pseudo_moves) {
        const bool reference =
          reference_legal_on_copy(position, move);
        const Position before = position;
        const bool actual =
          is_legal_move(position, move);

        if (actual != reference)
            individual_results = false;
        if (!positions_equal(position, before))
            individual_restoration = false;
        if (reference)
            expected.push_back(move);
    }

    constexpr Move prefix = Move::normal(
      make_square(FILE_D, RANK_4),
      make_square(FILE_E, RANK_4));
    MoveList actual;
    actual.push_back(prefix);
    generate_legal_moves(position, actual);

    bool exact_order =
      actual.size() == expected.size() + 1
      && actual[0] == prefix;
    if (exact_order) {
        for (std::size_t index = 0;
             index < expected.size();
             ++index) {
            if (actual[index + 1] != expected[index]) {
                exact_order = false;
                break;
            }
        }
    }

    return individual_results
        && individual_restoration
        && exact_order
        && positions_equal(position, original);
}

[[nodiscard]] constexpr Square rotate_clockwise(
  Square square) noexcept {
    return make_square(
      File(int(rank_of(square))),
      Rank(BOARD_FILES + 1 - int(file_of(square))));
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

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        const Color rotated_color = next_color(color);

        for (const CastlingSide side : CASTLING_SIDES) {
            if (position.has_castling_right(color, side))
                rotated.set_castling_right(
                  rotated_color, side);
        }

        const Square en_passant =
          position.en_passant_square(color);
        if (en_passant != SQ_NONE) {
            rotated.set_en_passant_square(
              rotated_color,
              rotate_clockwise(en_passant));
        }
    }

    return rotated;
}

[[nodiscard]] constexpr Move rotate_clockwise(
  Move move) noexcept {
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

    return Move::none();
}

struct FrontierStats {
    std::uint64_t positions = 0;
    std::uint64_t candidates = 0;
    std::uint64_t accepted_without_transition = 0;
    std::uint64_t transition_candidates = 0;
};

// Recursion follows the oracle list, so an optimized-list omission cannot
// remove the descendant positions from the comparison.
[[nodiscard]] bool compare_oracle_frontier(
  Position& position,
  int remaining_depth,
  FrontierStats& stats) {
    ++stats.positions;
    const Position original = position;

    MoveList expected;
    reference_generate_legal_moves(position, expected);

    MoveList actual;
    generate_legal_moves(position, actual);
    if (!move_lists_equal(actual, expected)
        || !positions_equal(position, original))
        return false;

    const bool expected_has_move = !expected.empty();
    if (has_legal_move(position) != expected_has_move
        || !positions_equal(position, original))
        return false;

    if (reference_complete_king_set(position)) {
        const Detail::LegalMoveContext context =
          Detail::make_legal_move_context(position);
        MoveList pseudo_moves;
        generate_moves(position, pseudo_moves);
        stats.candidates += pseudo_moves.size();

        for (const Move move : pseudo_moves) {
            if (Detail::captures_opposing_king(
                  position,
                  move,
                  position.side_to_move())
                || Detail::can_accept_without_transition(
                  position, move, context))
                ++stats.accepted_without_transition;
            else
                ++stats.transition_candidates;
        }
    }

    if (remaining_depth == 0)
        return true;

    for (const Move move : expected) {
        UndoState undo;
        do_move(position, move, undo);
        const bool child_matches =
          compare_oracle_frontier(
            position, remaining_depth - 1, stats);
        undo_move(position, move, undo);

        if (!positions_equal(position, original)
            || !child_matches)
            return false;
    }

    return true;
}

void expect_move_legality(
  Position& position,
  Move move,
  bool expected,
  std::string_view message) {
    MoveList pseudo_moves;
    generate_moves(position, pseudo_moves);
    const bool generated =
      contains_move(pseudo_moves, move);
    expect(generated,
           "the tested move is generated pseudo-legal");
    if (!generated)
        return;

    const Position before = position;
    expect(
      is_legal_move(position, move) == expected,
      message);
    expect(
      positions_equal(position, before),
      "individual legality testing restores the complete position");
}

[[nodiscard]] constexpr Position terminal_base() noexcept {
    Position position;
    position.set_side_to_move(RED);
    add_missing_kings(position);
    position.put_piece(
      R_KNIGHT, make_square(FILE_F, RANK_6));
    return position;
}

[[nodiscard]] constexpr Direction ray_offset(
  RayDirection direction) noexcept {
    return Detail::RAY_OFFSETS[
      std::to_underlying(direction)];
}

[[nodiscard]] constexpr Square advance_on_ray(
  Square square,
  RayDirection direction,
  int distance) noexcept {
    for (int step = 0; step < distance; ++step)
        square = square + ray_offset(direction);

    return square;
}

[[nodiscard]] constexpr bool is_orthogonal(
  RayDirection direction) noexcept {
    return direction == RayDirection::NORTH
        || direction == RayDirection::EAST
        || direction == RayDirection::SOUTH
        || direction == RayDirection::WEST;
}

[[nodiscard]] constexpr Position ray_blocker_position(
  RayDirection direction,
  Color slider_color,
  PieceType slider_type,
  bool add_second_blocker = false,
  Color second_color = RED,
  Color first_color = RED) noexcept {
    constexpr Square king =
      make_square(FILE_H, RANK_8);

    Position position;
    position.set_side_to_move(RED);
    position.put_piece(R_KING, king);
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    position.put_piece(
      Y_KING, make_square(FILE_G, RANK_14));
    position.put_piece(
      G_KING, make_square(FILE_N, RANK_8));
    position.put_piece(
      make_piece(first_color, KNIGHT),
      advance_on_ray(king, direction, 1));

    if (add_second_blocker) {
        position.put_piece(
          make_piece(second_color, PAWN),
          advance_on_ray(king, direction, 2));
    }

    position.put_piece(
      make_piece(slider_color, slider_type),
      advance_on_ray(king, direction, 3));
    return position;
}

[[nodiscard]] constexpr Position mixed_special_position() noexcept {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_1));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    position.put_piece(
      Y_KING, make_square(FILE_G, RANK_14));
    position.put_piece(
      G_KING, make_square(FILE_N, RANK_8));

    position.put_piece(
      R_ROOK, make_square(FILE_D, RANK_1));
    position.put_piece(
      R_ROOK, make_square(FILE_K, RANK_1));
    position.set_castling_right(
      RED, CastlingSide::KING_SIDE);
    position.set_castling_right(
      RED, CastlingSide::QUEEN_SIDE);

    position.put_piece(
      R_PAWN, make_square(FILE_D, RANK_5));
    position.put_piece(
      B_PAWN, make_square(FILE_D, RANK_6));
    position.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_6));

    position.put_piece(
      R_PAWN, make_square(FILE_B, RANK_10));
    position.put_piece(
      G_ROOK, make_square(FILE_C, RANK_11));
    return position;
}

[[nodiscard]] constexpr Position pin_position() noexcept {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      R_ROOK, make_square(FILE_H, RANK_7));
    position.put_piece(
      B_ROOK, make_square(FILE_H, RANK_10));
    add_missing_kings(position);
    return position;
}

[[nodiscard]] constexpr Position single_check_position() noexcept {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      R_ROOK, make_square(FILE_F, RANK_7));
    position.put_piece(
      B_ROOK, make_square(FILE_H, RANK_10));
    add_missing_kings(position);
    return position;
}

[[nodiscard]] constexpr Position double_check_position() noexcept {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      R_ROOK, make_square(FILE_F, RANK_7));
    position.put_piece(
      B_ROOK, make_square(FILE_H, RANK_10));
    position.put_piece(
      G_BISHOP, make_square(FILE_K, RANK_8));
    position.put_piece(
      G_KING, make_square(FILE_M, RANK_10));
    add_missing_kings(position);
    return position;
}

[[nodiscard]] constexpr Position en_passant_xray_position(
  bool occupied_target = false) noexcept {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(
      R_KING, make_square(FILE_D, RANK_4));
    position.put_piece(
      R_PAWN, make_square(FILE_D, RANK_5));
    position.put_piece(
      B_PAWN, make_square(FILE_D, RANK_6));
    position.put_piece(
      B_ROOK, make_square(FILE_D, RANK_10));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    if (occupied_target) {
        position.put_piece(
          G_ROOK, make_square(FILE_C, RANK_6));
    }
    position.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_6));
    add_missing_kings(position);
    return position;
}

[[nodiscard]] constexpr Position en_passant_evasion_position(
  bool occupied_target = false) noexcept {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(
      R_KING, make_square(FILE_E, RANK_5));
    position.put_piece(
      R_PAWN, make_square(FILE_D, RANK_5));
    position.put_piece(
      B_PAWN, make_square(FILE_D, RANK_6));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    if (occupied_target) {
        position.put_piece(
          G_ROOK, make_square(FILE_C, RANK_6));
    }
    position.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_6));
    add_missing_kings(position);
    return position;
}

[[nodiscard]] constexpr Position promotion_position() noexcept {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      R_PAWN, make_square(FILE_H, RANK_10));
    position.put_piece(
      B_ROOK, make_square(FILE_H, RANK_13));
    position.put_piece(
      B_KNIGHT, make_square(FILE_G, RANK_11));
    add_missing_kings(position);
    return position;
}

[[nodiscard]] constexpr Position en_passant_promotion_position() noexcept {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(
      R_KING, make_square(FILE_D, RANK_8));
    position.put_piece(
      R_PAWN, make_square(FILE_D, RANK_10));
    position.put_piece(
      B_PAWN, make_square(FILE_D, RANK_11));
    position.put_piece(
      B_ROOK, make_square(FILE_D, RANK_13));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    position.put_piece(
      G_KNIGHT, make_square(FILE_C, RANK_11));
    position.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_11));
    add_missing_kings(position);
    return position;
}

[[nodiscard]] constexpr Position legal_en_passant_promotion_position() noexcept {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      R_PAWN, make_square(FILE_D, RANK_10));
    position.put_piece(
      B_PAWN, make_square(FILE_D, RANK_11));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    position.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_11));
    add_missing_kings(position);
    return position;
}

[[nodiscard]] constexpr Position terminal_promotion_position() noexcept {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      R_PAWN, make_square(FILE_H, RANK_10));
    position.put_piece(
      B_ROOK, make_square(FILE_H, RANK_13));
    position.put_piece(
      G_KING, make_square(FILE_G, RANK_11));
    add_missing_kings(position);
    return position;
}

[[nodiscard]] constexpr Position terminal_en_passant_position() noexcept {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(
      R_KING, make_square(FILE_D, RANK_4));
    position.put_piece(
      R_PAWN, make_square(FILE_D, RANK_5));
    position.put_piece(
      B_PAWN, make_square(FILE_D, RANK_6));
    position.put_piece(
      B_ROOK, make_square(FILE_D, RANK_10));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    position.put_piece(
      G_KING, make_square(FILE_C, RANK_6));
    position.set_en_passant_square(
      BLUE, make_square(FILE_C, RANK_6));
    add_missing_kings(position);
    return position;
}

[[nodiscard]] constexpr Position castling_position() noexcept {
    Position position;
    position.set_side_to_move(RED);
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
    return position;
}

[[nodiscard]] constexpr bool constexpr_legal_case() noexcept {
    Position position = terminal_base();
    const Position original = position;
    const Move move = Move::normal(
      make_square(FILE_F, RANK_6),
      make_square(FILE_G, RANK_8));

    return is_legal_move(position, move)
        && positions_equal(position, original);
}

static_assert(constexpr_legal_case());

void test_slider_blocker_detection() {
    constexpr Square king =
      make_square(FILE_H, RANK_8);

    bool compatible_rays = true;
    bool incompatible_rays = true;
    bool teammate_cases = true;
    bool multiple_blockers = true;

    for (std::size_t direction_index = 0;
         direction_index < RAY_DIRECTION_NB;
         ++direction_index) {
        const RayDirection direction =
          static_cast<RayDirection>(direction_index);
        const Square candidate =
          advance_on_ray(king, direction, 1);
        const PieceType compatible =
          is_orthogonal(direction) ? ROOK : BISHOP;
        const PieceType incompatible =
          is_orthogonal(direction) ? BISHOP : ROOK;

        for (const Color opponent : {BLUE, GREEN}) {
            for (const PieceType slider :
                 {compatible, QUEEN}) {
                Position position =
                  ray_blocker_position(
                    direction, opponent, slider);
                Color moving_color = RED;
                Square rotated_candidate = candidate;

                for (int rotation = 0;
                     rotation < COLOR_NB;
                     ++rotation) {
                    compatible_rays &=
                      !in_check(
                        position, moving_color)
                      && slider_blockers_to_king(
                           position, moving_color)
                           == Bitboard::from_square(
                                rotated_candidate);
                    position =
                      rotate_clockwise(position);
                    moving_color =
                      next_color(moving_color);
                    rotated_candidate =
                      rotate_clockwise(
                        rotated_candidate);
                }
            }

            const Position wrong_slider =
              ray_blocker_position(
                direction, opponent, incompatible);
            incompatible_rays &=
              slider_blockers_to_king(
                wrong_slider, RED)
                .empty();
        }

        const Position teammate_slider =
          ray_blocker_position(
            direction, YELLOW, QUEEN);
        const Position teammate_first =
          ray_blocker_position(
            direction,
            BLUE,
            QUEEN,
            false,
            RED,
            YELLOW);
        teammate_cases &=
          slider_blockers_to_king(
            teammate_slider, RED)
              .empty()
          && slider_blockers_to_king(
               teammate_first, RED)
               .empty();

        for (const Color second_color :
             {RED, YELLOW, BLUE}) {
            const Position two_blockers =
              ray_blocker_position(
                direction,
                GREEN,
                QUEEN,
                true,
                second_color);
            multiple_blockers &=
              slider_blockers_to_king(
                two_blockers, RED)
                .empty();
        }
    }

    expect(
      compatible_rays,
      "all compatible opposing slider rays identify their sole blocker");
    expect(
      incompatible_rays,
      "incompatible slider types do not create king-ray blockers");
    expect(
      teammate_cases,
      "teammate pieces and sliders do not create mover-king pins");
    expect(
      multiple_blockers,
      "a ray with two intervening pieces has no sole blocker");
}

void test_fast_path_classification() {
    Position unpinned = terminal_base();
    const Move knight_move = Move::normal(
      make_square(FILE_F, RANK_6),
      make_square(FILE_G, RANK_8));
    const Detail::LegalMoveContext unpinned_context =
      Detail::make_legal_move_context(unpinned);
    expect(
      Detail::can_accept_without_transition(
        unpinned, knight_move, unpinned_context),
      "an unpinned non-king move outside check uses the fast path");

    Position pinned = pin_position();
    const Detail::LegalMoveContext pinned_context =
      Detail::make_legal_move_context(pinned);
    expect(
      pinned_context.slider_blockers
        == Bitboard::from_square(
             make_square(FILE_H, RANK_7)),
      "the legality context records the pinned rook");
    expect(
      !Detail::can_accept_without_transition(
        pinned,
        Move::normal(
          make_square(FILE_H, RANK_7),
          make_square(FILE_I, RANK_7)),
        pinned_context)
        && !Detail::can_accept_without_transition(
          pinned,
          Move::normal(
            make_square(FILE_H, RANK_7),
            make_square(FILE_H, RANK_8)),
          pinned_context),
      "all moves by a slider blocker use the transition test");

    Position checked = single_check_position();
    const Detail::LegalMoveContext checked_context =
      Detail::make_legal_move_context(checked);
    expect(
      checked_context.checked
        && !Detail::can_accept_without_transition(
          checked,
          Move::normal(
            make_square(FILE_F, RANK_7),
            make_square(FILE_H, RANK_7)),
          checked_context),
      "every nonterminal check evasion uses the transition test");

    Position king_position = terminal_base();
    const Detail::LegalMoveContext king_context =
      Detail::make_legal_move_context(king_position);
    expect(
      !Detail::can_accept_without_transition(
        king_position,
        Move::normal(
          make_square(FILE_H, RANK_5),
          make_square(FILE_G, RANK_5)),
        king_context),
      "ordinary king moves use the transition test");

    Position en_passant =
      en_passant_xray_position();
    const Detail::LegalMoveContext en_passant_context =
      Detail::make_legal_move_context(en_passant);
    expect(
      !Detail::can_accept_without_transition(
        en_passant,
        Move::en_passant(
          make_square(FILE_D, RANK_5),
          make_square(FILE_C, RANK_6)),
        en_passant_context),
      "en-passant moves use the transition test");

    Position castling = castling_position();
    const Detail::LegalMoveContext castling_context =
      Detail::make_legal_move_context(castling);
    const CastlingGeometry& geometry =
      castling_geometry(
        RED, CastlingSide::KING_SIDE);
    expect(
      Detail::can_accept_without_transition(
        castling,
        Move::castling(
          geometry.king_source,
          geometry.king_destination),
        castling_context),
      "generated castling uses its completed king-path validation");

    Position promotions = mixed_special_position();
    const Detail::LegalMoveContext promotion_context =
      Detail::make_legal_move_context(promotions);
    expect(
      Detail::can_accept_without_transition(
        promotions,
        Move::promotion(
          make_square(FILE_B, RANK_10),
          make_square(FILE_B, RANK_11),
          QUEEN),
        promotion_context)
        && Detail::can_accept_without_transition(
          promotions,
          Move::promotion(
            make_square(FILE_B, RANK_10),
            make_square(FILE_C, RANK_11),
            QUEEN),
          promotion_context),
      "safe quiet and capture promotions use the fast path");
}

void test_terminal_king_sets() {
    const Position base = terminal_base();
    const Move move = Move::normal(
      make_square(FILE_F, RANK_6),
      make_square(FILE_G, RANK_8));
    constexpr Move prefix = Move::normal(
      make_square(FILE_D, RANK_4),
      make_square(FILE_E, RANK_4));

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);

        Position missing = base;
        missing.remove_piece(
          missing.pieces(color, KING).lsb());
        const Position missing_before = missing;

        expect(
          !is_legal_move(missing, move),
          "a missing king rejects individual legality");
        MoveList missing_moves;
        missing_moves.push_back(prefix);
        generate_legal_moves(missing, missing_moves);
        expect(
          missing_moves.size() == 1
            && missing_moves[0] == prefix,
          "a missing king appends no legal moves");
        expect(
          positions_equal(missing, missing_before),
          "missing-king filtering leaves the position unchanged");

        Position duplicate = base;
        duplicate.put_piece(
          make_piece(color, KING),
          make_square(FILE_E, RANK_6));
        const Position duplicate_before = duplicate;

        expect(
          !is_legal_move(duplicate, move),
          "duplicate same-color kings reject individual legality");
        MoveList duplicate_moves;
        duplicate_moves.push_back(prefix);
        generate_legal_moves(
          duplicate, duplicate_moves);
        expect(
          duplicate_moves.size() == 1
            && duplicate_moves[0] == prefix,
          "duplicate same-color kings append no legal moves");
        expect(
          positions_equal(
            duplicate, duplicate_before),
          "duplicate-king filtering leaves the position unchanged");
    }
}

void test_pins_and_rotation() {
    Position position = pin_position();
    Move illegal = Move::normal(
      make_square(FILE_H, RANK_7),
      make_square(FILE_I, RANK_7));
    Move legal = Move::normal(
      make_square(FILE_H, RANK_7),
      make_square(FILE_H, RANK_8));

    for (int rotation = 0;
         rotation < COLOR_NB;
         ++rotation) {
        expect_move_legality(
          position,
          illegal,
          false,
          "a pinned rook cannot leave its king's ray");
        expect_move_legality(
          position,
          legal,
          true,
          "a pinned rook may remain between its king and attacker");
        expect(
          filter_matches_copy_oracle(position),
          "rotated pin filtering matches the copy oracle and restores state");

        position = rotate_clockwise(position);
        illegal = rotate_clockwise(illegal);
        legal = rotate_clockwise(legal);
    }
}

void test_check_evasions() {
    Position single = single_check_position();
    const Move block = Move::normal(
      make_square(FILE_F, RANK_7),
      make_square(FILE_H, RANK_7));
    const Move irrelevant = Move::normal(
      make_square(FILE_F, RANK_7),
      make_square(FILE_G, RANK_7));

    expect(in_check(single, RED),
           "the single-check fixture starts in check");
    expect_move_legality(
      single,
      block,
      true,
      "blocking the only checker is legal");
    expect_move_legality(
      single,
      irrelevant,
      false,
      "a move that does not answer check is illegal");

    Position double_check = double_check_position();
    expect(
      checkers(double_check, RED).popcount() == 2,
      "the double-check fixture has two checkers");
    expect_move_legality(
      double_check,
      block,
      false,
      "blocking only one checker does not answer double check");

    MoveList legal_moves;
    generate_legal_moves(double_check, legal_moves);
    bool only_king_moves = true;
    const Square king_square =
      make_square(FILE_H, RANK_5);
    for (const Move move : legal_moves) {
        if (move.from() != king_square)
            only_king_moves = false;
    }
    expect(
      only_king_moves,
      "every legal double-check evasion moves the checked king");
    expect(
      filter_matches_copy_oracle(single)
        && filter_matches_copy_oracle(double_check),
      "single- and double-check filtering restore complete state");
}

void test_king_destinations_and_turn_selection() {
    Position unsafe;
    unsafe.set_side_to_move(RED);
    unsafe.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    unsafe.put_piece(
      B_KNIGHT, make_square(FILE_F, RANK_7));
    add_missing_kings(unsafe);

    expect_move_legality(
      unsafe,
      Move::normal(
        make_square(FILE_H, RANK_5),
        make_square(FILE_G, RANK_5)),
      false,
      "a king cannot move onto a knight attack");
    expect_move_legality(
      unsafe,
      Move::normal(
        make_square(FILE_H, RANK_5),
        make_square(FILE_H, RANK_6)),
      false,
      "a second attacked king destination is rejected");
    expect_move_legality(
      unsafe,
      Move::normal(
        make_square(FILE_H, RANK_5),
        make_square(FILE_I, RANK_5)),
      true,
      "an unattacked king destination is legal");

    Position next_player_checked;
    next_player_checked.set_side_to_move(RED);
    next_player_checked.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    next_player_checked.put_piece(
      B_KING, make_square(FILE_D, RANK_8));
    next_player_checked.put_piece(
      R_ROOK, make_square(FILE_D, RANK_5));
    next_player_checked.put_piece(
      R_KNIGHT, make_square(FILE_F, RANK_6));
    add_missing_kings(next_player_checked);
    const Move quiet = Move::normal(
      make_square(FILE_F, RANK_6),
      make_square(FILE_G, RANK_8));

    expect(in_check(next_player_checked, BLUE),
           "the next player starts in check");
    expect_move_legality(
      next_player_checked,
      quiet,
      true,
      "legality tests the mover rather than the next player");
}

void test_teammate_king_semantics() {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(
      R_KING, make_square(FILE_E, RANK_5));
    position.put_piece(
      Y_KING, make_square(FILE_H, RANK_8));
    position.put_piece(
      R_ROOK, make_square(FILE_H, RANK_9));
    position.put_piece(
      B_ROOK, make_square(FILE_H, RANK_11));
    add_missing_kings(position);

    const Move exposes_teammate = Move::normal(
      make_square(FILE_H, RANK_9),
      make_square(FILE_G, RANK_9));
    expect(!in_check(position, YELLOW),
           "the teammate king starts behind a blocker");
    expect_move_legality(
      position,
      exposes_teammate,
      true,
      "only the moving color's king constrains legality");

    Position moved = position;
    UndoState undo;
    do_move(moved, exposes_teammate, undo);
    expect(in_check(moved, YELLOW),
           "the legal move exposes the teammate king");
}

[[nodiscard]] constexpr Position terminal_capture_position(
  Color captured_color) noexcept {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      R_ROOK, make_square(FILE_F, RANK_5));
    position.put_piece(
      B_ROOK, make_square(FILE_H, RANK_10));
    position.put_piece(
      make_piece(captured_color, KING),
      make_square(FILE_F, RANK_8));
    add_missing_kings(position);
    return position;
}

void test_opposing_king_captures() {
    const Move capture = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));

    for (const Color captured_color :
         {BLUE, GREEN}) {
        Position position =
          terminal_capture_position(captured_color);
        expect(in_check(position, RED),
               "the terminal-capture fixture starts in check");
        expect_move_legality(
          position,
          capture,
          true,
          "capturing either opposing king is immediately legal");

        MoveList legal_moves;
        generate_legal_moves(position, legal_moves);
        expect(
          contains_move(legal_moves, capture),
          "legal generation retains an opposing-king capture");

        Position moved = position;
        UndoState undo;
        do_move(moved, capture, undo);
        expect(
          moved.pieces(captured_color, KING).empty()
            && in_check(moved, RED),
          "the king capture ends the game before mover safety is evaluated");
    }

    Position king_capture;
    king_capture.set_side_to_move(RED);
    king_capture.put_piece(
      R_KING, make_square(FILE_H, RANK_8));
    king_capture.put_piece(
      B_KING, make_square(FILE_G, RANK_9));
    king_capture.put_piece(
      G_ROOK, make_square(FILE_G, RANK_14));
    add_missing_kings(king_capture);
    const Move defended_king = Move::normal(
      make_square(FILE_H, RANK_8),
      make_square(FILE_G, RANK_9));

    expect_move_legality(
      king_capture,
      defended_king,
      true,
      "a defended opposing king may be captured terminally");

    Position teammate_target;
    teammate_target.set_side_to_move(RED);
    teammate_target.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    teammate_target.put_piece(
      R_ROOK, make_square(FILE_F, RANK_5));
    teammate_target.put_piece(
      Y_KING, make_square(FILE_F, RANK_8));
    add_missing_kings(teammate_target);
    MoveList pseudo_moves;
    generate_moves(teammate_target, pseudo_moves);
    expect(
      !contains_move(pseudo_moves, capture),
      "pseudo generation never targets the teammate king");
}

void test_special_opposing_king_captures() {
    Position promotion =
      terminal_promotion_position();

    for (const PieceType piece_type :
         PROMOTION_TYPES) {
        const Move move = Move::promotion(
          make_square(FILE_H, RANK_10),
          make_square(FILE_G, RANK_11),
          piece_type);
        expect_move_legality(
          promotion,
          move,
          true,
          "a promotion capture of an opposing king is terminally legal");

        Position moved = promotion;
        UndoState undo;
        do_move(moved, move, undo);
        expect(
          moved.pieces(GREEN, KING).empty()
            && in_check(moved, RED),
          "terminal promotion bypasses the revealed rook attack");
    }

    Position en_passant =
      terminal_en_passant_position();
    const Move move = Move::en_passant(
      make_square(FILE_D, RANK_5),
      make_square(FILE_C, RANK_6));
    expect_move_legality(
      en_passant,
      move,
      true,
      "occupied-target en passant may capture an opposing king terminally");

    Position moved = en_passant;
    UndoState undo;
    do_move(moved, move, undo);
    expect(
      moved.pieces(GREEN, KING).empty()
        && moved.pieces(BLUE, PAWN).empty()
        && in_check(moved, RED),
      "terminal en passant captures both pieces before the rook x-ray");
}

void test_en_passant_legality() {
    const Move en_passant = Move::en_passant(
      make_square(FILE_D, RANK_5),
      make_square(FILE_C, RANK_6));

    for (const bool occupied_target :
         {false, true}) {
        Position xray =
          en_passant_xray_position(occupied_target);
        expect_move_legality(
          xray,
          en_passant,
          false,
          "en passant cannot expose a rook attack on the mover's king");

        Position evasion =
          en_passant_evasion_position(occupied_target);
        expect(in_check(evasion, RED),
               "the en-passant evasion starts in pawn check");
        expect_move_legality(
          evasion,
          en_passant,
          true,
          "en passant may remove the checking pawn");

        expect(
          filter_matches_copy_oracle(xray)
            && filter_matches_copy_oracle(evasion),
          "en-passant filtering matches the copy oracle and restores both captures");
    }

    Position promotion =
      en_passant_promotion_position();
    for (const PieceType piece_type :
         PROMOTION_TYPES) {
        expect_move_legality(
          promotion,
          Move::en_passant(
            make_square(FILE_D, RANK_10),
            make_square(FILE_C, RANK_11),
            piece_type),
          false,
          "an en-passant promotion cannot expose a rook x-ray");
    }

    Position legal_promotion =
      legal_en_passant_promotion_position();
    MoveList legal_moves;
    generate_legal_moves(legal_promotion, legal_moves);
    std::size_t previous_index = 0;
    bool first = true;

    for (const PieceType piece_type :
         PROMOTION_TYPES) {
        const Move move = Move::en_passant(
          make_square(FILE_D, RANK_10),
          make_square(FILE_C, RANK_11),
          piece_type);
        const std::size_t index =
          move_index(legal_moves, move);
        expect(
          index != legal_moves.size(),
          "a safe en-passant promotion is legal");
        if (!first) {
            expect(
              previous_index < index,
              "legal en-passant promotions preserve promotion order");
        }

        first = false;
        previous_index = index;
    }
}

void test_promotion_legality_and_order() {
    Position position = promotion_position();
    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);

    std::size_t previous_index = 0;
    bool first = true;

    for (const PieceType piece_type :
         PROMOTION_TYPES) {
        const Move push = Move::promotion(
          make_square(FILE_H, RANK_10),
          make_square(FILE_H, RANK_11),
          piece_type);
        const Move capture = Move::promotion(
          make_square(FILE_H, RANK_10),
          make_square(FILE_G, RANK_11),
          piece_type);
        const std::size_t index =
          move_index(legal_moves, push);

        expect(
          index != legal_moves.size(),
          "a promotion that remains on the pin ray is legal");
        if (!first) {
            expect(
              previous_index < index,
              "legal promotions preserve queen-rook-bishop-knight order");
        }
        expect(
          !contains_move(legal_moves, capture),
          "a promotion capture that leaves the pin ray is illegal");

        first = false;
        previous_index = index;
    }

    expect(
      filter_matches_copy_oracle(position),
      "promotion filtering matches the copy oracle and restores state");
}

void test_castling_and_rotation() {
    Position position = castling_position();
    Move kingside = Move::castling(
      make_square(FILE_H, RANK_1),
      make_square(FILE_J, RANK_1));
    Move queenside = Move::castling(
      make_square(FILE_H, RANK_1),
      make_square(FILE_F, RANK_1));

    for (int rotation = 0;
         rotation < COLOR_NB;
         ++rotation) {
        MoveList legal_moves;
        generate_legal_moves(position, legal_moves);
        const std::size_t kingside_index =
          move_index(legal_moves, kingside);
        const std::size_t queenside_index =
          move_index(legal_moves, queenside);

        expect(
          kingside_index != legal_moves.size()
            && queenside_index != legal_moves.size(),
          "both legal castling moves survive legal filtering");
        expect(
          kingside_index < queenside_index,
          "kingside castling remains before queenside castling");
        expect(
          filter_matches_copy_oracle(position),
          "castling filtering restores rights and board state");

        position = rotate_clockwise(position);
        kingside = rotate_clockwise(kingside);
        queenside = rotate_clockwise(queenside);
    }
}

void test_filter_oracle_fixtures() {
    const std::array<Position, 12> fixtures = {
      pin_position(),
      single_check_position(),
      double_check_position(),
      en_passant_xray_position(),
      en_passant_xray_position(true),
      en_passant_evasion_position(),
      promotion_position(),
      en_passant_promotion_position(),
      legal_en_passant_promotion_position(),
      terminal_promotion_position(),
      terminal_en_passant_position(),
      castling_position(),
    };

    bool passed = true;
    for (Position position : fixtures) {
        for (int rotation = 0;
             rotation < COLOR_NB;
             ++rotation) {
            if (!filter_matches_copy_oracle(position))
                passed = false;
            position = rotate_clockwise(position);
        }
    }

    expect(
      passed,
      "all rotations of every targeted fixture match the transition oracle");
}

void test_oracle_reachable_frontiers() {
    Position starting = make_starting_position();
    FrontierStats starting_stats;
    const bool starting_matches =
      compare_oracle_frontier(
        starting, 4, starting_stats);

    expect(
      starting_matches,
      "the starting-position depth-four frontier matches the transition oracle");
    expect(
      starting_stats.positions == 160266,
      "the starting frontier compares every position through depth four");

    Position special = mixed_special_position();
    FrontierStats special_stats;
    bool special_matches = true;
    for (int rotation = 0;
         rotation < COLOR_NB;
         ++rotation) {
        special_matches &=
          compare_oracle_frontier(
            special, 3, special_stats);
        special = rotate_clockwise(special);
    }

    expect(
      special_matches,
      "all rotated special-move frontiers match the transition oracle");
    expect(
      starting_stats.accepted_without_transition > 0
        && special_stats.accepted_without_transition > 0
        && special_stats.transition_candidates > 0,
      "frontier comparisons exercise both legality paths");
}

void test_exact_remaining_capacity() {
    Position position = terminal_base();
    const Position original = position;
    MoveList expected;
    reference_generate_legal_moves(
      position, expected);

    expect(
      !expected.empty()
        && expected.size() <= MoveList::capacity(),
      "the capacity fixture has a bounded nonempty legal list");
    if (expected.empty()
        || expected.size() > MoveList::capacity())
        return;

    constexpr Move prefix = Move::normal(
      make_square(FILE_D, RANK_4),
      make_square(FILE_E, RANK_4));
    MoveList moves;
    const std::size_t prefix_count =
      MoveList::capacity() - expected.size();
    for (std::size_t index = 0;
         index < prefix_count;
         ++index)
        moves.push_back(prefix);

    generate_legal_moves(position, moves);

    bool suffix_matches =
      moves.full()
      && moves.size() == MoveList::capacity();
    if (suffix_matches) {
        for (std::size_t index = 0;
             index < expected.size();
             ++index) {
            if (moves[prefix_count + index]
                != expected[index]) {
                suffix_matches = false;
                break;
            }
        }
    }

    expect(
      suffix_matches,
      "legal generation fills exactly the caller-provided remaining capacity");
    expect(
      positions_equal(position, original),
      "exact-capacity generation restores the complete position");
}

}  // namespace

int main() {
    test_slider_blocker_detection();
    test_fast_path_classification();
    test_terminal_king_sets();
    test_pins_and_rotation();
    test_check_evasions();
    test_king_destinations_and_turn_selection();
    test_teammate_king_semantics();
    test_opposing_king_captures();
    test_special_opposing_king_captures();
    test_en_passant_legality();
    test_promotion_legality_and_order();
    test_castling_and_rotation();
    test_filter_oracle_fixtures();
    test_oracle_reachable_frontiers();
    test_exact_remaining_capacity();

    if (failures != 0) {
        std::cerr << failures
                  << " legal-move test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All legal-move tests passed\n";
    return EXIT_SUCCESS;
}
