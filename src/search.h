#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>

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
    bool repetition_sensitive = false;
};

inline constexpr int LATE_MOVE_REDUCTION = 1;
inline constexpr int LATE_MOVE_MIN_DEPTH = 2;

// Quiet ordinals are zero-based, so ordinal one is the second quiet move.
inline constexpr std::size_t LATE_MOVE_MIN_QUIET_ORDINAL = 1;
inline constexpr std::size_t LATE_MOVE_DEEP_QUIET_ORDINAL = 8;
inline constexpr int ADAPTIVE_LATE_MOVE_MIN_DEPTH = 4;
inline constexpr int ADAPTIVE_LATE_MOVE_DEEP_DEPTH = 6;
inline constexpr int ADAPTIVE_LATE_MOVE_VERY_DEEP_DEPTH = 8;
inline constexpr std::size_t ADAPTIVE_LATE_MOVE_EARLY_ORDINAL = 2;
inline constexpr std::size_t ADAPTIVE_LATE_MOVE_MIDDLE_ORDINAL = 4;
inline constexpr int TACTICAL_LATE_MOVE_MIN_DEPTH = 3;
inline constexpr int NONPV_FIRST_MOVE_REDUCTION_MIN_DEPTH = 4;
inline constexpr int NONPV_FIRST_MOVE_SECOND_REDUCTION_DEPTH = 6;

inline constexpr int REVERSE_FUTILITY_MAX_DEPTH = 5;
inline constexpr Score REVERSE_FUTILITY_BASE_MARGIN =
  PAWN_VALUE;
inline constexpr Score REVERSE_FUTILITY_DEPTH_MARGIN =
  Score{2} * PAWN_VALUE;
inline constexpr int LATE_MOVE_FUTILITY_DEPTH = 1;
inline constexpr Score LATE_MOVE_FUTILITY_MARGIN =
  PAWN_VALUE;
inline constexpr int LATE_MOVE_PRUNING_MAX_DEPTH = 8;
inline constexpr int LATE_MOVE_PRUNING_MIN_DEPTH = 3;
inline constexpr int PARENT_FUTILITY_MAX_DEPTH = 32;
inline constexpr int PARENT_FUTILITY_MAX_REDUCED_DEPTH = 12;
inline constexpr Score PARENT_FUTILITY_BASE_MARGIN = 50;
inline constexpr Score PARENT_FUTILITY_DEPTH_MARGIN = 120;
inline constexpr int CAPTURE_FUTILITY_MAX_CHILD_DEPTH = 6;
inline constexpr Score CAPTURE_FUTILITY_BASE_MARGIN = 218;
inline constexpr Score CAPTURE_FUTILITY_DEPTH_MARGIN = 223;
inline constexpr int MAIN_SEARCH_EXCHANGE_MAX_DEPTH = 1;
inline constexpr Score MAIN_SEARCH_EXCHANGE_DEPTH_MARGIN =
  Score{2} * PAWN_VALUE;
inline constexpr std::size_t
  MAX_MAIN_SEARCH_EXCHANGE_NODES = 64;
inline constexpr int NULL_MOVE_MIN_DEPTH = 4;
inline constexpr int NULL_MOVE_BASE_TOTAL_REDUCTION = 7;
inline constexpr int NULL_MOVE_DEPTH_DIVISOR = 3;
inline constexpr int NULL_MOVE_MIN_CHILD_DEPTH = 0;

struct SearchWindowBounds {
    Score alpha = -INFINITE_SCORE;
    Score beta = INFINITE_SCORE;

    [[nodiscard]] constexpr bool cutoff() const noexcept {
        return alpha >= beta;
    }

    [[nodiscard]] friend constexpr bool operator==(
      const SearchWindowBounds&,
      const SearchWindowBounds&) noexcept = default;
};

[[nodiscard]] constexpr Score
pvs_scout_beta(Score alpha) noexcept {
    assert(alpha < INFINITE_SCORE);
    return alpha + Score{1};
}

// The first ordered move establishes a score with the node's full window.
// Every later move starts with a PVS scout regardless of the node's incoming
// window.
[[nodiscard]] constexpr bool is_pvs_scout_move(
  std::size_t move_index) noexcept {
    return move_index != 0;
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
    const bool finite_losing_mate_bound =
      alpha > -INFINITE_SCORE
      && alpha <= -TABLE_MATE_THRESHOLD;
    const bool finite_winning_mate_bound =
      beta < INFINITE_SCORE
      && beta >= TABLE_MATE_THRESHOLD;
    // A broad window spanning both mate bands is the finite equivalent of a
    // full window after mate-distance tightening. A mate-sensitive window has
    // a bound in exactly one mate band.
    return finite_losing_mate_bound
        != finite_winning_mate_bound;
}

// A non-terminal node cannot produce a mate score closer than its next ply.
// Tightening both bounds preserves all attainable scores and can prove a
// cutoff without searching a move.
[[nodiscard]] constexpr SearchWindowBounds
mate_distance_window(
  Score alpha,
  Score beta,
  int ply) noexcept {
    assert(-INFINITE_SCORE <= alpha);
    assert(alpha < beta);
    assert(beta <= INFINITE_SCORE);
    assert(ply >= 0);
    assert(ply < MAX_SEARCH_PLY);

    const Score shortest_loss =
      -MATE_SCORE + static_cast<Score>(ply + 1);
    const Score shortest_win =
      MATE_SCORE - static_cast<Score>(ply + 1);

    if (alpha < shortest_loss)
        alpha = shortest_loss;
    if (beta > shortest_win)
        beta = shortest_win;

    return {alpha, beta};
}

[[nodiscard]] constexpr int integer_log2(
  std::size_t value) noexcept {
    assert(value > 0);

    int result = 0;
    while (value > 1) {
        value >>= 1;
        ++result;
    }
    return result;
}

// Null-window nodes reduce increasingly late quiet moves more deeply. Later
// killer and positive-history quiets receive only the minimum reduction. The
// nominal child depth remains non-negative, and every move that raises alpha
// is verified at its unreduced depth before its score is used.
[[nodiscard]] constexpr int adaptive_late_move_reduction(
  int depth,
  std::size_t quiet_ordinal,
  bool scout_search,
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
        || (quiet
            && quiet_ordinal < LATE_MOVE_MIN_QUIET_ORDINAL)
        || !scout_search
        || checked
        || move_type != MoveType::NORMAL
        || moving_piece_type < PAWN
        || moving_piece_type > QUEEN
        || !quiet
        || mate_score_window
        || opposing_king_checked) {
        return 0;
    }

    if (killer_priority != 0 || history_score > 0)
        return LATE_MOVE_REDUCTION;

    int reduction = LATE_MOVE_REDUCTION;

    if (depth >= ADAPTIVE_LATE_MOVE_MIN_DEPTH
        && quiet_ordinal >= ADAPTIVE_LATE_MOVE_EARLY_ORDINAL)
        ++reduction;
    if (depth >= ADAPTIVE_LATE_MOVE_DEEP_DEPTH
        && quiet_ordinal >= ADAPTIVE_LATE_MOVE_MIDDLE_ORDINAL)
        ++reduction;
    if (depth >= ADAPTIVE_LATE_MOVE_VERY_DEEP_DEPTH
        && quiet_ordinal >= LATE_MOVE_DEEP_QUIET_ORDINAL)
        ++reduction;
    if (history_score < 0
        && depth >= ADAPTIVE_LATE_MOVE_MIN_DEPTH)
        ++reduction;

    const int logarithmic_scale =
      integer_log2(static_cast<std::size_t>(depth))
      * integer_log2(quiet_ordinal + 1);
    reduction += logarithmic_scale / 3;

    const int maximum = depth > 2 ? depth - 2 : depth - 1;
    return reduction < maximum ? reduction : maximum;
}

// A later ordinary capture that bounded exchange analysis proves locally
// losing may be searched one ply shallower. Checks, recaptures, repeated
// histories, and mate-score windows retain nominal depth. Terminal results
// are independent of the requested depth and remain exact.
[[nodiscard]] constexpr bool tactical_late_move_reduction_allowed(
  int depth,
  bool scout_search,
  bool team_checked,
  MoveType move_type,
  bool promotion,
  PieceType captured_piece_type,
  bool proven_below_zero,
  bool repetition_sensitive,
  bool recapture,
  bool opposing_king_checked,
  bool mate_score_window) noexcept {
    assert(depth > 0);
    assert(is_ok(move_type));
    assert(is_ok(captured_piece_type));

    return depth >= TACTICAL_LATE_MOVE_MIN_DEPTH
        && scout_search
        && !team_checked
        && move_type == MoveType::NORMAL
        && !promotion
        && captured_piece_type >= PAWN
        && captured_piece_type <= QUEEN
        && proven_below_zero
        && !repetition_sensitive
        && !recapture
        && !opposing_king_checked
        && !mate_score_window;
}

// A non-PV node without a preferred move may search its first ordinary move
// one ply shallower, or two plies shallower at deeper nodes. Tactical special
// moves, recaptures, checks, and terminal children retain their nominal depth.
[[nodiscard]] constexpr bool
nonpv_first_move_reduction_allowed(
  int depth,
  int ply,
  bool null_window,
  bool team_checked,
  bool preferred_move_available,
  MoveType move_type,
  bool promotion,
  bool opposing_king_capture,
  bool recapture,
  bool opposing_king_checked,
  bool child_terminal,
  bool mate_score_window) noexcept {
    assert(depth > 0);
    assert(ply >= 0);
    assert(is_ok(move_type));

    return depth >= NONPV_FIRST_MOVE_REDUCTION_MIN_DEPTH
        && ply > 0
        && null_window
        && !team_checked
        && !preferred_move_available
        && move_type == MoveType::NORMAL
        && !promotion
        && !opposing_king_capture
        && !recapture
        && !opposing_king_checked
        && !child_terminal
        && !mate_score_window;
}

// Move-count pruning applies only to late quiet scout moves. Depth-one and
// depth-two nodes use it only below the root in an incoming null window. The
// quadratic threshold grows rapidly enough that the rule becomes inactive at
// deep nodes unless a position has unusually high mobility.
[[nodiscard]] constexpr bool late_move_count_pruning_allowed(
  int depth,
  int ply,
  bool null_window,
  std::size_t quiet_ordinal,
  bool team_checked,
  bool quiet,
  MoveType move_type,
  PieceType moving_piece_type,
  KillerPriority killer_priority,
  HistoryScore history_score,
  bool mate_score_window,
  bool opposing_king_checked,
  bool child_terminal) noexcept {
    assert(depth > 0);
    assert(ply >= 0);
    assert(is_ok(move_type));
    assert(is_ok(moving_piece_type));

    const std::size_t threshold =
      depth == 1
        ? std::size_t{4}
        : static_cast<std::size_t>(
            (3 + depth * depth) / 2);
    const bool eligible_depth =
      (depth >= LATE_MOVE_PRUNING_MIN_DEPTH
       && depth <= LATE_MOVE_PRUNING_MAX_DEPTH)
      || (depth <= 2 && ply > 0 && null_window);
    return eligible_depth
        && quiet_ordinal >= threshold
        && !team_checked
        && quiet
        && move_type == MoveType::NORMAL
        && moving_piece_type >= PAWN
        && moving_piece_type <= QUEEN
        && killer_priority == 0
        && history_score <= 0
        && !mate_score_window
        && !opposing_king_checked
        && !child_terminal;
}

// Parent futility omits a late quiet move when the static score plus a margin
// based on its reduced search depth cannot raise alpha. Tactical moves,
// checking moves, terminal children, successful history moves, and mate-score
// windows remain searchable.
[[nodiscard]] constexpr bool parent_futility_pruning_allowed(
  int depth,
  int reduced_child_depth,
  bool team_checked,
  bool quiet,
  MoveType move_type,
  PieceType moving_piece_type,
  KillerPriority killer_priority,
  HistoryScore history_score,
  bool mate_score_window,
  bool opposing_king_checked,
  bool child_terminal,
  Score static_score,
  Score alpha) noexcept {
    assert(depth > 0);
    assert(reduced_child_depth >= 0);
    assert(is_ok(move_type));
    assert(is_ok(moving_piece_type));

    if (depth > PARENT_FUTILITY_MAX_DEPTH
        || reduced_child_depth
             > PARENT_FUTILITY_MAX_REDUCED_DEPTH
        || team_checked
        || !quiet
        || move_type != MoveType::NORMAL
        || moving_piece_type < PAWN
        || moving_piece_type > QUEEN
        || killer_priority != 0
        || history_score > 0
        || mate_score_window
        || opposing_king_checked
        || child_terminal) {
        return false;
    }

    const std::int64_t margin =
      static_cast<std::int64_t>(
        PARENT_FUTILITY_BASE_MARGIN)
      + static_cast<std::int64_t>(
          PARENT_FUTILITY_DEPTH_MARGIN)
          * reduced_child_depth;
    return static_cast<std::int64_t>(static_score)
             + margin
           <= static_cast<std::int64_t>(alpha);
}

// Capture futility omits a shallow ordinary capture when the position score,
// captured material, and a child-depth margin cannot reach alpha. Checks,
// terminal children, promotions, en passant, and king captures are retained.
[[nodiscard]] constexpr bool
capture_futility_pruning_allowed(
  int child_depth,
  bool scout_move,
  bool team_checked,
  MoveType move_type,
  bool promotion,
  PieceType captured_piece_type,
  bool next_color_checked,
  bool opposing_king_checked,
  bool child_terminal,
  bool mate_score_window,
  Score static_score,
  Score alpha) noexcept {
    assert(child_depth >= 0);
    assert(is_ok(move_type));
    assert(is_ok(captured_piece_type));

    if (child_depth > CAPTURE_FUTILITY_MAX_CHILD_DEPTH
        || !scout_move
        || team_checked
        || move_type != MoveType::NORMAL
        || promotion
        || captured_piece_type < PAWN
        || captured_piece_type > QUEEN
        || next_color_checked
        || opposing_king_checked
        || child_terminal
        || mate_score_window) {
        return false;
    }

    const std::int64_t futility_score =
      static_cast<std::int64_t>(static_score)
      + CAPTURE_FUTILITY_BASE_MARGIN
      + static_cast<std::int64_t>(
          CAPTURE_FUTILITY_DEPTH_MARGIN)
          * child_depth
      + piece_value(captured_piece_type);
    return futility_score
        <= static_cast<std::int64_t>(alpha);
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

// Returns whether color owns a knight, bishop, rook, or queen. Reverse
// futility pruning requires movable non-pawn material from the active player
// to avoid pawn-only zugzwang cases.
[[nodiscard]] constexpr bool color_has_non_pawn_material(
  const Position& position,
  Color color) noexcept {
    assert(is_ok(color));

    const Bitboard color_pieces = position.pieces(color);
    const Bitboard non_pawn_material =
      position.pieces(KNIGHT)
      | position.pieces(BISHOP)
      | position.pieces(ROOK)
      | position.pieces(QUEEN);
    return bool(color_pieces & non_pawn_material);
}

[[nodiscard]] constexpr Score reverse_futility_margin(
  int depth) noexcept {
    assert(depth > 0);
    assert(depth <= REVERSE_FUTILITY_MAX_DEPTH);
    return REVERSE_FUTILITY_BASE_MARGIN
         + static_cast<Score>(depth)
             * REVERSE_FUTILITY_DEPTH_MARGIN;
}

// Reverse futility is limited to shallow scout nodes whose team is safe and
// has non-pawn material. The comparison uses a widened integer to keep window
// sentinels and margins free from overflow.
[[nodiscard]] constexpr bool
reverse_futility_pruning_allowed(
  int depth,
  bool scout_node,
  bool team_checked,
  bool mate_score_window,
  bool has_non_pawn_material,
  Score static_score,
  Score beta) noexcept {
    assert(depth > 0);
    assert(beta <= INFINITE_SCORE);

    if (depth > REVERSE_FUTILITY_MAX_DEPTH
        || !scout_node
        || team_checked
        || mate_score_window
        || !has_non_pawn_material) {
        return false;
    }

    return static_cast<std::int64_t>(static_score)
             - reverse_futility_margin(depth)
           >= static_cast<std::int64_t>(beta);
}

// At depth one, quiescence can stand pat after an ordinary quiet move that
// gives no team check and reaches a non-terminal position. Its parent score
// therefore cannot exceed the negated child static score. The additional
// margin keeps borderline moves in the tree.
[[nodiscard]] constexpr bool
late_move_futility_pruning_allowed(
  int depth,
  std::size_t quiet_ordinal,
  bool team_checked,
  bool quiet,
  MoveType move_type,
  PieceType moving_piece_type,
  KillerPriority killer_priority,
  HistoryScore history_score,
  bool mate_score_window,
  bool opposing_king_checked,
  bool child_terminal,
  Score parent_static_score,
  Score alpha) noexcept {
    assert(depth > 0);
    assert(is_ok(move_type));
    assert(is_ok(moving_piece_type));

    if (depth != LATE_MOVE_FUTILITY_DEPTH
        || quiet_ordinal
             < LATE_MOVE_MIN_QUIET_ORDINAL
        || team_checked
        || !quiet
        || move_type != MoveType::NORMAL
        || moving_piece_type < KNIGHT
        || moving_piece_type > QUEEN
        || killer_priority != 0
        || history_score > 0
        || mate_score_window
        || opposing_king_checked
        || child_terminal) {
        return false;
    }

    return static_cast<std::int64_t>(parent_static_score)
             + LATE_MOVE_FUTILITY_MARGIN
           <= static_cast<std::int64_t>(alpha);
}

[[nodiscard]] constexpr Score
main_search_exchange_threshold(int depth) noexcept {
    assert(depth > 0);
    assert(depth <= MAIN_SEARCH_EXCHANGE_MAX_DEPTH);
    return -static_cast<Score>(depth)
           * MAIN_SEARCH_EXCHANGE_DEPTH_MARGIN;
}

// Main-search exchange pruning is limited to late ordinary knight and bishop
// captures in shallow, repetition-insensitive scout nodes. Promotions,
// en-passant moves, king captures, checked teams, and mate-score windows
// remain full-width.
[[nodiscard]] constexpr bool
main_search_exchange_pruning_candidate(
  const Position& position,
  Move move,
  int depth,
  bool scout_move,
  bool team_checked,
  bool mate_score_window,
  bool repetition_sensitive) noexcept {
    assert(depth > 0);
    assert(is_ok(move));

    const PieceType moving_piece_type =
      type_of(position.piece_on(move.from()));

    return depth <= MAIN_SEARCH_EXCHANGE_MAX_DEPTH
        && scout_move
        && !team_checked
        && !mate_score_window
        && !repetition_sensitive
        && move.type() == MoveType::NORMAL
        && !move.is_promotion()
        && moving_piece_type >= KNIGHT
        && moving_piece_type <= BISHOP
        && is_capture_move(position, move)
        && !Detail::captures_opposing_king(
             position,
             move,
             position.side_to_move());
}

// Returns true only when a completed bounded exchange traversal proves that
// move is strictly below threshold. Exhausted traversals return UNKNOWN and
// therefore remain searchable.
[[nodiscard]] constexpr bool
main_search_exchange_is_proven_below(
  const Position& position,
  Move move,
  Score threshold,
  std::size_t& remaining_exchange_nodes) noexcept {
    return ExchangeDetail::move_at_least(
             position,
             move,
             threshold,
             &remaining_exchange_nodes)
        == ExchangeDetail::ThresholdResult::BELOW;
}

// A locally losing capture is retained when its child checks either opposing
// king or completes the game.
[[nodiscard]] constexpr bool
main_search_exchange_prunes_child(
  bool proven_below,
  bool opposing_king_checked,
  bool child_terminal) noexcept {
    return proven_below
        && !opposing_king_checked
        && !child_terminal;
}

// A reduced result above alpha is not used until the same move completes a
// scout search at the nominal depth.
[[nodiscard]] constexpr bool lmr_verification_required(
  int reduction,
  Score score,
  Score alpha) noexcept {
    assert(reduction >= 0);
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

[[nodiscard]] constexpr bool is_mate_score(
  Score score) noexcept {
    return score >= TABLE_MATE_THRESHOLD
        || score <= -TABLE_MATE_THRESHOLD;
}

// Null-move pruning is limited to selective scout nodes. Repetition-sensitive
// histories and positions without movable non-pawn material are excluded
// because passing is least reliable in those cases.
[[nodiscard]] constexpr bool null_move_pruning_allowed(
  int depth,
  bool scout_node,
  bool mate_score_window,
  bool team_checked,
  bool has_non_pawn_material,
  bool repetition_sensitive,
  bool null_move_allowed,
  Score static_score,
  Score beta) noexcept {
    assert(depth > 0);
    assert(beta <= INFINITE_SCORE);

    return depth >= NULL_MOVE_MIN_DEPTH
        && scout_node
        && !mate_score_window
        && !team_checked
        && has_non_pawn_material
        && !repetition_sensitive
        && null_move_allowed
        && static_cast<std::int64_t>(static_score)
             >= static_cast<std::int64_t>(beta);
}

// The total null-move reduction grows with depth. Shallow null probes enter
// quiescence; the parent has already retained a legal move for bound returns.
[[nodiscard]] constexpr int null_move_child_depth(
  int depth) noexcept {
    assert(depth >= NULL_MOVE_MIN_DEPTH);

    const int total_reduction =
      NULL_MOVE_BASE_TOTAL_REDUCTION
      + depth / NULL_MOVE_DEPTH_DIVISOR;
    const int child_depth = depth - total_reduction;
    return child_depth > NULL_MOVE_MIN_CHILD_DEPTH
      ? child_depth
      : NULL_MOVE_MIN_CHILD_DEPTH;
}

// A constructed NullMoveState advances one color without moving a piece and
// starts an isolated repetition segment at the passed position. Destruction
// restores both the history segment and every modified position field.
class NullMoveState {
  public:
    NullMoveState(
      Position& position,
      PositionHistory& history)
        : position_(position),
          history_(history),
          passing_color_(position.side_to_move()),
          en_passant_target_(
            position.en_passant_square(
              passing_color_)),
          original_key_(position.key()) {
        position_.clear_en_passant_square(
          passing_color_);
        position_.set_side_to_move(
          next_color(passing_color_));
        passed_key_ = position_.key();

        try {
            history_.push_irreversible(passed_key_);
        } catch (...) {
            restore_position();
            throw;
        }
    }

    NullMoveState(const NullMoveState&) = delete;
    NullMoveState& operator=(const NullMoveState&) = delete;
    NullMoveState(NullMoveState&&) = delete;
    NullMoveState& operator=(NullMoveState&&) = delete;

    ~NullMoveState() noexcept {
        history_.pop(passed_key_);
        restore_position();
    }

  private:
    constexpr void restore_position() noexcept {
        position_.set_side_to_move(passing_color_);
        if (en_passant_target_ == SQ_NONE) {
            position_.clear_en_passant_square(
              passing_color_);
        } else {
            position_.set_en_passant_square(
              passing_color_,
              en_passant_target_);
        }
        assert(position_.key() == original_key_);
    }

    Position& position_;
    PositionHistory& history_;
    Color passing_color_ = RED;
    Square en_passant_target_ = SQ_NONE;
    PositionKey original_key_ = 0;
    PositionKey passed_key_ = 0;
};

template<
  bool EnableLateMoveReductions = true,
  bool EnableForwardPruning = true,
  bool EnableNullMovePruning = true,
  bool EnableExchangePruning = true,
  typename State>
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
  Move preferred_move = Move::none(),
  bool null_move_allowed = true,
  bool transposition_allowed = true,
  std::optional<Score> known_static_score = std::nullopt,
  Square previous_destination = SQ_NONE) {
    assert(depth >= 0);
    assert(ply >= 0);
    assert(depth + ply <= MAX_SEARCH_DEPTH);
    assert(-INFINITE_SCORE <= alpha);
    assert(alpha < beta);
    assert(beta <= INFINITE_SCORE);
    assert(history.current_key() == position.key());
    assert(
      previous_destination == SQ_NONE
      || is_ok(previous_destination));

    if (depth == 0) {
        const auto result =
          quiescence_with_repetition(
            position,
            history,
            ply,
            0,
            alpha,
            beta,
            state,
            transposition_allowed,
            known_static_score,
            previous_destination);
        if (!result)
            return std::unexpected(
              result.error());

        return NodeResult{
          result->score,
          Move::none(),
          result->repetition_sensitive,
        };
    }

    assert(!known_static_score);

    const NodeEntry entry = state.enter_node(ply);
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
    const bool history_repetition_sensitive =
      history.has_repeated_position();

    TranspositionTable* const table =
      transposition_allowed
        ? state.table()
        : nullptr;
    Move table_move = Move::none();
    const TranspositionEntry* preview = nullptr;
    const TranspositionEntry* cached = nullptr;
    const Detail::KingLayout king_layout =
      Detail::king_layout(position);
    Detail::LegalMoveContext node_legal_context{};
    const Detail::LegalMoveContext* legal_context = nullptr;
    if (king_layout == Detail::KingLayout::COMPLETE) {
        node_legal_context =
          Detail::make_legal_move_context(position);
        legal_context = &node_legal_context;
    }

    const auto cached_move_is_legal =
      [&](Move move) noexcept {
        return move.is_board_move()
            && legal_context
            && Detail::is_cached_move_legal(
                 position, move, *legal_context);
    };

    if (table) {
        preview = table->find(position.key());
        if (preview
            && preview->best_move.is_board_move()) {
            table_move = preview->best_move;
        }

        const bool preview_is_eligible =
          !history_repetition_sensitive
          && preview
          && (preview->generation == table->generation()
              || preview->matches_history(
                   history.context()));
        if (preview_is_eligible) {
            const Score cached_score =
              score_from_table(preview->score, ply);
            const bool cutoff =
              transposition_cutoff(
                preview->depth,
                depth,
                preview->bound,
                cached_score,
                alpha,
                beta);
            if (cutoff
                && cached_move_is_legal(
                     preview->best_move)) {
                if (preview->generation
                    != table->generation()) {
                    cached = table->probe(
                      position.key(),
                      history.context());
                    assert(cached == preview);
                }

                return NodeResult{
                  cached_score,
                  preview->best_move,
                  false,
                };
            }
        }
    }

    const Move legal_fallback =
      legal_context
        ? Detail::first_legal_move_with_context(
            position, *legal_context)
        : Move::none();
    const PositionResult position_result =
      Detail::classify_result_with_facts(
        position,
        history,
        king_layout,
        legal_fallback.is_board_move(),
        legal_context && legal_context->checked);

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
          history_repetition_sensitive,
        };
    }

    assert(legal_context);
    assert(legal_fallback.is_board_move());
    const SearchWindowBounds mate_bounds =
      mate_distance_window(alpha, beta, ply);
    if (mate_bounds.cutoff()) {
        return NodeResult{
          mate_bounds.alpha,
          legal_fallback,
          history_repetition_sensitive,
        };
    }
    alpha = mate_bounds.alpha;
    beta = mate_bounds.beta;

    const bool reduction_context =
      EnableLateMoveReductions
      && depth >= LATE_MOVE_MIN_DEPTH
      && !mate_score_window;
    const bool reverse_futility_context =
      EnableForwardPruning
      && depth <= REVERSE_FUTILITY_MAX_DEPTH
      && null_window
      && !mate_score_window;
    const bool late_move_futility_context =
      EnableForwardPruning
      && depth == LATE_MOVE_FUTILITY_DEPTH
      && !mate_score_window;
    const bool parent_futility_context =
      EnableForwardPruning
      && null_window
      && depth <= PARENT_FUTILITY_MAX_DEPTH
      && !mate_score_window;
    const bool capture_futility_context =
      EnableForwardPruning
      && ply > 0
      && depth - 1
           <= CAPTURE_FUTILITY_MAX_CHILD_DEPTH
      && !mate_score_window;
    const bool exchange_pruning_context =
      EnableExchangePruning
      && depth <= MAIN_SEARCH_EXCHANGE_MAX_DEPTH
      && !mate_score_window
      && !history_repetition_sensitive;
    const bool null_move_context =
      EnableNullMovePruning
      && null_move_allowed
      && depth >= NULL_MOVE_MIN_DEPTH
      && null_window
      && !mate_score_window
      && !history_repetition_sensitive;
    const bool node_team_checked =
      (reduction_context
       || reverse_futility_context
       || late_move_futility_context
       || parent_futility_context
       || capture_futility_context
       || exchange_pruning_context
       || null_move_context)
      && team_has_checked_king(
           position,
           team_of(position.side_to_move()));
    const bool futility_context =
      late_move_futility_context
      && !node_team_checked;

    if (table && !history_repetition_sensitive) {
        cached = table->probe(
          position.key(), history.context());
    }

    if (cached
        && cached_move_is_legal(
             cached->best_move)) {
        const Score cached_score =
          score_from_table(
            cached->score, ply);

        const bool cutoff =
          transposition_cutoff(
            cached->depth,
            depth,
            cached->bound,
            cached_score,
            alpha,
            beta);

        if (cutoff) {
            return NodeResult{
              cached_score,
              cached->best_move,
              false,
            };
        }
    }

    const bool preferred_move_available =
      cached_move_is_legal(preferred_move);
    const bool table_move_available =
      table_move == preferred_move
        ? preferred_move_available
        : cached_move_is_legal(table_move);
    const Move ordering_move =
      preferred_move_available
        ? preferred_move
        : table_move_available
            ? table_move
            : Move::none();

    std::optional<Score> node_static_score;
    if ((null_move_context
         || reverse_futility_context
         || parent_futility_context
         || capture_futility_context)
        && !node_team_checked) {
        node_static_score =
          preview && preview->has_static_evaluation()
            ? preview->static_evaluation
            : evaluate(position);
    }

    if (null_move_context
        && !node_team_checked) {
        if (null_move_pruning_allowed(
              depth,
              null_window,
              mate_score_window,
              node_team_checked,
              color_has_non_pawn_material(
                position,
                position.side_to_move()),
              history_repetition_sensitive,
              null_move_allowed,
              *node_static_score,
              beta)) {
            std::expected<
              NodeResult,
              SearchStopReason> null_result{
                NodeResult{}};
            bool null_position_has_legal_move = false;
            {
                NullMoveState null_move{
                  position, history};
                null_position_has_legal_move =
                  first_legal_move(position)
                    .is_board_move();
                if (null_position_has_legal_move) {
                    null_result =
                      alpha_beta<
                        EnableLateMoveReductions,
                        EnableForwardPruning,
                        EnableNullMovePruning,
                        EnableExchangePruning>(
                        position,
                        history,
                        null_move_child_depth(depth),
                        ply + 1,
                        -beta,
                        -beta + Score{1},
                        state,
                        Move::none(),
                        false,
                        false,
                        std::nullopt,
                        SQ_NONE);
                }
            }

            if (null_position_has_legal_move
                && !null_result) {
                return std::unexpected(
                  null_result.error());
            }

            if (null_position_has_legal_move) {
                const Score null_score =
                  -null_result->score;
                const bool null_cutoff =
                  null_score >= beta
                  && !is_mate_score(null_score)
                  && !null_result
                        ->repetition_sensitive;
                if (null_cutoff) {
                    const Move bound_move =
                      ordering_move.is_board_move()
                        ? ordering_move
                        : legal_fallback;
                    return NodeResult{
                      null_score,
                      bound_move,
                      history_repetition_sensitive,
                    };
                }
            }
        }
    }

    if (reverse_futility_context
        && !node_team_checked) {
        if (reverse_futility_pruning_allowed(
              depth,
              null_window,
              node_team_checked,
              mate_score_window,
              color_has_non_pawn_material(
                position,
                position.side_to_move()),
              *node_static_score,
              beta)) {
            const Move bound_move =
              ordering_move.is_board_move()
                ? ordering_move
                : legal_fallback;
            return NodeResult{
              *node_static_score,
              bound_move,
              history_repetition_sensitive,
            };
        }
    }

    MoveList legal_moves;
    Detail::generate_legal_moves_with_context(
      position, legal_moves, *legal_context);
    assert(!legal_moves.empty());

    OrderingExchangeBands exchange_bands;
    order_moves(
      position,
      legal_moves,
      state.ordering_buffer,
      state.quiet_history,
      state.killer_moves(ply),
      ordering_move,
      exchange_bands);

    Score best_score = -INFINITE_SCORE;
    Move best_move = Move::none();
    bool earlier_final_children_sensitive = false;
    bool repetition_sensitive =
      history_repetition_sensitive;
    std::size_t quiet_count = 0;
    std::size_t remaining_exchange_nodes =
      MAX_MAIN_SEARCH_EXCHANGE_NODES;

    // Strict score comparison preserves the first searched move among moves
    // that receive equal search scores.
    for (std::size_t move_index = 0;
         move_index < legal_moves.size();
         ++move_index) {
        const Move move = legal_moves[move_index];
        const bool scout_move =
          is_pvs_scout_move(move_index);
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
        bool selective_candidate =
          (reduction_context
           || futility_context
           || parent_futility_context)
          && !node_team_checked
          && scout_move
          && quiet
          && move.type() == MoveType::NORMAL;
        if (selective_candidate) {
            const Piece moving_piece =
              position.piece_on(move.from());
            moving_piece_type =
              type_of(moving_piece);
            selective_candidate =
              moving_piece_type >= PAWN
              && moving_piece_type <= QUEEN;

            if (selective_candidate && quiet) {
                killer_priority =
                  state.killer_moves(ply).priority(move);
                history_score =
                  state.quiet_history.score(
                    moving_piece, move.to());
            }
        }
        const bool reduction_candidate =
          reduction_context
          && selective_candidate
          && quiet
          && quiet_ordinal
               >= LATE_MOVE_MIN_QUIET_ORDINAL;
        const bool futility_candidate =
          futility_context
          && selective_candidate
          && quiet
          && quiet_ordinal
               >= LATE_MOVE_MIN_QUIET_ORDINAL;
        const Piece captured_piece =
          position.piece_on(move.to());
        const bool opposing_king_capture =
          Detail::captures_opposing_king(
            position,
            move,
            position.side_to_move());
        const PieceType captured_piece_type =
          captured_piece == NO_PIECE
            ? NO_PIECE_TYPE
            : type_of(captured_piece);
        const bool recapture =
          previous_destination != SQ_NONE
          && move.to() == previous_destination;
        const bool proven_losing_capture =
          exchange_bands.proven_below_zero(move_index);
        const bool capture_futility_shape =
          capture_futility_context
          && scout_move
          && captured_piece != NO_PIECE
          && move.type() == MoveType::NORMAL
          && !move.is_promotion()
          && captured_piece_type >= PAWN
          && captured_piece_type <= QUEEN;
        const bool tactical_reduction_shape =
          EnableLateMoveReductions
          && depth >= TACTICAL_LATE_MOVE_MIN_DEPTH
          && scout_move
          && !node_team_checked
          && move.type() == MoveType::NORMAL
          && !move.is_promotion()
          && captured_piece_type >= PAWN
          && captured_piece_type <= QUEEN
          && proven_losing_capture
          && !history_repetition_sensitive
          && !recapture
          && !mate_score_window;
        const bool next_color_checked =
          capture_futility_shape
          && in_check(
               position,
               next_color(
                 position.side_to_move()));
        const bool capture_futility_candidate =
          capture_futility_shape
          && capture_futility_pruning_allowed(
               depth - 1,
               scout_move,
               node_team_checked,
               move.type(),
               move.is_promotion(),
               captured_piece_type,
               next_color_checked,
               false,
               false,
               mate_score_window,
               *node_static_score,
               alpha);
        const bool tactical_reduction_candidate =
          tactical_reduction_shape
          && tactical_late_move_reduction_allowed(
               depth,
               scout_move,
               node_team_checked,
               move.type(),
               move.is_promotion(),
               captured_piece_type,
               proven_losing_capture,
               history_repetition_sensitive,
               recapture,
               false,
               mate_score_window);
        const bool exchange_pruning_candidate =
          exchange_pruning_context
          && main_search_exchange_pruning_candidate(
               position,
               move,
               depth,
               scout_move,
               node_team_checked,
               mate_score_window,
               history_repetition_sensitive);
        const bool exchange_proven_below =
          exchange_pruning_candidate
          && main_search_exchange_is_proven_below(
               position,
               move,
               main_search_exchange_threshold(depth),
               remaining_exchange_nodes);
        std::expected<NodeResult, SearchStopReason>
          child_result{NodeResult{}};

        {
            ChildState child{position, history, move};
            int tactical_reduction = 0;
            if (tactical_reduction_candidate) {
                const bool opposing_king_checked =
                  team_has_checked_king(
                    position,
                    team_of(
                      position.side_to_move()));
                if (tactical_late_move_reduction_allowed(
                      depth,
                      scout_move,
                      node_team_checked,
                      move.type(),
                      move.is_promotion(),
                      captured_piece_type,
                      true,
                      history_repetition_sensitive,
                      recapture,
                      opposing_king_checked,
                      mate_score_window)) {
                    tactical_reduction = 1;
                }
            }

            if (capture_futility_candidate) {
                const bool opposing_king_checked =
                  team_has_checked_king(
                    position,
                    team_of(
                      position.side_to_move()));
                const bool child_terminal =
                  !opposing_king_checked
                  && (history.is_threefold()
                      || !has_legal_move(position));
                if (capture_futility_pruning_allowed(
                      depth - 1,
                      scout_move,
                      node_team_checked,
                      move.type(),
                      move.is_promotion(),
                      captured_piece_type,
                      next_color_checked,
                      opposing_king_checked,
                      child_terminal,
                      mate_score_window,
                      *node_static_score,
                      alpha)) {
                    continue;
                }
            }

            if (exchange_proven_below) {
                const bool opposing_king_checked =
                  team_has_checked_king(
                    position,
                    team_of(
                      position.side_to_move()));
                const bool child_terminal =
                  terminal_result(
                    position, history)
                    .is_terminal();
                if (main_search_exchange_prunes_child(
                      exchange_proven_below,
                      opposing_king_checked,
                      child_terminal)) {
                    continue;
                }
            }

            int first_move_reduction = 0;
            if (!scout_move
                && EnableLateMoveReductions
                && nonpv_first_move_reduction_allowed(
                     depth,
                     ply,
                     null_window,
                     node_team_checked,
                     ordering_move.is_board_move(),
                     move.type(),
                     move.is_promotion(),
                     opposing_king_capture,
                     recapture,
                     false,
                     false,
                     mate_score_window)) {
                const bool opposing_king_checked =
                  team_has_checked_king(
                    position,
                    team_of(
                      position.side_to_move()));
                const bool child_terminal =
                  !opposing_king_checked
                  && (history.is_threefold()
                      || !has_legal_move(position));
                if (nonpv_first_move_reduction_allowed(
                      depth,
                      ply,
                      null_window,
                      node_team_checked,
                      ordering_move.is_board_move(),
                      move.type(),
                      move.is_promotion(),
                      opposing_king_capture,
                      recapture,
                      opposing_king_checked,
                      child_terminal,
                      mate_score_window)) {
                    first_move_reduction =
                      depth
                          >= NONPV_FIRST_MOVE_SECOND_REDUCTION_DEPTH
                        ? 2
                        : 1;
                }
            }

            // Every move advances to the opposing team, so the child score
            // and child window are negated for the parent perspective.
            if (!scout_move) {
                child_result =
                  alpha_beta<
                    EnableLateMoveReductions,
                    EnableForwardPruning,
                    EnableNullMovePruning,
                    EnableExchangePruning>(
                    position,
                    history,
                    depth - 1 - first_move_reduction,
                    ply + 1,
                    -beta,
                    -alpha,
                    state,
                    Move::none(),
                    true,
                    transposition_allowed,
                    std::nullopt,
                    move.to());
            } else {
                const Score scout_beta =
                  pvs_scout_beta(alpha);
                const bool opposing_king_checked =
                  (reduction_candidate
                   || futility_candidate
                   || (parent_futility_context
                       && selective_candidate))
                  && team_has_checked_king(
                    position,
                    team_of(
                      position.side_to_move()));
                const int reduction =
                  reduction_candidate
                    ? adaptive_late_move_reduction(
                        depth,
                        quiet_ordinal,
                        scout_move,
                        node_team_checked,
                        quiet,
                        move.type(),
                        moving_piece_type,
                        killer_priority,
                        history_score,
                        mate_score_window,
                        opposing_king_checked)
                    : tactical_reduction;
                assert(reduction >= 0);
                assert(reduction <= depth - 1);
                const int futility_child_depth =
                  depth - 1 - reduction;
                const bool move_count_pruning_candidate =
                  EnableForwardPruning
                  && selective_candidate
                  && late_move_count_pruning_allowed(
                       depth,
                       ply,
                       null_window,
                       quiet_ordinal,
                       node_team_checked,
                       quiet,
                       move.type(),
                       moving_piece_type,
                       killer_priority,
                       history_score,
                       mate_score_window,
                       opposing_king_checked,
                       false);
                const bool parent_futility_candidate =
                  parent_futility_context
                  && selective_candidate
                  && parent_futility_pruning_allowed(
                       depth,
                       futility_child_depth,
                       node_team_checked,
                       quiet,
                       move.type(),
                       moving_piece_type,
                       killer_priority,
                       history_score,
                       mate_score_window,
                       opposing_king_checked,
                       false,
                       *node_static_score,
                       alpha);
                if (move_count_pruning_candidate
                    || parent_futility_candidate) {
                    const bool child_terminal =
                      history.is_threefold()
                      || !has_legal_move(position);
                    const bool prune_by_move_count =
                      move_count_pruning_candidate
                      && late_move_count_pruning_allowed(
                           depth,
                           ply,
                           null_window,
                           quiet_ordinal,
                           node_team_checked,
                           quiet,
                           move.type(),
                           moving_piece_type,
                           killer_priority,
                           history_score,
                           mate_score_window,
                           opposing_king_checked,
                           child_terminal);
                    const bool prune_by_futility =
                      parent_futility_candidate
                      && parent_futility_pruning_allowed(
                           depth,
                           futility_child_depth,
                           node_team_checked,
                           quiet,
                           move.type(),
                           moving_piece_type,
                           killer_priority,
                           history_score,
                           mate_score_window,
                           opposing_king_checked,
                           child_terminal,
                           *node_static_score,
                           alpha);
                    if (prune_by_move_count
                        || prune_by_futility) {
                        // The sentinel excludes the unsearched move from
                        // quiet-history fail-low training after a cutoff.
                        legal_moves[move_index] = Move::none();
                        continue;
                    }
                }
                std::optional<Score> child_static_score;
                if (futility_candidate)
                    child_static_score = evaluate(position);

                if (futility_candidate
                    && late_move_futility_pruning_allowed(
                         depth,
                         quiet_ordinal,
                         node_team_checked,
                         quiet,
                         move.type(),
                         moving_piece_type,
                         killer_priority,
                         history_score,
                         mate_score_window,
                         opposing_king_checked,
                         false,
                         -*child_static_score,
                         alpha)) {
                    const bool child_terminal =
                      history.is_threefold()
                      || !has_legal_move(position);
                    if (!child_terminal) {
                        // The sentinel excludes this unsearched move from
                        // quiet-history fail-low training after a later cutoff.
                        legal_moves[move_index] =
                          Move::none();
                        continue;
                    }
                }

                child_result =
                  alpha_beta<
                    EnableLateMoveReductions,
                    EnableForwardPruning,
                    EnableNullMovePruning,
                    EnableExchangePruning>(
                    position,
                    history,
                    depth - 1 - reduction,
                    ply + 1,
                    -scout_beta,
                    -alpha,
                    state,
                    Move::none(),
                    true,
                    transposition_allowed,
                    child_static_score,
                    move.to());

                if (child_result
                    && lmr_verification_required(
                         reduction,
                         -child_result->score,
                         alpha)) {
                    child_result =
                      alpha_beta<
                        EnableLateMoveReductions,
                        EnableForwardPruning,
                        EnableNullMovePruning,
                        EnableExchangePruning>(
                        position,
                        history,
                        depth - 1,
                        ply + 1,
                        -scout_beta,
                        -alpha,
                        state,
                        Move::none(),
                        true,
                        transposition_allowed,
                        child_static_score,
                        move.to());
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
                            EnableLateMoveReductions,
                            EnableForwardPruning,
                            EnableNullMovePruning,
                            EnableExchangePruning>(
                            position,
                            history,
                            depth - 1,
                            ply + 1,
                            -beta,
                            -alpha,
                            state,
                            Move::none(),
                            true,
                            transposition_allowed,
                            child_static_score,
                            move.to());
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

        const bool beta_cutoff = alpha >= beta;
        repetition_sensitive =
          repetition_sensitive_after_final_child(
            history_repetition_sensitive,
            earlier_final_children_sensitive,
            child_result->repetition_sensitive,
            beta_cutoff);

        if (beta_cutoff) {
            if (quiet) {
                const Piece cutoff_piece =
                  position.piece_on(
                    move.from());
                state.killer_moves(ply).record(move);
                state.quiet_history.reward(
                  cutoff_piece,
                  move.to(),
                  depth);

                // Only completed earlier quiet searches are trained as
                // fail-lows. An aliased cutoff entry is not updated in both
                // directions.
                for (std::size_t prior_index = 0;
                     prior_index < move_index;
                     ++prior_index) {
                    const Move prior =
                      legal_moves[prior_index];
                    if (!prior.is_board_move()
                        || is_tactical_move(
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

        earlier_final_children_sensitive =
          earlier_final_children_sensitive
          || child_result->repetition_sensitive;
    }

    assert(is_ok(best_move));

    if (table && !repetition_sensitive) {
        table->store(
          position.key(),
          history.context(),
          depth,
          score_to_table(best_score, ply),
          classify_bound(
            best_score,
            original_alpha,
            original_beta),
          best_move,
          node_static_score.value_or(
            NO_STATIC_EVALUATION));
    }

    return NodeResult{
      best_score,
      best_move,
      repetition_sensitive,
    };
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
// late quiet scout moves first use a depth- and move-order-dependent reduced
// search whose child retains at least two nominal plies when possible. A reduced
// result that raises alpha is verified at full depth before it can update the
// node. Shallow, safety-gated futility pruning uses static margins
// to omit late quiet moves and fail-high scout nodes. Safety-gated null-move
// pruning tests deep scout nodes in an isolated history segment without
// transposition-table access. At depth one, late ordinary knight and bishop
// captures proven below the two-pawn exchange margin are omitted unless the
// child checks an opposing king or is terminal. Mate-distance bounds limit
// impossible mate scores. At the nominal horizon, quiescence continues
// captures, promotions, and every legal check evasion. Terminal positions end
// a line before evaluation. The position is restored before return, and
// history is read-only.
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
  > MAX_EVALUATION_SCORE);
static_assert(
  MATE_SCORE + MAX_SEARCH_PLY
  < INFINITE_SCORE);

}  // namespace Mockingbird
