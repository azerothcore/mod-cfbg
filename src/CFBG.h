/*
 * Copyright (С) since 2019 Andrei Guluaev (Winfidonarleyan/Kargatum) https://github.com/Winfidonarleyan
 * Copyright (С) since 2019+ AzerothCore <www.azerothcore.org>
 * Licence MIT https://opensource.org/MIT
 */

#ifndef _CFBG_H_
#define _CFBG_H_

#include "Common.h"
#include "Battleground.h"
#include "BattlegroundQueue.h"
#include "Player.h"
#include <unordered_map>

enum FakeMorphs
{
    // FAKE_M_GOBLIN = 20582,
    // FAKE_F_GOBLIN = 20583,

    // FAKE_M_UNDEAD missing
    // FAKE_F_UNDEAD missing

    FAKE_M_FEL_ORC        = 21267,
    FAKE_F_ORC            = 20316,

    FAKE_M_DWARF          = 20317,
    // FAKE_F_DWARF missing

    FAKE_M_NIGHT_ELF      = 20318,
    // FAKE_F_NIGHT_ELF missing

    FAKE_F_DRAENEI        = 20323,
    FAKE_M_BROKEN_DRAENEI = 21105,

    FAKE_M_TROLL          = 20321,
    // FAKE_F_TROLL missing

    FAKE_M_HUMAN          = 19723,
    FAKE_F_HUMAN          = 19724,
    FAKE_M_BLOOD_ELF      = 20578,
    FAKE_F_BLOOD_ELF      = 20579,
    FAKE_F_GNOME          = 20320,
    FAKE_M_GNOME          = 20580,
    FAKE_F_TAUREN         = 20584,
    FAKE_M_TAUREN         = 20585

};

struct FakePlayer
{
    // Fake
    uint8   FakeRace;
    uint32  FakeMorph;
    TeamId  FakeTeamID;

    // Real
    uint8   RealRace;
    uint32  RealMorph;
    uint32  RealNativeMorph;
    TeamId  RealTeamID;
};

struct CrossFactionGroupInfo
{
    explicit CrossFactionGroupInfo(GroupQueueInfo* groupInfo);

    uint32 AveragePlayersLevel{ 0 };
    uint32 AveragePlayersItemLevel{ 0 };
    //uint32 JoiningPlayers{ 0 };
    bool IsHunterJoining{ false };

    CrossFactionGroupInfo() = delete;
    CrossFactionGroupInfo(CrossFactionGroupInfo&&) = delete;
};

enum Factions
{
    FACTION_FROSTWOLF_CLAN = 729,
    FACTION_STORMPIKE_GUARD = 730
};

struct RaceData
{
    uint8 charClass;
    std::vector<uint8> availableRacesA;
    std::vector<uint8> availableRacesH;
};

RaceData const raceData[12] =
{
    { CLASS_NONE, { 0 }, { 0 } },
    { CLASS_WARRIOR,      { RACE_HUMAN, RACE_DWARF, RACE_GNOME, RACE_NIGHTELF, RACE_DRAENEI  }, { RACE_ORC, RACE_TAUREN, RACE_TROLL } },
    { CLASS_PALADIN,      { RACE_HUMAN, RACE_DWARF, RACE_DRAENEI },                             { RACE_BLOODELF } },
    { CLASS_HUNTER,       { RACE_DWARF, RACE_NIGHTELF, RACE_DRAENEI },                          { RACE_ORC, RACE_TAUREN, RACE_TROLL, RACE_BLOODELF } },
    { CLASS_ROGUE,        { RACE_HUMAN, RACE_DWARF, RACE_GNOME, RACE_NIGHTELF },                { RACE_ORC, RACE_TROLL, RACE_BLOODELF, RACE_UNDEAD_PLAYER } },
    { CLASS_PRIEST,       { RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_DRAENEI  },             { RACE_TROLL, RACE_BLOODELF, RACE_UNDEAD_PLAYER } },
    { CLASS_DEATH_KNIGHT, { RACE_HUMAN, RACE_DWARF, RACE_GNOME,  RACE_NIGHTELF, RACE_DRAENEI }, { RACE_ORC, RACE_TAUREN, RACE_TROLL, RACE_BLOODELF, RACE_UNDEAD_PLAYER } },
    { CLASS_SHAMAN,       { RACE_DRAENEI },                                                     { RACE_ORC, RACE_TAUREN, RACE_TROLL  } },
    { CLASS_MAGE,         { RACE_HUMAN, RACE_GNOME },                                           { RACE_BLOODELF, RACE_TROLL } },
    { CLASS_WARLOCK,      { RACE_HUMAN, RACE_GNOME },                                           { RACE_ORC, RACE_BLOODELF } },
    { CLASS_NONE, { 0 }, { 0 } },
    { CLASS_DRUID,        { RACE_NIGHTELF },                                                    { RACE_TAUREN } },
};

enum CFBGSettings
{
    SETTING_CFBG_RACE = 0
};

class CFBG
{
public:
    static CFBG* instance();

    void LoadConfig();

    inline bool IsEnableSystem() const
    {
        return _IsEnableSystem;
    }

    inline bool IsEnableAvgIlvl() const
    {
        return _IsEnableAvgIlvl;
    }

    inline bool IsEnableBalancedTeams() const
    {
        return _IsEnableBalancedTeams;
    }

    inline bool IsEnableBalanceClassLowLevel() const
    {
        return _IsEnableBalanceClassLowLevel;
    }

    inline bool IsEnableEvenTeams() const
    {
        return _IsEnableEvenTeams;
    }

    inline bool IsEnableResetCooldowns() const
    {
        return _IsEnableResetCooldowns;
    }

    inline uint32 EvenTeamsMaxPlayersThreshold() const
    {
        return _EvenTeamsMaxPlayersThreshold;
    }

    inline uint32 GetMaxPlayersCountInGroup() const
    {
        return _MaxPlayersCountInGroup;
    }

    inline bool RandomizeRaces() const
    {
        return _randomizeRaces;
    }

    uint32 GetBGTeamAverageItemLevel(Battleground* bg, TeamId team);
    uint32 GetBGTeamSumPlayerLevel(Battleground* bg, TeamId team);
    uint32 GetAllPlayersCountInBG(Battleground* bg);

    TeamId GetLowerTeamIdInBG(Battleground* bg, GroupQueueInfo* groupInfo);
    TeamId GetLowerAvgIlvlTeamInBg(Battleground* bg);
    TeamId SelectBgTeam(Battleground* bg, GroupQueueInfo* groupInfo);

    bool IsAvgIlvlTeamsInBgEqual(Battleground* bg);
    bool SendRealNameQuery(Player* player);
    bool IsPlayerFake(Player* player);
    bool ShouldForgetInListPlayers(Player* player);
    bool IsPlayingNative(Player* player);

    void ValidatePlayerForBG(Battleground* bg, Player* player, TeamId teamId);
    void SetFakeRaceAndMorph(Player* player);
    void SetFactionForRace(Player* player, uint8 Race);
    void ClearFakePlayer(Player* player);
    void DoForgetPlayersInList(Player* player);
    void FitPlayerInTeam(Player* player, bool action, Battleground* bg);
    void DoForgetPlayersInBG(Player* player, Battleground* bg);
    void SetForgetBGPlayers(Player* player, bool value);
    bool ShouldForgetBGPlayers(Player* player);
    void SetForgetInListPlayers(Player* player, bool value);
    void UpdateForget(Player* player);
    void SendMessageQueue(BattlegroundQueue* bgQueue, Battleground* bg, PvPDifficultyEntry const* bracketEntry, Player* leader);

    bool FillPlayersToCFBG(BattlegroundQueue* bgqueue, Battleground* bg, BattlegroundBracketId bracket_id);
    bool CheckCrossFactionMatch(BattlegroundQueue* bgqueue, BattlegroundBracketId bracket_id, uint32 minPlayers, uint32 maxPlayers);

private:
    typedef std::unordered_map<Player*, FakePlayer> FakePlayersContainer;
    typedef std::unordered_map<Player*, ObjectGuid> FakeNamePlayersContainer;
    typedef std::unordered_map<Player*, bool> ForgetBGPlayersContainer;
    typedef std::unordered_map<Player*, bool> ForgetInListPlayersContainer;

    FakePlayersContainer _fakePlayerStore;
    FakeNamePlayersContainer _fakeNamePlayersStore;
    ForgetBGPlayersContainer _forgetBGPlayersStore;
    ForgetInListPlayersContainer _forgetInListPlayersStore;
    std::unordered_map<GroupQueueInfo*, CrossFactionGroupInfo> _groupsInfo;

    // For config
    bool _IsEnableSystem;
    bool _IsEnableAvgIlvl;
    bool _IsEnableBalancedTeams;
    bool _IsEnableBalanceClassLowLevel;
    bool _IsEnableEvenTeams;
    bool _IsEnableResetCooldowns;
    bool _showPlayerName;
    bool _randomizeRaces;
    uint32 _EvenTeamsMaxPlayersThreshold;
    uint32 _MaxPlayersCountInGroup;
    uint8 _balanceClassMinLevel;
    uint8 _balanceClassMaxLevel;
    uint8 _balanceClassLevelDiff;

    bool isClassJoining(uint8 _class, Player* player, uint32 minLevel);

    void RandomRaceMorph(uint8* race, uint32* morph, TeamId team, uint8 _class, uint8 gender);

    uint8 GetRandomRace(std::initializer_list<uint32> races);
    uint32 GetMorphFromRace(uint8 race, uint8 gender);
    TeamId getTeamWithLowerClass(Battleground *bg, Classes c);
    uint8 getBalanceClassMinLevel(const Battleground *bg) const;
};

#define sCFBG CFBG::instance()

#endif // _KARGATUM_CFBG_H_
