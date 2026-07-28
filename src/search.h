#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>

#include "quiescence.h"

namespace Mockingbird {

// SearchResult::nodes counts every entered node, including the root and
// quiescence nodes.
struct SearchResult {
    Move best_move = Move::none();
    Score score = DRAW_SCORE;
    std::uint64_t nodes = 0;

    [[nodiscard]] constexpr bool has_move() const noexcept {
        return best_move.is_board_move();
    }

    [[nodiscard]] friend constexpr bool operator==(
      const SearchResult&,
      const SearchResult&) noexcept = default;
};

namespace SearchDetail {

struct NodeResult {
    Score score = DRAW_SCORE;
    Move best_move = Move::none();
};

inline constexpr Score TABLE_MATE_THRESHOLD =
  MATE_SCORE - MAX_SEARCH_PLY;
inline constexpr int LATE_MOVE_REDUCTION = 1;
inline constexpr int LATE_MOVE_MIN_DEPTH = 4;

// Quiet ordinals are zero-based, so ordinal four is the fifth quiet move.
inline constexpr std::size_t LATE_MOVE_MIN_QUIET_ORDINAL = 4;

// Mate scores are stored relative to the current node and reconstructed for
// the probing node's distance from the root.
[[nodiscard]] constexpr Score score_to_table(
  Score score,
  int ply) noexcept {
    assert(ply >= 0);
    assert(ply <= MAX_SEARCH_PLY);

    if (score >= TABLE_MATE_THRESHOLD)
        return score + static_cast<Score>(ply);
    if (score <= -TABLE_MATE_THRESHOLD)
        return score - static_cast<Score>(ply);
    return score;
}

[[nodiscard]] constexpr Score score_from_table(
  Score score,
  int ply) noexcept {
    assert(ply >= 0);
    assert(ply <= MAX_SEARCH_PLY);

    if (score >= TABLE_MATE_THRESHOLD)
        return score - static_cast<Score>(ply);
    if (score <= -TABLE_MATE_THRESHOLD)
        return score + static_cast<Score>(ply);
    return score;
}

[[nodiscard]] constexpr TranspositionBound
classify_bound(
  Score score,
  Score original_alpha,
  Score original_beta) noexcept {
    assert(original_alpha < original_beta);

    if (score <= original_alpha)
        return TranspositionBound::UPPER;
    if (score >= original_beta)
        return TranspositionBound::LOWER;
    return TranspositionBound::EXACT;
}

[[nodiscard]] constexpr Score
pvs_scout_beta(Score alpha) noexcept {
    assert(alpha < INFINITE_SCORE);
    return alpha + Score{1};
}

[[nodiscard]] constexpr bool is_null_window(
  Score alpha,
  Score beta) noexcept {
    assert(alpha < beta);
    return beta == alpha + Score{1};
}

[[nodiscard]] constexpr bool is_mate_score_window(
  Score alpha,
  Score beta) noexcept {
    assert(alpha < beta);
    return alpha <= -TABLE_MATE_THRESHOLD
        || beta >= TABLE_MATE_THRESHOLD;
}

// The one-ply reduction applies at depth four or greater to the fifth or later
// quiet move in a null window. Only ordinary knight, bishop, rook, and queen
// moves with non-positive history are eligible. Checked nodes, mate-score
// windows, killers, and child positions with a checked opposing king return
// zero.
[[nodiscard]] constexpr int late_move_reduction(
  int depth,
  std::size_t quiet_ordinal,
  bool null_window,
  bool checked,
  bool quiet,
  MoveType move_type,
  PieceType moving_piece_type,
  KillerPriority killer_priority,
  HistoryScore history_score,
  bool mate_score_window,
  bool opposing_king_checked) noexcept {
    assert(depth > 0);
    assert(is_ok(move_type));
    assert(is_ok(moving_piece_type));

    if (depth < LATE_MOVE_MIN_DEPTH
        || quiet_ordinal
             < LATE_MOVE_MIN_QUIET_ORDINAL
        || !null_window
        || checked
        || !quiet
        || move_type != MoveType::NORMAL
        || moving_piece_type < KNIGHT
        || moving_piece_type > QUEEN
        || killer_priority != 0
        || history_score > 0
        || mate_score_window
        || opposing_king_checked) {
        return 0;
    }

    return LATE_MOVE_REDUCTION;
}

// Returns whether at least one king belonging to team is currently checked.
[[nodiscard]] constexpr bool team_has_checked_king(
  const Position& position,
  Team team) noexcept {
    assert(is_ok(team));

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        if (team_of(color) == team
            && in_check(position, color)) {
            return true;
        }
    }

    return false;
}

// A reduced result above alpha is not used until the same move completes a
// scout search at the nominal depth.
[[nodiscard]] constexpr bool lmr_verification_required(
  int reduction,
  Score score,
  Score alpha) noexcept {
    assert(reduction >= 0);
    assert(reduction <= LATE_MOVE_REDUCTION);
    return reduction != 0 && score > alpha;
}

// A scout score in the open interval (alpha, beta) raises alpha without
// reaching the caller's beta bound.
[[nodiscard]] constexpr bool
pvs_research_required(
  Score score,
  Score alpha,
  Score beta) noexcept {
    assert(alpha < beta);
    return score > alpha && score < beta;
}

template<bool EnableLateMoveReductions = true, typename State>
[[nodiscard]] inline
std::expected<NodeResult, SearchStopReason>
alpha_beta(
  Position& position,
  PositionHistory& history,
  int depth,
  int ply,
  Score alpha,
  Score beta,
  State& state,
  Move preferred_move = Move::none()) {
    assert(depth >= 0);
    assert(ply >= 0);
    assert(depth + ply <= MAX_SEARCH_DEPTH);
    assert(-INFINITE_SCORE <= alpha);
    assert(alpha < beta);
    assert(beta <= INFINITE_SCORE);
    assert(history.current_key() == position.key());

    if (depth == 0) {
        const auto score =
          quiescence(
            position,
            history,
            ply,
            0,
            alpha,
            beta,
            state);
        if (!score)
            return std::unexpected(
              score.error());

        return NodeResult{
          *score,
          Move::none(),
        };
    }

    const NodeEntry entry = state.enter_node();
    if (!entry)
        return std::unexpected(entry.error());

    const Score original_alpha = alpha;
    const Score original_beta = beta;
    const bool null_window =
      is_null_window(
        original_alpha, original_beta);
    const bool mate_score_window =
      is_mate_score_window(
        original_alpha, original_beta);
    const bool reduction_context =
      EnableLateMoveReductions
      && depth >= LATE_MOVE_MIN_DEPTH
      && null_window
      && !mate_score_window;
    const bool reduction_node_checked =
      reduction_context && in_check(position);

    MoveList legal_moves;
    generate_legal_moves(position, legal_moves);
    const PositionResult position_result =
      terminal_result(position, history, legal_moves);

    assert(position_result.is_valid());
    if (!position_result.is_valid())
        return {};

    if (position_result.is_terminal()) {
        return NodeResult{
          terminal_score(
            position_result,
            team_of(position.side_to_move()),
            ply),
          Move::none(),
        };
    }

    TranspositionTable* const table =
      state.table();
    Move table_move = Move::none();

    if (table) {
        const TranspositionEntry* cached =
          table->probe(
            position.key(),
            history.context());
        table_move =
          cached
              && cached->best_move.is_board_move()
            ? cached->best_move
            : table->best_move(position.key());

        if (cached
            && cached->depth == depth
            && OrderingDetail::contains_move(
                 legal_moves,
                 cached->best_move)) {
            const Score cached_score =
              score_from_table(
                cached->score, ply);

            const bool cutoff =
              cached->bound
                  == TranspositionBound::EXACT
              || (cached->bound
                    == TranspositionBound::LOWER
                  && cached_score >= beta)
              || (cached->bound
                    == TranspositionBound::UPPER
                  && cached_score <= alpha);

            if (cutoff) {
                return NodeResult{
                  cached_score,
                  cached->best_move,
                };
            }
        }
    }

    const Move ordering_move =
      OrderingDetail::contains_move(
        legal_moves, preferred_move)
        ? preferred_move
        : table_move;
    order_moves(
      position,
      legal_moves,
      state.ordering_buffer,
      state.quiet_history,
      state.killer_moves(ply),
      ordering_move);

    Score best_score = -INFINITE_SCORE;
    Move best_move = Move::none();
    std::size_t quiet_count = 0;

    // Strict score comparison preserves the first searched move among moves
    // that receive equal search scores.
    for (std::size_t move_index = 0;
         move_index < legal_moves.size();
         ++move_index) {
        const Move move = legal_moves[move_index];
        const bool quiet =
          !is_tactical_move(position, move);
        const std::size_t quiet_ordinal =
          quiet_count;
        if (quiet)
            ++quiet_count;

        PieceType moving_piece_type =
          NO_PIECE_TYPE;
        KillerPriority killer_priority = 0;
        HistoryScore history_score = 0;
        bool reduction_candidate =
          reduction_context
          && !reduction_node_checked
          && move_index != 0
          && quiet
          && quiet_ordinal
               >= LATE_MOVE_MIN_QUIET_ORDINAL
          && move.type() == MoveType::NORMAL;
        if (reduction_candidate) {
            const Piece moving_piece =
              position.piece_on(move.from());
            moving_piece_type =
              type_of(moving_piece);
            reduction_candidate =
              moving_piece_type >= KNIGHT
              && moving_piece_type <= QUEEN;

            if (reduction_candidate) {
                killer_priority =
                  state.killer_moves(ply).priority(move);
                history_score =
                  state.quiet_history.score(
                    moving_piece, move.to());
            }
        }
        std::expected<NodeResult, SearchStopReason>
          child_result{NodeResult{}};

        {
            ChildState child{position, history, move};
            // Every move advances to the opposing team, so the child score
            // and child window are negated for the parent perspective.
            if (move_index == 0) {
                child_result =
                  alpha_beta<
                    EnableLateMoveReductions>(
                    position,
                    history,
                    depth - 1,
                    ply + 1,
                    -beta,
                    -alpha,
                    state);
            } else {
                const Score scout_beta =
                  pvs_scout_beta(alpha);
                const bool opposing_king_checked =
                  reduction_candidate
                  && killer_priority == 0
                  && history_score <= 0
                  && team_has_checked_king(
                    position,
                    team_of(
                      position.side_to_move()));
                const int reduction =
                  reduction_candidate
                    ? late_move_reduction(
                        depth,
                        quiet_ordinal,
                        null_window,
                        reduction_node_checked,
                        quiet,
                        move.type(),
                        moving_piece_type,
                        killer_priority,
                        history_score,
                        mate_score_window,
                        opposing_king_checked)
                    : 0;
                assert(reduction >= 0);
                assert(reduction <= depth - 1);

                child_result =
                  alpha_beta<
                    EnableLateMoveReductions>(
                    position,
                    history,
                    depth - 1 - reduction,
                    ply + 1,
                    -scout_beta,
                    -alpha,
                    state);

                if (child_result
                    && lmr_verification_required(
                         reduction,
                         -child_result->score,
                         alpha)) {
                    child_result =
                      alpha_beta<
                        EnableLateMoveReductions>(
                        position,
                        history,
                        depth - 1,
                        ply + 1,
                        -scout_beta,
                        -alpha,
                        state);
                }

                if (child_result) {
                    const Score scout_score =
                      -child_result->score;
                    if (pvs_research_required(
                          scout_score,
                          alpha,
                          beta)) {
                        child_result =
                          alpha_beta<
                            EnableLateMoveReductions>(
                            position,
                            history,
                            depth - 1,
                            ply + 1,
                            -beta,
                            -alpha,
                            state);
                    }
                }
            }
        }

        if (!child_result)
            return std::unexpected(
              child_result.error());

        const Score candidate_score =
          -child_result->score;

        if (candidate_score > best_score) {
            best_score = candidate_score;
            best_move = move;
        }

        if (candidate_score > alpha)
            alpha = candidate_score;

        if (alpha >= beta) {
            if (quiet) {
                const Piece cutoff_piece =
                  position.piece_on(
                    move.from());
                state.killer_moves(ply).record(move);
                state.quiet_history.reward(
                  cutoff_piece,
                  move.to(),
                  depth);

                // Every earlier move completed before this cutoff. An
                // aliased cutoff entry is not updated in both directions.
                for (std::size_t prior_index = 0;
                     prior_index < move_index;
                     ++prior_index) {
                    const Move prior =
                      legal_moves[prior_index];
                    if (is_tactical_move(
                          position, prior)) {
                        continue;
                    }

                    const Piece prior_piece =
                      position.piece_on(
                        prior.from());
                    if (prior_piece == cutoff_piece
                        && prior.to() == move.to()) {
                        continue;
                    }

                    state.quiet_history.penalize(
                      prior_piece,
                      prior.to(),
                      depth);
                }
            }
            break;
        }
    }

    assert(is_ok(best_move));

    if (table) {
        table->store(
          position.key(),
          history.context(),
          depth,
          score_to_table(best_score, ply),
          classify_bound(
            best_score,
            original_alpha,
            original_beta),
          best_move);
    }

    return NodeResult{best_score, best_move};
}

[[nodiscard]] inline bool valid_search_input(
  const Position& position,
  const PositionHistory& history,
  int depth) noexcept {
    const bool valid_depth =
      depth >= 0 && depth <= MAX_SEARCH_DEPTH;
    assert(valid_depth);
    if (!valid_depth)
        return false;

    const bool matching_history =
      history.current_key() == position.key();
    assert(matching_history);
    return matching_history;
}

[[nodiscard]] inline SearchResult run_search(
  Position& position,
  const PositionHistory& history,
  int depth,
  TranspositionTable* table) {
    PositionHistory search_history{history};
    SearchState state{
      UnlimitedBudget{},
      table};
    const auto result =
      alpha_beta(
        position,
        search_history,
        depth,
        0,
        -INFINITE_SCORE,
        INFINITE_SCORE,
        state);

    assert(result.has_value());
    if (!result)
        return {};

    return {
      result->best_move,
      result->score,
      state.nodes,
    };
}

}  // namespace SearchDetail

// Returns the nominal fixed-depth negamax result. The first ordered move at
// each main node uses the caller's complete window. Later moves use a null
// window and re-search when they raise alpha without reaching beta. Eligible
// late quiet moves at null-window nodes first use a search reduced by one ply.
// A reduced result that raises alpha is verified at full depth before it can
// update the node. At the nominal horizon, quiescence continues captures,
// promotions, and every legal check evasion. Terminal positions end a line
// before evaluation. The position is restored before return, and history is
// read-only.
// Preconditions:
// - depth is in the inclusive range 0..MAX_SEARCH_DEPTH;
// - history.current_key() equals position.key();
// - the position has a result-valid king layout.
[[nodiscard]] inline SearchResult search(
  Position& position,
  const PositionHistory& history,
  int depth) {
    if (!SearchDetail::valid_search_input(
          position, history, depth)) {
        return {};
    }

    return SearchDetail::run_search(
      position, history, depth, nullptr);
}

// Uses caller-owned table storage and advances its generation once for this
// root search. Table contents remain available to later calls.
[[nodiscard]] inline SearchResult search(
  Position& position,
  const PositionHistory& history,
  int depth,
  TranspositionTable& table) {
    if (!SearchDetail::valid_search_input(
          position, history, depth)) {
        return {};
    }

    table.new_search();

    return SearchDetail::run_search(
      position, history, depth, &table);
}

static_assert(!SearchResult{}.has_move());
static_assert(
  SearchDetail::LATE_MOVE_REDUCTION > 0);
static_assert(
  SearchDetail::LATE_MOVE_MIN_DEPTH
  > SearchDetail::LATE_MOVE_REDUCTION);
static_assert(
  SearchDetail::TABLE_MATE_THRESHOLD
  > MAX_MATERIAL_SCORE);
static_assert(
  MATE_SCORE + MAX_SEARCH_PLY
  < INFINITE_SCORE);

}  // namespace Mockingbird
