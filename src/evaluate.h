#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "attacks.h"
#include "position.h"

namespace Mockingbird {

using Score = std::int32_t;

// One pawn is represented by 100 score units. Kings have no material value
// because terminal results are scored separately from static evaluation.
inline constexpr Score PAWN_VALUE = 100;
inline constexpr Score KNIGHT_VALUE = 320;
inline constexpr Score BISHOP_VALUE = 330;
inline constexpr Score ROOK_VALUE = 500;
inline constexpr Score QUEEN_VALUE = 900;
inline constexpr Score KING_VALUE = 0;

inline constexpr auto PIECE_VALUES =
  StaticEvaluationDetail::MATERIAL_VALUES;

inline constexpr Score MAX_PIECE_VALUE = [] {
    Score maximum = 0;

    for (const Score value : PIECE_VALUES) {
        if (value > maximum)
            maximum = value;
    }

    return maximum;
}();

inline constexpr Score MAX_MATERIAL_SCORE =
  static_cast<Score>(PLAYABLE_SQUARE_NB)
  * MAX_PIECE_VALUE;

// Positional terms are clamped below the mate-score range used by search.
inline constexpr Score MAX_POSITIONAL_SCORE = 200'000;
inline constexpr Score MAX_EVALUATION_SCORE =
  MAX_MATERIAL_SCORE + MAX_POSITIONAL_SCORE;

// Precondition: piece_type is a real piece type.
[[nodiscard]] constexpr Score piece_value(
  PieceType piece_type) noexcept {
    assert(is_ok(piece_type));
    return PIECE_VALUES[std::size_t(piece_type)];
}

// Returns friendly material minus opposing material for perspective.
// Precondition: perspective is a valid team.
[[nodiscard]] constexpr Score material_balance(
  const Position& position,
  Team perspective) noexcept {
    assert(is_ok(perspective));
    const Score red_yellow_material =
      position.static_evaluation_state().material;
    return perspective == RED_YELLOW
      ? red_yellow_material
      : -red_yellow_material;
}

namespace EvaluationDetail {

struct TaperedScore {
    Score middlegame = 0;
    Score endgame = 0;

    constexpr TaperedScore& operator+=(
      const TaperedScore& other) noexcept {
        middlegame += other.middlegame;
        endgame += other.endgame;
        return *this;
    }
};

struct RelativeCoordinates {
    int file;
    int rank;
};

struct CompactRelativeCoordinates {
    std::uint8_t file;
    std::uint8_t rank;
};

struct EvaluationTerms {
    Score material = 0;
    Score positional = 0;
};

struct KingPressure {
    int attacker_count = 0;
    int attacker_weight = 0;
};

using PawnFileCounts =
  StaticEvaluationState::PawnFileCounts;

inline constexpr std::array<Score, PIECE_TYPE_NB>
  CENTRALIZATION_MIDDLEGAME =
    StaticEvaluationDetail::CENTRALIZATION_MIDDLEGAME;
inline constexpr std::array<Score, PIECE_TYPE_NB>
  CENTRALIZATION_ENDGAME =
    StaticEvaluationDetail::CENTRALIZATION_ENDGAME;
inline constexpr std::array<Score, PIECE_TYPE_NB>
  MOBILITY_MIDDLEGAME = {
    0, 0, 4, 3, 2, 1, 0,
};
inline constexpr std::array<Score, PIECE_TYPE_NB>
  MOBILITY_ENDGAME = {
    0, 0, 3, 4, 4, 2, 0,
};
inline constexpr std::array<Score, PIECE_TYPE_NB>
  THREAT_MIDDLEGAME = {
    0, 5, 10, 10, 15, 24, 0,
};
inline constexpr std::array<Score, PIECE_TYPE_NB>
  THREAT_ENDGAME = {
    0, 4, 8, 8, 12, 18, 0,
};
inline constexpr std::array<Score, PIECE_TYPE_NB>
  LOOSE_PIECE_MIDDLEGAME = {
    0, 4, 8, 8, 12, 20, 0,
};
inline constexpr std::array<Score, PIECE_TYPE_NB>
  LOOSE_PIECE_ENDGAME = {
    0, 4, 7, 7, 10, 16, 0,
};
inline constexpr int MAX_PHASE = 48;
inline constexpr TaperedScore BISHOP_PAIR_BONUS = {18, 28};
inline constexpr TaperedScore CONNECTED_ROOK_BONUS = {10, 18};
inline constexpr TaperedScore SEMI_OPEN_ROOK_LINE_BONUS = {1, 3};
inline constexpr TaperedScore OPEN_ROOK_LINE_BONUS = {3, 7};
inline constexpr TaperedScore DOUBLED_PAWN_PENALTY = {10, 16};
inline constexpr TaperedScore ISOLATED_PAWN_PENALTY = {8, 12};
inline constexpr TaperedScore SUPPORTED_PAWN_BONUS = {6, 10};
inline constexpr TaperedScore DIRECT_KING_ATTACK_PENALTY = {
  500, 50,
};
inline constexpr std::array<int, PIECE_TYPE_NB>
  KING_ATTACK_WEIGHTS = {
    0, 2, 4, 4, 5, 7, 1,
};
inline constexpr int KING_PRESSURE_INPUT_CAP = 48;
inline constexpr int KING_PRESSURE_DIVISOR = 12;
inline constexpr int KING_PRESSURE_ENDGAME_DIVISOR = 4;
inline constexpr std::array<RayDirection, 4> ORTHOGONAL_DIRECTIONS = {
  RayDirection::NORTH,
  RayDirection::EAST,
  RayDirection::SOUTH,
  RayDirection::WEST,
};
inline constexpr std::array<std::array<Color, 2>, TEAM_NB>
  TEAM_COLORS = {{
    {RED, YELLOW},
    {BLUE, GREEN},
  }};

[[nodiscard]] constexpr Team opposing_team(Team team) noexcept {
    assert(is_ok(team));
    return team == RED_YELLOW ? BLUE_GREEN : RED_YELLOW;
}

// Coordinates are rotated into Red's frame. Rank increases toward promotion
// and file runs across the owning player's back rank.
[[nodiscard]] constexpr RelativeCoordinates relative_coordinates(
  Color color,
  Square square) noexcept {
    const StaticEvaluationDetail::RelativeCoordinates coordinates =
      StaticEvaluationDetail::relative_coordinates(color, square);
    return {coordinates.file, coordinates.rank};
}

[[nodiscard]] consteval auto make_relative_coordinate_table() {
    std::array<
      std::array<CompactRelativeCoordinates, SQUARE_NB>,
      COLOR_NB>
      table{};

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        for (int square_index = 0;
             square_index < SQUARE_NB;
             ++square_index) {
            const Square square = Square(square_index);
            if (is_ok(square)) {
                const RelativeCoordinates coordinates =
                  relative_coordinates(color, square);
                table[std::size_t(color)]
                     [std::size_t(square)] =
                  {
                    static_cast<std::uint8_t>(coordinates.file),
                    static_cast<std::uint8_t>(coordinates.rank),
                  };
            }
        }
    }

    return table;
}

inline constexpr auto RELATIVE_COORDINATE_TABLE =
  make_relative_coordinate_table();

// Preconditions: color and square are valid.
[[nodiscard]] constexpr RelativeCoordinates
cached_relative_coordinates(
  Color color,
  Square square) noexcept {
    assert(is_ok(color));
    assert(is_ok(square));
    const CompactRelativeCoordinates coordinates =
      RELATIVE_COORDINATE_TABLE[std::size_t(color)]
                               [std::size_t(square)];
    return {
      static_cast<int>(coordinates.file),
      static_cast<int>(coordinates.rank),
    };
}

// Returns SQ_NONE when the relative coordinate is outside the playable cross.
[[nodiscard]] constexpr Square oriented_square(
  Color color,
  int relative_file,
  int relative_rank) noexcept {
    assert(is_ok(color));

    int file = relative_file;
    int rank = relative_rank;

    switch (color) {
        case RED:
            break;
        case BLUE:
            file = relative_rank;
            rank = BOARD_FILES + 1 - relative_file;
            break;
        case YELLOW:
            file = BOARD_FILES + 1 - relative_file;
            rank = BOARD_RANKS + 1 - relative_rank;
            break;
        case GREEN:
            file = BOARD_RANKS + 1 - relative_rank;
            rank = relative_file;
            break;
        case COLOR_NB:
            return SQ_NONE;
    }

    if (file < FILE_A || file > FILE_N
        || rank < RANK_1 || rank > RANK_14)
        return SQ_NONE;

    const Square square = make_square(File(file), Rank(rank));
    return is_ok(square) ? square : SQ_NONE;
}

// The four central squares score 16. The playable tips of each arm score -2.
[[nodiscard]] constexpr Score centralization(Square square) noexcept {
    return StaticEvaluationDetail::centralization(square);
}

[[nodiscard]] constexpr int pawn_advancement(
  Color color,
  Square square) noexcept {
    return StaticEvaluationDetail::pawn_advancement(color, square);
}

[[nodiscard]] constexpr Bitboard piece_attacks(
  PieceType piece_type,
  Color color,
  Square square,
  const Bitboard& occupied) noexcept {
    assert(is_ok(piece_type));
    assert(is_ok(color));
    assert(is_ok(square));

    switch (piece_type) {
        case PAWN:
            return pawn_attacks(color, square);
        case KNIGHT:
            return knight_attacks(square);
        case BISHOP:
            return bishop_attacks(square, occupied);
        case ROOK:
            return rook_attacks(square, occupied);
        case QUEEN:
            return queen_attacks(square, occupied);
        case KING:
            return king_attacks(square);
        case NO_PIECE_TYPE:
        case PIECE_TYPE_NB:
            break;
    }

    return {};
}

[[nodiscard]] constexpr TaperedScore square_score(
  PieceType piece_type,
  Color color,
  Square square) noexcept {
    const Score center = centralization(square);
    TaperedScore score = {
      center * CENTRALIZATION_MIDDLEGAME[std::size_t(piece_type)],
      center * CENTRALIZATION_ENDGAME[std::size_t(piece_type)],
    };

    if (piece_type == PAWN) {
        const Score advancement =
          static_cast<Score>(pawn_advancement(color, square));
        score.middlegame +=
          2 * advancement + advancement * advancement / 2;
        score.endgame +=
          4 * advancement + advancement * advancement;
    }

    return score;
}

[[nodiscard]] consteval bool static_square_values_match() {
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        for (int type_index = PAWN;
             type_index <= KING;
             ++type_index) {
            const PieceType piece_type = PieceType(type_index);
            const Piece piece = make_piece(color, piece_type);
            for (int square_index = 0;
                 square_index < SQUARE_NB;
                 ++square_index) {
                const Square square = Square(square_index);
                if (!is_ok(square))
                    continue;

                const TaperedScore expected =
                  square_score(piece_type, color, square);
                const StaticEvaluationDetail::SquareValue actual =
                  StaticEvaluationDetail::square_value(piece, square);
                if (actual.middlegame != expected.middlegame
                    || actual.endgame != expected.endgame) {
                    return false;
                }
            }
        }
    }

    return true;
}

static_assert(static_square_values_match());

[[nodiscard]] constexpr TaperedScore rook_line_score(
  const Bitboard& all_pawns,
  const Bitboard& team_pawns,
  Square rook) noexcept {
    assert(is_ok(rook));

    TaperedScore score;

    for (const RayDirection direction : ORTHOGONAL_DIRECTIONS) {
        const Bitboard line = ray_attacks(direction, rook);
        if (line.empty())
            continue;

        if ((line & all_pawns).empty())
            score += OPEN_ROOK_LINE_BONUS;
        else if ((line & team_pawns).empty())
            score += SEMI_OPEN_ROOK_LINE_BONUS;
    }

    return score;
}

constexpr void add_score(
  std::array<TaperedScore, TEAM_NB>& scores,
  Team team,
  TaperedScore value) noexcept {
    scores[std::size_t(team)] += value;
}

// Pressure grows quadratically when several pieces reach a king's immediate
// zone. Capping the input keeps the term bounded in promoted-piece positions.
[[nodiscard]] constexpr TaperedScore king_pressure_penalty(
  KingPressure pressure,
  int attacked_ring_squares) noexcept {
    assert(pressure.attacker_count >= 0);
    assert(pressure.attacker_weight >= 0);
    assert(attacked_ring_squares >= 0);
    assert(attacked_ring_squares <= 8);

    const int raw_pressure =
      pressure.attacker_weight
      + 2 * pressure.attacker_count
      + 2 * attacked_ring_squares;
    const int bounded_pressure =
      raw_pressure < KING_PRESSURE_INPUT_CAP
        ? raw_pressure
        : KING_PRESSURE_INPUT_CAP;
    const Score middlegame =
      static_cast<Score>(
        bounded_pressure * bounded_pressure
        / KING_PRESSURE_DIVISOR);
    return {
      middlegame,
      middlegame / KING_PRESSURE_ENDGAME_DIVISOR,
    };
}

// Files are measured in the pawn owner's frame. Doubled pawns count every
// pawn beyond the first, isolated pawns have no pawn on either adjacent file,
// and supported pawns occupy a square attacked by a same-color pawn.
[[nodiscard]] constexpr TaperedScore pawn_structure_score(
  const PawnFileCounts& file_counts,
  const Bitboard& pawns,
  const Bitboard& same_color_pawn_attacks) noexcept {
    int doubled = 0;
    int isolated = 0;

    for (std::size_t file = 0;
         file < file_counts.size();
         ++file) {
        const int count =
          file_counts[file];
        assert(count >= 0);
        if (count == 0)
            continue;

        doubled += count - 1;
        const bool adjacent_file_has_pawn =
          (file > 0
           && file_counts[file - 1] != 0)
          || (file + 1 < file_counts.size()
              && file_counts[file + 1] != 0);
        if (!adjacent_file_has_pawn)
            isolated += count;
    }

    const int supported =
      (pawns & same_color_pawn_attacks).popcount();
    return {
      static_cast<Score>(supported)
          * SUPPORTED_PAWN_BONUS.middlegame
        - static_cast<Score>(doubled)
          * DOUBLED_PAWN_PENALTY.middlegame
        - static_cast<Score>(isolated)
          * ISOLATED_PAWN_PENALTY.middlegame,
      static_cast<Score>(supported)
          * SUPPORTED_PAWN_BONUS.endgame
        - static_cast<Score>(doubled)
          * DOUBLED_PAWN_PENALTY.endgame
        - static_cast<Score>(isolated)
          * ISOLATED_PAWN_PENALTY.endgame,
    };
}

[[nodiscard]] constexpr Score blend(
  TaperedScore score,
  int phase) noexcept {
    assert(phase >= 0);
    assert(phase <= MAX_PHASE);

    const std::int64_t numerator =
      std::int64_t{score.middlegame} * phase
      + std::int64_t{score.endgame} * (MAX_PHASE - phase);
    return static_cast<Score>(numerator / MAX_PHASE);
}

[[nodiscard]] constexpr Score clamp_evaluation(Score score) noexcept {
    if (score > MAX_EVALUATION_SCORE)
        return MAX_EVALUATION_SCORE;
    if (score < -MAX_EVALUATION_SCORE)
        return -MAX_EVALUATION_SCORE;
    return score;
}

[[nodiscard]] constexpr EvaluationTerms evaluation_terms(
  const Position& position) noexcept {
    std::array<TaperedScore, TEAM_NB> team_scores{};
    const StaticEvaluationState& static_evaluation =
      position.static_evaluation_state();
    team_scores[RED_YELLOW] = {
      static_evaluation.square_middlegame,
      static_evaluation.square_endgame,
    };
    std::array<Bitboard, COLOR_NB> color_attacks{};
    std::array<Bitboard, COLOR_NB> color_pawn_attacks{};
    std::array<int, TEAM_NB> connected_rook_links{};
    std::array<Bitboard, COLOR_NB> king_zones{};
    std::array<KingPressure, COLOR_NB> king_pressures{};
    const Bitboard& occupied = position.occupied();
    std::array<Bitboard, COLOR_NB> color_pawns{};
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        color_pawns[std::size_t(color)] =
          position.pieces(color, PAWN);
        color_pawn_attacks[std::size_t(color)] =
          pawn_attacks(
            color,
            color_pawns[std::size_t(color)]);
        color_attacks[std::size_t(color)] =
          color_pawn_attacks[std::size_t(color)];
    }
    const std::array<Bitboard, TEAM_NB> team_pieces = {
      position.pieces(RED_YELLOW),
      position.pieces(BLUE_GREEN),
    };
    const Bitboard& all_pawns = position.pieces(PAWN);
    const std::array<Bitboard, TEAM_NB> team_pawns = {
      all_pawns & team_pieces[RED_YELLOW],
      all_pawns & team_pieces[BLUE_GREEN],
    };
    const Bitboard& all_rooks = position.pieces(ROOK);
    const std::array<Bitboard, TEAM_NB> team_rooks = {
      all_rooks & team_pieces[RED_YELLOW],
      all_rooks & team_pieces[BLUE_GREEN],
    };

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        Bitboard kings =
          position.pieces(Color(color_index), KING);
        while (kings) {
            const Square king = kings.pop_lsb();
            king_zones[std::size_t(color_index)]
              |= king_attacks(king);
            king_zones[std::size_t(color_index)].set(king);
        }
    }

    const std::array<Bitboard, TEAM_NB> team_king_zones = {
      king_zones[RED] | king_zones[YELLOW],
      king_zones[BLUE] | king_zones[GREEN],
    };

    // Applying the opposite pawn geometry to a king zone produces every
    // source square from which an attacking pawn can reach that zone.
    for (int attacker_index = 0;
         attacker_index < COLOR_NB;
         ++attacker_index) {
        const Color attacker = Color(attacker_index);
        const Team defending_team =
          opposing_team(team_of(attacker));
        const Color reverse =
          next_color(next_color(attacker));

        for (const Color defender :
             TEAM_COLORS[std::size_t(defending_team)]) {
            const int attacker_count =
              (color_pawns[std::size_t(attacker)]
               & pawn_attacks(
                   reverse,
                   king_zones[std::size_t(defender)]))
                .popcount();
            KingPressure& pressure =
              king_pressures[std::size_t(defender)];
            pressure.attacker_count += attacker_count;
            pressure.attacker_weight +=
              attacker_count
              * KING_ATTACK_WEIGHTS[std::size_t(PAWN)];
        }
    }

    const Score material = static_evaluation.material;
    int phase = static_evaluation.phase;

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        const Team team = team_of(color);
        const Bitboard& friendly =
          team_pieces[std::size_t(team)];

        for (int type_index = KNIGHT;
             type_index <= KING;
             ++type_index) {
            const PieceType piece_type = PieceType(type_index);
            Bitboard pieces =
              position.pieces(color, piece_type);

            while (pieces) {
                const Square square = pieces.pop_lsb();
                const Bitboard attacks = piece_attacks(
                  piece_type,
                  color,
                  square,
                  occupied);
                color_attacks[std::size_t(color)] |= attacks;

                const Team defending_team =
                  opposing_team(team);
                const Bitboard attacked_king_zones =
                  attacks
                  & team_king_zones[
                      std::size_t(defending_team)];
                if (attacked_king_zones) {
                    for (const Color defender :
                         TEAM_COLORS[
                           std::size_t(defending_team)]) {
                        if (!(attacked_king_zones
                              & king_zones[
                                  std::size_t(defender)])) {
                            continue;
                        }

                        KingPressure& pressure =
                          king_pressures[
                            std::size_t(defender)];
                        ++pressure.attacker_count;
                        pressure.attacker_weight +=
                          KING_ATTACK_WEIGHTS[
                            std::size_t(piece_type)];
                    }
                }
                if (piece_type == ROOK) {
                    add_score(
                      team_scores,
                      team,
                      rook_line_score(
                        all_pawns,
                        team_pawns[std::size_t(team)],
                        square));
                    connected_rook_links[
                      std::size_t(team)] +=
                        (attacks
                         & team_rooks[std::size_t(team)])
                          .popcount();
                }

                if (piece_type >= KNIGHT
                    && piece_type <= QUEEN) {
                    const Score mobility = static_cast<Score>(
                      (attacks & ~friendly).popcount());
                    add_score(
                      team_scores,
                      team,
                      {
                        mobility
                          * MOBILITY_MIDDLEGAME[
                              std::size_t(piece_type)],
                        mobility
                          * MOBILITY_ENDGAME[
                              std::size_t(piece_type)],
                      });
                }
            }
        }
    }

    if (phase > MAX_PHASE)
        phase = MAX_PHASE;

    const std::array<Bitboard, TEAM_NB> team_attacks = {
      color_attacks[RED] | color_attacks[YELLOW],
      color_attacks[BLUE] | color_attacks[GREEN],
    };

    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        add_score(
          team_scores,
          team_of(color),
          pawn_structure_score(
            static_evaluation.pawn_file_counts[
              std::size_t(color)],
            color_pawns[std::size_t(color)],
            color_pawn_attacks[std::size_t(color)]));
    }

    // Threats are scored once per target even when several teammates attack it.
    for (int team_index = 0;
         team_index < TEAM_NB;
         ++team_index) {
        const Team team = Team(team_index);
        const Team opponent = opposing_team(team);
        const Bitboard threatened =
          team_attacks[std::size_t(team)]
          & team_pieces[std::size_t(opponent)];
        if (threatened.empty())
            continue;

        const Bitboard loose =
          threatened & ~team_attacks[std::size_t(opponent)];

        for (int type_index = PAWN;
             type_index <= QUEEN;
             ++type_index) {
            const PieceType piece_type = PieceType(type_index);
            const Score threatened_count = static_cast<Score>(
              (threatened & position.pieces(piece_type)).popcount());
            const Score loose_count = static_cast<Score>(
              (loose & position.pieces(piece_type)).popcount());

            add_score(
              team_scores,
              team,
              {
                threatened_count
                    * THREAT_MIDDLEGAME[std::size_t(piece_type)]
                  + loose_count
                    * LOOSE_PIECE_MIDDLEGAME[
                        std::size_t(piece_type)],
                threatened_count
                    * THREAT_ENDGAME[std::size_t(piece_type)]
                  + loose_count
                    * LOOSE_PIECE_ENDGAME[
                        std::size_t(piece_type)],
              });
        }
    }

    // King shelter belongs to an individual color; danger is supplied by the
    // complete opposing team attack map.
    for (int color_index = 0;
         color_index < COLOR_NB;
         ++color_index) {
        const Color color = Color(color_index);
        const Team team = team_of(color);
        const Team opponent = opposing_team(team);
        Bitboard kings = position.pieces(color, KING);

        while (kings) {
            const Square king = kings.pop_lsb();
            const RelativeCoordinates relative =
              cached_relative_coordinates(color, king);
            const Piece shelter_pawn =
              make_piece(color, PAWN);
            TaperedScore safety;

            for (int distance = 1; distance <= 2; ++distance) {
                for (int lateral = -1; lateral <= 1; ++lateral) {
                    const Square shelter_square = oriented_square(
                      color,
                      relative.file + lateral,
                      relative.rank + distance);
                    if (shelter_square == SQ_NONE
                        || position.piece_on(shelter_square)
                             != shelter_pawn)
                        continue;

                    safety.middlegame += distance == 1 ? 8 : 4;
                    safety.endgame += distance == 1 ? 2 : 1;
                }
            }

            const Bitboard& opponent_attacks =
              team_attacks[std::size_t(opponent)];
            const Bitboard& king_ring =
              king_attacks(king);
            const Score attacked_ring_squares = static_cast<Score>(
              (opponent_attacks & king_ring).popcount());
            safety.middlegame -= 10 * attacked_ring_squares;
            safety.endgame -= 4 * attacked_ring_squares;
            const TaperedScore pressure_penalty =
              king_pressure_penalty(
                king_pressures[std::size_t(color)],
                attacked_ring_squares);
            safety.middlegame -=
              pressure_penalty.middlegame;
            safety.endgame -=
              pressure_penalty.endgame;

            if (opponent_attacks.test(king)) {
                safety.middlegame -=
                  DIRECT_KING_ATTACK_PENALTY.middlegame;
                safety.endgame -=
                  DIRECT_KING_ATTACK_PENALTY.endgame;
            }

            Bitboard king_zone = king_ring;
            king_zone.set(king);
            const auto& enemy_colors =
              TEAM_COLORS[std::size_t(opponent)];
            const Score first_enemy_pressure =
              static_cast<Score>(
                (color_attacks[
                   std::size_t(enemy_colors[0])]
                 & king_zone)
                  .popcount());
            const Score second_enemy_pressure =
              static_cast<Score>(
                (color_attacks[
                   std::size_t(enemy_colors[1])]
                 & king_zone)
                  .popcount());
            const Score coordinated_pressure =
              first_enemy_pressure
              * second_enemy_pressure;
            safety.middlegame -= 2 * coordinated_pressure;
            safety.endgame -= coordinated_pressure;

            add_score(team_scores, team, safety);
        }

        if (static_evaluation.bishop_counts[
              std::size_t(color)] >= 2) {
            add_score(team_scores, team, BISHOP_PAIR_BONUS);
        }
    }

    // A connected pair has an unobstructed orthogonal ray between two allied
    // rooks. Each visible pair contributes one link from each rook.
    for (int team_index = 0;
         team_index < TEAM_NB;
         ++team_index) {
        const Team team = Team(team_index);
        const int links =
          connected_rook_links[std::size_t(team)];
        assert(links % 2 == 0);
        const int connected_pairs = links / 2;

        add_score(
          team_scores,
          team,
          {
            static_cast<Score>(connected_pairs)
              * CONNECTED_ROOK_BONUS.middlegame,
            static_cast<Score>(connected_pairs)
              * CONNECTED_ROOK_BONUS.endgame,
          });
    }

    const TaperedScore balance = {
      team_scores[RED_YELLOW].middlegame
        - team_scores[BLUE_GREEN].middlegame,
      team_scores[RED_YELLOW].endgame
        - team_scores[BLUE_GREEN].endgame,
    };
    return {
      material,
      blend(balance, phase),
    };
}

[[nodiscard]] constexpr Score positional_balance(
  const Position& position) noexcept {
    return evaluation_terms(position).positional;
}

}  // namespace EvaluationDetail

// The score is positive when the side-to-move team is favored. Positional
// terms are expressed from Red and Yellow's perspective before the final sign
// conversion, which preserves exact team antisymmetry.
[[nodiscard]] constexpr Score evaluate(
  const Position& position) noexcept {
    const EvaluationDetail::EvaluationTerms terms =
      EvaluationDetail::evaluation_terms(position);
    const Score red_yellow_score =
      terms.material + terms.positional;
    const Score bounded =
      EvaluationDetail::clamp_evaluation(red_yellow_score);

    return team_of(position.side_to_move()) == RED_YELLOW
      ? bounded
      : -bounded;
}

static_assert(std::numeric_limits<Score>::is_signed);
static_assert(sizeof(Score) >= 4);
static_assert(MAX_PIECE_VALUE == QUEEN_VALUE);
static_assert(MAX_MATERIAL_SCORE == 144000);
static_assert(MAX_POSITIONAL_SCORE > 0);
static_assert(MAX_EVALUATION_SCORE == 344000);
static_assert(
  MAX_EVALUATION_SCORE
  < std::numeric_limits<Score>::max());
static_assert(
  -MAX_EVALUATION_SCORE
  > std::numeric_limits<Score>::lowest());
static_assert(piece_value(KING) == 0);
static_assert(evaluate(Position{}) == 0);

}  // namespace Mockingbird
