#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "position.h"

namespace Mockingbird {

// FenError identifies the field or token class that failed validation.
enum class FenError : std::uint8_t {
    FIELD_COUNT,
    SIDE,
    DEAD_FLAGS,
    UNSUPPORTED_DEAD_PLAYER,
    KINGSIDE_FLAGS,
    QUEENSIDE_FLAGS,
    POINTS,
    HALFMOVE,
    EN_PASSANT,
    EN_PASSANT_COORDINATE,
    EN_PASSANT_VICTIM,
    RANK_COUNT,
    RANK_TOKEN,
    RANK_WIDTH,
    CUTOUT,
    EMPTY_RUN,
    PIECE,
};

// offset is the zero-based byte position associated with the failure.
struct FenFailure {
    FenError code = FenError::FIELD_COUNT;
    std::size_t offset = 0;

    [[nodiscard]] friend constexpr bool operator==(
      const FenFailure&,
      const FenFailure&) noexcept = default;
};

using FenParseResult = std::expected<Position, FenFailure>;

// The supported FEN4 fields are:
//
//     side-dead-kingside-queenside-points-halfmove-[en-passant-]board
//
// Side uses R, B, Y, or G. The four comma-separated flag and point lists use
// Red, Blue, Yellow, Green order. Dead-player flags must all be zero because
// Position does not contain eliminated-player state.
//
// The optional en-passant object has this exact structure:
//
//     {'enPassant':('target:victim','','','')}
//
// Its four entries use the same color order. Board ranks run from 14 through
// 1 and are separated by '/'. Comma-separated board tokens are "x" for one
// cut-out square, a positive decimal empty run, or a lowercase color followed
// by an uppercase piece type.
//
// ASCII whitespace may surround the complete notation and individual
// hyphen-separated fields. Whitespace inside a field is rejected.
[[nodiscard]] FenParseResult parse_fen(
  std::string_view fen) noexcept;

// Serialization emits all players alive, zero points, and a zero halfmove
// count. Board contents, side to move, castling rights, and en-passant targets
// are read from position.
[[nodiscard]] std::string serialize_fen(
  const Position& position);

}  // namespace Mockingbird
