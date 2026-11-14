/*
 * Copyright (С) since 2019+ AzerothCore <www.azerothcore.org>
 * Licence MIT https://opensource.org/MIT
 */

#include "CFBGBestSplit.h"
#include "CFBGTestUtil.h"
#include "gtest/gtest.h"
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

using CFBGBestSplit::Item;
using CFBGBestSplit::Problem;
using CFBGTest::AbsDiff;
using CFBGTest::SplitMix64;

namespace
{
    struct RefScore
    {
        uint32_t levelGap;
        uint32_t ilvlGap;
        uint32_t disguised;
        uint64_t rank;

        auto Key() const
        {
            return std::make_tuple(levelGap, ilvlGap, disguised, rank);
        }
    };

    // Scores on its own, never through CFBGBestSplit::Evaluate.
    RefScore Score(Problem const& p, std::vector<uint8_t> const& sides)
    {
        uint64_t levelA = p.baseLevel[0];
        uint64_t levelH = p.baseLevel[1];
        uint64_t ilvlA = p.baseIlvl[0];
        uint64_t ilvlH = p.baseIlvl[1];
        RefScore s{ 0, 0, 0, 0 };
        std::size_t const n = p.items.size();
        for (std::size_t i = 0; i < n; ++i)
        {
            Item const& it = p.items[i];
            if (sides[i] == 0)
            {
                levelA += it.level;
                ilvlA += it.ilvl;
                s.disguised += it.size - it.realA;
            }
            else
            {
                levelH += it.level;
                ilvlH += it.ilvl;
                s.disguised += it.realA;
            }

            if (sides[i] != it.preferred)
                s.rank |= uint64_t(1) << (n - 1 - i);
        }

        s.levelGap = AbsDiff(levelA, levelH);
        s.ilvlGap = AbsDiff(ilvlA, ilvlH);
        return s;
    }

    uint32_t CountA(Problem const& p, std::vector<uint8_t> const& sides)
    {
        uint32_t count = 0;
        for (std::size_t i = 0; i < p.items.size(); ++i)
            if (sides[i] == 0)
                count += p.items[i].size;
        return count;
    }

    std::vector<uint8_t> Current(Problem const& p)
    {
        std::vector<uint8_t> sides;
        for (Item const& it : p.items)
            sides.push_back(it.current);
        return sides;
    }

    std::vector<uint8_t> BruteForce(Problem const& p)
    {
        std::vector<uint8_t> const current = Current(p);
        uint32_t const targetA = CountA(p, current);
        std::size_t const n = p.items.size();

        std::vector<uint8_t> best = current;
        RefScore bestScore = Score(p, current);
        std::vector<uint8_t> sides(n);
        for (uint64_t mask = 0; mask < (uint64_t(1) << n); ++mask)
        {
            for (std::size_t i = 0; i < n; ++i)
                sides[i] = (mask >> i) & 1;
            if (CountA(p, sides) != targetA)
                continue;

            RefScore s = Score(p, sides);
            if (s.Key() < bestScore.Key())
            {
                best = sides;
                bestScore = s;
            }
        }

        return best;
    }

    std::vector<uint8_t> ApplyKeep(Problem const& p,
        std::vector<uint8_t> const& best)
    {
        std::vector<uint8_t> const current = Current(p);
        if (p.keepCurrentUnlessLevelImproves &&
            Score(p, best).levelGap >= Score(p, current).levelGap)
            return current;
        return best;
    }

    Problem Generate(SplitMix64& rng, std::size_t n, bool narrowIlvl)
    {
        Problem p;
        bool const narrowLevel = rng.Range(0, 1) == 1;
        for (std::size_t i = 0; i < n; ++i)
        {
            Item it;
            uint32_t const r = rng.Range(0, 9);
            it.size = r < 7 ? 1 : (r < 9 ? 2 : 3);
            for (uint32_t m = 0; m < it.size; ++m)
            {
                it.level += narrowLevel ? rng.Range(70, 72) :
                    rng.Range(10, 80);
                it.ilvl += narrowIlvl ? rng.Range(0, 2) : rng.Range(0, 260);
                it.realA += rng.Range(0, 1);
            }
            it.current = static_cast<uint8_t>(rng.Range(0, 1));
            it.preferred = static_cast<uint8_t>(rng.Range(0, 1));
            p.items.push_back(it);
        }

        if (rng.Range(0, 1))
        {
            p.baseLevel = { rng.Range(0, 400), rng.Range(0, 400) };
            p.baseIlvl = { rng.Range(0, 1500), rng.Range(0, 1500) };
        }
        return p;
    }

    Item Solo(uint32_t level, uint8_t current, uint32_t ilvl = 0,
        uint32_t realA = 0)
    {
        Item it;
        it.size = 1;
        it.level = level;
        it.ilvl = ilvl;
        it.realA = realA;
        it.current = current;
        it.preferred = current;
        return it;
    }

    std::vector<uint8_t> Sides(std::initializer_list<int> values)
    {
        std::vector<uint8_t> sides;
        for (int v : values)
            sides.push_back(static_cast<uint8_t>(v));
        return sides;
    }
}

TEST(CFBGBestSplitTest, TwoVsTwoKeepsLowestOnItsSide)
{
    Problem p;
    p.items = { Solo(71, 0), Solo(75, 1), Solo(75, 0), Solo(77, 1) };
    p.keepCurrentUnlessLevelImproves = true;

    auto sides = CFBGBestSplit::Solve(p);
    ASSERT_EQ(sides.size(), 4u);
    std::array<uint32_t, 2> sums{};
    for (std::size_t i = 0; i < sides.size(); ++i)
        sums[sides[i]] += p.items[i].level;

    EXPECT_EQ(sides[0], 0);
    EXPECT_EQ(sums[0], 148u);
    EXPECT_EQ(sums[1], 150u);
}

TEST(CFBGBestSplitTest, LevelBeatsIlvl)
{
    Problem p;
    p.baseLevel = { 0, 2 };
    p.baseIlvl = { 0, 100 };
    p.items = { Solo(10, 0, 100), Solo(11, 1, 50) };
    EXPECT_EQ(CFBGBestSplit::Solve(p), Sides({ 1, 0 }));
}

TEST(CFBGBestSplitTest, IlvlBreaksLevelTie)
{
    Problem p;
    p.baseIlvl = { 20, 0 };
    p.items = { Solo(10, 1, 0), Solo(10, 0, 20) };
    EXPECT_EQ(CFBGBestSplit::Solve(p), Sides({ 0, 1 }));
}

TEST(CFBGBestSplitTest, DisguiseBreaksLevelAndIlvlTie)
{
    Problem p;
    p.items = { Solo(10, 1, 0, 1), Solo(10, 0, 0, 0) };
    EXPECT_EQ(CFBGBestSplit::Solve(p), Sides({ 0, 1 }));
}

TEST(CFBGBestSplitTest, RankBreaksFullTie)
{
    Problem p;
    p.items = { Solo(10, 0), Solo(10, 1) };
    p.items[0].preferred = 1;
    p.items[1].preferred = 0;
    EXPECT_EQ(CFBGBestSplit::Solve(p), Sides({ 1, 0 }));
}

TEST(CFBGBestSplitTest, KeepModeIgnoresIlvlAndDisguiseAtLevel80)
{
    SplitMix64 rng{ 80 };
    for (int round = 0; round < 50; ++round)
    {
        Problem p;
        p.keepCurrentUnlessLevelImproves = true;
        for (int i = 0; i < 8; ++i)
            p.items.push_back(Solo(80, static_cast<uint8_t>(i % 2),
                rng.Range(150, 260), rng.Range(0, 1)));
        EXPECT_EQ(CFBGBestSplit::Solve(p), Current(p)) << "round " << round;
    }
}

TEST(CFBGBestSplitTest, KeepModeIgnoresBetterIlvlOnLevelTie)
{
    Problem p;
    p.baseIlvl = { 20, 0 };
    p.items = { Solo(10, 1, 0), Solo(10, 0, 20) };
    p.keepCurrentUnlessLevelImproves = true;
    EXPECT_EQ(CFBGBestSplit::Solve(p), Current(p));
}

TEST(CFBGBestSplitTest, ExactMatchesBruteForce)
{
    SplitMix64 rng{ 12345 };
    auto check = [](Problem p, std::string const& label)
    {
        std::vector<uint8_t> const best = BruteForce(p);
        for (bool keep : { false, true })
        {
            p.keepCurrentUnlessLevelImproves = keep;
            EXPECT_EQ(CFBGBestSplit::Solve(p), ApplyKeep(p, best))
                << label << " keep " << keep;
        }
    };

    for (int round = 0; round < 2000; ++round)
        check(Generate(rng, rng.Range(1, 12), false),
            "wide " + std::to_string(round));

    for (int round = 0; round < 2000; ++round)
        check(Generate(rng, rng.Range(1, 12), true),
            "narrow " + std::to_string(round));

    for (std::size_t n : { 16u, 17u, 18u, 20u })
        check(Generate(rng, n, false), "large " + std::to_string(n));
}

TEST(CFBGBestSplitTest, CountsPreservedAndNeverWorse)
{
    SplitMix64 rng{ 777 };
    for (int round = 0; round < 200; ++round)
    {
        std::size_t const n = round % 2 ? rng.Range(21, 80) :
            rng.Range(1, 20);
        Problem p = Generate(rng, n, round % 3 == 0);
        std::vector<uint8_t> const current = Current(p);
        uint32_t const currentGap = Score(p, current).levelGap;

        for (bool keep : { false, true })
        {
            p.keepCurrentUnlessLevelImproves = keep;
            auto sides = CFBGBestSplit::Solve(p);
            ASSERT_EQ(sides.size(), n);
            EXPECT_EQ(CountA(p, sides), CountA(p, current))
                << "round " << round << " keep " << keep;

            uint32_t const gap = Score(p, sides).levelGap;
            EXPECT_LE(gap, currentGap) << "round " << round;
            if (keep && sides != current)
                EXPECT_LT(gap, currentGap) << "round " << round;
        }
    }
}

TEST(CFBGBestSplitTest, LocalSearchEvensManySolos)
{
    SplitMix64 rng{ 2121 };
    for (int round = 0; round < 50; ++round)
    {
        Problem p;
        uint32_t total = 0;
        uint32_t const n = 2 * rng.Range(11, 40);
        for (uint32_t i = 0; i < n; ++i)
        {
            p.items.push_back(Solo(rng.Range(70, 80),
                static_cast<uint8_t>(i % 2)));
            total += p.items.back().level;
        }

        auto sides = CFBGBestSplit::Solve(p);
        ASSERT_EQ(sides.size(), n);
        EXPECT_EQ(CountA(p, sides), n / 2) << "round " << round;
        EXPECT_EQ(Score(p, sides).levelGap, total % 2) << "round " << round;
    }
}

TEST(CFBGBestSplitTest, LocalSearchLeavesNoImprovingSwap)
{
    auto key = [](RefScore const& s)
    {
        return std::make_tuple(s.levelGap, s.ilvlGap, s.disguised);
    };

    SplitMix64 rng{ 4242 };
    for (int round = 0; round < 40; ++round)
    {
        Problem const p = Generate(rng, rng.Range(21, 80), round % 3 == 0);
        auto sides = CFBGBestSplit::Solve(p);
        ASSERT_EQ(sides.size(), p.items.size());
        RefScore const reached = Score(p, sides);

        for (std::size_t i = 0; i < sides.size(); ++i)
            for (std::size_t j = 0; j < sides.size(); ++j)
            {
                if (sides[i] != 0 || sides[j] != 1 ||
                    p.items[i].size != p.items[j].size)
                    continue;

                std::swap(sides[i], sides[j]);
                EXPECT_FALSE(key(Score(p, sides)) < key(reached))
                    << "round " << round << " swap " << i << "/" << j;
                std::swap(sides[i], sides[j]);
            }
    }
}

TEST(CFBGBestSplitTest, LocalSearchUsesIlvlOnLevelTie)
{
    Problem p;
    for (int i = 0; i < 22; ++i)
        p.items.push_back(Solo(80, static_cast<uint8_t>(i % 2),
            i % 2 ? 100 : 200));

    auto sides = CFBGBestSplit::Solve(p);
    ASSERT_EQ(sides.size(), 22u);
    EXPECT_EQ(CountA(p, sides), 11u);
    EXPECT_EQ(Score(p, sides).ilvlGap, 100u);
}

TEST(CFBGBestSplitTest, LocalSearchUsesDisguiseOnLevelAndIlvlTie)
{
    Problem p;
    for (int i = 0; i < 22; ++i)
        p.items.push_back(Solo(80, static_cast<uint8_t>(i % 2), 0,
            i % 2 ? 1 : 0));

    auto sides = CFBGBestSplit::Solve(p);
    ASSERT_EQ(sides.size(), 22u);
    EXPECT_EQ(CountA(p, sides), 11u);
    EXPECT_EQ(Score(p, sides).disguised, 0u);
}

TEST(CFBGBestSplitTest, GateOpenMovesThePlayerThatEvensLevels)
{
    // 71, 75, 77 on Alliance vs 75 on Horde: moving the 75 gives 148/150
    Problem p;
    p.baseLevel = { 0, 75 };
    p.items = { Solo(71, 1), Solo(75, 0), Solo(77, 0) };
    for (Item& it : p.items)
        it.preferred = 1;

    EXPECT_EQ(CFBGBestSplit::Solve(p), Sides({ 0, 1, 0 }));
}

TEST(CFBGBestSplitTest, GateOpenPrefersPlayerReturningToRealFaction)
{
    Problem p;
    p.baseLevel = { 0, 70 };
    p.items = { Solo(75, 1, 0, 1), Solo(75, 0, 0, 0), Solo(75, 0, 0, 1) };
    for (Item& it : p.items)
        it.preferred = 1;

    EXPECT_EQ(CFBGBestSplit::Solve(p), Sides({ 0, 1, 0 }));
}

TEST(CFBGBestSplitTest, GateOpenFullTieMovesEarliest)
{
    Problem p;
    p.baseLevel = { 0, 70 };
    p.items = { Solo(75, 0, 0, 1), Solo(75, 1, 0, 1), Solo(75, 0, 0, 1) };
    for (Item& it : p.items)
        it.preferred = 1;

    EXPECT_EQ(CFBGBestSplit::Solve(p), Sides({ 1, 0, 0 }));
}
