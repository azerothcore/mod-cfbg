/*
 * Copyright (С) since 2019+ AzerothCore <www.azerothcore.org>
 * Licence MIT https://opensource.org/MIT
 */

#include "Battleground.h"
#include "BattlegroundQueue.h"
#include "CFBG.h"
#include "CFBGTestAccess.h"
#include "CFBGTestUtil.h"
#include "DBCStores.h"
#include "ObjectAccessor.h"
#include "ScriptMgr.h"
// keep last: its `using namespace testing` makes Field and Optional
// ambiguous in core headers
#include "IntegrationTestFixture.h"
#include <algorithm>
#include <array>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using CFBGTest::AbsDiff;
using CFBGTest::SplitMix64;

namespace
{
    constexpr BattlegroundBracketId TestBracket = BG_BRACKET_ID_FIRST;
    constexpr uint8 TestHordeRace = RACE_ORC;

    struct RaceAccess : Player
    {
        static void Set(Player* player, uint8 race)
        {
            player->*(&RaceAccess::m_realRace) = race;
        }
    };

    class CFBGTestBattleground : public Battleground
    {
    public:
        CFBGTestBattleground(uint32 instanceId, uint32 maxPerTeam,
            uint32 minLevel, uint32 maxLevel)
        {
            SetBgTypeID(BATTLEGROUND_WS);
            SetInstanceID(instanceId);
            SetMaxPlayersPerTeam(maxPerTeam);
            SetLevelRange(minLevel, maxLevel);
        }

        void AddInBg(Player* player, TeamId team)
        {
            m_Players[player->GetGUID()] = player;
            UpdatePlayersCountByTeam(team, false);
            IncreaseInvitedCount(team);
            player->setTeamId(team);
            player->SetBattlegroundId(GetInstanceID(), GetBgTypeID(), 0, true,
                false, team);
        }

        void AddLoading(Player* player, TeamId team)
        {
            IncreaseInvitedCount(team);
            player->SetBattlegroundId(GetInstanceID(), GetBgTypeID(), 0, true,
                false, team);
        }
    };

    uint32 GroupLevel(GroupQueueInfo const* gInfo)
    {
        uint32 sum = 0;
        for (auto const& guid : gInfo->Players)
            if (Player* p = ObjectAccessor::FindConnectedPlayer(guid))
                sum += p->GetLevel();
        return sum;
    }

    struct PassResult
    {
        std::vector<int> pool; // per group: TEAM_ALLIANCE, TEAM_HORDE or -1
        std::vector<TeamId> teamId;
        std::array<uint32, 2> count{};
        std::array<uint32, 2> level{};
    };

    // One queue/BG setup that can be run with the switch on and off.
    struct Scenario
    {
        std::unique_ptr<BattlegroundQueue> queue =
            std::make_unique<BattlegroundQueue>();
        std::unique_ptr<CFBGTestBattleground> bg;
        std::vector<GroupQueueInfo*> groups;
        std::vector<TeamId> initial;
        std::array<uint32, 2> base{}; // in-BG, pending and loading levels
        uint32 minPlayers = 2;
        uint32 maxPlayers = 10;

        GroupQueueInfo* Queue(std::vector<ObjectGuid> const& members,
            TeamId fallbackTeam, uint32 joinTime)
        {
            auto* gInfo = new GroupQueueInfo{};
            for (auto const& guid : members)
                gInfo->Players.insert(guid);
            gInfo->teamId = fallbackTeam;
            gInfo->RealTeamID = fallbackTeam;
            gInfo->BgTypeId = BATTLEGROUND_WS;
            gInfo->JoinTime = joinTime;
            gInfo->BracketId = TestBracket;
            gInfo->GroupType = BG_QUEUE_CFBG;
            queue->m_QueuedGroups[TestBracket][BG_QUEUE_CFBG].push_back(gInfo);
            groups.push_back(gInfo);
            initial.push_back(fallbackTeam);
            return gInfo;
        }

        GroupQueueInfo* Queue(std::vector<Player*> const& members,
            TeamId fallbackTeam, uint32 joinTime)
        {
            std::vector<ObjectGuid> guids;
            for (Player* p : members)
                guids.push_back(p->GetGUID());
            return Queue(guids, fallbackTeam, joinTime);
        }

        void InBg(Player* player, TeamId team)
        {
            bg->AddInBg(player, team);
            base[team] += player->GetLevel();
        }

        void Loading(Player* player, TeamId team)
        {
            bg->AddLoading(player, team);
            base[team] += player->GetLevel();
        }

        void Pending(Player* player, TeamId team, uint32 joinTime)
        {
            auto* gInfo = new GroupQueueInfo{};
            gInfo->Players.insert(player->GetGUID());
            gInfo->teamId = team;
            gInfo->RealTeamID = team;
            gInfo->BgTypeId = BATTLEGROUND_WS;
            gInfo->JoinTime = joinTime;
            gInfo->BracketId = TestBracket;
            gInfo->GroupType = BG_QUEUE_CFBG;
            gInfo->IsInvitedToBGInstanceGUID = bg->GetInstanceID();
            queue->m_QueuedGroups[TestBracket][BG_QUEUE_CFBG].push_back(gInfo);
            bg->IncreaseInvitedCount(team);
            base[team] += player->GetLevel();
        }

        PassResult Run(bool bestSplit)
        {
            for (std::size_t i = 0; i < groups.size(); ++i)
                groups[i]->teamId = initial[i];
            queue->m_SelectionPools[TEAM_ALLIANCE].Init();
            queue->m_SelectionPools[TEAM_HORDE].Init();

            auto options = CFBGTestAccess::Snapshot();
            options.bestSplit = bestSplit;
            CFBGTestAccess::Apply(options);

            if (bg)
                sCFBG->FillPlayersToCFBG(queue.get(), bg.get(), TestBracket);
            else
                sCFBG->CheckCrossFactionMatch(queue.get(), TestBracket,
                    minPlayers, maxPlayers);

            PassResult r;
            r.level = base;
            for (GroupQueueInfo* gInfo : groups)
            {
                int pool = -1;
                for (auto team : { TEAM_ALLIANCE, TEAM_HORDE })
                {
                    auto const& sel = queue->m_SelectionPools[team]
                        .SelectedGroups;
                    if (std::find(sel.begin(), sel.end(), gInfo) != sel.end())
                    {
                        EXPECT_EQ(pool, -1) << "group in both pools";
                        pool = team;
                        r.level[team] += GroupLevel(gInfo);
                    }
                }
                r.pool.push_back(pool);
                r.teamId.push_back(gInfo->teamId);
            }

            for (auto team : { TEAM_ALLIANCE, TEAM_HORDE })
                r.count[team] =
                    queue->m_SelectionPools[team].GetPlayerCount();
            return r;
        }

        // Smallest level gap over every side choice for the groups `off`
        // invited that keeps off's per-side head counts.
        uint32 OptimalGap(PassResult const& off) const
        {
            std::vector<std::size_t> invited;
            for (std::size_t i = 0; i < groups.size(); ++i)
                if (off.pool[i] != -1)
                    invited.push_back(i);

            std::vector<uint32> sizes;
            std::vector<uint32> levels;
            for (std::size_t i : invited)
            {
                sizes.push_back(groups[i]->Players.size());
                levels.push_back(GroupLevel(groups[i]));
            }

            uint32 best = UINT32_MAX;
            std::size_t const n = invited.size();
            for (uint64_t mask = 0; mask < (uint64_t(1) << n); ++mask)
            {
                uint32 countA = 0;
                std::array<uint32, 2> level = base;
                for (std::size_t j = 0; j < n; ++j)
                {
                    int const side = (mask >> j) & 1 ? TEAM_ALLIANCE :
                        TEAM_HORDE;
                    if (side == TEAM_ALLIANCE)
                        countA += sizes[j];
                    level[side] += levels[j];
                }

                if (countA == off.count[TEAM_ALLIANCE])
                    best = std::min(best, AbsDiff(level[0], level[1]));
            }
            return best;
        }
    };

    class CFBGSelectionTest : public IntegrationTestFixture
    {
    protected:
        void SetUp() override
        {
            IntegrationTestFixture::SetUp();
            ScriptRegistry<BGScript>::InitEnabledHooksIfNeeded(
                ALLBATTLEGROUNDHOOK_END);
            _savedOptions = CFBGTestAccess::Snapshot();
            CFBGTestAccess::Apply(CFBGTestAccess::LiveLike());

            if (!sChrRacesStore.LookupEntry(TestHordeRace))
            {
                auto* entry = new ChrRacesEntry{};
                entry->RaceID = TestHordeRace;
                entry->TeamID = 1;
                sChrRacesStore.SetEntry(TestHordeRace, entry);
                _ownsRaceEntry = true;
            }
        }

        void TearDown() override
        {
            for (Player* player : _faked)
                CFBGTestAccess::UnmarkFake(player);
            _faked.clear();
            for (Player* player : _registered)
                HashMapHolder<Player>::Remove(player);
            _registered.clear();
            if (_ownsRaceEntry)
                sChrRacesStore.SetEntry(TestHordeRace, nullptr);
            CFBGTestAccess::Apply(_savedOptions);
            IntegrationTestFixture::TearDown();
        }

        Player* MakePlayer(uint8 level, uint8 cls = CLASS_WARRIOR,
            TeamId team = TEAM_ALLIANCE)
        {
            Player* player = CreateTestPlayer(_nextGuid++);
            player->SetUInt32Value(UNIT_FIELD_LEVEL, level);
            player->SetByteValue(UNIT_FIELD_BYTES_0, 1, cls);
            player->setTeamId(team);
            HashMapHolder<Player>::Insert(player);
            _registered.push_back(player);
            return player;
        }

        static void ResetPlayer(Player* player, uint8 level, TeamId team)
        {
            player->SetBattlegroundId(0, BATTLEGROUND_TYPE_NONE,
                PLAYER_MAX_BATTLEGROUND_QUEUES, false, false, TEAM_NEUTRAL);
            player->SetUInt32Value(UNIT_FIELD_LEVEL, level);
            player->setTeamId(team);
        }

        static void MakeRealHorde(Player* player)
        {
            RaceAccess::Set(player, TestHordeRace);
        }

        void MarkFake(Player* player)
        {
            CFBGTestAccess::MarkFake(player);
            _faked.push_back(player);
        }

        static void SetOption(bool CFBGTestAccess::Options::* field, bool v)
        {
            auto options = CFBGTestAccess::Snapshot();
            options.*field = v;
            CFBGTestAccess::Apply(options);
        }

        static std::unique_ptr<CFBGTestBattleground> MakeBg(uint32 maxPerTeam,
            uint32 minLevel, uint32 maxLevel)
        {
            return std::make_unique<CFBGTestBattleground>(NextInstanceId(),
                maxPerTeam, minLevel, maxLevel);
        }

        static uint32 NextInstanceId()
        {
            static uint32 next = 700000;
            return ++next;
        }

        static std::string LevelsText(std::vector<Player*> const& order)
        {
            std::ostringstream text;
            for (Player const* p : order)
                text << uint32(p->GetLevel()) << ' ';
            return text.str();
        }

        // Generated inputs that compare the switch on against off.
        struct GenConfig
        {
            uint32 seed;
            int inputs;
            bool topUp;
            uint8 levelLo;
            uint8 levelHi;
        };

        void RunGenerated(GenConfig const& cfg);

    private:
        CFBGTestAccess::Options _savedOptions{};
        std::vector<Player*> _registered;
        std::vector<Player*> _faked;
        bool _ownsRaceEntry = false;
        ObjectGuid::LowType _nextGuid = 1;
    };

    void CFBGSelectionTest::RunGenerated(GenConfig const& cfg)
    {
        SplitMix64 rng{ cfg.seed };
        std::vector<Player*> pool;
        for (int i = 0; i < 60; ++i)
            pool.push_back(MakePlayer(cfg.levelLo));
        uint8 const allianceRace = pool.front()->getRace(true);

        bool const balanced = CFBGTestAccess::Snapshot().balancedTeams;
        int changed = 0;
        int optimumChecks = 0;
        for (int input = 0; input < cfg.inputs; ++input)
        {
            // mixed real factions give level ties a split with fewer
            // disguised players, which keep mode must still turn down
            for (Player* p : pool)
            {
                ResetPlayer(p, static_cast<uint8>(
                    rng.Range(cfg.levelLo, cfg.levelHi)), TEAM_ALLIANCE);
                RaceAccess::Set(p, rng.Range(0, 1) ? TestHordeRace :
                    allianceRace);
            }
            std::size_t nextPlayer = 0;
            auto take = [&pool, &nextPlayer]() { return pool[nextPlayer++]; };

            SetOption(&CFBGTestAccess::Options::evenTeams, rng.Range(0, 1));

            Scenario s;
            uint32 const maxChoices[] = { 10, 15, 40 };
            s.maxPlayers = maxChoices[rng.Range(0, 2)];
            s.minPlayers = rng.Range(2, 5);

            if (cfg.topUp)
            {
                s.bg = MakeBg(s.maxPlayers, 70, 79);
                for (uint32 n = rng.Range(0, 4); n; --n)
                    s.InBg(take(), TEAM_ALLIANCE);
                for (uint32 n = rng.Range(0, 4); n; --n)
                    s.InBg(take(), TEAM_HORDE);
                for (uint32 n = rng.Range(0, 2); n; --n)
                    s.Pending(take(), rng.Range(0, 1) ? TEAM_HORDE :
                        TEAM_ALLIANCE, 10 + n);
                for (uint32 n = rng.Range(0, 2); n; --n)
                    s.Loading(take(), rng.Range(0, 1) ? TEAM_HORDE :
                        TEAM_ALLIANCE);
            }

            uint32 const groupCount = rng.Range(1, 20);
            for (uint32 g = 0; g < groupCount; ++g)
            {
                uint32 const r = rng.Range(0, 9);
                uint32 size = r < 7 ? 1 : (r < 9 ? 2 : 3);
                std::vector<ObjectGuid> members;
                for (; size && nextPlayer < pool.size(); --size)
                    members.push_back(take()->GetGUID());
                if (input % 50 == 0 && g == 0)
                    members.push_back(ObjectGuid::Create<HighGuid::Player>(
                        900000 + input));
                if (members.empty())
                    break;
                s.Queue(members, rng.Range(0, 1) ? TEAM_HORDE : TEAM_ALLIANCE,
                    100 + g);
            }

            PassResult const off = s.Run(false);
            PassResult const on = s.Run(true);

            std::string const label = "seed " + std::to_string(cfg.seed) +
                " input " + std::to_string(input);
            for (std::size_t i = 0; i < s.groups.size(); ++i)
            {
                EXPECT_EQ(on.pool[i] == -1, off.pool[i] == -1) << label;
                if (off.pool[i] == -1)
                    EXPECT_EQ(on.teamId[i], off.teamId[i]) << label;
            }
            EXPECT_EQ(on.count, off.count) << label;

            uint32 const onGap = AbsDiff(on.level[0], on.level[1]);
            uint32 const offGap = AbsDiff(off.level[0], off.level[1]);
            EXPECT_LE(onGap, offGap) << label;
            if (onGap == offGap)
                EXPECT_EQ(on.pool, off.pool) << label;
            changed += on.pool != off.pool;

            std::size_t invited = 0;
            for (int p : off.pool)
                invited += p != -1;
            if (balanced && invited >= 2 && invited <= 12)
            {
                EXPECT_EQ(onGap, s.OptimalGap(off)) << label;
                ++optimumChecks;
            }
        }

        // The checks above hold trivially if the solver never acts.
        if (balanced && cfg.levelLo != cfg.levelHi)
        {
            EXPECT_GT(changed, 0) << "seed " << cfg.seed;
            EXPECT_GT(optimumChecks, cfg.inputs / 4) << "seed " << cfg.seed;
        }
        else
            EXPECT_EQ(changed, 0) << "seed " << cfg.seed;
    }
}

TEST_F(CFBGSelectionTest, TwoVsTwoFormsBestSplitInEveryOrder)
{
    std::array<Player*, 4> players{ MakePlayer(71), MakePlayer(75),
        MakePlayer(75), MakePlayer(77) };

    std::vector<std::string> failures;
    for (uint32 maxPlayers : { 10u, 15u })
    {
        std::array<int, 4> idx{ 0, 1, 2, 3 };
        do
        {
            Scenario s;
            s.maxPlayers = maxPlayers;
            std::vector<Player*> order;
            for (int i : idx)
            {
                order.push_back(players[i]);
                s.Queue({ players[i] }, TEAM_ALLIANCE, 1000 + order.size());
            }

            PassResult r = s.Run(true);
            uint32 low = std::min(r.level[0], r.level[1]);
            uint32 high = std::max(r.level[0], r.level[1]);
            if (r.count[0] != 2u || r.count[1] != 2u || low != 148u ||
                high != 150u)
            {
                std::ostringstream text;
                text << "max " << maxPlayers << " order "
                    << LevelsText(order) << "-> " << r.count[0] << "v"
                    << r.count[1] << " levels " << r.level[0] << "/"
                    << r.level[1];
                failures.push_back(text.str());
            }
        } while (std::next_permutation(idx.begin(), idx.end()));
    }

    std::ostringstream all;
    for (auto const& f : failures)
        all << f << '\n';
    EXPECT_TRUE(failures.empty()) << failures.size() << " of 48 failed:\n"
        << all.str();
}

TEST_F(CFBGSelectionTest, SwitchOffKeepsGreedySplitInEveryOrder)
{
    std::array<Player*, 4> players{ MakePlayer(71), MakePlayer(75),
        MakePlayer(75), MakePlayer(77) };
    std::set<std::string> const best{ "71 75 77 75 ", "75 71 77 75 ",
        "75 77 75 71 ", "77 75 75 71 " };

    for (uint32 maxPlayers : { 10u, 15u })
    {
        int bestCount = 0;
        std::array<int, 4> idx{ 0, 1, 2, 3 };
        do
        {
            Scenario s;
            s.maxPlayers = maxPlayers;
            std::vector<Player*> order;
            for (int i : idx)
            {
                order.push_back(players[i]);
                s.Queue({ players[i] }, TEAM_ALLIANCE, 1000 + order.size());
            }

            PassResult r = s.Run(false);
            std::string const text = LevelsText(order);
            bool const isBest = best.count(text) > 0;
            bestCount += isBest;
            EXPECT_EQ(std::min(r.level[0], r.level[1]), isBest ? 148u : 146u)
                << "max " << maxPlayers << " order " << text;
            EXPECT_EQ(std::max(r.level[0], r.level[1]), isBest ? 150u : 152u)
                << "max " << maxPlayers << " order " << text;
        } while (std::next_permutation(idx.begin(), idx.end()));

        EXPECT_EQ(bestCount, 8);
    }
}

TEST_F(CFBGSelectionTest, TopUpOfOneVsOne)
{
    struct Case
    {
        uint8 inA;
        uint8 inH;
        uint8 q1;
        uint8 q2;
        uint32 low;
        uint32 high;
        bool sameAsOff;
    };

    Case const cases[] = {
        { 75, 71, 75, 77, 148, 150, false },
        { 75, 71, 77, 75, 148, 150, false },
        { 71, 77, 75, 75, 146, 152, true },
        { 75, 75, 71, 77, 146, 152, true },
        { 75, 75, 77, 71, 146, 152, true },
    };

    for (Case const& c : cases)
    {
        Scenario s;
        s.bg = MakeBg(10, 70, 79);
        s.InBg(MakePlayer(c.inA), TEAM_ALLIANCE);
        s.InBg(MakePlayer(c.inH), TEAM_HORDE);
        s.Queue({ MakePlayer(c.q1) }, TEAM_ALLIANCE, 1);
        s.Queue({ MakePlayer(c.q2) }, TEAM_ALLIANCE, 2);

        std::string const label = std::to_string(c.inA) + " vs " +
            std::to_string(c.inH) + " + " + std::to_string(c.q1) + ", " +
            std::to_string(c.q2);
        PassResult const off = s.Run(false);
        PassResult const on = s.Run(true);
        EXPECT_EQ(std::min(on.level[0], on.level[1]), c.low) << label;
        EXPECT_EQ(std::max(on.level[0], on.level[1]), c.high) << label;
        if (c.sameAsOff)
            EXPECT_EQ(on.pool, off.pool) << label;
    }
}

TEST_F(CFBGSelectionTest, GeneratedFormationsMatchSwitchOffExceptSides)
{
    RunGenerated({ 101, 300, false, 70, 79 });
}

TEST_F(CFBGSelectionTest, GeneratedTopUpsMatchSwitchOffExceptSides)
{
    RunGenerated({ 202, 300, true, 70, 79 });
}

TEST_F(CFBGSelectionTest, LargeFormationsReachOptimum)
{
    for (uint32 n : { 16u, 18u, 20u })
    {
        SplitMix64 rng{ n };
        Scenario s;
        s.maxPlayers = 40;
        for (uint32 i = 0; i < n; ++i)
            s.Queue({ MakePlayer(static_cast<uint8>(rng.Range(70, 79))) },
                TEAM_ALLIANCE, 1 + i);

        PassResult const off = s.Run(false);
        PassResult const on = s.Run(true);
        std::size_t invited = 0;
        for (int p : off.pool)
            invited += p != -1;
        ASSERT_GE(invited, 16u);
        EXPECT_EQ(AbsDiff(on.level[0], on.level[1]), s.OptimalGap(off))
            << "n " << n;
    }
}

TEST_F(CFBGSelectionTest, Level80PassKeepsSwitchOffSides)
{
    RunGenerated({ 303, 40, false, 80, 80 });
    RunGenerated({ 404, 40, true, 80, 80 });
}

TEST_F(CFBGSelectionTest, BalancedTeamsOffIgnoresSwitch)
{
    SetOption(&CFBGTestAccess::Options::balancedTeams, false);
    RunGenerated({ 505, 60, false, 70, 79 });
    RunGenerated({ 606, 60, true, 70, 79 });
}

TEST_F(CFBGSelectionTest, HunterPassKeepsSwitchOffSides)
{
    struct Case
    {
        uint8 minLevel;
        uint8 maxLevel;
        uint32 maxPerTeam;
        bool hunterLast;
    };

    // The hunter group is staged (hunterLast false) or looked at and then
    // skipped by the cap (hunterLast true).
    Case const cases[] = {
        { 10, 19, 10, false },
        { 10, 19, 3, true },
        { 20, 29, 3, true },
    };

    for (Case const& c : cases)
    {
        uint8 const o = c.minLevel - 10;
        Scenario s;
        s.bg = MakeBg(c.maxPerTeam, c.minLevel, c.maxLevel);
        s.InBg(MakePlayer(c.maxLevel, CLASS_HUNTER), TEAM_ALLIANCE);
        s.InBg(MakePlayer(c.maxLevel, CLASS_HUNTER), TEAM_HORDE);
        s.Queue({ MakePlayer(11 + o) }, TEAM_ALLIANCE, 1);
        s.Queue({ MakePlayer(15 + o) }, TEAM_ALLIANCE, 2);
        s.Queue({ MakePlayer(15 + o) }, TEAM_ALLIANCE, 3);
        s.Queue({ MakePlayer(17 + o, c.hunterLast ? CLASS_WARRIOR :
            CLASS_HUNTER) }, TEAM_ALLIANCE, 4);
        if (c.hunterLast)
            s.Queue({ MakePlayer(18 + o, CLASS_HUNTER) }, TEAM_ALLIANCE, 5);

        std::string const label = "range " + std::to_string(c.minLevel) +
            " max " + std::to_string(c.maxPerTeam);

        // Without the hunter rule the solver would change these sides.
        SetOption(&CFBGTestAccess::Options::classLowLevel, false);
        PassResult const plainOff = s.Run(false);
        PassResult const plainOn = s.Run(true);
        ASSERT_NE(plainOn.pool, plainOff.pool) << label;

        SetOption(&CFBGTestAccess::Options::classLowLevel, true);
        PassResult const off = s.Run(false);
        PassResult const on = s.Run(true);
        EXPECT_EQ(on.pool, off.pool) << label;
        EXPECT_EQ(on.teamId, off.teamId) << label;
    }
}

TEST_F(CFBGSelectionTest, TopUpCountsLoadingPlayer)
{
    Scenario s;
    s.bg = MakeBg(10, 70, 79);
    s.InBg(MakePlayer(70), TEAM_ALLIANCE);
    s.Loading(MakePlayer(79), TEAM_HORDE);
    s.Queue({ MakePlayer(78) }, TEAM_ALLIANCE, 1);
    s.Queue({ MakePlayer(75) }, TEAM_ALLIANCE, 2);

    PassResult const off = s.Run(false);
    EXPECT_EQ(off.pool, (std::vector<int>{ TEAM_HORDE, TEAM_ALLIANCE }));
    EXPECT_EQ(off.level, (std::array<uint32, 2>{ 145, 157 }));

    PassResult const on = s.Run(true);
    EXPECT_EQ(on.pool, (std::vector<int>{ TEAM_ALLIANCE, TEAM_HORDE }));
    EXPECT_EQ(on.level, (std::array<uint32, 2>{ 148, 154 }));
}

TEST_F(CFBGSelectionTest, GateOpenMovesByLevel)
{
    auto bg = MakeBg(10, 70, 79);
    Player* low = MakePlayer(71);
    Player* hunter = MakePlayer(75, CLASS_HUNTER);
    Player* high = MakePlayer(77);
    Player* rogue = MakePlayer(75, CLASS_ROGUE);
    bg->AddInBg(low, TEAM_ALLIANCE);
    bg->AddInBg(hunter, TEAM_ALLIANCE);
    bg->AddInBg(high, TEAM_ALLIANCE);
    bg->AddInBg(rogue, TEAM_HORDE);

    CFBGStartMoves const moves =
        CFBGTestAccess::SelectStartMovesByLevel(bg.get());
    EXPECT_EQ(moves.from, TEAM_ALLIANCE);
    EXPECT_EQ(moves.to, TEAM_HORDE);
    EXPECT_EQ(moves.players, (std::vector<Player*>{ hunter }));

    EXPECT_EQ(CFBGTestAccess::PickStartMoveByFaction(bg.get(), TEAM_ALLIANCE,
        TEAM_HORDE), low);
}

TEST_F(CFBGSelectionTest, UseStartMovesByLevelFollowsBothSwitches)
{
    for (bool balanced : { false, true })
    {
        for (bool bestSplit : { false, true })
        {
            SetOption(&CFBGTestAccess::Options::balancedTeams, balanced);
            SetOption(&CFBGTestAccess::Options::bestSplit, bestSplit);
            EXPECT_EQ(CFBGTestAccess::UseStartMovesByLevel(),
                balanced && bestSplit)
                << "balanced " << balanced << " bestSplit " << bestSplit;
        }
    }
}

TEST_F(CFBGSelectionTest, GateOpenFactionPickPrefersDisguisedPlayer)
{
    auto bg = MakeBg(10, 70, 79);
    Player* low = MakePlayer(71);
    Player* hunter = MakePlayer(75, CLASS_HUNTER);
    Player* high = MakePlayer(77);
    Player* disguised = MakePlayer(76);
    Player* rogue = MakePlayer(75, CLASS_ROGUE);
    MakeRealHorde(disguised);
    MarkFake(disguised);
    bg->AddInBg(low, TEAM_ALLIANCE);
    bg->AddInBg(hunter, TEAM_ALLIANCE);
    bg->AddInBg(high, TEAM_ALLIANCE);
    bg->AddInBg(disguised, TEAM_ALLIANCE);
    bg->AddInBg(rogue, TEAM_HORDE);
    ASSERT_EQ(disguised->GetTeamId(true), TEAM_HORDE);

    EXPECT_EQ(CFBGTestAccess::PickStartMoveByFaction(bg.get(), TEAM_ALLIANCE,
        TEAM_HORDE), disguised);

    std::vector<Player*> const eligible{ low, hunter, high, disguised };
    EXPECT_EQ(CFBGTestAccess::StartMoveSetByFaction(eligible, 1, TEAM_HORDE),
        (std::vector<Player*>{ disguised }));
}
