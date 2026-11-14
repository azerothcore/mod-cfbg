/*
 * Copyright (С) since 2019+ AzerothCore <www.azerothcore.org>
 * Licence MIT https://opensource.org/MIT
 */

#ifndef _CFBG_TEST_ACCESS_H_
#define _CFBG_TEST_ACCESS_H_

#include "CFBG.h"
#include <cstddef>
#include <vector>

struct CFBGTestAccess
{
    struct Options
    {
        bool enableSystem;
        bool avgIlvl;
        bool balancedTeams;
        bool bestSplit;
        bool evenTeams;
        bool classLowLevel;
        uint32 evenTeamsThreshold;
        uint8 classMinLevel;
        uint8 classMaxLevel;
        uint8 classLevelDiff;
        bool onEntry;
        bool atStart;
    };

    static inline Options Snapshot()
    {
        CFBG const* c = sCFBG;
        return Options{ c->_IsEnableSystem, c->_IsEnableAvgIlvl,
            c->_IsEnableBalancedTeams, c->_IsEnableBestSplit,
            c->_IsEnableEvenTeams,
            c->_IsEnableBalanceClassLowLevel, c->_EvenTeamsMaxPlayersThreshold,
            c->_balanceClassMinLevel, c->_balanceClassMaxLevel,
            c->_balanceClassLevelDiff, c->_IsEnableBalanceTeamsOnEntry,
            c->_IsEnableBalanceTeamsAtStart };
    }

    static inline void Apply(Options const& o)
    {
        CFBG* c = sCFBG;
        c->_IsEnableSystem = o.enableSystem;
        c->_IsEnableAvgIlvl = o.avgIlvl;
        c->_IsEnableBalancedTeams = o.balancedTeams;
        c->_IsEnableBestSplit = o.bestSplit;
        c->_IsEnableEvenTeams = o.evenTeams;
        c->_IsEnableBalanceClassLowLevel = o.classLowLevel;
        c->_EvenTeamsMaxPlayersThreshold = o.evenTeamsThreshold;
        c->_balanceClassMinLevel = o.classMinLevel;
        c->_balanceClassMaxLevel = o.classMaxLevel;
        c->_balanceClassLevelDiff = o.classLevelDiff;
        c->_IsEnableBalanceTeamsOnEntry = o.onEntry;
        c->_IsEnableBalanceTeamsAtStart = o.atStart;
    }

    static inline CFBGStartMoves SelectStartMovesByLevel(Battleground* bg)
    {
        return sCFBG->SelectStartMovesByLevel(bg);
    }

    static inline Player* PickStartMoveByFaction(Battleground* bg,
        TeamId larger, TeamId smaller)
    {
        return sCFBG->PickStartMoveByFaction(bg, larger, smaller);
    }

    static inline std::vector<Player*> StartMoveSetByFaction(
        std::vector<Player*> const& eligible, std::size_t k, TeamId to)
    {
        return sCFBG->StartMoveSetByFaction(eligible, k, to);
    }

    static inline bool UseStartMovesByLevel()
    {
        return sCFBG->UseStartMovesByLevel();
    }

    static inline void MarkFake(Player* player)
    {
        sCFBG->_fakePlayerStore[player] = FakePlayer{};
    }

    static inline void UnmarkFake(Player* player)
    {
        sCFBG->_fakePlayerStore.erase(player);
    }

    // Unlike CFBG.conf.dist: even teams, item level and the hunter rule on,
    // and the hunter rule reaches level 29.
    static inline Options LiveLike()
    {
        return Options{ .enableSystem = true, .avgIlvl = true,
            .balancedTeams = true, .bestSplit = true, .evenTeams = true,
            .classLowLevel = true, .evenTeamsThreshold = 0,
            .classMinLevel = 10, .classMaxLevel = 29, .classLevelDiff = 2,
            .onEntry = true, .atStart = true };
    }
};

#endif // _CFBG_TEST_ACCESS_H_
