#include "legal.h"
#include "perft.h"
#include "repetition.h"
#include "setup.h"
#include "transition.h"

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

using namespace Mockingbird;

inline constexpr PositionKey KEY_A = 0x1111111111111111ULL;
inline constexpr PositionKey KEY_B = 0x2222222222222222ULL;
inline constexpr PositionKey KEY_C = 0x3333333333333333ULL;
inline constexpr PositionKey KEY_D = 0x4444444444444444ULL;
inline constexpr PositionKey KEY_E = 0x5555555555555555ULL;

inline constexpr std::array<CastlingSide, CASTLING_SIDE_NB>
  CASTLING_SIDES = {
    CastlingSide::KING_SIDE,
    CastlingSide::QUEEN_SIDE,
};

inline constexpr std::array<Move, 8> KNIGHT_CYCLE = {
  Move::normal(
    make_square(FILE_E, RANK_1),
    make_square(FILE_F, RANK_3)),
  Move::normal(
    make_square(FILE_A, RANK_5),
    make_square(FILE_C, RANK_4)),
  Move::normal(
    make_square(FILE_E, RANK_14),
    make_square(FILE_F, RANK_12)),
  Move::normal(
    make_square(FILE_N, RANK_5),
    make_square(FILE_L, RANK_4)),
  Move::normal(
    make_square(FILE_F, RANK_3),
    make_square(FILE_E, RANK_1)),
  Move::normal(
    make_square(FILE_C, RANK_4),
    make_square(FILE_A, RANK_5)),
  Move::normal(
    make_square(FILE_F, RANK_12),
    make_square(FILE_E, RANK_14)),
  Move::normal(
    make_square(FILE_L, RANK_4),
    make_square(FILE_N, RANK_5)),
};

// Records a failed condition and allows the remaining tests to run.
void expect(bool condition, std::string_view message) {
    if (condition)
        return;

    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

void copy_assign(
  PositionHistory& destination,
  const PositionHistory& source) {
    destination = source;
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
        if (left.pieces(PieceType(type_index))
            != right.pieces(PieceType(type_index)))
            return false;
    }

    return true;
}

template<std::size_t Size>
[[nodiscard]] constexpr bool all_distinct(
  const std::array<PositionKey, Size>& keys) noexcept {
    for (std::size_t left = 0; left < keys.size(); ++left) {
        for (std::size_t right = left + 1;
             right < keys.size();
             ++right) {
            if (keys[left] == keys[right])
                return false;
        }
    }

    return true;
}

[[nodiscard]] constexpr bool constexpr_history_smoke() {
    PositionHistory history{KEY_A};
    if (history.size() != 1
        || history.capacity()
             < PositionHistory::INITIAL_RESERVE
        || history.current_key() != KEY_A
        || history.current_count() != 1
        || history.is_twofold()
        || history.is_threefold())
        return false;

    history.push(KEY_B);
    history.push(KEY_C);
    history.push(KEY_D);
    history.push(KEY_A);
    if (history.count(KEY_A) != 2
        || history.current_count() != 2
        || !history.is_twofold()
        || history.is_threefold())
        return false;

    history.push(KEY_B);
    history.push(KEY_C);
    history.push(KEY_D);
    history.push(KEY_A);
    if (history.current_count() != 3
        || !history.is_twofold()
        || !history.is_threefold())
        return false;

    history.pop(KEY_A);
    if (history.current_key() != KEY_D
        || history.count(KEY_A) != 2)
        return false;

    PositionHistory copied{history};
    if (copied.size() != history.size()
        || copied.capacity()
             < PositionHistory::INITIAL_RESERVE
        || copied.current_key() != history.current_key()
        || copied.count(KEY_A) != 2)
        return false;

    PositionHistory assigned{KEY_E};
    assigned = copied;
    if (assigned.size() != copied.size()
        || assigned.capacity()
             < PositionHistory::INITIAL_RESERVE
        || assigned.current_key() != copied.current_key()
        || assigned.count(KEY_A) != 2)
        return false;

    history.reset(KEY_E);
    return history.size() == 1
        && history.current_key() == KEY_E
        && history.current_count() == 1
        && history.count(KEY_A) == 0
        && !history.is_twofold()
        && !history.is_threefold();
}

static_assert(PositionHistory::INITIAL_RESERVE > 0);
static_assert(constexpr_history_smoke());
static_assert(std::is_copy_constructible_v<PositionHistory>);
static_assert(std::is_copy_assignable_v<PositionHistory>);
static_assert(std::is_nothrow_move_constructible_v<PositionHistory>);
static_assert(std::is_nothrow_move_assignable_v<PositionHistory>);
static_assert(noexcept(
  std::declval<const PositionHistory&>().size()));
static_assert(noexcept(
  std::declval<const PositionHistory&>().capacity()));
static_assert(noexcept(
  std::declval<const PositionHistory&>().current_key()));
static_assert(noexcept(
  std::declval<const PositionHistory&>().context()));
static_assert(noexcept(
  std::declval<const PositionHistory&>().count(KEY_A)));
static_assert(noexcept(
  std::declval<const PositionHistory&>().current_count()));
static_assert(noexcept(
  std::declval<const PositionHistory&>().is_twofold()));
static_assert(noexcept(
  std::declval<const PositionHistory&>().is_threefold()));
static_assert(noexcept(
  std::declval<PositionHistory&>().pop(KEY_A)));
static_assert(!noexcept(PositionHistory{KEY_A}));
static_assert(!noexcept(
  std::declval<PositionHistory&>().reset(KEY_A)));
static_assert(!noexcept(
  std::declval<PositionHistory&>().push(KEY_A)));

void test_history_context() {
    PositionHistory first{KEY_A};
    first.push(KEY_B);
    first.push(KEY_C);
    first.push(KEY_D);

    PositionHistory reordered{KEY_C};
    reordered.push(KEY_A);
    reordered.push(KEY_B);
    reordered.push(KEY_D);

    expect(
      first.context() == reordered.context(),
      "equal key multisets have equal history contexts");

    PositionHistory different{KEY_A};
    different.push(KEY_B);
    different.push(KEY_E);
    different.push(KEY_D);
    expect(
      first.context() != different.context(),
      "different equal-length key multisets have different contexts");

    const HistoryContext before_branch =
      first.context();
    first.push(KEY_A);
    expect(
      first.context() != before_branch
        && first.context().length == 5,
      "pushing a key updates both fingerprints and the length");
    first.pop(KEY_A);
    expect(
      first.context() == before_branch,
      "popping a key restores the preceding history context");

    const PositionHistory copied{first};
    expect(
      copied.context() == first.context(),
      "copy construction preserves the history context");

    PositionHistory assigned{KEY_E};
    assigned = first;
    expect(
      assigned.context() == first.context(),
      "copy assignment preserves the history context");

    assigned.reset(KEY_E);
    const PositionHistory reset_reference{KEY_E};
    expect(
      assigned.context()
        == reset_reference.context(),
      "reset replaces the complete history context");
}

void test_seed_reset_and_thresholds() {
    PositionHistory history{KEY_A};

    expect(history.size() == 1,
           "construction records exactly the initial key");
    expect(
      history.capacity()
        >= PositionHistory::INITIAL_RESERVE,
      "construction establishes at least the initial reserve");
    expect(history.current_key() == KEY_A,
           "the seeded key is current");
    expect(history.count(KEY_A) == 1,
           "the seeded key has one occurrence");
    expect(history.count(KEY_B) == 0,
           "an absent key has no occurrences");
    expect(history.current_count() == 1,
           "the initial current count is one");
    expect(!history.is_twofold(),
           "one occurrence is not twofold");
    expect(!history.is_threefold(),
           "one occurrence is not threefold");

    history.push(KEY_B);
    history.push(KEY_C);
    history.push(KEY_D);
    history.push(KEY_A);

    expect(history.size() == 5,
           "four pushes extend the seeded history");
    expect(history.current_key() == KEY_A,
           "the most recently pushed key is current");
    expect(history.count(KEY_A) == 2,
           "the current key has two occurrences");
    expect(history.current_count() == 2,
           "current_count includes the current occurrence");
    expect(history.is_twofold(),
           "two current occurrences are twofold");
    expect(!history.is_threefold(),
           "two current occurrences are not threefold");

    history.push(KEY_B);
    history.push(KEY_C);
    history.push(KEY_D);
    history.push(KEY_A);

    expect(history.current_count() == 3,
           "the third matching push creates three occurrences");
    expect(history.is_twofold(),
           "three occurrences also satisfy twofold");
    expect(history.is_threefold(),
           "three current occurrences are threefold");
    expect(history.count(KEY_B) == 2
             && history.count(KEY_C) == 2
             && history.count(KEY_D) == 2,
           "noncurrent occurrence counts remain exact");

    history.reset(KEY_E);
    expect(history.size() == 1,
           "reset leaves one initial entry");
    expect(history.current_key() == KEY_E,
           "reset establishes the replacement key");
    expect(history.current_count() == 1,
           "the reset key occurs once");
    expect(history.count(KEY_A) == 0
             && history.count(KEY_B) == 0
             && history.count(KEY_C) == 0
             && history.count(KEY_D) == 0,
           "reset discards every earlier key");
    expect(!history.is_twofold()
             && !history.is_threefold(),
           "a reset history is not repeated");
}

void test_growth_and_full_unwind() {
    constexpr std::size_t EXTRA_ENTRIES = 73;
    constexpr PositionKey FIRST_GROWN_KEY =
      0x1000000000000000ULL;

    PositionHistory history{KEY_A};
    const std::size_t initial_capacity =
      history.capacity();
    const std::size_t push_count =
      initial_capacity + EXTRA_ENTRIES;

    for (std::size_t index = 0;
         index < push_count;
         ++index) {
        history.push(
          FIRST_GROWN_KEY
          + static_cast<PositionKey>(index));
    }

    expect(
      history.size() == push_count + 1,
      "history grows beyond its initial storage capacity");
    expect(history.capacity() >= history.size(),
           "grown capacity contains every live entry");
    expect(
      history.capacity() > initial_capacity,
      "capacity increases after initial storage is exhausted");
    const std::size_t grown_capacity =
      history.capacity();
    expect(
      history.current_key()
        == FIRST_GROWN_KEY
             + static_cast<PositionKey>(push_count - 1),
      "growth preserves the most recently pushed key");
    expect(history.count(KEY_A) == 1,
           "growth preserves the initial key");
    expect(
      history.count(FIRST_GROWN_KEY) == 1,
      "growth preserves the first appended key");

    for (std::size_t remaining = push_count;
         remaining > 0;
         --remaining) {
        const PositionKey expected =
          FIRST_GROWN_KEY
          + static_cast<PositionKey>(remaining - 1);
        expect(history.current_key() == expected,
               "grown entries pop in reverse order");
        history.pop(expected);
    }

    expect(history.size() == 1,
           "unwinding grown storage retains only the seed");
    expect(history.current_key() == KEY_A,
           "unwinding grown storage restores the seed");
    expect(history.current_count() == 1,
           "the restored seed occurs once");
    expect(history.capacity() == grown_capacity,
           "popping entries retains grown storage");

    history.push(KEY_E);
    expect(history.current_key() == KEY_E
             && history.size() == 2,
           "grown storage remains reusable after full unwind");
    expect(history.capacity() == grown_capacity,
           "reusing grown storage does not allocate");

    history.reset(KEY_B);
    expect(history.size() == 1
             && history.current_key() == KEY_B,
           "reset reuses grown storage for a new seed");
    expect(history.capacity() == grown_capacity,
           "reset retains grown storage");
}

void test_copying_and_branching() {
    PositionHistory original{KEY_A};
    original.push(KEY_B);
    original.push(KEY_A);
    original.push(KEY_C);

    PositionHistory copied = original;
    expect(copied.size() == original.size(),
           "copy construction preserves size");
    expect(
      copied.capacity()
        >= PositionHistory::INITIAL_RESERVE,
      "copy construction establishes at least the initial reserve");
    expect(copied.current_key() == KEY_C,
           "copy construction preserves the current key");
    expect(copied.count(KEY_A) == 2
             && copied.count(KEY_B) == 1
             && copied.count(KEY_C) == 1,
           "copy construction preserves occurrence counts");

    copied.pop(KEY_C);
    copied.push(KEY_D);
    expect(copied.current_key() == KEY_D,
           "a copied history accepts a replacement branch");
    expect(copied.count(KEY_C) == 0,
           "a popped branch is excluded from copied counts");
    expect(copied.count(KEY_D) == 1,
           "the replacement branch is counted");
    expect(original.current_key() == KEY_C
             && original.count(KEY_C) == 1
             && original.count(KEY_D) == 0,
           "mutating a copy does not change the source");

    PositionHistory assigned{KEY_E};
    assigned = original;
    expect(assigned.size() == original.size(),
           "copy assignment preserves size");
    expect(
      assigned.capacity()
        >= PositionHistory::INITIAL_RESERVE,
      "copy assignment establishes at least the initial reserve");
    expect(assigned.current_key() == KEY_C,
           "copy assignment preserves the current key");
    expect(assigned.count(KEY_A) == 2,
           "copy assignment preserves repeated keys");

    const std::size_t assigned_size = assigned.size();
    const std::size_t assigned_capacity =
      assigned.capacity();
    copy_assign(assigned, assigned);
    expect(assigned.size() == assigned_size
             && assigned.capacity() == assigned_capacity
             && assigned.current_key() == KEY_C
             && assigned.count(KEY_A) == 2,
           "self-assignment preserves contents and storage");

    assigned.reset(KEY_D);
    expect(assigned.size() == 1
             && assigned.current_key() == KEY_D,
           "an assigned copy resets independently");
    expect(original.size() == 4
             && original.current_key() == KEY_C,
           "resetting an assigned copy does not change the source");
}

void test_grown_history_copying() {
    constexpr PositionKey FIRST_KEY =
      0x6000000000000000ULL;

    PositionHistory source{KEY_A};
    const std::size_t initial_capacity =
      source.capacity();

    for (std::size_t index = 0;
         index < initial_capacity;
         ++index) {
        source.push(
          FIRST_KEY
          + static_cast<PositionKey>(index));
    }

    expect(source.capacity() > initial_capacity,
           "the copy source grows beyond its initial storage");

    source.pop(
      FIRST_KEY
      + static_cast<PositionKey>(initial_capacity - 1));
    const std::size_t source_capacity =
      source.capacity();
    const std::size_t source_size = source.size();
    const PositionKey source_current =
      source.current_key();

    PositionHistory copied{source};
    expect(copied.size() == source_size
             && copied.current_key() == source_current,
           "copy construction preserves a grown history");
    expect(copied.capacity() >= source_capacity,
           "copy construction preserves grown reserve");

    const std::size_t copied_capacity =
      copied.capacity();
    copied.push(KEY_E);
    expect(copied.capacity() == copied_capacity,
           "a grown copy has storage for the next entry");
    expect(source.size() == source_size
             && source.current_key() == source_current,
           "extending a grown copy does not change its source");

    PositionHistory assigned{KEY_D};
    assigned = source;
    expect(assigned.size() == source_size
             && assigned.current_key() == source_current,
           "copy assignment preserves a grown history");
    expect(assigned.capacity() >= source_capacity,
           "copy assignment preserves grown reserve");

    const std::size_t assigned_capacity =
      assigned.capacity();
    assigned.push(KEY_E);
    expect(assigned.capacity() == assigned_capacity,
           "a grown assigned copy has storage for the next entry");
}

void test_moving_and_reactivation() {
    PositionHistory source{KEY_A};
    source.push(KEY_B);
    source.push(KEY_A);
    source.push(KEY_C);

    PositionHistory moved{std::move(source)};
    expect(moved.size() == 4
             && moved.current_key() == KEY_C
             && moved.count(KEY_A) == 2,
           "move construction transfers the recorded history");

    source.reset(KEY_D);
    expect(source.size() == 1
             && source.current_key() == KEY_D,
           "reset reactivates a move-construction source");
    expect(
      source.capacity()
        >= PositionHistory::INITIAL_RESERVE,
      "reactivation restores the initial reserve");

    PositionHistory target{KEY_E};
    target = std::move(moved);
    expect(target.size() == 4
             && target.current_key() == KEY_C
             && target.count(KEY_A) == 2,
           "move assignment transfers the recorded history");

    moved.reset(KEY_B);
    expect(moved.size() == 1
             && moved.current_key() == KEY_B,
           "reset reactivates a move-assignment source");
    expect(
      moved.capacity()
        >= PositionHistory::INITIAL_RESERVE,
      "move-assignment source reactivation restores the initial reserve");
}

void test_complete_state_distinctions() {
    const Position base = make_starting_position();
    const Position identical = make_starting_position();

    Position different_side = base;
    different_side.set_side_to_move(BLUE);

    Position different_castling = base;
    different_castling.clear_castling_right(
      RED, CastlingSide::KING_SIDE);

    constexpr Square first_target =
      make_square(FILE_H, RANK_3);
    constexpr Square second_target =
      make_square(FILE_I, RANK_3);

    Position red_target = base;
    red_target.set_en_passant_square(
      RED, first_target);

    Position blue_target = base;
    blue_target.set_en_passant_square(
      BLUE, first_target);

    Position other_target = base;
    other_target.set_en_passant_square(
      RED, second_target);

    const std::array<PositionKey, 6> distinct_keys = {
      base.key(),
      different_side.key(),
      different_castling.key(),
      red_target.key(),
      blue_target.key(),
      other_target.key(),
    };

    expect(identical.key() == base.key(),
           "independently constructed identical states share a key");
    expect(all_distinct(distinct_keys),
           "stored side, castling, and en-passant states alter the key");

    PositionHistory history{base.key()};
    history.push(identical.key());
    expect(history.current_count() == 2
             && history.is_twofold(),
           "identical complete states count as a repetition");

    history.push(different_side.key());
    expect(history.current_count() == 1,
           "a different side to move has a distinct key");
    history.push(different_castling.key());
    expect(history.current_count() == 1,
           "different castling rights have a distinct key");
    history.push(red_target.key());
    expect(history.current_count() == 1,
           "a stored en-passant target changes the key");
    history.push(blue_target.key());
    expect(history.current_count() == 1,
           "the stored en-passant owner changes the key");
    history.push(other_target.key());
    expect(history.current_count() == 1,
           "the stored en-passant target square changes the key");
    expect(history.count(base.key()) == 2,
           "state distinctions do not change the base count");
}

void test_legal_four_player_cycle_and_unwind() {
    constexpr std::size_t CYCLE_NB = 2;
    constexpr std::size_t PLY_NB =
      CYCLE_NB * KNIGHT_CYCLE.size();

    Position position = make_starting_position();
    const Position initial = position;
    const PositionKey initial_key = position.key();
    PositionHistory history{initial_key};
    std::array<UndoState, PLY_NB> undo_states{};

    for (std::size_t ply = 0; ply < PLY_NB; ++ply) {
        const Move move =
          KNIGHT_CYCLE[ply % KNIGHT_CYCLE.size()];
        MoveList legal_moves;
        generate_legal_moves(position, legal_moves);

        const bool generated =
          contains_move(legal_moves, move);
        expect(generated,
               "every knight-cycle move is generated legal");
        if (!generated)
            return;

        do_move(position, move, undo_states[ply]);
        history.push(position.key());

        expect(position.key() == position.recompute_key(),
               "cycle move preserves the cached-key invariant");
        expect(history.current_key() == position.key(),
               "push synchronizes history with the moved position");
        expect(history.size() == ply + 2,
               "cycle pushes one history entry per ply");

        const std::size_t completed_cycles =
          (ply + 1) / KNIGHT_CYCLE.size();
        expect(
          history.count(initial_key)
            == 1 + completed_cycles,
          "the initial key recurs only at completed cycles");

        if (ply + 1 == KNIGHT_CYCLE.size()) {
            expect(positions_equal(position, initial),
                   "one knight cycle restores the complete position");
            expect(history.current_count() == 2,
                   "one cycle creates the second root occurrence");
            expect(history.is_twofold(),
                   "one cycle creates a twofold repetition");
            expect(!history.is_threefold(),
                   "one cycle does not create a threefold repetition");
        }
    }

    expect(positions_equal(position, initial),
           "two knight cycles restore the complete position");
    expect(history.current_count() == 3,
           "two cycles create the third root occurrence");
    expect(history.is_twofold(),
           "three occurrences satisfy twofold detection");
    expect(history.is_threefold(),
           "two cycles create a threefold repetition");

    for (std::size_t ply = PLY_NB;
         ply-- > 0;) {
        const PositionKey child_key = position.key();
        history.pop(child_key);
        undo_move(
          position,
          KNIGHT_CYCLE[ply % KNIGHT_CYCLE.size()],
          undo_states[ply]);

        expect(position.key() == position.recompute_key(),
               "cycle undo preserves the cached-key invariant");
        expect(history.current_key() == position.key(),
               "pop synchronizes history with the restored position");
        expect(history.size() == ply + 1,
               "cycle undo removes one history entry per ply");
    }

    expect(positions_equal(position, initial),
           "full cycle unwind restores the initial position");
    expect(history.size() == 1
             && history.current_key() == initial_key,
           "full cycle unwind restores the seeded history");
    expect(history.current_count() == 1,
           "full cycle unwind removes repeated child entries");
    expect(!history.is_twofold()
             && !history.is_threefold(),
           "the fully unwound root is not repeated");
}

void test_generation_and_perft_noninterference() {
    Position position = make_starting_position();
    const Position initial = position;
    PositionHistory history{position.key()};

    const std::size_t history_size = history.size();
    const PositionKey history_key =
      history.current_key();

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    expect(!legal_moves.empty(),
           "the starting position has legal moves");
    expect(positions_equal(position, initial),
           "legal generation restores the position");
    expect(history.size() == history_size
             && history.current_key() == history_key
             && history.current_count() == 1,
           "legal generation does not alter position history");

    if (!legal_moves.empty()) {
        const bool legal =
          is_legal_move(position, legal_moves[0]);
        expect(legal,
               "a generated legal move remains legal when retested");
        expect(positions_equal(position, initial),
               "legality testing restores the position");
        expect(history.size() == history_size
                 && history.current_key() == history_key,
               "legality testing does not alter position history");
    }

    const std::uint64_t nodes = perft(position, 2);
    expect(nodes != 0,
           "perft visits descendants from the starting position");
    expect(positions_equal(position, initial),
           "perft restores the position");
    expect(history.size() == history_size
             && history.current_key() == history_key
             && history.current_count() == 1,
           "perft does not alter position history");
}

}  // namespace

int main() {
    test_history_context();
    test_seed_reset_and_thresholds();
    test_growth_and_full_unwind();
    test_copying_and_branching();
    test_grown_history_copying();
    test_moving_and_reactivation();
    test_complete_state_distinctions();
    test_legal_four_player_cycle_and_unwind();
    test_generation_and_perft_noninterference();

    if (failures != 0) {
        std::cerr << failures
                  << " history test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "All history tests passed\n";
    return EXIT_SUCCESS;
}
