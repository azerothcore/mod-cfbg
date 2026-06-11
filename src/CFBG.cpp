/*
 * Copyright (С) since 2019 Andrei Guluaev (Winfidonarleyan/Kargatum) https://github.com/Winfidonarleyan
 * Copyright (С) since 2019+ AzerothCore <www.azerothcore.org>
 * Licence MIT https://opensource.org/MIT
 */

#include "CFBG.h"
#include "BattlegroundQueue.h"
#include "BattlegroundUtils.h"
#include "Chat.h"
#include "Config.h"
#include "Containers.h"
#include "Language.h"
#include "Opcodes.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "GameTime.h"
#include "Player.h"
#include "WorldSessionMgr.h"

constexpr uint32 MapAlteracValley = 30;

CrossFactionGroupInfo::CrossFactionGroupInfo(GroupQueueInfo* groupInfo)
{
    uint32 sumLevels = 0;
    uint32 sumAverageItemLevels = 0;
    uint32 playersCount = 0;

    for (auto const& playerGuid : groupInfo->Players)
    {
        auto player = ObjectAccessor::FindConnectedPlayer(playerGuid);
        if (!player)
            continue;

        if (player->getClass() == CLASS_HUNTER && !IsHunterJoining)
            IsHunterJoining = true;

        sumLevels += player->GetLevel();
        sumAverageItemLevels += player->GetAverageItemLevel();
        playersCount++;

        SumAverageItemLevel += player->GetAverageItemLevel();
        SumPlayerLevel += player->GetLevel();
    }

    if (!playersCount)
        return;

    AveragePlayersLevel = sumLevels / playersCount;
    AveragePlayersItemLevel = sumAverageItemLevels / playersCount;
}

CFBG::CFBG()
{
    _raceData =
    {
        RaceData{ CLASS_NONE,           { 0 }, { 0 } },
        RaceData{ CLASS_WARRIOR,        { RACE_HUMAN, RACE_DWARF, RACE_GNOME, RACE_DRAENEI  }, { RACE_ORC, RACE_TAUREN, RACE_TROLL } },
        RaceData{ CLASS_PALADIN,        { RACE_HUMAN, RACE_DWARF, RACE_DRAENEI }, { RACE_BLOODELF } },
        RaceData{ CLASS_HUNTER,         { RACE_DWARF, RACE_DRAENEI }, { RACE_ORC, RACE_TAUREN, RACE_TROLL, RACE_BLOODELF } },
        RaceData{ CLASS_ROGUE,          { RACE_HUMAN, RACE_DWARF, RACE_GNOME }, { RACE_ORC, RACE_TROLL, RACE_BLOODELF } },
        RaceData{ CLASS_PRIEST,         { RACE_HUMAN, RACE_DWARF, RACE_DRAENEI  }, { RACE_TROLL, RACE_BLOODELF } },
        RaceData{ CLASS_DEATH_KNIGHT,   { RACE_HUMAN, RACE_DWARF, RACE_GNOME, RACE_DRAENEI }, { RACE_ORC, RACE_TAUREN, RACE_TROLL, RACE_BLOODELF } },
        RaceData{ CLASS_SHAMAN,         { RACE_DRAENEI }, { RACE_ORC, RACE_TAUREN, RACE_TROLL  } },
        RaceData{ CLASS_MAGE,           { RACE_HUMAN, RACE_GNOME }, { RACE_BLOODELF, RACE_TROLL } },
        RaceData{ CLASS_WARLOCK,        { RACE_HUMAN, RACE_GNOME }, { RACE_ORC, RACE_BLOODELF } },
        RaceData{ CLASS_NONE,           { 0 }, { 0 } },
        RaceData{ CLASS_DRUID,          { RACE_HUMAN }, { RACE_TAUREN } }
    };

    _raceInfo =
    {
        CFBGRaceInfo{ RACE_HUMAN,    "human",    TEAM_HORDE    },
        CFBGRaceInfo{ RACE_NIGHTELF, "nightelf", TEAM_HORDE    },
        CFBGRaceInfo{ RACE_DWARF,    "dwarf",    TEAM_HORDE    },
        CFBGRaceInfo{ RACE_GNOME,    "gnome",    TEAM_HORDE    },
        CFBGRaceInfo{ RACE_DRAENEI,  "draenei",  TEAM_HORDE    },
        CFBGRaceInfo{ RACE_ORC,      "orc",      TEAM_ALLIANCE },
        CFBGRaceInfo{ RACE_BLOODELF, "bloodelf", TEAM_ALLIANCE },
        CFBGRaceInfo{ RACE_TROLL,    "troll",    TEAM_ALLIANCE },
        CFBGRaceInfo{ RACE_TAUREN,   "tauren",   TEAM_ALLIANCE }
    };
}

CFBG* CFBG::instance()
{
    static CFBG instance;
    return &instance;
}

void CFBG::LoadConfig()
{
    _IsEnableSystem = sConfigMgr->GetOption<bool>("CFBG.Enable", false);
    if (!_IsEnableSystem)
        return;

    _IsEnableWGSystem = sConfigMgr->GetOption<bool>("CFBG.Battlefield.Enable", true);
    _IsEnableWGTeamLock = sConfigMgr->GetOption<bool>("CFBG.Battlefield.TeamLock.Enable", true);
    _IsEnableWGNativePriority = sConfigMgr->GetOption<bool>("CFBG.Battlefield.NativePriority.Enable", true);
    _IsEnableWGReapplyOnResurrect = sConfigMgr->GetOption<bool>("CFBG.Battlefield.ReapplyOnResurrect.Enable", true);
    _IsEnableAvgIlvl = sConfigMgr->GetOption<bool>("CFBG.Include.Avg.Ilvl.Enable", false);
    _IsEnableBalancedTeams = sConfigMgr->GetOption<bool>("CFBG.BalancedTeams", false);
    _IsEnableEvenTeams = sConfigMgr->GetOption<bool>("CFBG.EvenTeams.Enabled", false);
    _IsEnableBalanceClassLowLevel = sConfigMgr->GetOption<bool>("CFBG.BalancedTeams.Class.LowLevel", true);
    _IsEnableResetCooldowns = sConfigMgr->GetOption<bool>("CFBG.ResetCooldowns", false);
    _IsEnableBalanceTeamsOnEntry = sConfigMgr->GetOption<bool>("CFBG.BalanceTeamsOnEntry.Enabled", true);
    _showPlayerName = sConfigMgr->GetOption<bool>("CFBG.Show.PlayerName", false);
    _EvenTeamsMaxPlayersThreshold = sConfigMgr->GetOption<uint32>("CFBG.EvenTeams.MaxPlayersThreshold", 0);
    _MaxPlayersCountInGroup = sConfigMgr->GetOption<uint32>("CFBG.Players.Count.In.Group", 3);
    _balanceClassMinLevel = sConfigMgr->GetOption<uint8>("CFBG.BalancedTeams.Class.MinLevel", 10);
    _balanceClassMaxLevel = sConfigMgr->GetOption<uint8>("CFBG.BalancedTeams.Class.MaxLevel", 19);
    _balanceClassLevelDiff = sConfigMgr->GetOption<uint8>("CFBG.BalancedTeams.Class.LevelDiff", 2);
    _randomizeRaces = sConfigMgr->GetOption<bool>("CFBG.RandomRaceSelection", true);
}

uint32 CFBG::GetBGTeamAverageItemLevel(Battleground* bg, TeamId team)
{
    if (!bg)
    {
        return 0;
    }

    uint32 sum = 0;
    uint32 count = 0;

    for (auto const& [playerGuid, player] : bg->GetPlayers())
    {
        if (player && player->GetTeamId() == team)
        {
            sum += player->GetAverageItemLevel();
            count++;
        }
    }

    if (!count || !sum)
    {
        return 0;
    }

    return sum / count;
}

uint32 CFBG::GetBGTeamSumPlayerLevel(Battleground* bg, TeamId team)
{
    if (!bg)
    {
        return 0;
    }

    uint32 sum = 0;

    for (auto const& [playerGuid, player] : bg->GetPlayers())
    {
        if (player && player->GetTeamId() == team)
        {
            sum += player->GetLevel();
        }
    }

    return sum;
}

std::optional<TeamId> CFBG::ResolveHunterOverride(Battleground* bg, CrossFactionGroupInfo const& cfGroupInfo)
{
    if (!IsEnableEvenTeams())
        return std::nullopt;

    uint32 playerLevel = cfGroupInfo.AveragePlayersLevel;

    // if CFBG.BalancedTeams.Class.LowLevel is enabled, balance the quantity of
    // hunters per team when a hunter is joining within the configured level band.
    if (IsEnableBalanceClassLowLevel() &&
        (playerLevel >= _balanceClassMinLevel && playerLevel <= _balanceClassMaxLevel) &&
        (playerLevel >= getBalanceClassMinLevel(bg)) &&
        cfGroupInfo.IsHunterJoining)
    {
        return getTeamWithLowerClass(bg, CLASS_HUNTER);
    }

    return std::nullopt;
}

TeamId CFBG::ResolveBalancedTeam(TeamBalanceContext const& ctx)
{
    // 1. Head-count: the smaller side always wins.
    if (ctx.countA != ctx.countH)
        return ctx.countA < ctx.countH ? TEAM_ALLIANCE : TEAM_HORDE;

    // 2. Level sum (only if CFBG.BalancedTeams).
    if (IsEnableBalancedTeams())
    {
        TeamId team = ctx.fallback;

        // First select team - where the sum of the levels is less
        if (ctx.levelSumA != ctx.levelSumH)
            team = ctx.levelSumA < ctx.levelSumH ? TEAM_ALLIANCE : TEAM_HORDE;

        // EvenTeams refinement (only if CFBG.EvenTeams.Enabled).
        if (IsEnableEvenTeams())
        {
            if (ctx.hunterOverride)
            {
                team = *ctx.hunterOverride;
            }
            // Zero-denominator guard: formation / low-occupancy can present an
            // empty side; float division by 0 would yield inf/NaN and garbage.
            else if (ctx.evenCountA > 0 && ctx.evenCountH > 0)
            {
                // We need to have a diff of 0.5f
                // Range of calculation: [minBgLevel, maxBgLevel], i.e: [10,20)
                float avgLvlAlliance = ctx.levelSumA / (float)ctx.evenCountA;
                float avgLvlHorde = ctx.levelSumH / (float)ctx.evenCountH;

                if (std::abs(avgLvlAlliance - avgLvlHorde) >= 0.5f)
                    team = avgLvlAlliance < avgLvlHorde ? TEAM_ALLIANCE : TEAM_HORDE;
                else // it's balanced, so we should only check the ilvl
                    team = ctx.avgIlvlA < ctx.avgIlvlH ? TEAM_ALLIANCE : TEAM_HORDE;
            }
        }
        else if (ctx.levelSumA == ctx.levelSumH)
        {
            team = ctx.avgIlvlA < ctx.avgIlvlH ? TEAM_ALLIANCE : TEAM_HORDE;
        }

        return team;
    }

    // 3. Item level (only if CFBG.Include.Avg.Ilvl.Enable).
    if (IsEnableAvgIlvl() && ctx.avgIlvlA != ctx.avgIlvlH)
        return ctx.avgIlvlA < ctx.avgIlvlH ? TEAM_ALLIANCE : TEAM_HORDE;

    // 4. Fallback: the provisional / candidate team.
    return ctx.fallback;
}

uint8 CFBG::getBalanceClassMinLevel(const Battleground* bg) const
{
    return static_cast<uint8>(bg->GetMaxLevel()) - _balanceClassLevelDiff;
}

TeamId CFBG::getTeamWithLowerClass(Battleground *bg, Classes c)
{
    uint16 hordeClassQty = 0;
    uint16 allianceClassQty = 0;

    for (auto const& [playerGuid, player] : bg->GetPlayers())
    {
        if (player && player->getClass() == c)
        {
            if (player->GetTeamId() == TEAM_ALLIANCE)
            {
                allianceClassQty++;
            }
            else
            {
                hordeClassQty++;
            }
        }
    }

    return hordeClassQty > allianceClassQty ? TEAM_ALLIANCE : TEAM_HORDE;
}

void CFBG::ValidatePlayerForBG(Battleground* bg, Player* player)
{
    if (!_IsEnableSystem || !bg || bg->isArena() || !player)
        return;

    // A WG fake survives the teleport into a BG: OnPlayerUpdateZone's
    // InBattleground() guard blocks the deferred clear. Drop it so
    // BalanceTeamsOnEntry and SetFakeRaceAndMorph run against BG state.
    if (IsPlayerFake(player))
        ClearFakePlayer(player);

    BalanceTeamsOnEntry(bg, player);

    TeamId teamId{ player->GetBgTeamId() };

    if (player->GetTeamId(true) == teamId)
        return;

    BGData& bgdata = player->GetBGData();

    if (bgdata.bgTeamId != teamId)
        bgdata.bgTeamId = teamId;

    SetFakeRaceAndMorph(player);

    if (bg->GetMapId() == MapAlteracValley)
    {
        if (teamId == TEAM_HORDE)
        {
            player->GetReputationMgr().ApplyForceReaction(FACTION_FROSTWOLF_CLAN, REP_FRIENDLY, true);
            player->GetReputationMgr().ApplyForceReaction(FACTION_STORMPIKE_GUARD, REP_HOSTILE, true);
        }
        else
        {
            player->GetReputationMgr().ApplyForceReaction(FACTION_FROSTWOLF_CLAN, REP_HOSTILE, true);
            player->GetReputationMgr().ApplyForceReaction(FACTION_STORMPIKE_GUARD, REP_FRIENDLY, true);
        }

        player->GetReputationMgr().SendForceReactions();
    }
}

void CFBG::BalanceTeamsOnEntry(Battleground* bg, Player* player)
{
    // The invite-time team was chosen using level/ilvl, but declined invites can
    // leave the teams uneven once players actually arrive. Here we only correct
    // that head-count distortion for solo entrants.
    if (!IsEnableSystem() || !IsEnableBalanceTeamsOnEntry())
        return;

    if (bg->isArena() || bg->isRated())
        return;

    // Solo entrants only: never split a party across teams.
    if (player->GetGroup())
        return;

    // Genuine first entry only: skip relog re-adds (already in the BG), otherwise
    // the invited-count books would be adjusted a second time.
    if (bg->GetPlayers().find(player->GetGUID()) != bg->GetPlayers().end())
        return;

    // Never flip a player who is already faked. The faction sync that backs
    // GetTeamId() -- SetFakeRaceAndMorph -> SetFactionForRace -> setTeamId() -- is
    // skipped for already-faked players (its IsPlayerFake guard). Flipping bgTeamId
    // now would therefore move the player to the new side for grouping/graveyards/
    // win purposes while GetTeamId() (used by flag capture and scoring) stays on the
    // OLD side -- e.g. an Alliance player on Horde's side capturing Alliance flags.
    // Fresh entrants are not faked yet (the morph runs right after this), so they
    // are still rebalanced normally.
    if (IsPlayerFake(player))
        return;

    TeamId provisional = player->GetBgTeamId();

    // Live head counts correctly EXCLUDE the entering player (counted later in
    // Battleground::AddPlayer).
    int32 countA = bg->GetPlayersCountByTeam(TEAM_ALLIANCE);
    int32 countH = bg->GetPlayersCountByTeam(TEAM_HORDE);

    // Sides already balanced: keep the provisional team, no morph / count churn.
    if (countA == countH)
        return;

    TeamId corrected = (countA < countH) ? TEAM_ALLIANCE : TEAM_HORDE;

    if (corrected == provisional)
        return;

    // Move the reserved slot to the corrected side. DecreaseInvitedCount is safe:
    // accept does not decrement, so the player is still counted as invited on the
    // provisional team; the method is underflow-guarded. The matching decrement
    // happens in RemovePlayerAtLeave on the (corrected) current team -> zero-sum.
    bg->DecreaseInvitedCount(provisional);
    bg->IncreaseInvitedCount(corrected);
    player->GetBGData().bgTeamId = corrected;

    // The player was already teleported to the provisional base before AddPlayer;
    // move them to the corrected base so they don't spawn at the enemy's.
    Position const* startPos = bg->GetTeamStartPosition(corrected);
    player->TeleportTo(bg->GetMapId(), startPos->GetPositionX(), startPos->GetPositionY(),
        startPos->GetPositionZ(), startPos->GetOrientation());
}

uint32 CFBG::GetMorphFromRace(uint8 race, uint8 gender)
{
    switch (race)
    {
        case RACE_BLOODELF:
            return gender == GENDER_MALE ? FAKE_M_BLOOD_ELF : FAKE_F_BLOOD_ELF;
        case RACE_ORC:
            return gender == GENDER_MALE ? FAKE_M_FEL_ORC : FAKE_F_ORC;
        case RACE_TROLL:
            return gender == GENDER_MALE ? FAKE_M_TROLL : FAKE_F_BLOOD_ELF;
        case RACE_TAUREN:
            return gender == GENDER_MALE ? FAKE_M_TAUREN : FAKE_F_TAUREN;
        case RACE_DRAENEI:
            return gender == GENDER_MALE ? FAKE_M_BROKEN_DRAENEI : FAKE_F_DRAENEI;
        case RACE_DWARF:
            return gender == GENDER_MALE ? FAKE_M_DWARF : FAKE_F_HUMAN;
        case RACE_GNOME:
            return gender == GENDER_MALE ? FAKE_M_GNOME : FAKE_F_GNOME;
        case RACE_NIGHTELF: // female is missing and male causes client crashes...
        case RACE_HUMAN:
            return gender == GENDER_MALE ? FAKE_M_HUMAN : FAKE_F_HUMAN;
        default:
            // Default: Blood elf.
            return gender == GENDER_MALE ? FAKE_M_BLOOD_ELF : FAKE_F_BLOOD_ELF;
    }
}

CFBG::RandomSkinInfo CFBG::GetRandomRaceMorph(TeamId team, uint8 playerClass, uint8 gender)
{
    uint8 playerRace = Acore::Containers::SelectRandomContainerElement(team == TEAM_ALLIANCE ? _raceData[playerClass].availableRacesH : _raceData[playerClass].availableRacesA);
    uint32 playerMorph = GetMorphFromRace(playerRace, gender);

    return { playerRace, playerMorph };
}

void CFBG::SetFakeRaceAndMorph(Player* player)
{
    if (!player->InBattleground() || player->GetTeamId(true) == player->GetBgTeamId() || IsPlayerFake(player))
        return;

    // generate random race and morph
    RandomSkinInfo skinInfo{ GetRandomRaceMorph(player->GetTeamId(true), player->getClass(), player->getGender()) };

    uint8 selectedRace = player->GetPlayerSetting("mod-cfbg", SETTING_CFBG_RACE).value;

    if (!RandomizeRaces() && selectedRace && IsRaceValidForFaction(player->GetTeamId(true), selectedRace))
    {
        skinInfo.first = selectedRace;
        skinInfo.second = GetMorphFromRace(skinInfo.first, player->getGender());
    }

    FakePlayer fakePlayerInfo
    {
        skinInfo.first,
        skinInfo.second,
        player->TeamIdForRace(skinInfo.first),
        player->getRace(true),
        player->GetDisplayId(),
        player->GetNativeDisplayId(),
        player->GetTeamId(true)
    };

    player->setRace(fakePlayerInfo.FakeRace);
    SetFactionForRace(player, fakePlayerInfo.FakeRace, fakePlayerInfo.FakeTeamID);
    player->SetDisplayId(fakePlayerInfo.FakeMorph);
    player->SetNativeDisplayId(fakePlayerInfo.FakeMorph);

    _fakePlayerStore.emplace(player, std::move(fakePlayerInfo));
}

void CFBG::SetFakeRaceAndMorphForBF(Player* player, TeamId assignedTeam)
{
    if (!player || IsPlayerFake(player))
        return;

    TeamId realTeam = player->GetTeamId(true);
    if (realTeam == assignedTeam)
        return;

    // Generate a race/morph from the assigned team's faction (opposite of real faction)
    RandomSkinInfo skinInfo{ GetRandomRaceMorph(realTeam, player->getClass(), player->getGender()) };

    uint8 selectedRace = player->GetPlayerSetting("mod-cfbg", SETTING_CFBG_RACE).value;

    if (!RandomizeRaces() && selectedRace && IsRaceValidForFaction(realTeam, selectedRace))
    {
        skinInfo.first = selectedRace;
        skinInfo.second = GetMorphFromRace(skinInfo.first, player->getGender());
    }

    FakePlayer fakePlayerInfo
    {
        skinInfo.first,
        skinInfo.second,
        assignedTeam,
        player->getRace(true),
        player->GetDisplayId(),
        player->GetNativeDisplayId(),
        realTeam
    };

    player->setRace(fakePlayerInfo.FakeRace);
    SetFactionForRace(player, fakePlayerInfo.FakeRace, assignedTeam);
    player->SetDisplayId(fakePlayerInfo.FakeMorph);
    player->SetNativeDisplayId(fakePlayerInfo.FakeMorph);

    _fakePlayerStore.emplace(player, std::move(fakePlayerInfo));
}

void CFBG::SetFactionForRace(Player* player, uint8 Race, TeamId teamId)
{
    if (!player)
        return;

    player->setTeamId(teamId);

    ChrRacesEntry const* DBCRace = sChrRacesStore.LookupEntry(Race);
    player->SetFaction(DBCRace ? DBCRace->FactionID : 0);

    for (Unit* controlled : player->m_Controlled)
    {
        if (controlled)
            controlled->SetFaction(player->GetFaction());
    }
}

void CFBG::ClearFakePlayer(Player* player)
{
    if (!IsPlayerFake(player))
        return;

    player->setRace(_fakePlayerStore[player].RealRace);
    player->SetDisplayId(_fakePlayerStore[player].RealMorph);
    player->SetNativeDisplayId(_fakePlayerStore[player].RealNativeMorph);
    SetFactionForRace(player, _fakePlayerStore[player].RealRace, _fakePlayerStore[player].RealTeamID);

    // Clear forced faction reactions. Rank doesn't matter here, not used when they are removed.
    player->GetReputationMgr().ApplyForceReaction(FACTION_FROSTWOLF_CLAN, REP_FRIENDLY, false);
    player->GetReputationMgr().ApplyForceReaction(FACTION_STORMPIKE_GUARD, REP_FRIENDLY, false);

    _fakePlayerStore.erase(player);
}

void CFBG::ReapplyFakePlayer(Player* player)
{
    FakePlayer const* info = GetFakePlayer(player);
    if (!info)
        return;

    // Re-push the stored fake values after a resurrect so the assigned faction
    // and morph survive the ghost->alive transition.
    player->setRace(info->FakeRace);
    SetFactionForRace(player, info->FakeRace, info->FakeTeamID);
    player->SetDisplayId(info->FakeMorph);
    player->SetNativeDisplayId(info->FakeMorph);
}

bool CFBG::IsPlayerFake(Player* player)
{
    return _fakePlayerStore.contains(player);
}

FakePlayer const* CFBG::GetFakePlayer(Player* player) const
{
    return Acore::Containers::MapGetValuePtr(_fakePlayerStore, player);
}

std::optional<TeamId> CFBG::GetWGWarAssignment(ObjectGuid guid) const
{
    auto const& itr = _wgWarAssignmentStore.find(guid);
    if (itr == _wgWarAssignmentStore.end())
        return std::nullopt;

    return itr->second;
}

void CFBG::SetWGWarAssignment(ObjectGuid guid, TeamId team)
{
    _wgWarAssignmentStore[guid] = team;
}

void CFBG::ClearWGWarAssignments()
{
    _wgWarAssignmentStore.clear();
    _wgCensusValid = false;
    _wgMajorityNativeKept = 0;
}

TeamId CFBG::ResolveWGWarTeam(Player* player, uint32 nativeAllianceInvited, uint32 nativeHordeInvited)
{
    // Capture the native split once: at the first join PlayersInWar is empty
    // and nobody is faked, so the invited counts are the true native census.
    if (!_wgCensusValid)
    {
        _wgMajorityTeam = (nativeAllianceInvited >= nativeHordeInvited) ? TEAM_ALLIANCE : TEAM_HORDE;
        _wgMajorityFairShare = (nativeAllianceInvited + nativeHordeInvited) / 2;
        _wgMajorityNativeKept = 0;
        _wgCensusValid = true;
    }

    TeamId realTeam = player->GetTeamId(true);

    // Minority stays native.
    if (realTeam != _wgMajorityTeam)
        return realTeam;

    // Majority keeps its fair share native in accept order; the rest (latest
    // to commit) flip.
    if (_wgMajorityNativeKept < _wgMajorityFairShare)
    {
        ++_wgMajorityNativeKept;
        return realTeam;
    }

    return (_wgMajorityTeam == TEAM_ALLIANCE) ? TEAM_HORDE : TEAM_ALLIANCE;
}

void CFBG::DoForgetPlayersInList(Player* player)
{
    // m_FakePlayers is filled from a vector within the battleground
    // they were in previously so all players that have been in that BG will be invalidated.
    for (auto const& itr : _fakeNamePlayersStore)
    {
        WorldPacket data(SMSG_INVALIDATE_PLAYER, 8);
        data << itr.second;
        player->GetSession()->SendPacket(&data);

        if (Player* _player = ObjectAccessor::FindPlayer(itr.second))
            player->GetSession()->SendNameQueryOpcode(_player->GetGUID());
    }

    _fakeNamePlayersStore.erase(player);
}

void CFBG::FitPlayerInTeam(Player* player, bool action, Battleground* bg)
{
    if (!_IsEnableSystem)
        return;

    if (!bg)
        bg = player->GetBattleground();

    if ((!bg || bg->isArena()) && action)
        return;

    if (action)
        SetForgetBGPlayers(player, true);
    else
        SetForgetInListPlayers(player, true);
}

void CFBG::SetForgetBGPlayers(Player* player, bool value)
{
    _forgetBGPlayersStore[player] = value;
}

bool CFBG::ShouldForgetBGPlayers(Player* player)
{
    return _forgetBGPlayersStore[player];
}

void CFBG::SetForgetInListPlayers(Player* player, bool value)
{
    _forgetInListPlayersStore[player] = value;
}

bool CFBG::ShouldForgetInListPlayers(Player* player)
{
    return _forgetInListPlayersStore.find(player) != _forgetInListPlayersStore.end() && _forgetInListPlayersStore[player];
}

void CFBG::DoForgetPlayersInBG(Player* player, Battleground* bg)
{
    for (auto const& itr : bg->GetPlayers())
    {
        // Here we invalidate players in the bg to the added player
        WorldPacket data1(SMSG_INVALIDATE_PLAYER, 8);
        data1 << itr.first;
        player->GetSession()->SendPacket(&data1);

        if (Player* _player = ObjectAccessor::FindPlayer(itr.first))
        {
            player->GetSession()->SendNameQueryOpcode(_player->GetGUID()); // Send namequery answer instantly if player is available

            // Here we invalidate the player added to players in the bg
            WorldPacket data2(SMSG_INVALIDATE_PLAYER, 8);
            data2 << player->GetGUID();
            _player->GetSession()->SendPacket(&data2);
            _player->GetSession()->SendNameQueryOpcode(player->GetGUID());
        }
    }
}

bool CFBG::SendRealNameQuery(Player* player)
{
    if (IsPlayingNative(player))
        return false;

    WorldPacket data(SMSG_NAME_QUERY_RESPONSE, (8 + 1 + 1 + 1 + 1 + 1 + 10));
    data << player->GetGUID().WriteAsPacked();                  // player guid
    data << uint8(0);                                           // added in 3.1; if > 1, then end of packet
    data << player->GetName();                                  // played name
    data << uint8(0);                                           // realm name for cross realm BG usage
    data << uint8(player->getRace(true));
    data << uint8(player->getGender());
    data << uint8(player->getClass());
    data << uint8(0);                                           // is not declined
    player->GetSession()->SendPacket(&data);

    return true;
}

bool CFBG::IsPlayingNative(Player* player)
{
    return player->GetTeamId(true) == player->GetBGData().bgTeamId;
}

std::array<uint32, 2> CFBG::GetProjectedBaseCounts(Battleground* bg, BattlegroundQueue* queue, BattlegroundBracketId bracketId) const
{
    if (!bg)
        return { 0, 0 };

    std::array<uint32, 2> counts{ bg->GetPlayersCountByTeam(TEAM_ALLIANCE), bg->GetPlayersCountByTeam(TEAM_HORDE) };

    // An invite reserves the slot even if the player is disconnected; groups
    // invited to a different instance are reserved elsewhere and don't count.
    for (auto const& gInfo : queue->m_QueuedGroups[bracketId][BG_QUEUE_CFBG])
        if (gInfo->IsInvitedToBGInstanceGUID == bg->GetInstanceID())
            counts[gInfo->teamId] += gInfo->Players.size();

    return counts;
}

void CFBG::SelectBalancedGroups(BattlegroundQueue* queue, BattlegroundBracketId bracketId, Battleground* bg, uint32 maxPerTeam, uint32 allowedDiff)
{
    auto base = GetProjectedBaseCounts(bg, queue, bracketId);

    // Base level sums mirror the head counts: in-BG players plus the
    // invited-not-yet-entered ones (disconnected invitees contribute 0).
    std::array<uint32, 2> baseLevelSum{};
    if (bg)
    {
        baseLevelSum = { GetBGTeamSumPlayerLevel(bg, TEAM_ALLIANCE), GetBGTeamSumPlayerLevel(bg, TEAM_HORDE) };

        for (auto const& gInfo : queue->m_QueuedGroups[bracketId][BG_QUEUE_CFBG])
        {
            if (gInfo->IsInvitedToBGInstanceGUID != bg->GetInstanceID())
                continue;

            for (auto const& playerGuid : gInfo->Players)
                if (auto player = ObjectAccessor::FindConnectedPlayer(playerGuid))
                    baseLevelSum[gInfo->teamId] += player->GetLevel();
        }
    }

    std::array<GroupsList, 2> staged;
    std::array<uint32, 2> stagedCount{};
    std::array<uint32, 2> stagedLevelSum{};
    std::array<uint32, 2> stagedIlvlSum{};

    // Stage: greedy FIFO pass onto the smaller projected side; groups are
    // staged whole -- a premade party is never split.
    for (auto const& gInfo : queue->m_QueuedGroups[bracketId][BG_QUEUE_CFBG])
    {
        if (gInfo->IsInvitedToBGInstanceGUID)
            continue;

        auto cfInfo = CrossFactionGroupInfo(gInfo);

        TeamBalanceContext ctx;
        ctx.countA = base[TEAM_ALLIANCE] + stagedCount[TEAM_ALLIANCE];
        ctx.countH = base[TEAM_HORDE] + stagedCount[TEAM_HORDE];

        ctx.levelSumA = baseLevelSum[TEAM_ALLIANCE] + stagedLevelSum[TEAM_ALLIANCE];
        ctx.levelSumH = baseLevelSum[TEAM_HORDE] + stagedLevelSum[TEAM_HORDE];

        // Fold the candidate's level sum into its projected side.
        if (gInfo->teamId == TEAM_ALLIANCE)
            ctx.levelSumA += cfInfo.SumPlayerLevel;
        else
            ctx.levelSumH += cfInfo.SumPlayerLevel;

        // ilvl metric: live BG average when reinforcing, staged sums at formation.
        ctx.avgIlvlA = bg ? GetBGTeamAverageItemLevel(bg, TEAM_ALLIANCE) : stagedIlvlSum[TEAM_ALLIANCE];
        ctx.avgIlvlH = bg ? GetBGTeamAverageItemLevel(bg, TEAM_HORDE) : stagedIlvlSum[TEAM_HORDE];

        ctx.evenCountA = ctx.countA;
        ctx.evenCountH = ctx.countH;

        ctx.hunterOverride = bg ? ResolveHunterOverride(bg, cfInfo) : std::nullopt;
        ctx.fallback = gInfo->teamId;

        TeamId team = ResolveBalancedTeam(ctx);

        // team is the smaller projected side; under a symmetric cap, a group
        // that does not fit there cannot fit the other side either.
        if (base[team] + stagedCount[team] + gInfo->Players.size() > maxPerTeam)
            continue;

        staged[team].emplace_back(gInfo);
        stagedCount[team] += gInfo->Players.size();
        stagedLevelSum[team] += cfInfo.SumPlayerLevel;
        stagedIlvlSum[team] += cfInfo.SumAverageItemLevel;
    }

    // Repair: move or drop staged groups until the diff fits allowedDiff. Moves
    // strictly shrink the diff, drops the selection, so the loop terminates.
    while (true)
    {
        uint32 projA = base[TEAM_ALLIANCE] + stagedCount[TEAM_ALLIANCE];
        uint32 projH = base[TEAM_HORDE] + stagedCount[TEAM_HORDE];
        uint32 diff = projA > projH ? projA - projH : projH - projA;

        if (diff <= allowedDiff)
            break;

        TeamId larger = projA > projH ? TEAM_ALLIANCE : TEAM_HORDE;
        TeamId smaller = larger == TEAM_ALLIANCE ? TEAM_HORDE : TEAM_ALLIANCE;

        // Imbalance rooted in already-entered/invited players; the selection
        // only narrows the gap -- invite it as-is.
        if (staged[larger].empty())
            break;

        // Prefer moving a group across -- nobody loses the invite. Pick the
        // move landing closest to balance (first staged wins ties).
        GroupQueueInfo* moveGroup = nullptr;
        uint32 moveDiff = diff;

        for (auto const& gInfo : staged[larger])
        {
            uint32 transfer = gInfo->Players.size() * 2;
            uint32 newDiff = transfer > diff ? transfer - diff : diff - transfer;

            if (newDiff < moveDiff && base[smaller] + stagedCount[smaller] + gInfo->Players.size() <= maxPerTeam)
            {
                moveGroup = gInfo;
                moveDiff = newDiff;
            }
        }

        if (moveGroup)
        {
            std::erase(staged[larger], moveGroup);
            staged[smaller].emplace_back(moveGroup);
            stagedCount[larger] -= moveGroup->Players.size();
            stagedCount[smaller] += moveGroup->Players.size();
            continue;
        }

        // No useful move: drop the group landing closest to balance; ties drop
        // the newest. Dropped groups stay queued for the next update.
        GroupQueueInfo* dropGroup = nullptr;
        uint32 dropDiff = 0;

        for (auto const& gInfo : staged[larger])
        {
            uint32 size = gInfo->Players.size();
            uint32 newDiff = size > diff ? size - diff : diff - size;

            if (!dropGroup || newDiff < dropDiff || (newDiff == dropDiff && gInfo->JoinTime > dropGroup->JoinTime))
            {
                dropGroup = gInfo;
                dropDiff = newDiff;
            }
        }

        std::erase(staged[larger], dropGroup);
        stagedCount[larger] -= dropGroup->Players.size();
    }

    // Invite: write the staged selection into the pools; caps were enforced
    // while staging, so every AddGroup succeeds.
    queue->m_SelectionPools[TEAM_ALLIANCE].Init();
    queue->m_SelectionPools[TEAM_HORDE].Init();

    for (auto team : { TEAM_ALLIANCE, TEAM_HORDE })
    {
        for (auto const& gInfo : staged[team])
        {
            gInfo->teamId = team;
            queue->m_SelectionPools[team].AddGroup(gInfo, maxPerTeam - base[team]);
        }
    }
}

bool CFBG::CheckCrossFactionMatch(BattlegroundQueue* queue, BattlegroundBracketId bracket_id, uint32 minPlayers, uint32 maxPlayers)
{
    if (!IsEnableSystem())
        return false;

    SelectBalancedGroups(queue, bracket_id, nullptr, maxPlayers, IsEnableEvenTeams() ? 0 : 1);

    // Return when we're ready to start a BG, if we're in startup process
    if (queue->m_SelectionPools[TEAM_ALLIANCE].GetPlayerCount() >= minPlayers &&
        queue->m_SelectionPools[TEAM_HORDE].GetPlayerCount() >= minPlayers)
        return true;

    // Return false when we didn't manage to fill the BattleGround in Filling "mode".
    // reset selectionpool for further attempts
    queue->m_SelectionPools[TEAM_ALLIANCE].Init();
    queue->m_SelectionPools[TEAM_HORDE].Init();
    return true;
}

bool CFBG::FillPlayersToCFBG(BattlegroundQueue* bgqueue, Battleground* bg, BattlegroundBracketId bracket_id)
{
    if (!IsEnableSystem() || bg->isArena() || bg->isRated())
        return false;

    uint32 allowedDiff = 1;

    if (IsEnableEvenTeams())
    {
        // Threshold 0 enforces even teams at all sizes; otherwise relax to
        // diff <= 1 once the projected total reaches threshold * 2.
        uint32 threshold = EvenTeamsMaxPlayersThreshold();
        auto base = GetProjectedBaseCounts(bg, bgqueue, bracket_id);

        if (!threshold || base[TEAM_ALLIANCE] + base[TEAM_HORDE] < threshold * 2)
            allowedDiff = 0;
    }

    // Cap on GetMaxPlayersPerTeam, not GetFreeSlotsForTeam: the latter imposes
    // no relative cap with Battleground.InvitationType = 0.
    SelectBalancedGroups(bgqueue, bracket_id, bg, bg->GetMaxPlayersPerTeam(), allowedDiff);
    return true;
}

bool CFBG::isClassJoining(uint8 _class, Player* player, uint32 minLevel)
{
    if (!player)
    {
        return false;
    }

    return player->getClass() == _class && (player->GetLevel() >= minLevel);
}

void CFBG::UpdateForget(Player* player)
{
    Battleground* bg = player->GetBattleground();
    if (bg)
    {
        if (ShouldForgetBGPlayers(player) && bg)
        {
            DoForgetPlayersInBG(player, bg);
            SetForgetBGPlayers(player, false);
        }
    }
    else if (ShouldForgetInListPlayers(player))
    {
        DoForgetPlayersInList(player);
        SetForgetInListPlayers(player, false);
    }
}

void CFBG::SendMessageQueue(BattlegroundQueue* bgQueue, Battleground* bg, PvPDifficultyEntry const* bracketEntry, Player* leader)
{
    BattlegroundBracketId bracketId = bracketEntry->GetBracketId();

    auto bgName = bg->GetName();
    uint32 q_min_level = std::min(bracketEntry->minLevel, (uint32)80);
    uint32 q_max_level = std::min(bracketEntry->maxLevel, (uint32)80);
    uint32 MinPlayers = GetMinPlayersPerTeam(bg, bracketEntry) * 2;
    uint32 qTotal = bgQueue->GetPlayersCountInGroupsQueue(bracketId, (BattlegroundQueueGroupTypes)BG_QUEUE_CFBG);

    if (sWorld->getBoolConfig(CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_PLAYERONLY))
    {
        ChatHandler(leader->GetSession()).PSendSysMessage("CFBG {} (Levels: {} - {}). Registered: {}/{}", bgName.c_str(), q_min_level, q_max_level, qTotal, MinPlayers);
    }
    else
    {
        if (sWorld->getBoolConfig(CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_TIMED))
        {
            if (bgQueue->GetQueueAnnouncementTimer(bracketEntry->bracketId) < 0)
            {
                bgQueue->SetQueueAnnouncementTimer(bracketEntry->bracketId, sWorld->getIntConfig(CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_TIMER));
            }
        }
        else
        {
            // Defer to the shared core announcer (cross-faction); spam-window and
            // Limit gating now happen centrally in BattlegroundQueueAnnouncerUpdate.
            if (bgQueue->GetQueueAnnouncementTimer(bracketId) < 0)
            {
                bgQueue->SetQueueAnnouncementTimer(bracketId, BG_QUEUE_ANNOUNCER_IMMEDIATE_DEBOUNCE, true);
            }
        }
    }
}

bool CFBG::IsRaceValidForFaction(uint8 teamId, uint8 race)
{
    for (auto const& raceVariable : _raceInfo)
    {
        if (race == raceVariable.RaceId && teamId == raceVariable.TeamId)
        {
            return true;
        }
    }

    return false;
}
