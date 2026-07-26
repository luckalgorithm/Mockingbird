#include "result.h"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string_view>
#include <utility>

namespace {

int failures = 0;

using namespace Mockingbird;

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

template<std::size_t Size>
[[nodiscard]] constexpr PositionHistory make_history(
  const std::array<PositionKey, Size>& keys) {
    static_assert(Size > 0);

    PositionHistory history{keys[0]};
    for (std::size_t index = 1;
         index < keys.size();
         ++index)
        history.push(keys[index]);

    return history;
}

// Popping a copy exposes every key in reverse chronological order.
template<std::size_t Size>
[[nodiscard]] constexpr bool history_matches(
  PositionHistory history,
  const std::array<PositionKey, Size>& expected) noexcept {
    static_assert(Size > 0);

    if (history.size() != expected.size())
        return false;

    for (std::size_t index = expected.size();
         index-- > 1;) {
        if (history.current_key() != expected[index])
            return false;

        history.pop(expected[index]);
    }

    return history.size() == 1
        && history.current_key() == expected[0];
}

[[nodiscard]] constexpr Square rotate_clockwise(
  Square square) noexcept {
    return make_square(
      File(int(rank_of(square))),
      Rank(BOARD_FILES + 1 - int(file_of(square))));
}

[[nodiscard]] constexpr Move rotate_clockwise(
  Move move) noexcept {
    return Move::normal(
      rotate_clockwise(move.from()),
      rotate_clockwise(move.to()));
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

// The moving king is confined to d1, e1, d2, and e2 by board geometry.
[[nodiscard]] constexpr Position blocked_corner(
  bool checked,
  bool leave_escape) noexcept {
    Position position;
    position.set_side_to_move(RED);

    position.put_piece(
      R_KING, make_square(FILE_D, RANK_1));
    position.put_piece(
      Y_KING, make_square(FILE_E, RANK_1));
    position.put_piece(
      Y_PAWN, make_square(FILE_D, RANK_2));

    if (!leave_escape) {
        position.put_piece(
          Y_PAWN, make_square(FILE_E, RANK_2));
    }

    position.put_piece(
      B_KING, make_square(FILE_A, RANK_4));
    position.put_piece(
      G_KING, make_square(FILE_N, RANK_11));

    if (checked) {
        position.put_piece(
          B_KNIGHT, make_square(FILE_F, RANK_2));
    }

    return position;
}

[[nodiscard]] constexpr Position complete_kings() noexcept {
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

[[nodiscard]] constexpr Team opposing_team(
  Team team) noexcept {
    return team == RED_YELLOW ? BLUE_GREEN : RED_YELLOW;
}

template<std::size_t HistorySize>
void expect_classifiers(
  Position& position,
  const PositionHistory& history,
  const std::array<PositionKey, HistorySize>& expected_history,
  PositionResult expected_result,
  bool expected_legal_move) {
    const Position original = position;
    const std::size_t history_capacity =
      history.capacity();

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    const bool generated_legal_move =
      !legal_moves.empty();
    expect(
      positions_equal(position, original),
      "legal generation restores every position field");
    expect(
      generated_legal_move == expected_legal_move,
      "legal-list emptiness matches the expected legal-move state");

    expect(
      has_legal_move(position) == expected_legal_move,
      "has_legal_move agrees with the generated legal list");
    expect(
      positions_equal(position, original),
      "has_legal_move restores every position field");

    expect(
      terminal_result(position, history, legal_moves)
        == expected_result,
      "the supplied-list classifier returns the expected result");
    expect(
      positions_equal(position, original),
      "the supplied-list classifier leaves the position unchanged");

    expect(
      terminal_result(position, history)
        == expected_result,
      "the generating classifier returns the expected result");
    expect(
      positions_equal(position, original),
      "the generating classifier restores every position field");

    expect(
      history.capacity() == history_capacity
        && history_matches(history, expected_history),
      "classification leaves the complete history sequence unchanged");
}

[[nodiscard]] constexpr bool constexpr_result_cases() {
    Position mate = blocked_corner(true, false);
    const std::array mate_keys = {mate.key()};
    const PositionHistory mate_history =
      make_history(mate_keys);
    MoveList mate_moves;
    generate_legal_moves(mate, mate_moves);

    if (has_legal_move(mate)
        || !mate_moves.empty()
        || terminal_result(
             mate, mate_history, mate_moves)
             != PositionResult::checkmate(BLUE_GREEN)
        || terminal_result(mate, mate_history)
             != PositionResult::checkmate(BLUE_GREEN))
        return false;

    Position stalemate = blocked_corner(false, false);
    const std::array stalemate_keys = {
      stalemate.key(),
    };
    const PositionHistory stalemate_history =
      make_history(stalemate_keys);
    if (terminal_result(stalemate, stalemate_history)
        != PositionResult::stalemate())
        return false;

    Position ongoing = blocked_corner(true, true);
    const PositionKey key = ongoing.key();
    const std::array repetition_keys = {
      key,
      key ^ PositionKey{0x1111111111111111ULL},
      key,
      key ^ PositionKey{0x2222222222222222ULL},
      key,
    };
    const PositionHistory repetition_history =
      make_history(repetition_keys);
    MoveList ongoing_moves;
    generate_legal_moves(ongoing, ongoing_moves);

    return ongoing_moves.size() == 1
        && terminal_result(
             ongoing,
             repetition_history,
             ongoing_moves)
             == PositionResult::threefold_repetition()
        && terminal_result(
             ongoing, repetition_history)
             == PositionResult::threefold_repetition()
        && positions_equal(
             ongoing, blocked_corner(true, true))
        && history_matches(
             repetition_history, repetition_keys);
}

static_assert(constexpr_result_cases());
static_assert(
  PositionResult::king_capture(RED_YELLOW)
    .winning_team()
    == RED_YELLOW);
static_assert(
  PositionResult::checkmate(BLUE_GREEN)
    .winning_team()
    == BLUE_GREEN);
static_assert(PositionResult::stalemate().is_terminal());
static_assert(
  PositionResult::threefold_repetition().is_terminal());
static_assert(
  !PositionResult::invalid_position().is_terminal());
static_assert(
  !PositionResult::invalid_position().is_valid());
static_assert(noexcept(
  terminal_result(
    std::declval<const Position&>(),
    std::declval<const PositionHistory&>(),
    std::declval<const MoveList&>())));
static_assert(noexcept(
  terminal_result(
    std::declval<Position&>(),
    std::declval<const PositionHistory&>())));
static_assert(noexcept(
  has_legal_move(std::declval<Position&>())));

void test_result_properties() {
    const PositionResult ongoing;
    const PositionResult red_yellow_capture =
      PositionResult::king_capture(RED_YELLOW);
    const PositionResult blue_green_mate =
      PositionResult::checkmate(BLUE_GREEN);
    const PositionResult stalemate =
      PositionResult::stalemate();
    const PositionResult repetition =
      PositionResult::threefold_repetition();
    const PositionResult invalid =
      PositionResult::invalid_position();

    expect(
      ongoing.type() == ResultType::ONGOING
        && ongoing.is_valid()
        && !ongoing.is_terminal()
        && !ongoing.winning_team(),
      "the default result is valid and ongoing");
    expect(
      red_yellow_capture.type()
          == ResultType::KING_CAPTURE
        && red_yellow_capture.is_valid()
        && red_yellow_capture.is_terminal()
        && red_yellow_capture.winning_team()
             == RED_YELLOW,
      "a king-capture result stores its winning team");
    expect(
      blue_green_mate.type() == ResultType::CHECKMATE
        && blue_green_mate.is_valid()
        && blue_green_mate.is_terminal()
        && blue_green_mate.winning_team()
             == BLUE_GREEN,
      "a checkmate result stores its winning team");
    expect(
      stalemate.type() == ResultType::STALEMATE
        && stalemate.is_valid()
        && stalemate.is_terminal()
        && !stalemate.winning_team(),
      "stalemate is terminal without a winning team");
    expect(
      repetition.type()
          == ResultType::THREEFOLD_REPETITION
        && repetition.is_valid()
        && repetition.is_terminal()
        && !repetition.winning_team(),
      "threefold repetition is terminal without a winning team");
    expect(
      invalid.type() == ResultType::INVALID_POSITION
        && !invalid.is_valid()
        && !invalid.is_terminal()
        && !invalid.winning_team(),
      "an invalid layout is neither valid nor terminal");
}

void test_rotated_no_move_and_checked_positions() {
    Position stalemate = blocked_corner(false, false);
    Position mate = blocked_corner(true, false);
    Position checked_ongoing = blocked_corner(true, true);
    Move expected_escape = Move::normal(
      make_square(FILE_D, RANK_1),
      make_square(FILE_E, RANK_2));

    for (int rotation = 0;
         rotation < COLOR_NB;
         ++rotation) {
        expect(
          stalemate.side_to_move() == Color(rotation)
            && mate.side_to_move() == Color(rotation)
            && checked_ongoing.side_to_move()
                 == Color(rotation),
          "rotation advances each fixture to the expected color");

        expect(
          !in_check(stalemate)
            && in_check(mate)
            && in_check(checked_ongoing),
          "rotated fixtures retain their check states");

        const std::array stalemate_history_keys = {
          stalemate.key(),
        };
        const PositionHistory stalemate_history =
          make_history(stalemate_history_keys);
        expect_classifiers(
          stalemate,
          stalemate_history,
          stalemate_history_keys,
          PositionResult::stalemate(),
          false);

        const std::array mate_history_keys = {
          mate.key(),
        };
        const PositionHistory mate_history =
          make_history(mate_history_keys);
        expect_classifiers(
          mate,
          mate_history,
          mate_history_keys,
          PositionResult::checkmate(
            opposing_team(
              team_of(mate.side_to_move()))),
          false);

        MoveList legal_moves;
        generate_legal_moves(
          checked_ongoing, legal_moves);
        expect(
          legal_moves.size() == 1
            && legal_moves[0] == expected_escape,
          "the rotated checked position has its single king escape");

        const std::array ongoing_history_keys = {
          checked_ongoing.key(),
        };
        const PositionHistory ongoing_history =
          make_history(ongoing_history_keys);
        expect_classifiers(
          checked_ongoing,
          ongoing_history,
          ongoing_history_keys,
          PositionResult{},
          true);

        stalemate = rotate_clockwise(stalemate);
        mate = rotate_clockwise(mate);
        checked_ongoing =
          rotate_clockwise(checked_ongoing);
        expected_escape =
          rotate_clockwise(expected_escape);
    }
}

void test_missing_kings() {
    const Position complete = complete_kings();

    for (int missing_index = 0;
         missing_index < COLOR_NB;
         ++missing_index) {
        const Color missing_color =
          Color(missing_index);
        Position position = complete;
        position.remove_piece(
          position.pieces(missing_color, KING).lsb());

        const std::array history_keys = {
          position.key(),
        };
        const PositionHistory history =
          make_history(history_keys);
        expect_classifiers(
          position,
          history,
          history_keys,
          PositionResult::king_capture(
            opposing_team(team_of(missing_color))),
          false);
    }

    for (const Team missing_team :
         {RED_YELLOW, BLUE_GREEN}) {
        Position position = complete;

        for (int color_index = 0;
             color_index < COLOR_NB;
             ++color_index) {
            const Color color = Color(color_index);
            if (team_of(color) == missing_team) {
                position.remove_piece(
                  position.pieces(color, KING).lsb());
            }
        }

        const std::array history_keys = {
          position.key(),
        };
        const PositionHistory history =
          make_history(history_keys);
        expect_classifiers(
          position,
          history,
          history_keys,
          PositionResult::invalid_position(),
          false);
    }
}

void test_king_capture_transition() {
    Position position;
    position.set_side_to_move(RED);
    position.put_piece(
      R_KING, make_square(FILE_H, RANK_5));
    position.put_piece(
      R_ROOK, make_square(FILE_F, RANK_5));
    position.put_piece(
      B_ROOK, make_square(FILE_H, RANK_10));
    position.put_piece(
      B_KING, make_square(FILE_A, RANK_7));
    position.put_piece(
      Y_KING, make_square(FILE_G, RANK_14));
    position.put_piece(
      G_KING, make_square(FILE_F, RANK_8));

    const Position original = position;
    PositionHistory history{position.key()};
    const Move capture = Move::normal(
      make_square(FILE_F, RANK_5),
      make_square(FILE_F, RANK_8));

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    expect(in_check(position),
           "the king-capture fixture starts in check");
    expect(contains_move(legal_moves, capture),
           "legal generation retains the opposing-king capture");
    expect(has_legal_move(position),
           "the opposing-king capture satisfies has_legal_move");
    expect(
      terminal_result(position, history, legal_moves)
        == PositionResult{},
      "the supplied-list result is ongoing before the king capture");
    expect(
      terminal_result(position, history)
        == PositionResult{},
      "the generating result is ongoing before the king capture");
    expect(positions_equal(position, original),
           "pre-capture result checks preserve the position");

    UndoState undo;
    do_move(position, capture, undo);
    history.push(position.key());

    expect(
      terminal_result(position, history)
        == PositionResult::king_capture(RED_YELLOW),
      "capturing the Green king produces a Red-Yellow win");
    expect(
      position.pieces(GREEN, KING).empty(),
      "the terminal position has no Green king");

    const PositionKey captured_key = position.key();
    history.pop(captured_key);
    undo_move(position, capture, undo);

    expect(positions_equal(position, original),
           "undo restores the pre-capture position");
    expect(history.size() == 1
             && history.current_key() == original.key(),
           "undo restores the pre-capture history");
}

void test_invalid_king_layouts() {
    const Position complete = complete_kings();
    constexpr Square duplicate_square =
      make_square(FILE_G, RANK_7);

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        Position duplicate = complete;
        duplicate.put_piece(
          make_piece(Color(color_index), KING),
          duplicate_square);

        const std::array history_keys = {
          duplicate.key(),
        };
        const PositionHistory history =
          make_history(history_keys);
        expect_classifiers(
          duplicate,
          history,
          history_keys,
          PositionResult::invalid_position(),
          false);
    }

    for (const Color red_yellow :
         {RED, YELLOW}) {
        for (const Color blue_green :
             {BLUE, GREEN}) {
            Position missing_both = complete;
            missing_both.remove_piece(
              missing_both.pieces(
                red_yellow, KING).lsb());
            missing_both.remove_piece(
              missing_both.pieces(
                blue_green, KING).lsb());

            const std::array history_keys = {
              missing_both.key(),
            };
            const PositionHistory history =
              make_history(history_keys);
            expect_classifiers(
              missing_both,
              history,
              history_keys,
              PositionResult::invalid_position(),
              false);
        }
    }
}

void test_repetition_counts_and_stale_keys() {
    Position position = blocked_corner(true, true);
    const PositionKey key = position.key();
    const PositionKey first_filler =
      key ^ PositionKey{0x1111111111111111ULL};
    const PositionKey second_filler =
      key ^ PositionKey{0x2222222222222222ULL};
    const PositionKey stale =
      key ^ PositionKey{0x3333333333333333ULL};

    const std::array one_occurrence = {
      key,
    };
    const PositionHistory one_history =
      make_history(one_occurrence);
    expect_classifiers(
      position,
      one_history,
      one_occurrence,
      PositionResult{},
      true);

    const std::array two_occurrences = {
      key,
      first_filler,
      key,
    };
    const PositionHistory two_history =
      make_history(two_occurrences);
    expect(two_history.current_count() == 2,
           "the twofold fixture has two current occurrences");
    expect_classifiers(
      position,
      two_history,
      two_occurrences,
      PositionResult{},
      true);

    const std::array three_occurrences = {
      key,
      first_filler,
      key,
      second_filler,
      key,
    };
    const PositionHistory three_history =
      make_history(three_occurrences);
    expect(three_history.current_count() == 3,
           "the threefold fixture has three current occurrences");
    expect_classifiers(
      position,
      three_history,
      three_occurrences,
      PositionResult::threefold_repetition(),
      true);

    const std::array stale_occurrences = {
      stale,
      first_filler,
      stale,
      second_filler,
      stale,
      key,
    };
    const PositionHistory stale_history =
      make_history(stale_occurrences);
    expect(
      stale_history.count(stale) == 3
        && stale_history.current_count() == 1,
      "the stale fixture repeats only a noncurrent key");
    expect_classifiers(
      position,
      stale_history,
      stale_occurrences,
      PositionResult{},
      true);
}

template<std::size_t Size>
void expect_precedence(
  Position& position,
  const std::array<PositionKey, Size>& keys,
  PositionResult expected) {
    const PositionHistory history =
      make_history(keys);
    expect(history.is_threefold(),
           "a precedence fixture has a current threefold repetition");
    expect_classifiers(
      position,
      history,
      keys,
      expected,
      has_legal_move(position));
}

void test_classification_precedence() {
    Position missing = complete_kings();
    missing.remove_piece(
      missing.pieces(BLUE, KING).lsb());
    const PositionKey missing_key = missing.key();
    const std::array missing_keys = {
      missing_key,
      missing_key,
      missing_key,
    };
    expect_precedence(
      missing,
      missing_keys,
      PositionResult::king_capture(RED_YELLOW));

    Position invalid = complete_kings();
    invalid.put_piece(
      R_KING, make_square(FILE_G, RANK_7));
    const PositionKey invalid_key = invalid.key();
    const std::array invalid_keys = {
      invalid_key,
      invalid_key,
      invalid_key,
    };
    expect_precedence(
      invalid,
      invalid_keys,
      PositionResult::invalid_position());

    Position mate = blocked_corner(true, false);
    const PositionKey mate_key = mate.key();
    const std::array mate_keys = {
      mate_key,
      mate_key,
      mate_key,
    };
    expect_precedence(
      mate,
      mate_keys,
      PositionResult::checkmate(BLUE_GREEN));

    Position stalemate = blocked_corner(false, false);
    const PositionKey stalemate_key = stalemate.key();
    const std::array stalemate_keys = {
      stalemate_key,
      stalemate_key,
      stalemate_key,
    };
    expect_precedence(
      stalemate,
      stalemate_keys,
      PositionResult::stalemate());
}

}  // namespace

int main() {
    test_result_properties();
    test_rotated_no_move_and_checked_positions();
    test_missing_kings();
    test_king_capture_transition();
    test_invalid_king_layouts();
    test_repetition_counts_and_stale_keys();
    test_classification_precedence();

    if (failures != 0) {
        std::cerr << failures
                  << " result test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All result tests passed\n";
    return EXIT_SUCCESS;
}
