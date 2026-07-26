#include "legal.h"
#include "notation.h"
#include "setup.h"
#include "transition.h"
#include "zobrist.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

using namespace Mockingbird;

inline constexpr std::array<CastlingSide, CASTLING_SIDE_NB>
  CASTLING_SIDES = {
    CastlingSide::KING_SIDE,
    CastlingSide::QUEEN_SIDE,
};

inline constexpr std::array<PieceType, 4>
  PROMOTION_TYPES = {
    QUEEN,
    ROOK,
    BISHOP,
    KNIGHT,
};

// Records a failed condition and allows the remaining tests to run.
void expect(bool condition, std::string_view message) {
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

[[nodiscard]] consteval std::array<
  Square, PLAYABLE_SQUARE_NB>
make_playable_squares() {
    std::array<Square, PLAYABLE_SQUARE_NB> squares{};
    std::size_t count = 0;

    for (int square_index = 0;
         square_index < SQUARE_NB;
         ++square_index) {
        const Square square = Square(square_index);
        if (is_ok(square))
            squares[count++] = square;
    }

    return squares;
}

inline constexpr auto PLAYABLE_SQUARES =
  make_playable_squares();

[[nodiscard]] constexpr bool positions_equal(
  const Position& left,
  const Position& right) noexcept {
    if (left.side_to_move() != right.side_to_move()
        || left.occupied() != right.occupied())
        return false;

    for (const Square square : PLAYABLE_SQUARES) {
        if (left.piece_on(square) != right.piece_on(square))
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
        if (left.pieces(PieceType(type_index))
            != right.pieces(PieceType(type_index)))
            return false;
    }

    return true;
}

void expect_key_consistent(
  const Position& position,
  std::string_view message) {
    expect(position.key() == position.recompute_key(), message);
}

template<std::size_t Size>
[[nodiscard]] bool all_distinct(
  const std::array<PositionKey, Size>& keys) {
    for (std::size_t left = 0; left < Size; ++left) {
        for (std::size_t right = left + 1;
             right < Size;
             ++right) {
            if (keys[left] == keys[right])
                return false;
        }
    }

    return true;
}

void set_all_castling_rights(Position& position) {
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        for (const CastlingSide side : CASTLING_SIDES)
            position.set_castling_right(color, side);
    }
}

void expect_move_round_trip(
  Position position,
  Move move,
  std::string_view message) {
    const Position original = position;
    const PositionKey original_key = position.key();
    UndoState undo;

    do_move(position, move, undo);
    expect_key_consistent(
      position,
      "move result key matches full recomputation");

    undo_move(position, move, undo);
    expect(positions_equal(position, original), message);
    expect(position.key() == original_key,
           "undo restores the exact previous key");
    expect_key_consistent(
      position,
      "undo key matches full recomputation");
}

[[nodiscard]] constexpr bool constexpr_hash_smoke() {
    constexpr Square h8 =
      make_square(FILE_H, RANK_8);
    constexpr Square f7 =
      make_square(FILE_F, RANK_7);

    Position position;
    if (position.key() != position.recompute_key())
        return false;

    position.put_piece(R_KNIGHT, h8);
    position.put_piece(B_BISHOP, f7);
    position.set_side_to_move(RED);
    position.set_castling_right(
      GREEN, CastlingSide::QUEEN_SIDE);
    position.set_en_passant_square(
      YELLOW, make_square(FILE_H, RANK_12));
    if (position.key() != position.recompute_key())
        return false;

    const PositionKey original_key = position.key();
    UndoState undo;
    constexpr Move capture = Move::normal(h8, f7);
    do_move(position, capture, undo);
    if (position.key() != position.recompute_key())
        return false;

    undo_move(position, capture, undo);
    if (position.key() != original_key
        || position.key() != position.recompute_key())
        return false;

    position.clear();
    return position.key() == Position{}.key()
        && position.key() == position.recompute_key()
        && make_starting_position().key()
             == make_starting_position().recompute_key();
}

static_assert(PLAYABLE_SQUARES.size() == 160);
static_assert(constexpr_hash_smoke());
static_assert(
  Zobrist::piece(
    R_PAWN, make_square(FILE_D, RANK_1))
  == 0x8B050BD778AC143DULL);
static_assert(
  Position{}.key() == 0xE7285B1C702CD429ULL);
static_assert(
  make_starting_position().key()
  == 0xAA95A02F4D865E1FULL);

void test_piece_square_components_and_direct_mutators() {
    constexpr std::size_t PIECE_VARIANT_NB =
      std::size_t(COLOR_NB) * 6;
    constexpr std::size_t KEY_NB =
      1 + PLAYABLE_SQUARE_NB * PIECE_VARIANT_NB;

    std::array<PositionKey, KEY_NB> keys{};
    std::size_t key_index = 0;
    const Position empty;
    keys[key_index++] = empty.key();

    for (std::size_t square_index = 0;
         square_index < PLAYABLE_SQUARES.size();
         ++square_index) {
        const Square source =
          PLAYABLE_SQUARES[square_index];
        const Square destination =
          PLAYABLE_SQUARES[
            (square_index + 1) % PLAYABLE_SQUARES.size()];

        for (int color_index = 0;
             color_index < COLOR_NB;
             ++color_index) {
            const Color color = Color(color_index);

            for (int type_index = PAWN;
                 type_index <= KING;
                 ++type_index) {
                const Piece piece =
                  make_piece(color, PieceType(type_index));
                Position position;
                position.put_piece(piece, source);

                expect_key_consistent(
                  position,
                  "single-piece key matches recomputation");
                keys[key_index++] = position.key();

                position.move_piece(source, destination);
                Position expected;
                expected.put_piece(piece, destination);
                expect(position.key() == expected.key(),
                       "direct relocation matches independent construction");
                expect_key_consistent(
                  position,
                  "direct relocation key matches recomputation");

                const Piece removed =
                  position.remove_piece(destination);
                expect(removed == piece,
                       "direct removal returns the placed piece");
                expect(position.key() == empty.key(),
                       "put, move, and remove restore the empty key");
                expect_key_consistent(
                  position,
                  "direct removal key matches recomputation");
            }
        }
    }

    expect(key_index == keys.size(),
           "every piece-square component was recorded");
    expect(all_distinct(keys),
           "empty and every piece-square component have distinct keys");
}

void test_direct_capture_components() {
    constexpr Square source =
      make_square(FILE_H, RANK_8);
    constexpr Square destination =
      make_square(FILE_F, RANK_7);

    for (int moving_color_index = 0;
         moving_color_index < COLOR_NB;
         ++moving_color_index) {
        const Color moving_color =
          Color(moving_color_index);

        for (int moving_type_index = PAWN;
             moving_type_index <= KING;
             ++moving_type_index) {
            const Piece moving_piece =
              make_piece(
                moving_color,
                PieceType(moving_type_index));

            for (int captured_color_index = 0;
                 captured_color_index < COLOR_NB;
                 ++captured_color_index) {
                const Color captured_color =
                  Color(captured_color_index);

                for (int captured_type_index = PAWN;
                     captured_type_index <= KING;
                     ++captured_type_index) {
                    const Piece captured_piece =
                      make_piece(
                        captured_color,
                        PieceType(captured_type_index));
                    Position position;
                    position.put_piece(moving_piece, source);
                    position.put_piece(
                      captured_piece, destination);

                    const Piece removed =
                      position.move_piece(
                        source, destination);
                    Position expected;
                    expected.put_piece(
                      moving_piece, destination);

                    expect(removed == captured_piece,
                           "direct capture returns its victim");
                    expect(position.key() == expected.key(),
                           "direct capture matches independent construction");
                    expect_key_consistent(
                      position,
                      "direct capture key matches recomputation");
                }
            }
        }
    }
}

void test_side_components() {
    std::array<PositionKey, COLOR_NB> keys{};

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        Position position;
        const Color color = Color(color_index);
        position.set_side_to_move(color);
        keys[std::size_t(color)] = position.key();
        expect_key_consistent(
          position,
          "side-to-move key matches recomputation");

        const PositionKey key = position.key();
        position.set_side_to_move(color);
        expect(position.key() == key,
               "setting the current side is idempotent");
    }

    expect(all_distinct(keys),
           "all four side-to-move values have distinct keys");

    for (int original_index = 0;
         original_index < COLOR_NB;
         ++original_index) {
        for (int replacement_index = 0;
             replacement_index < COLOR_NB;
             ++replacement_index) {
            Position actual;
            actual.set_side_to_move(Color(original_index));
            actual.set_side_to_move(Color(replacement_index));

            Position expected;
            expected.set_side_to_move(
              Color(replacement_index));
            expect(actual.key() == expected.key(),
                   "side replacement matches direct construction");
            expect_key_consistent(
              actual,
              "replaced side key matches recomputation");
        }
    }
}

void test_castling_components() {
    std::array<PositionKey, 256> keys{};

    for (std::size_t mask = 0;
         mask < keys.size();
         ++mask) {
        Position position;

        for (std::size_t bit = 0; bit < 8; ++bit) {
            if ((mask & (std::size_t{1} << bit)) == 0)
                continue;

            position.set_castling_right(
              Color(int(bit / CASTLING_SIDE_NB)),
              static_cast<CastlingSide>(
                bit % CASTLING_SIDE_NB));
        }

        keys[mask] = position.key();
        expect_key_consistent(
          position,
          "castling mask key matches recomputation");
    }

    expect(all_distinct(keys),
           "all 256 castling-right masks have distinct keys");

    const Position empty;
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);

        for (const CastlingSide side : CASTLING_SIDES) {
            Position position;
            position.set_castling_right(color, side);
            const PositionKey set_key = position.key();
            position.set_castling_right(color, side);
            expect(position.key() == set_key,
                   "setting an existing castling right is idempotent");

            position.clear_castling_right(color, side);
            expect(position.key() == empty.key(),
                   "clearing a castling right restores the empty key");
            position.clear_castling_right(color, side);
            expect(position.key() == empty.key(),
                   "clearing an absent castling right is idempotent");
        }
    }

    Position forward;
    set_all_castling_rights(forward);
    Position reverse;
    for (int color_index = COLOR_NB; color_index-- > 0;) {
        for (std::size_t side_index =
               CASTLING_SIDE_NB;
             side_index-- > 0;) {
            reverse.set_castling_right(
              Color(color_index),
              static_cast<CastlingSide>(side_index));
        }
    }
    expect(forward.key() == reverse.key(),
           "castling setter order does not affect the key");

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        Position actual = forward;
        actual.clear_castling_rights(Color(color_index));

        Position expected;
        for (int retained_index = 0;
             retained_index < COLOR_NB;
             ++retained_index) {
            if (retained_index == color_index)
                continue;
            for (const CastlingSide side : CASTLING_SIDES) {
                expected.set_castling_right(
                  Color(retained_index), side);
            }
        }

        expect(actual.key() == expected.key(),
               "per-color castling clear has the expected key");
        expect_key_consistent(
          actual,
          "per-color castling clear matches recomputation");
    }

    forward.clear_castling_rights();
    expect(forward.key() == empty.key(),
           "global castling clear restores the empty key");
}

void test_en_passant_components() {
    constexpr std::size_t KEY_NB =
      1 + std::size_t(COLOR_NB) * PLAYABLE_SQUARE_NB;
    std::array<PositionKey, KEY_NB> keys{};
    std::size_t key_index = 0;
    const Position empty;
    keys[key_index++] = empty.key();

    for (int owner_index = 0;
         owner_index < COLOR_NB;
         ++owner_index) {
        const Color owner = Color(owner_index);

        for (std::size_t square_index = 0;
             square_index < PLAYABLE_SQUARES.size();
             ++square_index) {
            const Square square =
              PLAYABLE_SQUARES[square_index];
            const Square replacement =
              PLAYABLE_SQUARES[
                (square_index + 1)
                % PLAYABLE_SQUARES.size()];
            Position position;
            position.set_en_passant_square(owner, square);
            keys[key_index++] = position.key();
            expect_key_consistent(
              position,
              "en-passant component key matches recomputation");

            const PositionKey set_key = position.key();
            position.set_en_passant_square(owner, square);
            expect(position.key() == set_key,
                   "setting the same en-passant target is idempotent");

            position.set_en_passant_square(
              owner, replacement);
            Position expected;
            expected.set_en_passant_square(
              owner, replacement);
            expect(position.key() == expected.key(),
                   "replacing an en-passant target removes the old key");
            expect_key_consistent(
              position,
              "replaced en-passant key matches recomputation");

            position.clear_en_passant_square(owner);
            expect(position.key() == empty.key(),
                   "clearing an en-passant target restores the empty key");
            position.clear_en_passant_square(owner);
            expect(position.key() == empty.key(),
                   "clearing an absent en-passant target is idempotent");
        }
    }

    expect(key_index == keys.size(),
           "every en-passant owner-square component was recorded");
    expect(all_distinct(keys),
           "en-passant owner-square components have distinct keys");

    constexpr std::array<Square, COLOR_NB> TARGETS = {
      make_square(FILE_D, RANK_4),
      make_square(FILE_K, RANK_10),
      make_square(FILE_N, RANK_11),
      make_square(FILE_E, RANK_14),
    };

    Position forward;
    for (int owner_index = 0;
         owner_index < COLOR_NB;
         ++owner_index) {
        forward.set_en_passant_square(
          Color(owner_index),
          TARGETS[std::size_t(owner_index)]);
    }

    Position reverse;
    for (int owner_index = COLOR_NB;
         owner_index-- > 0;) {
        reverse.set_en_passant_square(
          Color(owner_index),
          TARGETS[std::size_t(owner_index)]);
    }
    expect(forward.key() == reverse.key(),
           "en-passant setter order does not affect the key");

    forward.clear_en_passant_square(BLUE);
    Position expected = reverse;
    expected.clear_en_passant_square(BLUE);
    expect(forward.key() == expected.key(),
           "clearing one of four targets preserves the other key components");
    expect_key_consistent(
      forward,
      "multi-target en-passant key matches recomputation");

    reverse.clear_en_passant_squares();
    expect(reverse.key() == empty.key(),
           "global en-passant clear restores the empty key");
}

[[nodiscard]] Position make_rich_position(bool reverse) {
    Position position;
    position.set_side_to_move(GREEN);

    for (int item = 0; item < 24; ++item) {
        const int logical = reverse ? 23 - item : item;
        const Color color = Color(logical / 6);
        const PieceType piece_type =
          PieceType(PAWN + logical % 6);
        position.put_piece(
          make_piece(color, piece_type),
          PLAYABLE_SQUARES[std::size_t(logical)]);
    }

    if (!reverse) {
        set_all_castling_rights(position);
        for (int color_index = 0;
             color_index < COLOR_NB;
             ++color_index) {
            position.set_en_passant_square(
              Color(color_index),
              PLAYABLE_SQUARES[
                std::size_t(40 * color_index)]);
        }
    } else {
        for (int color_index = COLOR_NB;
             color_index-- > 0;) {
            for (std::size_t side_index =
                   CASTLING_SIDE_NB;
                 side_index-- > 0;) {
                position.set_castling_right(
                  Color(color_index),
                  static_cast<CastlingSide>(side_index));
            }
            position.set_en_passant_square(
              Color(color_index),
              PLAYABLE_SQUARES[
                std::size_t(40 * color_index)]);
        }
    }

    return position;
}

void test_rich_state_copy_order_and_clear() {
    Position forward = make_rich_position(false);
    const Position reverse = make_rich_position(true);
    expect(positions_equal(forward, reverse),
           "opposite mutation orders construct the same rich state");
    expect(forward.key() == reverse.key(),
           "opposite mutation orders construct the same key");
    expect_key_consistent(
      forward,
      "rich position key matches recomputation");

    const Position copied = forward;
    Position assigned;
    assigned = forward;
    expect(copied.key() == forward.key()
             && assigned.key() == forward.key(),
           "copy construction and assignment preserve the key");
    expect_key_consistent(
      copied,
      "copied key matches recomputation");
    expect_key_consistent(
      assigned,
      "assigned key matches recomputation");

    forward.clear();
    const Position empty;
    expect(positions_equal(forward, empty),
           "clear restores the default public state");
    expect(forward.key() == empty.key(),
           "clear restores the default key");
    expect_key_consistent(
      forward,
      "cleared key matches recomputation");
}

void test_generated_legal_moves() {
    std::size_t tested_moves = 0;

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        Position position = make_starting_position();
        position.set_side_to_move(Color(color_index));
        const Position original = position;
        const PositionKey original_key = position.key();
        MoveList moves;
        generate_legal_moves(position, moves);

        expect(!moves.empty(),
               "each starting color has legal moves");
        expect(positions_equal(position, original),
               "legal generation preserves position state");
        expect(position.key() == original_key,
               "legal generation preserves the cached key");
        expect_key_consistent(
          position,
          "post-generation key matches recomputation");

        for (const Move move : moves) {
            ++tested_moves;
            expect_move_round_trip(
              position,
              move,
              "legal move undo restores the complete position");
        }
    }

    expect(tested_moves != 0,
           "legal move hash round trips were exercised");
}

void test_normal_and_double_push_moves() {
    constexpr Square knight_source =
      make_square(FILE_H, RANK_8);
    constexpr Square knight_destination =
      make_square(FILE_F, RANK_7);
    int double_push_count = 0;

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);

        Position quiet;
        quiet.set_side_to_move(color);
        quiet.put_piece(
          make_piece(color, KNIGHT), knight_source);
        expect_move_round_trip(
          quiet,
          Move::normal(
            knight_source, knight_destination),
          "quiet move undo restores its hash");

        Position capture = quiet;
        capture.put_piece(
          make_piece(next_color(color), BISHOP),
          knight_destination);
        expect_move_round_trip(
          capture,
          Move::normal(
            knight_source, knight_destination),
          "normal capture undo restores its hash");

        for (const Square source : PLAYABLE_SQUARES) {
            const Square destination =
              pawn_double_push_destination(color, source);
            if (destination == SQ_NONE)
                continue;

            ++double_push_count;
            Position position;
            position.set_side_to_move(color);
            position.put_piece(
              make_piece(color, PAWN), source);
            position.set_en_passant_square(
              color, make_square(FILE_H, RANK_8));
            expect_move_round_trip(
              position,
              Move::normal(source, destination),
              "double-push undo restores its hash");
        }
    }

    expect(double_push_count == 32,
           "all double-push sources were hash-tested");
}

void test_promotions() {
    constexpr std::array<Square, COLOR_NB> SOURCES = {
      make_square(FILE_H, RANK_10),
      make_square(FILE_J, RANK_8),
      make_square(FILE_H, RANK_5),
      make_square(FILE_E, RANK_8),
    };
    constexpr std::array<Square, COLOR_NB>
      QUIET_DESTINATIONS = {
        make_square(FILE_H, RANK_11),
        make_square(FILE_K, RANK_8),
        make_square(FILE_H, RANK_4),
        make_square(FILE_D, RANK_8),
    };
    constexpr std::array<Square, COLOR_NB>
      CAPTURE_DESTINATIONS = {
        make_square(FILE_G, RANK_11),
        make_square(FILE_K, RANK_9),
        make_square(FILE_I, RANK_4),
        make_square(FILE_D, RANK_7),
    };

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        const Square source =
          SOURCES[std::size_t(color)];

        for (const PieceType promotion : PROMOTION_TYPES) {
            Position quiet;
            quiet.set_side_to_move(color);
            quiet.put_piece(
              make_piece(color, PAWN), source);
            expect_move_round_trip(
              quiet,
              Move::promotion(
                source,
                QUIET_DESTINATIONS[std::size_t(color)],
                promotion),
              "quiet-promotion undo restores its hash");

            Position capture;
            capture.set_side_to_move(color);
            capture.put_piece(
              make_piece(color, PAWN), source);
            const Square destination =
              CAPTURE_DESTINATIONS[std::size_t(color)];
            capture.put_piece(
              make_piece(next_color(color), ROOK),
              destination);
            expect_move_round_trip(
              capture,
              Move::promotion(
                source, destination, promotion),
              "capture-promotion undo restores its hash");
        }
    }
}

void test_castling_and_right_updates() {
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);

        for (const CastlingSide side : CASTLING_SIDES) {
            const CastlingGeometry& geometry =
              castling_geometry(color, side);

            Position castling;
            castling.set_side_to_move(color);
            castling.set_castling_right(color, side);
            castling.put_piece(
              make_piece(color, KING),
              geometry.king_source);
            castling.put_piece(
              make_piece(color, ROOK),
              geometry.rook_source);
            expect_move_round_trip(
              castling,
              Move::castling(
                geometry.king_source,
                geometry.king_destination),
              "castling undo restores pieces, rights, and hash");

            Position rook_move;
            rook_move.set_side_to_move(color);
            set_all_castling_rights(rook_move);
            rook_move.put_piece(
              make_piece(color, ROOK),
              geometry.rook_source);
            expect_move_round_trip(
              rook_move,
              Move::normal(
                geometry.rook_source,
                geometry.rook_destination),
              "canonical rook move undo restores rights and hash");

            const Color attacker = next_color(color);
            Position rook_capture;
            rook_capture.set_side_to_move(attacker);
            set_all_castling_rights(rook_capture);
            rook_capture.put_piece(
              make_piece(attacker, ROOK),
              geometry.rook_destination);
            rook_capture.put_piece(
              make_piece(color, ROOK),
              geometry.rook_source);
            expect_move_round_trip(
              rook_capture,
              Move::normal(
                geometry.rook_destination,
                geometry.rook_source),
              "canonical rook capture undo restores rights and hash");
        }

        const CastlingGeometry& kingside =
          castling_geometry(
            color, CastlingSide::KING_SIDE);
        Position king_move;
        king_move.set_side_to_move(color);
        set_all_castling_rights(king_move);
        king_move.put_piece(
          make_piece(color, KING),
          kingside.king_source);
        expect_move_round_trip(
          king_move,
          Move::normal(
            kingside.king_source,
            kingside.king_transit),
          "ordinary king move undo restores rights and hash");

        constexpr Square attacker_source =
          make_square(FILE_F, RANK_7);
        constexpr Square king_square =
          make_square(FILE_H, RANK_8);
        const Color attacker = next_color(color);
        Position king_capture;
        king_capture.set_side_to_move(attacker);
        set_all_castling_rights(king_capture);
        king_capture.put_piece(
          make_piece(attacker, KNIGHT),
          attacker_source);
        king_capture.put_piece(
          make_piece(color, KING), king_square);
        expect_move_round_trip(
          king_capture,
          Move::normal(attacker_source, king_square),
          "king capture undo restores rights and hash");
    }
}

void test_en_passant_moves() {
    int tested_moves = 0;

    for (int owner_index = 0;
         owner_index < COLOR_NB;
         ++owner_index) {
        const Color owner = Color(owner_index);

        for (const Square owner_source :
             PLAYABLE_SQUARES) {
            const Square target =
              pawn_push_destination(owner, owner_source);
            const Square victim =
              pawn_double_push_destination(
                owner, owner_source);
            if (target == SQ_NONE || victim == SQ_NONE)
                continue;

            for (int moving_index = 0;
                 moving_index < COLOR_NB;
                 ++moving_index) {
                const Color moving_color =
                  Color(moving_index);
                if (team_of(moving_color) == team_of(owner))
                    continue;

                for (const Square source :
                     PLAYABLE_SQUARES) {
                    if (source == victim
                        || !pawn_attacks(
                              moving_color,
                              source).test(target))
                        continue;

                    const bool promotes =
                      is_pawn_promotion_square(
                        moving_color, target);
                    const std::size_t move_count =
                      promotes ? PROMOTION_TYPES.size() : 1;

                    for (const bool occupied_target :
                         {false, true}) {
                        Position base;
                        base.set_side_to_move(moving_color);
                        base.put_piece(
                          make_piece(moving_color, PAWN),
                          source);
                        base.put_piece(
                          make_piece(owner, PAWN), victim);
                        if (occupied_target) {
                            base.put_piece(
                              make_piece(owner, ROOK),
                              target);
                        }
                        base.set_en_passant_square(
                          owner, target);

                        for (std::size_t move_index = 0;
                             move_index < move_count;
                             ++move_index) {
                            const Move move = promotes
                              ? Move::en_passant(
                                  source,
                                  target,
                                  PROMOTION_TYPES[move_index])
                              : Move::en_passant(source, target);
                            ++tested_moves;
                            expect_move_round_trip(
                              base,
                              move,
                              occupied_target
                                ? "occupied-target en-passant undo restores its hash"
                                : "en-passant undo restores its hash");
                        }
                    }
                }
            }
        }
    }

    expect(tested_moves == 336,
           "all en-passant encodings and target occupancies were tested");
}

void test_en_passant_invalidation() {
    int tested_captures = 0;

    for (int owner_index = 0;
         owner_index < COLOR_NB;
         ++owner_index) {
        const Color owner = Color(owner_index);
        const Color attacker = next_color(owner);

        for (const Square source : PLAYABLE_SQUARES) {
            const Square target =
              pawn_push_destination(owner, source);
            const Square victim =
              pawn_double_push_destination(owner, source);
            if (target == SQ_NONE || victim == SQ_NONE)
                continue;

            ++tested_captures;
            Position position;
            position.set_side_to_move(attacker);
            position.put_piece(
              make_piece(attacker, ROOK), source);
            position.put_piece(
              make_piece(owner, PAWN), victim);
            position.set_en_passant_square(owner, target);
            expect_move_round_trip(
              position,
              Move::normal(source, victim),
              "passed-pawn capture undo restores target and hash");
        }
    }

    expect(tested_captures == 32,
           "all passed-pawn invalidation geometries were tested");

    constexpr Square b5 =
      make_square(FILE_B, RANK_5);
    constexpr Square c6 =
      make_square(FILE_C, RANK_6);
    constexpr Square d6 =
      make_square(FILE_D, RANK_6);
    constexpr Square k7 =
      make_square(FILE_K, RANK_7);
    constexpr Square k8 =
      make_square(FILE_K, RANK_8);
    constexpr Square l8 =
      make_square(FILE_L, RANK_8);

    Position multiple;
    multiple.put_piece(R_PAWN, b5);
    multiple.put_piece(R_PAWN, k7);
    multiple.put_piece(B_PAWN, d6);
    multiple.put_piece(G_PAWN, k8);
    multiple.set_en_passant_square(BLUE, c6);
    multiple.set_en_passant_square(GREEN, l8);
    expect_move_round_trip(
      multiple,
      Move::en_passant(b5, c6),
      "selected en-passant target undo restores all target keys");
}

void test_undo_state_reuse() {
    constexpr Square b10 =
      make_square(FILE_B, RANK_10);
    constexpr Square c11 =
      make_square(FILE_C, RANK_11);
    constexpr Square d11 =
      make_square(FILE_D, RANK_11);

    Position special;
    special.put_piece(R_PAWN, b10);
    special.put_piece(B_ROOK, c11);
    special.put_piece(B_PAWN, d11);
    special.set_en_passant_square(BLUE, c11);
    const Position special_original = special;
    const PositionKey special_key = special.key();

    UndoState reused;
    constexpr Move en_passant =
      Move::en_passant(b10, c11, QUEEN);
    do_move(special, en_passant, reused);
    expect_key_consistent(
      special,
      "reused-state special move key matches recomputation");
    undo_move(special, en_passant, reused);
    expect(positions_equal(special, special_original)
             && special.key() == special_key,
           "first undo-state use restores the exact key");

    constexpr Square h8 =
      make_square(FILE_H, RANK_8);
    constexpr Square f7 =
      make_square(FILE_F, RANK_7);
    Position ordinary;
    ordinary.put_piece(R_KNIGHT, h8);
    const Position ordinary_original = ordinary;
    const PositionKey ordinary_key = ordinary.key();
    constexpr Move quiet = Move::normal(h8, f7);

    do_move(ordinary, quiet, reused);
    expect_key_consistent(
      ordinary,
      "reused-state ordinary move key matches recomputation");
    undo_move(ordinary, quiet, reused);
    expect(positions_equal(ordinary, ordinary_original)
             && ordinary.key() == ordinary_key,
           "second undo-state use restores the exact key");
}

void test_multi_ply_lifo() {
    constexpr std::array<Move, 5> MOVES = {
      Move::normal(
        make_square(FILE_D, RANK_2),
        make_square(FILE_D, RANK_4)),
      Move::normal(
        make_square(FILE_A, RANK_5),
        make_square(FILE_B, RANK_5)),
      Move::normal(
        make_square(FILE_D, RANK_14),
        make_square(FILE_E, RANK_14)),
      Move::normal(
        make_square(FILE_N, RANK_5),
        make_square(FILE_M, RANK_5)),
      Move::normal(
        make_square(FILE_H, RANK_1),
        make_square(FILE_F, RANK_2)),
    };

    Position position;
    position.put_piece(
      R_PAWN, make_square(FILE_D, RANK_2));
    position.put_piece(
      B_ROOK, make_square(FILE_A, RANK_5));
    position.put_piece(
      Y_ROOK, make_square(FILE_D, RANK_14));
    position.put_piece(
      G_ROOK, make_square(FILE_N, RANK_5));
    position.put_piece(
      R_KNIGHT, make_square(FILE_H, RANK_1));

    std::array<Position, MOVES.size() + 1> snapshots{};
    std::array<PositionKey, MOVES.size() + 1> keys{};
    std::array<UndoState, MOVES.size()> undo_states{};
    snapshots[0] = position;
    keys[0] = position.key();

    for (std::size_t ply = 0;
         ply < MOVES.size();
         ++ply) {
        do_move(position, MOVES[ply], undo_states[ply]);
        expect_key_consistent(
          position,
          "multi-ply key matches recomputation after do");
        snapshots[ply + 1] = position;
        keys[ply + 1] = position.key();
    }

    for (std::size_t ply = MOVES.size();
         ply-- > 0;) {
        undo_move(
          position, MOVES[ply], undo_states[ply]);
        expect(positions_equal(position, snapshots[ply]),
               "LIFO undo restores the preceding state");
        expect(position.key() == keys[ply],
               "LIFO undo restores the preceding key");
        expect_key_consistent(
          position,
          "multi-ply key matches recomputation after undo");
    }
}

void test_setup_and_notation_round_trips() {
    const std::array<Position, 3> positions = {
      Position{},
      make_starting_position(),
      make_rich_position(false),
    };

    for (const Position& position : positions) {
        expect_key_consistent(
          position,
          "notation source key matches recomputation");
        const auto parsed =
          parse_position(serialize_position(position));
        expect(parsed.has_value(),
               "serialized hash fixture parses");
        if (!parsed)
            continue;

        expect(positions_equal(*parsed, position),
               "notation round trip preserves public state");
        expect(parsed->key() == position.key(),
               "notation round trip preserves the key");
        expect_key_consistent(
          *parsed,
          "parsed key matches full recomputation");
    }

    const Position first_start =
      make_starting_position();
    const Position second_start =
      make_starting_position();
    expect(first_start.key() == second_start.key(),
           "repeated starting-position construction is deterministic");
}

}  // namespace

int main() {
    test_piece_square_components_and_direct_mutators();
    test_direct_capture_components();
    test_side_components();
    test_castling_components();
    test_en_passant_components();
    test_rich_state_copy_order_and_clear();
    test_generated_legal_moves();
    test_normal_and_double_push_moves();
    test_promotions();
    test_castling_and_right_updates();
    test_en_passant_moves();
    test_en_passant_invalidation();
    test_undo_state_reuse();
    test_multi_ply_lifo();
    test_setup_and_notation_round_trips();

    if (failures != 0) {
        std::cerr << failures << " hash test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All hash tests passed\n";
    return EXIT_SUCCESS;
}
