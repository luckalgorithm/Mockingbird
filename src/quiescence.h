#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <utility>

#include "budget.h"
#include "evaluate.h"
#include "ordering.h"
#include "result.h"
#include "transposition.h"
#include "transition.h"

namespace Mockingbird {

inline constexpr Score DRAW_SCORE = 0;
inline constexpr int MAX_SEARCH_DEPTH = 256;
inline constexpr int MAX_QUIESCENCE_PLY = 16;
inline constexpr int MAX_SEARCH_PLY =
  MAX_SEARCH_DEPTH + MAX_QUIESCENCE_PLY;

// The score ranges leave space between material evaluation, mate scores, and
// the search-window bounds.
inline constexpr Score MATE_SCORE = 1'000'000;
inline constexpr Score INFINITE_SCORE = 2'000'000;

namespace SearchDetail {

template<typename Budget>
class BasicSearchState {
  public:
    constexpr BasicSearchState() noexcept = default;

    constexpr explicit BasicSearchState(
      Budget budget,
      TranspositionTable* table = nullptr) noexcept
        : budget_(std::move(budget)),
          table_(table) {}

    [[nodiscard]] NodeEntry enter_node() noexcept {
        return budget_.enter_node(nodes);
    }

    [[nodiscard]] constexpr const Budget&
    budget() const noexcept {
        return budget_;
    }

    [[nodiscard]] constexpr TranspositionTable*
    table() const noexcept {
        return table_;
    }

    // Main-search nodes with positive depth use ply values from zero through
    // MAX_SEARCH_DEPTH - 1.
    // Precondition: ply is in that inclusive range.
    [[nodiscard]] constexpr KillerMoves& killer_moves(
      int ply) noexcept {
        assert(ply >= 0);
        assert(ply < MAX_SEARCH_DEPTH);
        return killer_moves_[
          static_cast<std::size_t>(ply)];
    }

    // Main-search nodes with positive depth use ply values from zero through
    // MAX_SEARCH_DEPTH - 1.
    // Precondition: ply is in that inclusive range.
    [[nodiscard]] constexpr const KillerMoves&
    killer_moves(int ply) const noexcept {
        assert(ply >= 0);
        assert(ply < MAX_SEARCH_DEPTH);
        return killer_moves_[
          static_cast<std::size_t>(ply)];
    }

    std::uint64_t nodes = 0;
    MoveOrderingBuffer ordering_buffer;
    QuietHistory quiet_history;

  private:
    std::array<
      KillerMoves,
      static_cast<std::size_t>(MAX_SEARCH_DEPTH)>
      killer_moves_{};
    Budget budget_;
    TranspositionTable* table_ = nullptr;
};

using SearchState =
  BasicSearchState<UnlimitedBudget>;
using LimitedSearchState =
  BasicSearchState<SearchBudget>;

// A constructed ChildState owns one applied move and its history entry.
// Destruction removes the entry before undoing the move.
class ChildState {
  public:
    ChildState(
      Position& position,
      PositionHistory& history,
      Move move)
        : position_(position),
          history_(history),
          move_(move) {
        do_move(position_, move_, undo_);
        child_key_ = position_.key();

        try {
            history_.push(child_key_);
        } catch (...) {
            undo_move(position_, move_, undo_);
            throw;
        }
    }

    ChildState(const ChildState&) = delete;
    ChildState& operator=(const ChildState&) = delete;
    ChildState(ChildState&&) = delete;
    ChildState& operator=(ChildState&&) = delete;

    ~ChildState() noexcept {
        history_.pop(child_key_);
        undo_move(position_, move_, undo_);
    }

  private:
    Position& position_;
    PositionHistory& history_;
    Move move_;
    UndoState undo_;
    PositionKey child_key_ = 0;
};

// Returns a terminal score from the team-to-move perspective.
[[nodiscard]] constexpr Score terminal_score(
  const PositionResult& result,
  Team perspective,
  int ply) noexcept {
    assert(result.is_terminal());
    assert(is_ok(perspective));
    assert(ply >= 0);
    assert(ply <= MAX_SEARCH_PLY);

    const auto winner = result.winning_team();
    if (!winner)
        return DRAW_SCORE;

    const Score win_score =
      MATE_SCORE - static_cast<Score>(ply);
    return *winner == perspective
        ? win_score
        : -win_score;
}

// Searches legal captures and promotions until the position is quiet. Checked
// nodes search every legal evasion and do not use stand-pat evaluation.
// Terminal positions are classified before static evaluation. The fixed
// quiescence-ply bound ends sequences that reach the bound.
template<typename State>
[[nodiscard]] inline
std::expected<Score, SearchStopReason>
quiescence(
  Position& position,
  PositionHistory& history,
  int ply,
  int quiescence_ply,
  Score alpha,
  Score beta,
  State& state) {
    assert(ply >= 0);
    assert(quiescence_ply >= 0);
    assert(quiescence_ply <= MAX_QUIESCENCE_PLY);
    assert(ply >= quiescence_ply);
    assert(ply - quiescence_ply <= MAX_SEARCH_DEPTH);
    assert(ply <= MAX_SEARCH_PLY);
    assert(-INFINITE_SCORE <= alpha);
    assert(alpha < beta);
    assert(beta <= INFINITE_SCORE);
    assert(history.current_key() == position.key());

    const NodeEntry entry = state.enter_node();
    if (!entry)
        return std::unexpected(entry.error());

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    const PositionResult position_result =
      terminal_result(position, history, legal_moves);

    assert(position_result.is_valid());
    if (!position_result.is_valid())
        return DRAW_SCORE;

    if (position_result.is_terminal()) {
        return terminal_score(
          position_result,
          team_of(position.side_to_move()),
          ply);
    }

    // The bound is checked after terminal classification. At the bound,
    // static evaluation terminates both checked and non-checked lines.
    if (quiescence_ply == MAX_QUIESCENCE_PLY)
        return evaluate(position);

    const bool checked = in_check(position);
    Score best_score = -INFINITE_SCORE;

    if (!checked) {
        best_score = evaluate(position);
        if (best_score >= beta)
            return best_score;
        if (best_score > alpha)
            alpha = best_score;
    }

    // Non-checked nodes exclude quiet moves before sorting. Checked nodes use
    // the complete legal list.
    MoveList tactical_moves;
    MoveList* moves_to_search = &legal_moves;
    if (!checked) {
        for (const Move move : legal_moves) {
            if (is_tactical_move(position, move))
                tactical_moves.push_back(move);
        }
        moves_to_search = &tactical_moves;
    }

    order_moves(
      position,
      *moves_to_search,
      state.ordering_buffer,
      state.quiet_history);

    for (const Move move : *moves_to_search) {
        std::expected<Score, SearchStopReason>
          child_score{DRAW_SCORE};
        {
            ChildState child{position, history, move};
            child_score = quiescence(
              position,
              history,
              ply + 1,
              quiescence_ply + 1,
              -beta,
              -alpha,
              state);
        }

        if (!child_score)
            return std::unexpected(
              child_score.error());

        const Score candidate_score =
          -*child_score;

        if (candidate_score > best_score)
            best_score = candidate_score;

        if (candidate_score > alpha)
            alpha = candidate_score;

        if (alpha >= beta)
            break;
    }

    assert(best_score != -INFINITE_SCORE);
    return best_score;
}

}  // namespace SearchDetail

static_assert(MAX_SEARCH_DEPTH > 0);
static_assert(MAX_QUIESCENCE_PLY > 0);
static_assert(MAX_SEARCH_PLY > MAX_SEARCH_DEPTH);
static_assert(
  MATE_SCORE - MAX_SEARCH_PLY
  > MAX_MATERIAL_SCORE);
static_assert(MATE_SCORE < INFINITE_SCORE);
static_assert(
  INFINITE_SCORE
  < std::numeric_limits<Score>::max());
static_assert(
  -INFINITE_SCORE
  > std::numeric_limits<Score>::lowest());

}  // namespace Mockingbird
