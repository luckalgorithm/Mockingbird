#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "position.h"

namespace Mockingbird {

// A position notation contains four whitespace-separated fields:
//
//     board side-to-move castling en-passant
//
// The board field contains ranks 14 through 1 separated by '/'. Cut-out
// corner squares are omitted. Ranks 1 through 3 and 12 through 14 encode files
// d through k; ranks 4 through 11 encode files a through n. Decimal numbers
// from 1 through 14 encode runs of empty squares without leading zeroes.
// Pieces use a lowercase color r, b, y, or g followed by an uppercase type P,
// N, B, R, Q, or K.
//
// The side field is r, b, y, or g. The castling field is '-' or a sequence of
// color-and-side pairs such as rKrQbK. Parsing accepts the pairs in any order
// and rejects duplicates. Serialization orders them by Red, Blue, Yellow,
// Green and then kingside, queenside. The en-passant field is '-' when every
// target is absent; otherwise it contains four comma-separated targets in
// Red, Blue, Yellow, Green order. Each target is '-' or a playable lowercase
// coordinate whose rank has no leading zero. Serialization uses single spaces
// between fields.
enum class NotationError : std::uint8_t {
    FIELD_COUNT,
    RANK_COUNT,
    RANK_WIDTH,
    EMPTY_RUN,
    PIECE,
    SIDE,
    CASTLING,
    DUPLICATE_CASTLING,
    EN_PASSANT,
};

// offset is the zero-based byte position at which parsing failed.
struct NotationFailure {
    NotationError code = NotationError::FIELD_COUNT;
    std::size_t offset = 0;

    [[nodiscard]] friend constexpr bool operator==(
      const NotationFailure&,
      const NotationFailure&) noexcept = default;
};

using PositionParseResult =
  std::expected<Position, NotationFailure>;

// Parses one complete position notation. ASCII whitespace may surround fields
// or separate adjacent fields. Whitespace is not allowed inside a field.
[[nodiscard]] PositionParseResult parse_position(
  std::string_view notation) noexcept;

// Produces the canonical spelling of every state stored by Position.
[[nodiscard]] std::string serialize_position(
  const Position& position);

}  // namespace Mockingbird
