#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

#include "types.h"

namespace Mockingbird {

// PositionKey stores the cached hash used for position lookup.
using PositionKey = std::uint64_t;

namespace Zobrist {

namespace Detail {

inline constexpr std::size_t REAL_PIECE_TYPE_NB =
  static_cast<std::size_t>(KING - PAWN + 1);
inline constexpr std::size_t REAL_PIECE_NB =
  std::size_t(COLOR_NB) * REAL_PIECE_TYPE_NB;
inline constexpr std::size_t CASTLING_STATE_NB =
  std::size_t{1}
  << (std::size_t(COLOR_NB) * CASTLING_SIDE_NB);
inline constexpr std::size_t EN_PASSANT_STATE_NB =
  std::size_t(SQUARE_NB) + 1;

struct Keys {
    std::array<
      std::array<PositionKey, SQUARE_NB>,
      REAL_PIECE_NB>
      pieces{};
    std::array<PositionKey, COLOR_NB> sides{};
    std::array<PositionKey, CASTLING_STATE_NB> castling{};
    std::array<
      std::array<PositionKey, EN_PASSANT_STATE_NB>,
      COLOR_NB>
      en_passant{};
};

// The fixed initial state makes every generated key reproducible.
class Generator {
  public:
    constexpr explicit Generator(PositionKey state) noexcept
        : state_(state) {}

    [[nodiscard]] constexpr PositionKey next() noexcept {
        state_ += 0x9E3779B97F4A7C15ULL;

        PositionKey value = state_;
        value =
          (value ^ (value >> 30))
          * 0xBF58476D1CE4E5B9ULL;
        value =
          (value ^ (value >> 27))
          * 0x94D049BB133111EBULL;
        return value ^ (value >> 31);
    }

  private:
    PositionKey state_;
};

[[nodiscard]] consteval Keys make_keys() {
    Keys keys;
    Generator generator{0x4D6F636B696E6762ULL};

    for (auto& piece_keys : keys.pieces) {
        for (PositionKey& key : piece_keys)
            key = generator.next();
    }

    for (PositionKey& key : keys.sides)
        key = generator.next();

    for (PositionKey& key : keys.castling)
        key = generator.next();

    for (auto& color_keys : keys.en_passant) {
        for (int square_index = 0;
             square_index < SQUARE_NB;
             ++square_index) {
            color_keys[std::size_t(square_index)] =
              generator.next();
        }

        color_keys[std::size_t(SQ_NONE)] = 0;
    }

    return keys;
}

inline constexpr Keys KEYS = make_keys();

[[nodiscard]] constexpr std::size_t piece_index(
  Piece piece) noexcept {
    assert(is_ok(piece));

    return std::size_t(color_of(piece)) * REAL_PIECE_TYPE_NB
         + static_cast<std::size_t>(type_of(piece) - PAWN);
}

}  // namespace Detail

// Preconditions: piece is valid and square is playable.
[[nodiscard]] constexpr PositionKey piece(
  Piece piece,
  Square square) noexcept {
    assert(is_ok(piece));
    assert(is_ok(square));
    return Detail::KEYS.pieces[
      Detail::piece_index(piece)][std::size_t(square)];
}

// Precondition: color is valid.
[[nodiscard]] constexpr PositionKey side(
  Color color) noexcept {
    assert(is_ok(color));
    return Detail::KEYS.sides[std::size_t(color)];
}

[[nodiscard]] constexpr PositionKey castling(
  std::uint8_t rights) noexcept {
    return Detail::KEYS.castling[std::size_t(rights)];
}

// SQ_NONE represents an absent target.
// Preconditions: color is valid and square is playable or SQ_NONE.
[[nodiscard]] constexpr PositionKey en_passant(
  Color color,
  Square square) noexcept {
    assert(is_ok(color));
    assert(square == SQ_NONE || is_ok(square));
    return Detail::KEYS.en_passant[
      std::size_t(color)][std::size_t(square)];
}

static_assert(Detail::REAL_PIECE_NB == 24);
static_assert(Detail::CASTLING_STATE_NB == 256);
static_assert(sizeof(PositionKey) == 8);
static_assert(
  Zobrist::en_passant(RED, SQ_NONE) == 0);

}  // namespace Zobrist

}  // namespace Mockingbird
