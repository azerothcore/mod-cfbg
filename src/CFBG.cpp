/*
 * Copyright (С) since 2019 Andrei Guluaev (Winfidonarleyan/Kargatum) https://github.com/Winfidonarleyan
 * Licence MIT https://opensource.org/MIT
 */

#include "CFBG.h"
#include "Config.h"
#include "Log.h"
#include "ScriptMgr.h"
#include "GroupMgr.h"
#include "BattlegroundMgr.h"
#include "Opcodes.h"
#include "Chat.h"
#include "Language.h"

CFBG* CFBG::instance()
{
    static CFBG instance;
    return &instance;
}

void CFBG::LoadConfig()
{
    _IsEnableSystem = sConfigMgr->GetBoolDefault("CFBG.Enable", false);
    _IsEnableAvgIlvl = sConfigMgr->GetBoolDefault("CFBG.Include.Avg.Ilvl.Enable", false);
    _IsEnableBalancedTeams = sConfigMgr->GetBoolDefault("CFBG.BalancedTeams", false);
    _MaxPlayersCountInGroup = sConfigMgr->GetIntDefault("CFBG.Players.Count.In.Group", 3);
}

bool CFBG::IsEnableSystem()
{
    return _IsEnableSystem;
}

bool CFBG::IsEnableAvgIlvl()
{
    return _IsEnableAvgIlvl;
}

bool CFBG::IsEnableBalancedTeams()
{
    return _IsEnableBalancedTeams;
}

uint32 CFBG::GetMaxPlayersCountInGroup()
{
    return _MaxPlayersCountInGroup;
}

uint32 CFBG::GetBGTeamAverageItemLevel(Battleground* bg, TeamId team)
{
    if (!bg)
    {
        return 0;
    }

    uint32 sum = 0;
    uint32 count = 0;

    for (auto itr : bg->GetPlayers())
    {
        Player* player = itr.second;
        if (!!player && player->GetTeamId() == team)
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

    for (auto itr : bg->GetPlayers())
    {
        Player* player = itr.second;
        if (!!player && player->GetTeamId() == team)
        {
            sum += player->getLevel();
        }
    }

    return sum;
}

TeamId CFBG::GetLowerTeamIdInBG(Battleground* bg)
{
    int32 PlCountA = bg->GetPlayersCountByTeam(TEAM_ALLIANCE);
    int32 PlCountH = bg->GetPlayersCountByTeam(TEAM_HORDE);
    uint32 Diff = abs(PlCountA - PlCountH);

    if (Diff)
        return PlCountA < PlCountH ? TEAM_ALLIANCE : TEAM_HORDE;

    if (IsEnableBalancedTeams())
    {
        return GetLowerSumPlayerLvlTeamInBg(bg);
    }

    if (IsEnableAvgIlvl() && !IsAvgIlvlTeamsInBgEqual(bg))
        return GetLowerAvgIlvlTeamInBg(bg);

    uint8 rnd = urand(0, 1);

    if (rnd)
        return TEAM_ALLIANCE;

    return TEAM_HORDE;
}


TeamId CFBG::GetLowerSumPlayerLvlTeamInBg(Battleground* bg)
{
    uint32 playerLevelAlliance = GetBGTeamSumPlayerLevel(bg, TeamId::TEAM_ALLIANCE);
    uint32 playerLevelHorde = GetBGTeamSumPlayerLevel(bg, TeamId::TEAM_HORDE);

    if (playerLevelAlliance == playerLevelHorde)
    {
        return GetLowerAvgIlvlTeamInBg(bg);
    }

    return (playerLevelAlliance < playerLevelHorde) ? TEAM_ALLIANCE : TEAM_HORDE;
}

TeamId CFBG::GetLowerAvgIlvlTeamInBg(Battleground* bg)
{
    uint32 AvgAlliance = GetBGTeamAverageItemLevel(bg, TeamId::TEAM_ALLIANCE);
    uint32 AvgHorde = GetBGTeamAverageItemLevel(bg, TeamId::TEAM_HORDE);

    return (AvgAlliance < AvgHorde) ? TEAM_ALLIANCE : TEAM_HORDE;
}

bool CFBG::IsAvgIlvlTeamsInBgEqual(Battleground* bg)
{
    uint32 AvgAlliance = GetBGTeamAverageItemLevel(bg, TeamId::TEAM_ALLIANCE);
    uint32 AvgHorde = GetBGTeamAverageItemLevel(bg, TeamId::TEAM_HORDE);

    return AvgAlliance == AvgHorde;
}

void CFBG::ValidatePlayerForBG(Battleground* bg, Player* player, TeamId teamId)
{
    BGData bgdata = player->GetBGData();
    bgdata.bgTeamId = teamId;
    player->SetBGData(bgdata);

    SetFakeRaceAndMorph(player);

    float x, y, z, o;
    bg->GetTeamStartLoc(teamId, x, y, z, o);
    player->TeleportTo(bg->GetMapId(), x, y, z, o);
}

uint32 CFBG::GetAllPlayersCountInBG(Battleground* bg)
{
    return bg->GetPlayersSize();
}


template <std::size_t N>
uint8 CFBG::getRandomRace(const uint8 (&races)[N]) {
    int r = urand(0, N-1);
    return races[r];
}

uint32 CFBG::getMorphFromRace(uint8 race, uint8 gender) {

    if (gender == GENDER_MALE) {
        switch (race) {
            case RACE_ORC:
                return FAKE_M_FEL_ORC;
            case RACE_DWARF:
                return FAKE_M_DWARF;
            case RACE_NIGHTELF:
                return FAKE_M_NIGHT_ELF;
            case RACE_DRAENEI:
                return FAKE_M_BROKEN_DRAENEI;
            case RACE_TROLL:
                return FAKE_M_TROLL;
            case RACE_HUMAN:
                return FAKE_M_HUMAN;
            case RACE_BLOODELF:
                return FAKE_M_BLOOD_ELF;
            case RACE_GNOME:
                return FAKE_M_GNOME;
            case RACE_TAUREN:
                return FAKE_M_TAUREN;
            default:
                return FAKE_M_BLOOD_ELF; // this should never happen, it's to fix a warning about return value
        }
    } else {
        switch (race) {
            case RACE_ORC:
                return FAKE_F_ORC;
            case RACE_DRAENEI:
                return FAKE_F_DRAENEI;
            case RACE_HUMAN:
                return FAKE_F_HUMAN;
            case RACE_BLOODELF:
                return FAKE_F_BLOOD_ELF;
            case RACE_GNOME:
                return FAKE_F_GNOME;
            case RACE_TAUREN:
                return FAKE_F_TAUREN;
            default:
                return FAKE_F_BLOOD_ELF; // this should never happen, it to fix a warning about return value
        }
    }
}


void CFBG::randomRaceMorph(uint8* race, uint32* morph, TeamId team, uint8 _class, uint8 gender) {

    // if alliance find a horde race
    if (team == TEAM_ALLIANCE) {

        // default race because UNDEAD morph is missing
        *race = RACE_BLOODELF;

        /*
        * TROLL FEMALE morph is missing
        * therefore MALE and FEMALE are handled in a different way
        *
        * UNDEAD is missing too but for both gender
        */
        if (gender == GENDER_MALE) {
            *morph = FAKE_M_BLOOD_ELF;

            switch (_class) {
                case CLASS_DRUID:
                    *race = RACE_TAUREN;
                    *morph = FAKE_M_TAUREN;
                    break;
                case CLASS_SHAMAN:
                case CLASS_WARRIOR:
                    // UNDEAD missing (only for WARRIOR)
                    *race = getRandomRace({ RACE_ORC, RACE_TAUREN, RACE_TROLL });
                    *morph = getMorphFromRace(*race, gender);
                    break;
                case CLASS_PALADIN:
                    // BLOOD ELF, so default race
                    break;
                case CLASS_HUNTER:
                case CLASS_DEATH_KNIGHT:
                    // UNDEAD missing (only for DEATH_KNIGHT)
                    *race = getRandomRace({ RACE_ORC, RACE_TAUREN, RACE_TROLL, RACE_BLOODELF });
                    *morph = getMorphFromRace(*race, gender);
                    break;
                case CLASS_ROGUE:
                    // UNDEAD missing
                    *race = getRandomRace({ RACE_ORC, RACE_TROLL, RACE_BLOODELF });
                    *morph = getMorphFromRace(*race, gender);
                    break;
                case CLASS_MAGE:
                case CLASS_PRIEST:
                    // UNDEAD missing
                    *race = getRandomRace({ RACE_TROLL, RACE_BLOODELF });
                    *morph = getMorphFromRace(*race, gender);
                    break;
                case CLASS_WARLOCK:
                    // UNDEAD missing
                    *race = getRandomRace({ RACE_ORC, RACE_BLOODELF });
                    *morph = getMorphFromRace(*race, gender);
                    break;
            }
        }  else {
            *morph = FAKE_F_BLOOD_ELF;

            switch (_class) {
                case CLASS_DRUID:
                    *race = RACE_TAUREN;
                    *morph = FAKE_F_TAUREN;
                    break;
                case CLASS_SHAMAN:
                case CLASS_WARRIOR:
                    // UNDEAD missing (only for WARRIOR)
                    // TROLL FEMALE missing
                    *race = getRandomRace({ RACE_ORC, RACE_TAUREN });
                    *morph = getMorphFromRace(*race, gender);
                    break;
                case CLASS_HUNTER:
                case CLASS_DEATH_KNIGHT:
                    // TROLL FEMALE is missing
                    // UNDEAD is missing (only for DEATH_KNIGHT)
                    *race = getRandomRace({ RACE_ORC, RACE_TAUREN, RACE_BLOODELF });
                    *morph = getMorphFromRace(*race, gender);
                    break;
                case CLASS_ROGUE:
                case CLASS_WARLOCK:
                    // UNDEAD is missing
                    // TROLL FEMALE is missing (only for Rogue)
                    *race = getRandomRace({ RACE_ORC, RACE_BLOODELF });
                    *morph = getMorphFromRace(*race, gender);
                    break;
                case CLASS_PALADIN:
                    // BLOOD ELF, so default race
                case CLASS_MAGE:
                case CLASS_PRIEST:
                    // UNDEAD and TROLL FEMALE morph are missing so use BLOOD ELF (default race)
                    break;
            }
        }

    } else { // otherwise find an alliance race

        // default race
        *race = RACE_HUMAN;

        /*
        * FEMALE morphs DWARF and NIGHT ELF are missing
        * therefore MALE and FEMALE are handled in a different way
        */
        if (gender == GENDER_MALE) {
            *morph = FAKE_M_HUMAN;

            switch (_class) {
                case CLASS_DRUID:
                    *race = RACE_NIGHTELF;
                    *morph = FAKE_M_NIGHT_ELF;
                    break;
                case CLASS_SHAMAN:
                    *race = RACE_DRAENEI;
                    *morph = FAKE_M_BROKEN_DRAENEI;
                    break;
                case CLASS_WARRIOR:
                case CLASS_DEATH_KNIGHT:
                    *race = getRandomRace({ RACE_HUMAN, RACE_DWARF, RACE_GNOME, RACE_NIGHTELF, RACE_DRAENEI });
                    *morph = getMorphFromRace(*race, gender);
                    break;
                case CLASS_PALADIN:
                    *race = getRandomRace({ RACE_HUMAN, RACE_DWARF, RACE_DRAENEI });
                    *morph = getMorphFromRace(*race, gender);
                    break;
                case CLASS_HUNTER:
                    *race = getRandomRace({ RACE_DWARF, RACE_NIGHTELF, RACE_DRAENEI });
                    *morph = getMorphFromRace(*race, gender);
                    break;
                case CLASS_ROGUE:
                    *race = getRandomRace({ RACE_HUMAN, RACE_DWARF, RACE_GNOME, RACE_NIGHTELF });
                    *morph = getMorphFromRace(*race, gender);
                    break;
                case CLASS_PRIEST:
                    *race = getRandomRace({ RACE_HUMAN, RACE_DWARF, RACE_NIGHTELF, RACE_DRAENEI });
                    *morph = getMorphFromRace(*race, gender);
                    break;
                case CLASS_MAGE:
                    *race = getRandomRace({ RACE_HUMAN, RACE_GNOME, RACE_DRAENEI });
                    *morph = getMorphFromRace(*race, gender);
                    break;
                case CLASS_WARLOCK:
                    *race = getRandomRace({ RACE_HUMAN, RACE_GNOME });
                    *morph = getMorphFromRace(*race, gender);
                    break;
            }

        } else {
            *morph = FAKE_F_HUMAN;

            switch (_class) {
                case CLASS_DRUID:
                    // FEMALE NIGHT ELF is missing
                    break;
                case CLASS_SHAMAN:
                case CLASS_HUNTER:
                    // FEMALE DWARF and NIGHT ELF are missing (only for HUNTER)
                    *race = RACE_DRAENEI;
                    *morph = FAKE_F_DRAENEI;
                    break;
                case CLASS_WARRIOR:
                case CLASS_DEATH_KNIGHT:
                case CLASS_MAGE:
                    // DWARF and NIGHT ELF are missing (only for WARRIOR and DEATH_KNIGHT)
                    *race = getRandomRace({ RACE_HUMAN, RACE_GNOME, RACE_DRAENEI });
                    *morph = getMorphFromRace(*race, gender);
                    break;
                case CLASS_PALADIN:
                case CLASS_PRIEST:
                    // DWARF is missing
                    *race = getRandomRace({ RACE_HUMAN, RACE_DRAENEI });
                    *morph = getMorphFromRace(*race, gender);
                    break;
                case CLASS_ROGUE:
                case CLASS_WARLOCK:
                    // DWARF and NIGHT ELF are missing (only for ROGUE)
                    *race = getRandomRace({ RACE_HUMAN, RACE_GNOME });
                    *morph = getMorphFromRace(*race, gender);
                    break;
            }
        }
    }

}

void CFBG::SetFakeRaceAndMorph(Player* player)
{
    if (!player->InBattleground()) {
        return;
    }

    if (player->GetTeamId(true) == player->GetBgTeamId()) {
        return;
    }

    if (IsPlayerFake(player)) {
        return;
    }

    uint8 FakeRace;
    uint32 FakeMorph;

    // generate random race and morph
    this->randomRaceMorph(&FakeRace, &FakeMorph, player->GetTeamId(true), player->getClass(), player->getGender());

    FakePlayer fakePlayer;
    fakePlayer.FakeMorph    = FakeMorph;
    fakePlayer.FakeRace     = FakeRace;
    fakePlayer.FakeTeamID   = player->TeamIdForRace(FakeRace);
    fakePlayer.RealMorph    = player->GetDisplayId();
    fakePlayer.RealRace     = player->getRace(true);
    fakePlayer.RealTeamID   = player->GetTeamId(true);

    _fakePlayerStore[player] = fakePlayer;

    player->setRace(FakeRace);
    SetFactionForRace(player, FakeRace);
    player->SetDisplayId(FakeMorph);
    player->SetNativeDisplayId(FakeMorph);
}

void CFBG::SetFactionForRace(Player* player, uint8 Race)
{
    player->setTeamId(player->TeamIdForRace(Race));

    ChrRacesEntry const* DBCRace = sChrRacesStore.LookupEntry(Race);
    player->setFaction(DBCRace ? DBCRace->FactionID : 0);
}

void CFBG::ClearFakePlayer(Player* player)
{
    if (!IsPlayerFake(player))
        return;

    player->setRace(_fakePlayerStore[player].RealRace);
    player->SetDisplayId(_fakePlayerStore[player].RealMorph);
    player->SetNativeDisplayId(_fakePlayerStore[player].RealMorph);
    SetFactionForRace(player, _fakePlayerStore[player].RealRace);

    _fakePlayerStore.erase(player);
}

bool CFBG::IsPlayerFake(Player* player)
{
    auto const& itr = _fakePlayerStore.find(player);
    if (itr != _fakePlayerStore.end())
        return true;

    return false;
}

void CFBG::DoForgetPlayersInList(Player* player)
{
    // m_FakePlayers is filled from a vector within the battleground
    // they were in previously so all players that have been in that BG will be invalidated.
    for (auto itr : _fakeNamePlayersStore)
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
    return _forgetInListPlayersStore[player];
}

void CFBG::DoForgetPlayersInBG(Player* player, Battleground* bg)
{
    for (auto itr : bg->GetPlayers())
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
    data.appendPackGUID(player->GetGUID());                     // player guid
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

bool CFBG::FillPlayersToCFBGWithSpecific(BattlegroundQueue* bgqueue, Battleground* bg, const int32 aliFree, const int32 hordeFree, BattlegroundBracketId thisBracketId, BattlegroundQueue* specificQueue, BattlegroundBracketId specificBracketId)
{
    if (!IsEnableSystem() || bg->isArena() || bg->isRated())
        return false;

    // clear selection pools
    bgqueue->m_SelectionPools[TEAM_ALLIANCE].Init();
    bgqueue->m_SelectionPools[TEAM_HORDE].Init();

    // quick check if nothing we can do:
    if (!sBattlegroundMgr->isTesting() && bgqueue->m_QueuedGroups[thisBracketId][BG_QUEUE_CFBG].empty() && specificQueue->m_QueuedGroups[specificBracketId][BG_QUEUE_CFBG].empty())
        return false;

    // copy groups from both queues to new joined container
    BattlegroundQueue::GroupsQueueType m_QueuedBoth[BG_TEAMS_COUNT];
    m_QueuedBoth[TEAM_ALLIANCE].insert(m_QueuedBoth[TEAM_ALLIANCE].end(), specificQueue->m_QueuedGroups[specificBracketId][BG_QUEUE_CFBG].begin(), specificQueue->m_QueuedGroups[specificBracketId][BG_QUEUE_CFBG].end());
    m_QueuedBoth[TEAM_ALLIANCE].insert(m_QueuedBoth[TEAM_ALLIANCE].end(), bgqueue->m_QueuedGroups[thisBracketId][BG_QUEUE_CFBG].begin(), bgqueue->m_QueuedGroups[thisBracketId][BG_QUEUE_CFBG].end());
    m_QueuedBoth[TEAM_HORDE].insert(m_QueuedBoth[TEAM_HORDE].end(), specificQueue->m_QueuedGroups[specificBracketId][BG_QUEUE_CFBG].begin(), specificQueue->m_QueuedGroups[specificBracketId][BG_QUEUE_CFBG].end());
    m_QueuedBoth[TEAM_HORDE].insert(m_QueuedBoth[TEAM_HORDE].end(), bgqueue->m_QueuedGroups[thisBracketId][BG_QUEUE_CFBG].begin(), bgqueue->m_QueuedGroups[thisBracketId][BG_QUEUE_CFBG].end());

    // ally: at first fill as much as possible
    BattlegroundQueue::GroupsQueueType::const_iterator Ali_itr = m_QueuedBoth[TEAM_ALLIANCE].begin();
    for (; Ali_itr != m_QueuedBoth[TEAM_ALLIANCE].end() && bgqueue->m_SelectionPools[TEAM_ALLIANCE].AddGroup((*Ali_itr), aliFree); ++Ali_itr);

    // horde: at first fill as much as possible
    BattlegroundQueue::GroupsQueueType::const_iterator Horde_itr = m_QueuedBoth[TEAM_HORDE].begin();
    for (; Horde_itr != m_QueuedBoth[TEAM_HORDE].end() && bgqueue->m_SelectionPools[TEAM_HORDE].AddGroup((*Horde_itr), hordeFree); ++Horde_itr);

    return true;
}

bool CFBG::FillPlayersToCFBG(BattlegroundQueue* bgqueue, Battleground* bg, const int32 aliFree, const int32 hordeFree, BattlegroundBracketId bracket_id)
{
    if (!IsEnableSystem() || bg->isArena() || bg->isRated())
        return false;

    // clear selection pools
    bgqueue->m_SelectionPools[TEAM_ALLIANCE].Init();
    bgqueue->m_SelectionPools[TEAM_HORDE].Init();

    // quick check if nothing we can do:
    if (!sBattlegroundMgr->isTesting())
        if ((aliFree > hordeFree && bgqueue->m_QueuedGroups[bracket_id][BG_QUEUE_CFBG].empty()))
            return false;

    // ally: at first fill as much as possible
    BattlegroundQueue::GroupsQueueType::const_iterator Ali_itr = bgqueue->m_QueuedGroups[bracket_id][BG_QUEUE_CFBG].begin();
    for (; Ali_itr != bgqueue->m_QueuedGroups[bracket_id][BG_QUEUE_CFBG].end() && bgqueue->m_SelectionPools[TEAM_ALLIANCE].AddGroup((*Ali_itr), aliFree); ++Ali_itr);

    // horde: at first fill as much as possible
    BattlegroundQueue::GroupsQueueType::const_iterator Horde_itr = bgqueue->m_QueuedGroups[bracket_id][BG_QUEUE_CFBG].begin();
    for (; Horde_itr != bgqueue->m_QueuedGroups[bracket_id][BG_QUEUE_CFBG].end() && bgqueue->m_SelectionPools[TEAM_HORDE].AddGroup((*Horde_itr), hordeFree); ++Horde_itr);

    return true;
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

std::unordered_map<uint64, uint32> BGSpam;
bool CFBG::SendMessageQueue(BattlegroundQueue* bgqueue, Battleground* bg, PvPDifficultyEntry const* bracketEntry, Player* leader)
{
    if (!IsEnableSystem())
        return false;

    BattlegroundBracketId bracketId = bracketEntry->GetBracketId();

    char const* bgName = bg->GetName();
    uint32 q_min_level = std::min(bracketEntry->minLevel, (uint32)80);
    uint32 q_max_level = std::min(bracketEntry->maxLevel, (uint32)80);
    uint32 MinPlayers = bg->GetMinPlayersPerTeam() * 2;
    uint32 qPlayers = bgqueue->GetPlayersCountInGroupsQueue(bracketId, (BattlegroundQueueGroupTypes)BG_QUEUE_CFBG);

    if (sWorld->getBoolConfig(CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_PLAYERONLY))
        ChatHandler(leader->GetSession()).PSendSysMessage("CFBG %s (Levels: %u - %u). Registered: %u/%u", bgName, q_min_level, q_max_level, qPlayers, MinPlayers);
    else
    {
        auto searchGUID = BGSpam.find(leader->GetGUID());

        if (searchGUID == BGSpam.end())
            BGSpam[leader->GetGUID()] = 0;

        if (sWorld->GetGameTime() - BGSpam[leader->GetGUID()] >= 30)
        {
            BGSpam[leader->GetGUID()] = sWorld->GetGameTime();
            sWorld->SendWorldText(LANG_BG_QUEUE_ANNOUNCE_WORLD, bgName, q_min_level, q_max_level, qPlayers, MinPlayers);
        }
    }

    return true;
}
