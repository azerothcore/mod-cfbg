/*
 * Copyright (С) since 2019+ AzerothCore <www.azerothcore.org>
 * Licence MIT https://opensource.org/MIT
 */

#ifndef _CFBG_BEST_SPLIT_H_
#define _CFBG_BEST_SPLIT_H_

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

// Picks a side for each item (a queued group, or a player the gate-open
// repair may move) so the team level sums end up close: exact up to
// ExactLimit items, a swap search never worse than the current sides above
// that. Side 0 is Alliance, side 1 Horde (as TeamId).
namespace CFBGBestSplit
{
    // level and ilvl are sums over the group's members. realA is how many
    // of them are Alliance by real faction, never more than size.
    struct Item
    {
        uint32_t size = 0;
        uint32_t level = 0;
        uint32_t ilvl = 0;
        uint32_t realA = 0;
        uint8_t current = 0;
        uint8_t preferred = 0;
    };

    // Items are in tie-break priority order: on a full tie the earliest item
    // that differs stays on its preferred side.
    struct Problem
    {
        std::array<uint32_t, 2> baseLevel{};
        std::array<uint32_t, 2> baseIlvl{};
        std::vector<Item> items;
        bool keepCurrentUnlessLevelImproves = false;
    };

    struct Score
    {
        uint32_t levelGap = 0;
        uint32_t ilvlGap = 0;
        uint32_t disguised = 0;

        bool operator<(Score const& o) const
        {
            return std::tie(levelGap, ilvlGap, disguised) <
                std::tie(o.levelGap, o.ilvlGap, o.disguised);
        }
    };

    constexpr std::size_t ExactLimit = 20;

    inline uint32_t Gap(uint64_t a, uint64_t b)
    {
        return static_cast<uint32_t>(a > b ? a - b : b - a);
    }

    inline uint32_t Disguised(Item const& item, uint8_t side)
    {
        return side == 0 ? item.size - item.realA : item.realA;
    }

    inline std::vector<uint8_t> CurrentSides(Problem const& p)
    {
        std::vector<uint8_t> sides;
        sides.reserve(p.items.size());
        for (Item const& item : p.items)
            sides.push_back(item.current);
        return sides;
    }

    inline Score Evaluate(Problem const& p, std::vector<uint8_t> const& sides)
    {
        std::array<uint64_t, 2> level{ p.baseLevel[0], p.baseLevel[1] };
        std::array<uint64_t, 2> ilvl{ p.baseIlvl[0], p.baseIlvl[1] };
        Score score;
        for (std::size_t i = 0; i < p.items.size(); ++i)
        {
            Item const& item = p.items[i];
            level[sides[i]] += item.level;
            ilvl[sides[i]] += item.ilvl;
            score.disguised += Disguised(item, sides[i]);
        }

        score.levelGap = Gap(level[0], level[1]);
        score.ilvlGap = Gap(ilvl[0], ilvl[1]);
        return score;
    }

    namespace Detail
    {
        struct HalfEntry
        {
            uint32_t countA = 0;
            uint32_t levelA = 0;
            uint32_t ilvlA = 0;
            uint32_t disguised = 0;
            uint32_t rank = 0;
            uint32_t mask = 0; // bit j set: item first + j on side 0
        };

        inline std::vector<HalfEntry> Enumerate(Problem const& p,
            std::size_t first, std::size_t count)
        {
            std::vector<HalfEntry> entries;
            entries.reserve(std::size_t(1) << count);
            for (uint32_t mask = 0; mask < (uint32_t(1) << count); ++mask)
            {
                HalfEntry e;
                e.mask = mask;
                for (std::size_t j = 0; j < count; ++j)
                {
                    Item const& item = p.items[first + j];
                    uint8_t side = (mask >> j) & 1 ? 0 : 1;
                    if (side == 0)
                    {
                        e.countA += item.size;
                        e.levelA += item.level;
                        e.ilvlA += item.ilvl;
                    }
                    e.disguised += Disguised(item, side);
                    if (side != item.preferred)
                        e.rank |= uint32_t(1) << (count - 1 - j);
                }
                entries.push_back(e);
            }
            return entries;
        }

        inline auto LevelKey(HalfEntry const& e)
        {
            return std::make_tuple(e.countA, e.levelA);
        }

        inline auto FullKey(HalfEntry const& e)
        {
            return std::make_tuple(e.countA, e.levelA, e.ilvlA, e.disguised,
                e.rank);
        }
    }

    // Meet in the middle: every assignment of each half, then for each left
    // half the matching right halves are found by binary search.
    inline std::vector<uint8_t> SolveExact(Problem const& p)
    {
        using namespace Detail;

        std::size_t const n = p.items.size();
        std::size_t const nLeft = n / 2;
        std::size_t const nRight = n - nLeft;

        uint32_t targetA = 0;
        int64_t totalLevel = int64_t(p.baseLevel[0]) + p.baseLevel[1];
        int64_t totalIlvl = int64_t(p.baseIlvl[0]) + p.baseIlvl[1];
        for (Item const& item : p.items)
        {
            if (item.current == 0)
                targetA += item.size;
            totalLevel += item.level;
            totalIlvl += item.ilvl;
        }

        std::vector<HalfEntry> left = Enumerate(p, 0, nLeft);
        std::vector<HalfEntry> right = Enumerate(p, nLeft, nRight);

        // Same (count, level, ilvl) only differ in disguise and rank: keep
        // the best one.
        std::sort(right.begin(), right.end(),
            [](HalfEntry const& a, HalfEntry const& b)
            { return FullKey(a) < FullKey(b); });
        right.erase(std::unique(right.begin(), right.end(),
            [](HalfEntry const& a, HalfEntry const& b)
            {
                return a.countA == b.countA && a.levelA == b.levelA &&
                    a.ilvlA == b.ilvlA;
            }), right.end());

        auto countRange = [&right](uint32_t countA)
        {
            auto lo = std::lower_bound(right.begin(), right.end(), countA,
                [](HalfEntry const& e, uint32_t v) { return e.countA < v; });
            auto hi = std::upper_bound(lo, right.end(), countA,
                [](uint32_t v, HalfEntry const& e) { return v < e.countA; });
            return std::make_pair(lo, hi);
        };

        auto levelRange = [&right](uint32_t countA, uint32_t levelA)
        {
            auto const key = std::make_tuple(countA, levelA);
            auto lo = std::lower_bound(right.begin(), right.end(), key,
                [](HalfEntry const& e, auto const& v)
                { return LevelKey(e) < v; });
            auto hi = std::upper_bound(lo, right.end(), key,
                [](auto const& v, HalfEntry const& e)
                { return v < LevelKey(e); });
            return std::make_pair(lo, hi);
        };

        // Pass 1: best reachable level gap.
        std::optional<int64_t> bestLevelGap;
        for (HalfEntry const& l : left)
        {
            if (l.countA > targetA)
                continue;

            auto [lo, hi] = countRange(targetA - l.countA);
            if (lo == hi)
                continue;

            int64_t const need = totalLevel - 2 * (int64_t(p.baseLevel[0]) +
                l.levelA);
            auto it = std::lower_bound(lo, hi, need / 2,
                [](HalfEntry const& e, int64_t v)
                { return int64_t(e.levelA) < v; });

            for (auto cand : { it, it == lo ? hi : std::prev(it) })
            {
                if (cand == hi)
                    continue;
                int64_t gap = 2 * int64_t(cand->levelA) - need;
                gap = gap < 0 ? -gap : gap;
                if (!bestLevelGap || gap < *bestLevelGap)
                    bestLevelGap = gap;
            }
        }

        if (!bestLevelGap)
            return CurrentSides(p);

        // Pass 2: among the right halves reaching that level gap, the best by
        // ilvl gap, disguise and rank.
        using Key = std::tuple<int64_t, uint32_t, uint32_t, uint32_t>;
        std::optional<Key> bestKey;
        uint32_t bestLeftMask = 0;
        uint32_t bestRightMask = 0;

        for (HalfEntry const& l : left)
        {
            if (l.countA > targetA)
                continue;

            uint32_t const countR = targetA - l.countA;
            int64_t const need = totalLevel - 2 * (int64_t(p.baseLevel[0]) +
                l.levelA);
            int64_t const ilvlNeed = totalIlvl - 2 * (int64_t(p.baseIlvl[0]) +
                l.ilvlA);

            std::array<int64_t, 2> const doubled{ need + *bestLevelGap,
                need - *bestLevelGap };
            for (std::size_t k = 0; k < doubled.size(); ++k)
            {
                if (k == 1 && *bestLevelGap == 0)
                    break;
                if (doubled[k] < 0 || doubled[k] % 2 != 0)
                    continue;

                uint32_t const r = static_cast<uint32_t>(doubled[k] / 2);
                auto [lo, hi] = levelRange(countR, r);
                if (lo == hi)
                    continue;

                // Ceiling, so both ilvls tied on gap are seen.
                int64_t const ilvlTarget = ilvlNeed <= 0 ? 0 :
                    (ilvlNeed + 1) / 2;
                auto it = std::lower_bound(lo, hi, ilvlTarget,
                    [](HalfEntry const& e, int64_t v)
                    { return int64_t(e.ilvlA) < v; });

                for (auto cand : { it, it == lo ? hi : std::prev(it) })
                {
                    if (cand == hi)
                        continue;
                    int64_t ilvlGap = 2 * int64_t(cand->ilvlA) - ilvlNeed;
                    ilvlGap = ilvlGap < 0 ? -ilvlGap : ilvlGap;
                    Key key{ ilvlGap, l.disguised + cand->disguised, l.rank,
                        cand->rank };
                    if (!bestKey || key < *bestKey)
                    {
                        bestKey = key;
                        bestLeftMask = l.mask;
                        bestRightMask = cand->mask;
                    }
                }
            }
        }

        std::vector<uint8_t> sides(n);
        for (std::size_t j = 0; j < nLeft; ++j)
            sides[j] = (bestLeftMask >> j) & 1 ? 0 : 1;
        for (std::size_t j = 0; j < nRight; ++j)
            sides[nLeft + j] = (bestRightMask >> j) & 1 ? 0 : 1;
        return sides;
    }

    // Swap local search for passes too large for SolveExact. Swaps only
    // same-size items, so head counts stay put; never worse than current.
    inline std::vector<uint8_t> SolveLocal(Problem const& p)
    {
        std::vector<uint8_t> sides = CurrentSides(p);
        std::array<uint64_t, 2> level{ p.baseLevel[0], p.baseLevel[1] };
        std::array<uint64_t, 2> ilvl{ p.baseIlvl[0], p.baseIlvl[1] };
        uint32_t disguised = 0;
        for (std::size_t i = 0; i < p.items.size(); ++i)
        {
            level[sides[i]] += p.items[i].level;
            ilvl[sides[i]] += p.items[i].ilvl;
            disguised += Disguised(p.items[i], sides[i]);
        }

        Score score{ Gap(level[0], level[1]), Gap(ilvl[0], ilvl[1]),
            disguised };

        for (int sweep = 0; sweep < 64; ++sweep)
        {
            bool swapped = false;
            for (std::size_t i = 0; i < p.items.size(); ++i)
            {
                if (sides[i] != 0)
                    continue;

                Item const& a = p.items[i];
                for (std::size_t j = 0; j < p.items.size(); ++j)
                {
                    if (sides[j] != 1 || p.items[j].size != a.size)
                        continue;

                    Item const& h = p.items[j];
                    uint64_t const levelA = level[0] - a.level + h.level;
                    uint64_t const levelH = level[1] - h.level + a.level;
                    uint64_t const ilvlA = ilvl[0] - a.ilvl + h.ilvl;
                    uint64_t const ilvlH = ilvl[1] - h.ilvl + a.ilvl;
                    uint32_t const dis = disguised - Disguised(a, 0) -
                        Disguised(h, 1) + Disguised(a, 1) + Disguised(h, 0);
                    Score const next{ Gap(levelA, levelH), Gap(ilvlA, ilvlH),
                        dis };
                    if (!(next < score))
                        continue;

                    sides[i] = 1;
                    sides[j] = 0;
                    level = { levelA, levelH };
                    ilvl = { ilvlA, ilvlH };
                    disguised = dis;
                    score = next;
                    swapped = true;
                    break;
                }
            }

            if (!swapped)
                break;
        }

        return sides;
    }

    inline std::vector<uint8_t> Solve(Problem const& p)
    {
        if (p.items.empty())
            return {};

        std::vector<uint8_t> best = p.items.size() <= ExactLimit ?
            SolveExact(p) : SolveLocal(p);

        if (p.keepCurrentUnlessLevelImproves)
        {
            std::vector<uint8_t> current = CurrentSides(p);
            if (Evaluate(p, best).levelGap >= Evaluate(p, current).levelGap)
                return current;
        }

        return best;
    }
}

#endif // _CFBG_BEST_SPLIT_H_
