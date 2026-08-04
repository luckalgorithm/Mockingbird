#pragma once

#include <cassert>
#include <cstdint>
#include <optional>

#include "legal.h"
#include "repetition.h"

namespace Mockingbird {

// ResultType records the classification. INVALID_POSITION describes a
// malformed king layout rather than a completed game.
enum class ResultType : std::uint8_t {
    ONGOING,
    KING_CAPTURE,
    CHECKMATE,
    STALEMATE,
    THREEFOLD_REPETITION,
    INVALID_POSITION,
};

class PositionResult {
  public:
    constexpr PositionResult() noexcept = default;

    // Precondition: winner is a valid team.
    [[nodiscard]] static constexpr PositionResult king_capture(
      Team winner) noexcept {
        return PositionResult{
          ResultType::KING_CAPTURE, winner};
    }

    // Precondition: winner is a valid team.
    [[nodiscard]] static constexpr PositionResult checkmate(
      Team winner) noexcept {
        return PositionResult{
          ResultType::CHECKMATE, winner};
    }

    [[nodiscard]] static constexpr PositionResult stalemate() noexcept {
        return PositionResult{
          ResultType::STALEMATE, TEAM_NB};
    }

    [[nodiscard]] static constexpr PositionResult
    threefold_repetition() noexcept {
        return PositionResult{
          ResultType::THREEFOLD_REPETITION, TEAM_NB};
    }

    [[nodiscard]] static constexpr PositionResult
    invalid_position() noexcept {
        return PositionResult{
          ResultType::INVALID_POSITION, TEAM_NB};
    }

    [[nodiscard]] constexpr ResultType type() const noexcept {
        return type_;
    }

    // Invalid positions are not completed games.
    [[nodiscard]] constexpr bool is_terminal() const noexcept {
        return type_ != ResultType::ONGOING
            && type_ != ResultType::INVALID_POSITION;
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
        return type_ != ResultType::INVALID_POSITION;
    }

    // Only king-capture and checkmate results contain a winning team.
    [[nodiscard]] constexpr std::optional<Team>
    winning_team() const noexcept {
        if (!is_ok(winner_))
            return std::nullopt;

        return winner_;
    }

    [[nodiscard]] friend constexpr bool operator==(
      const PositionResult&,
      const PositionResult&) noexcept = default;

  private:
    constexpr PositionResult(
      ResultType type,
      Team winner) noexcept
        : type_(type),
          winner_(winner) {
        const bool has_winner =
          type == ResultType::KING_CAPTURE
          || type == ResultType::CHECKMATE;
        assert(has_winner == is_ok(winner));
        static_cast<void>(has_winner);
    }

    ResultType type_ = ResultType::ONGOING;
    Team winner_ = TEAM_NB;
};

namespace Detail {

enum class KingLayout : std::uint8_t {
    COMPLETE,
    RED_YELLOW_MISSING,
    BLUE_GREEN_MISSING,
    INVALID,
};

[[nodiscard]] constexpr KingLayout king_layout(
  const Position& position) noexcept {
    Team missing_team = TEAM_NB;
    int missing_king_count = 0;

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        const int king_count =
          position.pieces(color, KING).popcount();

        if (king_count > 1)
            return KingLayout::INVALID;
        if (king_count == 0) {
            missing_team = team_of(color);
            ++missing_king_count;
        }
    }

    if (missing_king_count > 1)
        return KingLayout::INVALID;
    if (missing_team == RED_YELLOW)
        return KingLayout::RED_YELLOW_MISSING;
    if (missing_team == BLUE_GREEN)
        return KingLayout::BLUE_GREEN_MISSING;
    return KingLayout::COMPLETE;
}

[[nodiscard]] constexpr Team opposing_team_for_result(
  Team team) noexcept {
    assert(is_ok(team));
    return team == RED_YELLOW ? BLUE_GREEN : RED_YELLOW;
}

// Classifies a position using facts already established by the caller.
// Preconditions: layout describes position; when layout is COMPLETE and no
// legal move exists, checked reports whether the moving color is in check.
[[nodiscard]] constexpr PositionResult classify_result_with_facts(
  const Position& position,
  const PositionHistory& history,
  KingLayout layout,
  bool legal_move_exists,
  bool checked) noexcept {
    assert(history.current_key() == position.key());

    switch (layout) {
        case KingLayout::RED_YELLOW_MISSING:
            return PositionResult::king_capture(BLUE_GREEN);

        case KingLayout::BLUE_GREEN_MISSING:
            return PositionResult::king_capture(RED_YELLOW);

        case KingLayout::INVALID:
            return PositionResult::invalid_position();

        case KingLayout::COMPLETE:
            break;
    }

    if (!legal_move_exists) {
        if (checked) {
            return PositionResult::checkmate(
              opposing_team_for_result(
                team_of(position.side_to_move())));
        }

        return PositionResult::stalemate();
    }

    if (history.is_threefold())
        return PositionResult::threefold_repetition();

    return {};
}

[[nodiscard]] constexpr PositionResult classify_result(
  const Position& position,
  const PositionHistory& history,
  bool legal_move_exists) noexcept {
    assert(history.current_key() == position.key());

    const KingLayout layout = king_layout(position);
    const bool checked =
      layout == KingLayout::COMPLETE
      && !legal_move_exists
      && in_check(position);
    return classify_result_with_facts(
      position,
      history,
      layout,
      legal_move_exists,
      checked);
}

}  // namespace Detail

// The legal_moves list must contain exactly the legal moves for position.
// The current history key must equal position.key().
[[nodiscard]] constexpr PositionResult terminal_result(
  const Position& position,
  const PositionHistory& history,
  const MoveList& legal_moves) noexcept {
    return Detail::classify_result(
      position, history, !legal_moves.empty());
}

// Classifies a position without retaining its generated moves. The position
// and history are unchanged. The current history key must equal position.key().
[[nodiscard]] constexpr PositionResult terminal_result(
  Position& position,
  const PositionHistory& history) noexcept {
    return Detail::classify_result(
      position, history, has_legal_move(position));
}

static_assert(
  PositionResult{}.type() == ResultType::ONGOING);
static_assert(!PositionResult{}.is_terminal());
static_assert(PositionResult{}.is_valid());
static_assert(!PositionResult{}.winning_team().has_value());

}  // namespace Mockingbird
