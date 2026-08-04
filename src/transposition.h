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

// Static evaluation cannot produce this value, so it marks an entry without a
// cached position evaluation.
inline constexpr Score NO_STATIC_EVALUATION =
  std::numeric_limits<Score>::min();

// Entries are identified by the complete position key. The history tag permits
// a score from an earlier root-search generation to be reused only when the
// caller has the same repetition context. The tag is a compact probabilistic
// guard; the complete position key remains available for exact position
// identity. Within one generation, repetition-insensitive scores can be shared
// across paths to the position.
struct TranspositionEntry {
    PositionKey position_key = 0;
    std::uint32_t history_tag = 0;
    Score score = 0;
    Score static_evaluation = NO_STATIC_EVALUATION;
    std::uint32_t generation = 0;
    Move best_move = Move::none();
    std::uint16_t depth = 0;
    TranspositionBound bound =
      TranspositionBound::NONE;
    bool stand_pat = false;

    [[nodiscard]] constexpr bool occupied() const noexcept {
        return bound != TranspositionBound::NONE;
    }

    [[nodiscard]] constexpr bool
    has_static_evaluation() const noexcept {
        return static_evaluation
            != NO_STATIC_EVALUATION;
    }

    [[nodiscard]] constexpr bool matches_position(
      PositionKey key) const noexcept {
        return occupied()
            && position_key == key;
    }

    [[nodiscard]] constexpr bool matches_history(
      const HistoryContext& context) const noexcept {
        return history_tag
            == make_history_tag(context);
    }

    [[nodiscard]] constexpr bool matches(
      PositionKey key,
      const HistoryContext& context) const noexcept {
        return matches_position(key)
            && matches_history(context);
    }

  private:
    [[nodiscard]] static constexpr std::uint32_t
    make_history_tag(
      const HistoryContext& context) noexcept {
        const std::uint64_t length =
          static_cast<std::uint64_t>(
            context.length);
        const std::uint64_t wide_tag =
          context.first
          ^ std::rotl(context.second, 23)
          ^ RepetitionDetail::mix(
              length
              ^ 0x9E3779B97F4A7C15ULL);
        return static_cast<std::uint32_t>(
          wide_tag ^ (wide_tag >> 32));
    }

    friend class TranspositionTable;
};

// Each bucket retains up to four entries that share the same position-key
// index. Full position keys are compared before any entry is used.
class TranspositionTable {
  public:
    static constexpr std::size_t BUCKET_SIZE = 4;
    static constexpr std::size_t
      DEFAULT_BUCKET_COUNT = 8192;

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

    // Samples the first 1,000 entries and returns current-generation
    // occupancy in per-mille units. Small tables sample their full capacity.
    // The method is intended for reporting between completed iterations, not
    // for the recursive search path.
    [[nodiscard]] std::uint16_t
    hashfull_per_mille() const noexcept {
        constexpr std::size_t SAMPLE_SIZE = 1000;
        const std::size_t sampled =
          capacity() < SAMPLE_SIZE
            ? capacity()
            : SAMPLE_SIZE;
        if (sampled == 0)
            return 0;

        std::size_t current = 0;
        for (std::size_t index = 0;
             index < sampled;
             ++index) {
            const TranspositionEntry& entry =
              buckets_[index / BUCKET_SIZE]
                .entries[index % BUCKET_SIZE];
            if (entry.occupied()
                && entry.generation == generation_) {
                ++current;
            }
        }

        return static_cast<std::uint16_t>(
          current * SAMPLE_SIZE / sampled);
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
      PositionKey key) const noexcept {
        const Bucket& bucket = bucket_for(key);
        for (const TranspositionEntry& entry :
             bucket.entries) {
            if (entry.matches_position(key))
                return &entry;
        }

        return nullptr;
    }

    // Returns an entry only when both its position and stored history tag
    // match. This overload is intended for diagnostics and tests; score probes
    // apply generation-aware eligibility separately.
    [[nodiscard]] const TranspositionEntry* find(
      PositionKey key,
      const HistoryContext& history) const noexcept {
        const TranspositionEntry* entry = find(key);
        return entry && entry->matches_history(history)
            ? entry
            : nullptr;
    }

    // Current-generation scores are position-keyed. A stale score is eligible
    // only for its original repetition context. A successful safe probe joins
    // the entry to the current root-search generation.
    [[nodiscard]] const TranspositionEntry* probe(
      PositionKey key,
      const HistoryContext& history) noexcept {
        Bucket& bucket = bucket_for(key);
        for (TranspositionEntry& entry :
             bucket.entries) {
            if (!entry.matches_position(key))
                continue;

            if (entry.generation != generation_
                && !entry.matches_history(history)) {
                return nullptr;
            }

            entry.generation = generation_;
            return &entry;
        }

        return nullptr;
    }

    // History-mismatched and stale entries can still provide a move hint.
    // Search validates the complete move against the generated move list.
    [[nodiscard]] Move best_move(
      PositionKey key,
      const HistoryContext&) const noexcept {
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
      Move best_move,
      Score static_evaluation =
        NO_STATIC_EVALUATION) noexcept {
        assert(depth > 0);
        assert(
          depth
          <= std::numeric_limits<
               std::uint16_t>::max());
        assert(bound != TranspositionBound::NONE);
        assert(
          best_move.is_board_move()
          || best_move.is_none());
        assert(
          valid_static_evaluation(
            static_evaluation));

        Bucket& bucket = bucket_for(key);
        for (TranspositionEntry& entry :
             bucket.entries) {
            if (!entry.matches_position(key))
                continue;

            const bool replace =
              entry.generation != generation_
              || depth > entry.depth
              || (depth == entry.depth
                  && (entry.bound
                        != TranspositionBound::EXACT
                      || bound
                           == TranspositionBound::EXACT));
            const Score retained_static_evaluation =
              select_static_evaluation(
                entry.static_evaluation,
                static_evaluation);
            if (replace) {
                entry = make_entry(
                  key,
                  history,
                  depth,
                  score,
                  bound,
                  best_move,
                  false,
                  retained_static_evaluation);
            } else {
                entry.generation = generation_;
                entry.static_evaluation =
                  retained_static_evaluation;
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
          best_move,
          false,
          static_evaluation);
    }

    // Stores a completed quiescence score without replacing positive-depth
    // information. A full bucket accepts the entry only when another
    // quiescence entry is available for replacement.
    // Preconditions:
    // - bound is EXACT, LOWER, or UPPER;
    // - best_move is a board move or Move::none();
    // - stand_pat implies best_move is Move::none().
    void store_quiescence(
      PositionKey key,
      const HistoryContext& history,
      Score score,
      TranspositionBound bound,
      Move best_move,
      bool stand_pat,
      Score static_evaluation =
        NO_STATIC_EVALUATION) noexcept {
        assert(bound != TranspositionBound::NONE);
        assert(
          best_move.is_board_move()
          || best_move.is_none());
        assert(!stand_pat || best_move.is_none());
        assert(
          valid_static_evaluation(
            static_evaluation));

        Bucket& bucket = bucket_for(key);
        for (TranspositionEntry& entry :
             bucket.entries) {
            if (!entry.matches_position(key))
                continue;

            const Score retained_static_evaluation =
              select_static_evaluation(
                entry.static_evaluation,
                static_evaluation);
            if (entry.depth > 0) {
                entry.static_evaluation =
                  retained_static_evaluation;
                return;
            }

            const bool replace =
              entry.generation != generation_
              || entry.bound
                   != TranspositionBound::EXACT
              || bound == TranspositionBound::EXACT;
            if (replace) {
                entry = make_entry(
                  key,
                  history,
                  0,
                  score,
                  bound,
                  best_move,
                  stand_pat,
                  retained_static_evaluation);
            } else {
                entry.generation = generation_;
                entry.static_evaluation =
                  retained_static_evaluation;
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

            if (entry.depth > 0)
                continue;

            if (!destination
                || prefer_replacement(
                     entry, *destination)) {
                destination = &entry;
            }
        }

        if (!destination)
            return;

        *destination = make_entry(
          key,
          history,
          0,
          score,
          bound,
          best_move,
          stand_pat,
          static_evaluation);
    }

  private:
    struct alignas(64) Bucket {
        std::array<
          TranspositionEntry,
          BUCKET_SIZE>
          entries{};
    };

    static_assert(sizeof(Bucket) == 128);
    static_assert(alignof(Bucket) == 64);

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
      Move best_move,
      bool stand_pat = false,
      Score static_evaluation =
        NO_STATIC_EVALUATION) const noexcept {
        return {
          key,
          TranspositionEntry::make_history_tag(
            history),
          score,
          static_evaluation,
          generation_,
          best_move,
          static_cast<std::uint16_t>(depth),
          bound,
          stand_pat,
        };
    }

    [[nodiscard]] static constexpr bool
    valid_static_evaluation(Score score) noexcept {
        return score == NO_STATIC_EVALUATION
            || (score >= -MAX_EVALUATION_SCORE
                && score <= MAX_EVALUATION_SCORE);
    }

    [[nodiscard]] static constexpr Score
    select_static_evaluation(
      Score cached,
      Score supplied) noexcept {
        return supplied == NO_STATIC_EVALUATION
          ? cached
          : supplied;
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
static_assert(
  !TranspositionEntry{}.has_static_evaluation());
static_assert(sizeof(TranspositionEntry) == 32);

}  // namespace Mockingbird
