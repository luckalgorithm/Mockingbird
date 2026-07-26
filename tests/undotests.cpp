#include "transition.h"
#include "movegen.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

int failures = 0;

constexpr std::array<Mockingbird::CastlingSide, 2> CASTLING_SIDES = {
  Mockingbird::CastlingSide::KING_SIDE,
  Mockingbird::CastlingSide::QUEEN_SIDE,
};

constexpr std::array<Mockingbird::PieceType, 4> PROMOTION_TYPES = {
  Mockingbird::QUEEN,
  Mockingbird::ROOK,
  Mockingbird::BISHOP,
  Mockingbird::KNIGHT,
};

// Records a failed condition and allows the remaining tests to run.
void expect(bool condition, std::string_view message) {
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

[[nodiscard]] constexpr bool positions_equal(
  const Mockingbird::Position& left,
  const Mockingbird::Position& right) noexcept {
    using namespace Mockingbird;

    if (left.side_to_move() != right.side_to_move()
        || left.key() != right.key()
        || left.occupied() != right.occupied())
        return false;

    for (int square_index = 0;
         square_index < SQUARE_NB;
         ++square_index) {
        const Square square = Square(square_index);
        if (is_ok(square)
            && left.piece_on(square) != right.piece_on(square))
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
        const PieceType piece_type = PieceType(type_index);
        if (left.pieces(piece_type) != right.pieces(piece_type))
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

void expect_consistent(const Mockingbird::Position& position) {
    using namespace Mockingbird;

    expect(position.key() == position.recompute_key(),
           "cached key matches the canonical position state");

    Bitboard occupied;
    std::array<Bitboard, COLOR_NB> by_color{};
    std::array<Bitboard, PIECE_TYPE_NB> by_type{};

    for (int square_index = 0;
         square_index < SQUARE_NB;
         ++square_index) {
        const Square square = Square(square_index);
        if (!is_ok(square))
            continue;

        const Piece piece = position.piece_on(square);
        if (piece == NO_PIECE)
            continue;

        expect(is_ok(piece), "mailbox contains a valid piece");
        occupied.set(square);
        by_color[std::size_t(color_of(piece))].set(square);
        by_type[std::size_t(type_of(piece))].set(square);
    }

    expect(position.occupied() == occupied,
           "combined occupancy matches the mailbox");

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        expect(
          position.pieces(color)
            == by_color[std::size_t(color)],
          "color occupancy matches the mailbox");
    }

    for (int type_index = PAWN;
         type_index <= KING;
         ++type_index) {
        const PieceType piece_type = PieceType(type_index);
        expect(
          position.pieces(piece_type)
            == by_type[std::size_t(piece_type)],
          "piece-type occupancy matches the mailbox");

        for (int color_index = 0;
             color_index < COLOR_NB;
             ++color_index) {
            const Color color = Color(color_index);
            expect(
              position.pieces(color, piece_type)
                == (by_color[std::size_t(color)]
                    & by_type[std::size_t(piece_type)]),
              "color-and-type occupancy matches the mailbox");
        }
    }
}

template<typename AfterMove>
void expect_round_trip(
  Mockingbird::Position position,
  Mockingbird::Move move,
  AfterMove after_move,
  std::string_view message) {
    const Mockingbird::Position original = position;
    Mockingbird::UndoState undo;

    Mockingbird::do_move(position, move, undo);
    expect_consistent(position);
    after_move(position);

    Mockingbird::undo_move(position, move, undo);
    expect(positions_equal(position, original), message);
    expect_consistent(position);
}

void set_all_castling_rights(Mockingbird::Position& position) {
    using namespace Mockingbird;

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        for (const CastlingSide side : CASTLING_SIDES)
            position.set_castling_right(color, side);
    }
}

[[nodiscard]] constexpr std::uint8_t castling_right_bit(
  Mockingbird::Color color,
  Mockingbird::CastlingSide side) noexcept {
    return static_cast<std::uint8_t>(
      1U
      << (std::size_t(color) * Mockingbird::CASTLING_SIDE_NB
          + std::to_underlying(side)));
}

[[nodiscard]] constexpr std::uint8_t color_castling_rights(
  Mockingbird::Color color) noexcept {
    return static_cast<std::uint8_t>(
      castling_right_bit(
        color, Mockingbird::CastlingSide::KING_SIDE)
      | castling_right_bit(
        color, Mockingbird::CastlingSide::QUEEN_SIDE));
}

inline constexpr std::uint8_t ALL_CASTLING_RIGHTS = 0xFF;

void expect_castling_rights(
  const Mockingbird::Position& position,
  std::uint8_t expected_rights,
  std::string_view message) {
    using namespace Mockingbird;

    bool matches = true;

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        for (const CastlingSide side : CASTLING_SIDES) {
            const bool expected =
              (expected_rights
               & castling_right_bit(color, side))
              != 0;
            matches &=
              position.has_castling_right(color, side)
              == expected;
        }
    }

    expect(matches, message);
}

[[nodiscard]] bool contains_move(
  const Mockingbird::MoveList& moves,
  Mockingbird::Move expected) {
    for (const Mockingbird::Move move : moves) {
        if (move == expected)
            return true;
    }

    return false;
}

[[nodiscard]] constexpr bool constexpr_do_and_undo() {
    using namespace Mockingbird;

    constexpr Square f7 = make_square(FILE_F, RANK_7);
    constexpr Square h8 = make_square(FILE_H, RANK_8);
    constexpr Move capture = Move::normal(h8, f7);

    Position position;
    position.put_piece(R_KNIGHT, h8);
    position.put_piece(B_BISHOP, f7);
    position.set_en_passant_square(
      GREEN, make_square(FILE_L, RANK_8));
    position.set_castling_right(
      YELLOW, CastlingSide::QUEEN_SIDE);
    const Position original = position;

    UndoState undo;
    do_move(position, capture, undo);
    if (position.side_to_move() != BLUE
        || position.piece_on(f7) != R_KNIGHT
        || !position.empty(h8))
        return false;

    undo_move(position, capture, undo);
    return positions_equal(position, original);
}

static_assert(constexpr_do_and_undo());

void test_undo_state_layout() {
    using namespace Mockingbird;

    static_assert(std::is_trivially_copyable_v<UndoState>);
    static_assert(sizeof(UndoState) <= 32);
    expect(sizeof(UndoState) < sizeof(Position),
           "undo state is smaller than a complete position");
}

void test_normal_moves_and_captures() {
    using namespace Mockingbird;

    constexpr Square source =
      make_square(FILE_H, RANK_8);
    constexpr Square destination =
      make_square(FILE_F, RANK_7);

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);

        Position quiet;
        quiet.set_side_to_move(color);
        quiet.put_piece(
          make_piece(color, KNIGHT), source);
        set_all_castling_rights(quiet);

        expect_round_trip(
          quiet,
          Move::normal(source, destination),
          [=](const Position& position) {
              expect(
                position.piece_on(destination)
                  == make_piece(color, KNIGHT),
                "quiet move relocates the moving piece");
              expect(position.empty(source),
                     "quiet move empties its source");
              expect(
                position.side_to_move() == next_color(color),
                "quiet move advances to the next color");

              for (int rights_color_index = 0;
                   rights_color_index < COLOR_NB;
                   ++rights_color_index) {
                  const Color rights_color =
                    Color(rights_color_index);
                  for (const CastlingSide side :
                       CASTLING_SIDES) {
                      expect(
                        position.has_castling_right(
                          rights_color, side),
                        "unrelated move preserves castling rights");
                  }
              }
          },
          "quiet move undo restores the complete position");

        Position capture;
        capture.set_side_to_move(color);
        capture.put_piece(
          make_piece(color, KNIGHT), source);
        capture.put_piece(
          make_piece(next_color(color), BISHOP),
          destination);

        expect_round_trip(
          capture,
          Move::normal(source, destination),
          [=](const Position& position) {
              expect(
                position.piece_on(destination)
                  == make_piece(color, KNIGHT),
                "capture replaces the destination piece");
              expect(
                position.pieces(
                  next_color(color), BISHOP).empty(),
                "capture removes the destination piece");
          },
          "capture undo restores both pieces");
    }
}

void test_pawn_push_state() {
    using namespace Mockingbird;

    int double_push_count = 0;

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);

        for (int source_index = 0;
             source_index < SQUARE_NB;
             ++source_index) {
            const Square source = Square(source_index);
            if (!is_ok(source))
                continue;

            const Square destination =
              pawn_double_push_destination(color, source);
            if (destination == SQ_NONE)
                continue;

            ++double_push_count;
            const Square skipped =
              pawn_push_destination(color, source);

            Position position;
            position.set_side_to_move(color);
            position.put_piece(
              make_piece(color, PAWN), source);
            position.set_en_passant_square(
              color, make_square(FILE_H, RANK_8));

            expect_round_trip(
              position,
              Move::normal(source, destination),
              [=](const Position& moved) {
                  expect(
                    moved.piece_on(destination)
                      == make_piece(color, PAWN),
                    "double push relocates the pawn");
                  expect(
                    moved.en_passant_square(color)
                      == skipped,
                    "double push stores its skipped square");
              },
              "double-push undo restores the complete position");

            Position non_pawn;
            non_pawn.set_side_to_move(color);
            non_pawn.put_piece(
              make_piece(color, ROOK), source);

            expect_round_trip(
              non_pawn,
              Move::normal(source, destination),
              [=](const Position& moved) {
                  expect(
                    moved.en_passant_square(color) == SQ_NONE,
                    "two-square non-pawn move creates no target");
              },
              "two-square non-pawn undo restores the position");
        }

        constexpr Square source =
          make_square(FILE_H, RANK_8);
        const Square destination =
          pawn_push_destination(color, source);

        Position single;
        single.set_side_to_move(color);
        single.put_piece(
          make_piece(color, PAWN), source);
        single.set_en_passant_square(
          color, make_square(FILE_E, RANK_6));

        expect_round_trip(
          single,
          Move::normal(source, destination),
          [=](const Position& moved) {
              expect(
                moved.en_passant_square(color) == SQ_NONE,
                "single push does not create an en-passant target");
          },
          "single-push undo restores the previous target");
    }

    expect(double_push_count == 32,
           "all thirty-two double-push sources were tested");
}

void test_double_push_preserves_other_targets() {
    using namespace Mockingbird;

    constexpr Square red_source =
      make_square(FILE_D, RANK_2);
    constexpr Square red_destination =
      make_square(FILE_D, RANK_4);
    constexpr Square red_target =
      make_square(FILE_D, RANK_3);
    constexpr Square blue_target =
      make_square(FILE_C, RANK_6);
    constexpr Square blue_victim =
      make_square(FILE_D, RANK_6);
    constexpr Square yellow_target =
      make_square(FILE_H, RANK_12);
    constexpr Square yellow_victim =
      make_square(FILE_H, RANK_11);
    constexpr Square green_target =
      make_square(FILE_L, RANK_8);
    constexpr Square green_victim =
      make_square(FILE_K, RANK_8);

    Position position;
    position.put_piece(R_PAWN, red_source);
    position.put_piece(B_PAWN, blue_victim);
    position.put_piece(Y_PAWN, yellow_victim);
    position.put_piece(G_PAWN, green_victim);
    position.set_en_passant_square(
      RED, make_square(FILE_E, RANK_3));
    position.set_en_passant_square(BLUE, blue_target);
    position.set_en_passant_square(YELLOW, yellow_target);
    position.set_en_passant_square(GREEN, green_target);

    expect_round_trip(
      position,
      Move::normal(red_source, red_destination),
      [](const Position& moved) {
          expect(
            moved.en_passant_square(RED) == red_target,
            "double push replaces its owner's previous target");
          expect(
            moved.en_passant_square(BLUE) == blue_target
              && moved.en_passant_square(YELLOW)
                   == yellow_target
              && moved.en_passant_square(GREEN)
                   == green_target,
            "double push preserves other owners' targets");
      },
      "double-push undo restores all four targets");
}

void test_promotions() {
    using namespace Mockingbird;

    constexpr std::array<Square, COLOR_NB> SOURCES = {
      make_square(FILE_H, RANK_10),
      make_square(FILE_J, RANK_8),
      make_square(FILE_H, RANK_5),
      make_square(FILE_E, RANK_8),
    };
    constexpr std::array<Square, COLOR_NB> QUIET_DESTINATIONS = {
      make_square(FILE_H, RANK_11),
      make_square(FILE_K, RANK_8),
      make_square(FILE_H, RANK_4),
      make_square(FILE_D, RANK_8),
    };
    constexpr std::array<Square, COLOR_NB> CAPTURE_DESTINATIONS = {
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
            const Square quiet_destination =
              QUIET_DESTINATIONS[std::size_t(color)];

            expect_round_trip(
              quiet,
              Move::promotion(
                source, quiet_destination, promotion),
              [=](const Position& moved) {
                  expect(
                    moved.piece_on(quiet_destination)
                      == make_piece(color, promotion),
                    "quiet promotion places the selected piece");
                  expect(
                    moved.pieces(color, PAWN).empty(),
                    "quiet promotion removes the pawn");
              },
              "quiet-promotion undo restores the pawn");

            Position capture;
            capture.set_side_to_move(color);
            capture.put_piece(
              make_piece(color, PAWN), source);
            const Square capture_destination =
              CAPTURE_DESTINATIONS[std::size_t(color)];
            capture.put_piece(
              make_piece(next_color(color), ROOK),
              capture_destination);

            expect_round_trip(
              capture,
              Move::promotion(
                source, capture_destination, promotion),
              [=](const Position& moved) {
                  expect(
                    moved.piece_on(capture_destination)
                      == make_piece(color, promotion),
                    "capture promotion places the selected piece");
                  expect(
                    moved.pieces(
                      next_color(color), ROOK).empty(),
                    "capture promotion removes its target");
              },
              "capture-promotion undo restores both pieces");
        }
    }
}

void test_castling_moves() {
    using namespace Mockingbird;

    int castling_count = 0;

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);

        for (const CastlingSide side : CASTLING_SIDES) {
            ++castling_count;
            const CastlingGeometry& geometry =
              castling_geometry(color, side);
            const CastlingSide other_side =
              side == CastlingSide::KING_SIDE
                ? CastlingSide::QUEEN_SIDE
                : CastlingSide::KING_SIDE;
            const CastlingGeometry& other =
              castling_geometry(color, other_side);

            Position position;
            position.set_side_to_move(color);
            set_all_castling_rights(position);
            position.put_piece(
              make_piece(color, KING),
              geometry.king_source);
            position.put_piece(
              make_piece(color, ROOK),
              geometry.rook_source);
            position.put_piece(
              make_piece(color, ROOK),
              other.rook_source);

            expect_round_trip(
              position,
              Move::castling(
                geometry.king_source,
                geometry.king_destination),
              [=](const Position& moved) {
                  expect(
                    moved.piece_on(
                      geometry.king_destination)
                      == make_piece(color, KING),
                    "castling places the king on its destination");
                  expect(
                    moved.piece_on(
                      geometry.rook_destination)
                      == make_piece(color, ROOK),
                    "castling places the rook on its destination");
                  expect(
                    moved.empty(geometry.king_source)
                      && moved.empty(geometry.rook_source),
                    "castling empties both source squares");
                  expect(
                    !moved.has_castling_right(
                      color, CastlingSide::KING_SIDE)
                      && !moved.has_castling_right(
                        color, CastlingSide::QUEEN_SIDE),
                    "castling clears both rights for its color");
                  expect_castling_rights(
                    moved,
                    static_cast<std::uint8_t>(
                      ALL_CASTLING_RIGHTS
                      & static_cast<std::uint8_t>(
                        ~color_castling_rights(color))),
                    "castling preserves the exact rights mask");

                  for (int other_color_index = 0;
                       other_color_index < COLOR_NB;
                       ++other_color_index) {
                      const Color other_color =
                        Color(other_color_index);
                      if (other_color == color)
                          continue;

                      for (const CastlingSide retained_side :
                           CASTLING_SIDES) {
                          expect(
                            moved.has_castling_right(
                              other_color, retained_side),
                            "castling preserves other colors' rights");
                      }
                  }
              },
              "castling undo restores king, rook, and rights");
        }
    }

    expect(castling_count == 8,
           "all eight castling moves were tested");
}

void test_castling_right_updates() {
    using namespace Mockingbird;

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        const CastlingGeometry& kingside =
          castling_geometry(color, CastlingSide::KING_SIDE);

        Position king_move;
        king_move.set_side_to_move(color);
        set_all_castling_rights(king_move);
        king_move.put_piece(
          make_piece(color, KING), kingside.king_source);

        expect_round_trip(
          king_move,
          Move::normal(
            kingside.king_source,
            kingside.king_transit),
          [=](const Position& moved) {
              expect(
                !moved.has_castling_right(
                  color, CastlingSide::KING_SIDE)
                  && !moved.has_castling_right(
                    color, CastlingSide::QUEEN_SIDE),
                "ordinary king move clears both rights");
              expect_castling_rights(
                moved,
                static_cast<std::uint8_t>(
                  ALL_CASTLING_RIGHTS
                  & static_cast<std::uint8_t>(
                    ~color_castling_rights(color))),
                "ordinary king move preserves the exact rights mask");
          },
          "king-move undo restores both castling rights");

        for (const CastlingSide side : CASTLING_SIDES) {
            const CastlingGeometry& geometry =
              castling_geometry(color, side);
            const CastlingSide other_side =
              side == CastlingSide::KING_SIDE
                ? CastlingSide::QUEEN_SIDE
                : CastlingSide::KING_SIDE;

            Position rook_move;
            rook_move.set_side_to_move(color);
            set_all_castling_rights(rook_move);
            rook_move.put_piece(
              make_piece(color, ROOK),
              geometry.rook_source);

            expect_round_trip(
              rook_move,
              Move::normal(
                geometry.rook_source,
                geometry.rook_destination),
              [=](const Position& moved) {
                  expect(
                    !moved.has_castling_right(color, side),
                    "canonical rook move clears its right");
                  expect(
                    moved.has_castling_right(
                      color, other_side),
                    "canonical rook move preserves the other right");
                  expect_castling_rights(
                    moved,
                    static_cast<std::uint8_t>(
                      ALL_CASTLING_RIGHTS
                      & static_cast<std::uint8_t>(
                        ~castling_right_bit(color, side))),
                    "canonical rook move preserves the exact rights mask");
              },
              "rook-move undo restores its castling right");

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

            expect_round_trip(
              rook_capture,
              Move::normal(
                geometry.rook_destination,
                geometry.rook_source),
              [=](const Position& moved) {
                  expect(
                    !moved.has_castling_right(color, side),
                    "capturing a canonical rook clears its right");
                  expect(
                    moved.has_castling_right(
                      color, other_side),
                    "canonical rook capture preserves the other right");
                  expect_castling_rights(
                    moved,
                    static_cast<std::uint8_t>(
                      ALL_CASTLING_RIGHTS
                      & static_cast<std::uint8_t>(
                        ~castling_right_bit(color, side))),
                    "canonical rook capture preserves the exact rights mask");
              },
              "rook-capture undo restores its castling right");
        }
    }
}

void test_captured_king_rights() {
    using namespace Mockingbird;

    constexpr Square attacker_source =
      make_square(FILE_F, RANK_7);
    constexpr Square king_square =
      make_square(FILE_H, RANK_8);

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color captured_color = Color(color_index);
        const Color attacker = next_color(captured_color);

        Position position;
        position.set_side_to_move(attacker);
        set_all_castling_rights(position);
        position.put_piece(
          make_piece(attacker, KNIGHT), attacker_source);
        position.put_piece(
          make_piece(captured_color, KING), king_square);

        expect_round_trip(
          position,
          Move::normal(attacker_source, king_square),
          [=](const Position& moved) {
              expect(
                moved.piece_on(king_square)
                  == make_piece(attacker, KNIGHT),
                "king capture places the attacking piece");
              expect_castling_rights(
                moved,
                static_cast<std::uint8_t>(
                  ALL_CASTLING_RIGHTS
                  & static_cast<std::uint8_t>(
                    ~color_castling_rights(
                      captured_color))),
                "captured king clears only its color's rights");
          },
          "king-capture undo restores king and rights");
    }
}

void test_en_passant_moves() {
    using namespace Mockingbird;

    int geometry_count = 0;
    int encoding_count = 0;

    for (int owner_index = 0;
         owner_index < COLOR_NB;
         ++owner_index) {
        const Color owner = Color(owner_index);

        for (int owner_source_index = 0;
             owner_source_index < SQUARE_NB;
             ++owner_source_index) {
            const Square owner_source =
              Square(owner_source_index);
            if (!is_ok(owner_source))
                continue;

            const Square target =
              pawn_push_destination(owner, owner_source);
            const Square victim =
              pawn_double_push_destination(
                owner, owner_source);
            if (target == SQ_NONE || victim == SQ_NONE)
                continue;

            for (int moving_color_index = 0;
                 moving_color_index < COLOR_NB;
                 ++moving_color_index) {
                const Color moving_color =
                  Color(moving_color_index);
                if (team_of(moving_color) == team_of(owner))
                    continue;

                for (int source_index = 0;
                     source_index < SQUARE_NB;
                     ++source_index) {
                    const Square source = Square(source_index);
                    if (!is_ok(source)
                        || source == victim
                        || !pawn_attacks(
                              moving_color, source).test(target))
                        continue;

                    ++geometry_count;
                    const bool promotes =
                      is_pawn_promotion_square(
                        moving_color, target);
                    const std::size_t move_count =
                      promotes ? PROMOTION_TYPES.size() : 1;
                    encoding_count +=
                      static_cast<int>(move_count);

                    for (bool occupied_target :
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
                              : Move::en_passant(
                                  source, target);

                            MoveList generated;
                            generate_pawn_moves(base, generated);
                            expect(
                              contains_move(generated, move),
                              "generated en-passant move is executable");

                            expect_round_trip(
                              base,
                              move,
                              [=](const Position& moved) {
                                  const Piece expected_piece =
                                    promotes
                                    ? make_piece(
                                        moving_color,
                                        PROMOTION_TYPES[
                                          move_index])
                                    : make_piece(
                                        moving_color, PAWN);
                                  expect(
                                    moved.piece_on(target)
                                      == expected_piece,
                                    "en passant places the moving piece");
                                  expect(
                                    moved.empty(victim),
                                    "en passant removes the passed pawn");
                                  expect(
                                    moved.en_passant_square(owner)
                                      == SQ_NONE,
                                    "en passant consumes its target");
                              },
                              occupied_target
                                ? "occupied-target en-passant undo restores both captures"
                                : "en-passant undo restores the passed pawn");
                        }
                    }
                }
            }
        }
    }

    expect(geometry_count == 120,
           "all one hundred twenty en-passant geometries were tested");
    expect(encoding_count == 168,
           "all one hundred sixty-eight en-passant encodings were tested");
}

void test_multiple_en_passant_targets() {
    using namespace Mockingbird;

    constexpr Square b5 = make_square(FILE_B, RANK_5);
    constexpr Square c6 = make_square(FILE_C, RANK_6);
    constexpr Square d6 = make_square(FILE_D, RANK_6);
    constexpr Square k7 = make_square(FILE_K, RANK_7);
    constexpr Square k8 = make_square(FILE_K, RANK_8);
    constexpr Square l8 = make_square(FILE_L, RANK_8);

    Position position;
    position.put_piece(R_PAWN, b5);
    position.put_piece(R_PAWN, k7);
    position.put_piece(B_PAWN, d6);
    position.put_piece(G_PAWN, k8);
    position.set_en_passant_square(BLUE, c6);
    position.set_en_passant_square(GREEN, l8);

    expect_round_trip(
      position,
      Move::en_passant(b5, c6),
      [](const Position& moved) {
          expect(
            moved.en_passant_square(BLUE) == SQ_NONE,
            "selected en-passant target is consumed");
          expect(
            moved.en_passant_square(GREEN)
              == make_square(FILE_L, RANK_8),
            "unselected en-passant target persists");
      },
      "multi-target en-passant undo restores both targets");
}

void test_en_passant_victim_invalidation() {
    using namespace Mockingbird;

    int capture_count = 0;

    for (int owner_index = 0;
         owner_index < COLOR_NB;
         ++owner_index) {
        const Color owner = Color(owner_index);
        const Color attacker = next_color(owner);

        for (int source_index = 0;
             source_index < SQUARE_NB;
             ++source_index) {
            const Square source = Square(source_index);
            if (!is_ok(source))
                continue;

            const Square target =
              pawn_push_destination(owner, source);
            const Square victim =
              pawn_double_push_destination(owner, source);
            if (target == SQ_NONE || victim == SQ_NONE)
                continue;

            ++capture_count;
            Position position;
            position.set_side_to_move(attacker);
            position.put_piece(
              make_piece(attacker, ROOK), source);
            position.put_piece(
              make_piece(owner, PAWN), victim);
            position.set_en_passant_square(owner, target);

            expect_round_trip(
              position,
              Move::normal(source, victim),
              [=](const Position& moved) {
                  expect(
                    moved.en_passant_square(owner) == SQ_NONE,
                    "capturing a passed pawn invalidates its target");
              },
              "passed-pawn capture undo restores its target");
        }
    }

    expect(capture_count == 32,
           "all passed-pawn capture orientations were tested");
}

void test_undo_state_reuse() {
    using namespace Mockingbird;

    constexpr Square b10 = make_square(FILE_B, RANK_10);
    constexpr Square c11 = make_square(FILE_C, RANK_11);
    constexpr Square d11 = make_square(FILE_D, RANK_11);
    constexpr Move en_passant =
      Move::en_passant(b10, c11, QUEEN);

    Position special;
    special.put_piece(R_PAWN, b10);
    special.put_piece(B_ROOK, c11);
    special.put_piece(B_PAWN, d11);
    special.set_en_passant_square(BLUE, c11);
    const Position special_original = special;

    UndoState reused;
    do_move(special, en_passant, reused);
    undo_move(special, en_passant, reused);
    expect(
      positions_equal(special, special_original),
      "first use restores an occupied en-passant promotion");

    constexpr Square f7 = make_square(FILE_F, RANK_7);
    constexpr Square h8 = make_square(FILE_H, RANK_8);
    constexpr Move quiet = Move::normal(h8, f7);

    Position ordinary;
    ordinary.put_piece(R_KNIGHT, h8);
    const Position ordinary_original = ordinary;

    do_move(ordinary, quiet, reused);
    undo_move(ordinary, quiet, reused);
    expect(
      positions_equal(ordinary, ordinary_original),
      "reused undo state does not retain prior captures");
    expect_consistent(ordinary);
}

void test_four_player_state_sequence() {
    using namespace Mockingbird;

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
    constexpr Square red_target =
      make_square(FILE_D, RANK_3);

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
    std::array<UndoState, MOVES.size()> undo_states{};
    snapshots[0] = position;

    for (std::size_t ply = 0;
         ply < MOVES.size();
         ++ply) {
        do_move(position, MOVES[ply], undo_states[ply]);
        snapshots[ply + 1] = position;
        expect_consistent(position);

        if (ply < 4) {
            expect(
              position.en_passant_square(RED)
                == red_target,
              "target persists through the other three turns");
        } else {
            expect(
              position.en_passant_square(RED) == SQ_NONE,
              "target expires when its owner moves again");
        }
    }

    expect(position.side_to_move() == BLUE,
           "five plies advance from Red to Blue");

    for (std::size_t ply = MOVES.size();
         ply-- > 0;) {
        undo_move(position, MOVES[ply], undo_states[ply]);
        expect(
          positions_equal(position, snapshots[ply]),
          "LIFO undo restores the preceding ply");
        expect_consistent(position);
    }

    expect(positions_equal(position, snapshots[0]),
           "multi-ply undo restores the initial position");
}

}  // namespace

int main() {
    test_undo_state_layout();
    test_normal_moves_and_captures();
    test_pawn_push_state();
    test_double_push_preserves_other_targets();
    test_promotions();
    test_castling_moves();
    test_castling_right_updates();
    test_captured_king_rights();
    test_en_passant_moves();
    test_multiple_en_passant_targets();
    test_en_passant_victim_invalidation();
    test_undo_state_reuse();
    test_four_player_state_sequence();

    if (failures != 0) {
        std::cerr << failures << " move-state test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All move-state tests passed\n";
    return EXIT_SUCCESS;
}
