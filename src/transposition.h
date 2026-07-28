#pragma once

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "evaluate.h"
#include "move.h"
#include "repetition.h"

namespace Mockingbird {

enum class TranspositionBound : std::uint8_t {
    NONE,
    EXACT,
    LOWER,
    UPPER,
};

// A score entry is identified by both the board position and the repetition
// history context under which the score was searched.
struct TranspositionEntry {
    PositionKey position_key = 0;
    HistoryContext history;
    Move best_move = Move::none();
    Score score = 0;
    int depth = 0;
    std::uint32_t generation = 0;
    TranspositionBound bound =
      TranspositionBound::NONE;

    [[nodiscard]] constexpr bool occupied() const noexcept {
        return bound != TranspositionBound::NONE;
    }

    [[nodiscard]] constexpr bool matches(
      PositionKey key,
      const HistoryContext& context) const noexcept {
        return occupied()
            && position_key == key
            && history == context;
    }
};

// Each bucket retains up to four entries that share the same position-key
// index. Full position keys are compared before any entry is used.
class TranspositionTable {
  public:
    static constexpr std::size_t BUCKET_SIZE = 4;
    static constexpr std::size_t
      DEFAULT_BUCKET_COUNT = 4096;

    // A zero bucket count is normalized to one in builds where assertions are
    // disabled.
    // Precondition: bucket_count is greater than zero.
    explicit TranspositionTable(
      std::size_t bucket_count =
        DEFAULT_BUCKET_COUNT)
        : buckets_(
            bucket_count == 0
              ? 1
              : bucket_count) {
        assert(bucket_count > 0);
    }

    [[nodiscard]] std::size_t
    bucket_count() const noexcept {
        return buckets_.size();
    }

    [[nodiscard]] std::size_t capacity() const noexcept {
        return buckets_.size() * BUCKET_SIZE;
    }

    [[nodiscard]] std::uint32_t
    generation() const noexcept {
        return generation_;
    }

    // The current generation marks entries stored or successfully probed
    // during one root search call.
    void new_search() noexcept {
        if (generation_
            == std::numeric_limits<
                 std::uint32_t>::max()) {
            clear();
            generation_ = 1;
            return;
        }

        ++generation_;
    }

    void clear() noexcept {
        for (Bucket& bucket : buckets_) {
            for (TranspositionEntry& entry :
                 bucket.entries)
                entry = {};
        }
    }

    [[nodiscard]] const TranspositionEntry* find(
      PositionKey key,
      const HistoryContext& history) const noexcept {
        const Bucket& bucket = bucket_for(key);
        for (const TranspositionEntry& entry :
             bucket.entries) {
            if (entry.matches(key, history))
                return &entry;
        }

        return nullptr;
    }

    // A successful probe refreshes the entry to the current generation.
    [[nodiscard]] const TranspositionEntry* probe(
      PositionKey key,
      const HistoryContext& history) noexcept {
        Bucket& bucket = bucket_for(key);
        for (TranspositionEntry& entry :
             bucket.entries) {
            if (!entry.matches(key, history))
                continue;

            entry.generation = generation_;
            return &entry;
        }

        return nullptr;
    }

    // An entry for another history can provide only a move hint. Search
    // validates the complete move against the generated move list.
    [[nodiscard]] Move best_move(
      PositionKey key,
      const HistoryContext& history) const noexcept {
        const TranspositionEntry* matching =
          find(key, history);
        if (matching
            && matching->best_move.is_board_move()) {
            return matching->best_move;
        }

        return best_move(key);
    }

    // Returns the preferred stored move for a position key without qualifying
    // the entry by history.
    [[nodiscard]] Move best_move(
      PositionKey key) const noexcept {
        const Bucket& bucket = bucket_for(key);
        const TranspositionEntry* preferred = nullptr;

        for (const TranspositionEntry& entry :
             bucket.entries) {
            if (!entry.occupied()
                || entry.position_key != key
                || !entry.best_move.is_board_move())
                continue;

            if (!preferred
                || prefer_move_entry(
                     entry, *preferred)) {
                preferred = &entry;
            }
        }

        return preferred
            ? preferred->best_move
            : Move::none();
    }

    // Stores a completed main-search score or bound.
    // Preconditions:
    // - depth is positive;
    // - bound is EXACT, LOWER, or UPPER;
    // - best_move is a board move or Move::none().
    void store(
      PositionKey key,
      const HistoryContext& history,
      int depth,
      Score score,
      TranspositionBound bound,
      Move best_move) noexcept {
        assert(depth > 0);
        assert(bound != TranspositionBound::NONE);
        assert(
          best_move.is_board_move()
          || best_move.is_none());

        Bucket& bucket = bucket_for(key);
        for (TranspositionEntry& entry :
             bucket.entries) {
            if (!entry.matches(key, history))
                continue;

            entry.generation = generation_;
            const bool replace =
              depth > entry.depth
              || (depth == entry.depth
                  && (entry.bound
                        != TranspositionBound::EXACT
                      || bound
                           == TranspositionBound::EXACT));
            if (replace) {
                entry = make_entry(
                  key,
                  history,
                  depth,
                  score,
                  bound,
                  best_move);
            }
            return;
        }

        TranspositionEntry* destination = nullptr;
        for (TranspositionEntry& entry :
             bucket.entries) {
            if (!entry.occupied()) {
                destination = &entry;
                break;
            }

            if (!destination
                || prefer_replacement(
                     entry, *destination)) {
                destination = &entry;
            }
        }

        assert(destination);
        *destination = make_entry(
          key,
          history,
          depth,
          score,
          bound,
          best_move);
    }

  private:
    struct Bucket {
        std::array<
          TranspositionEntry,
          BUCKET_SIZE>
          entries{};
    };

    [[nodiscard]] std::size_t bucket_index(
      PositionKey key) const noexcept {
        const std::size_t count = buckets_.size();
        if (std::has_single_bit(count)) {
            return static_cast<std::size_t>(
              key
              & static_cast<PositionKey>(
                  count - 1));
        }

        return static_cast<std::size_t>(
          key
          % static_cast<PositionKey>(count));
    }

    [[nodiscard]] Bucket& bucket_for(
      PositionKey key) noexcept {
        return buckets_[bucket_index(key)];
    }

    [[nodiscard]] const Bucket& bucket_for(
      PositionKey key) const noexcept {
        return buckets_[bucket_index(key)];
    }

    [[nodiscard]] constexpr TranspositionEntry
    make_entry(
      PositionKey key,
      const HistoryContext& history,
      int depth,
      Score score,
      TranspositionBound bound,
      Move best_move) const noexcept {
        return {
          key,
          history,
          best_move,
          score,
          depth,
          generation_,
          bound,
        };
    }

    [[nodiscard]] constexpr bool prefer_move_entry(
      const TranspositionEntry& candidate,
      const TranspositionEntry& current) const noexcept {
        const bool candidate_is_current =
          candidate.generation == generation_;
        const bool current_is_current =
          current.generation == generation_;

        if (candidate_is_current != current_is_current)
            return candidate_is_current;

        return candidate.depth > current.depth;
    }

    [[nodiscard]] constexpr bool prefer_replacement(
      const TranspositionEntry& candidate,
      const TranspositionEntry& current) const noexcept {
        const bool candidate_is_stale =
          candidate.generation != generation_;
        const bool current_is_stale =
          current.generation != generation_;

        if (candidate_is_stale != current_is_stale)
            return candidate_is_stale;

        const bool candidate_is_exact =
          candidate.bound
          == TranspositionBound::EXACT;
        const bool current_is_exact =
          current.bound
          == TranspositionBound::EXACT;

        if (candidate_is_exact != current_is_exact)
            return !candidate_is_exact;

        return candidate.depth < current.depth;
    }

    std::vector<Bucket> buckets_;
    std::uint32_t generation_ = 1;
};

static_assert(TranspositionTable::BUCKET_SIZE == 4);
static_assert(
  TranspositionTable::DEFAULT_BUCKET_COUNT > 0);
static_assert(
  TranspositionEntry{}.bound
  == TranspositionBound::NONE);
static_assert(!TranspositionEntry{}.occupied());

}  // namespace Mockingbird
