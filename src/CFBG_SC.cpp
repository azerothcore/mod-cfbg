/*
 *  Copyright (С) since 2019 Andrei Guluaev (Winfidonarleyan/Kargatum) https://github.com/Winfidonarleyan
 *  Copyright (С) since 2019+ AzerothCore <www.azerothcore.org>
 */

#include "CFBG.h"
#include "Battlefield.h"
#include "BattlefieldMgr.h"
#include "Group.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "ScriptMgr.h"
#include "BattlegroundQueue.h"

// CFBG custom script
class CFBG_BG : public BGScript
{
public:
    CFBG_BG() : BGScript("CFBG_BG", {
        ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_BEFORE_ADD_PLAYER,
        ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_ADD_PLAYER,
        ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_END_REWARD,
        ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_REMOVE_PLAYER_AT_LEAVE,
        ALLBATTLEGROUNDHOOK_ON_ADD_GROUP,
        ALLBATTLEGROUNDHOOK_CAN_FILL_PLAYERS_TO_BG,
        ALLBATTLEGROUNDHOOK_IS_CHECK_NORMAL_MATCH,
        ALLBATTLEGROUNDHOOK_CAN_SEND_MESSAGE_BG_QUEUE
    }) {}

    void OnBattlegroundBeforeAddPlayer(Battleground* bg, Player* player) override
    {
        sCFBG->ValidatePlayerForBG(bg, player);
    }

    void OnBattlegroundAddPlayer(Battleground* bg, Player* player) override
    {
        sCFBG->FitPlayerInTeam(player, true, bg);

        if (sCFBG->IsEnableResetCooldowns())
            player->RemoveArenaSpellCooldowns(true);
    }

    void OnBattlegroundEndReward(Battleground* bg, Player* player, TeamId /*winnerTeamId*/) override
    {
        if (!sCFBG->IsEnableSystem() || !bg || !player || bg->isArena())
            return;

        if (sCFBG->IsPlayerFake(player))
            sCFBG->ClearFakePlayer(player);
    }

    void OnBattlegroundRemovePlayerAtLeave(Battleground* bg, Player* player) override
    {
        if (!sCFBG->IsEnableSystem() || bg->isArena())
            return;

        sCFBG->FitPlayerInTeam(player, false, bg);

        if (sCFBG->IsPlayerFake(player))
            sCFBG->ClearFakePlayer(player);
    }

    void OnAddGroup(BattlegroundQueue* queue, GroupQueueInfo* ginfo, uint32& index, Player* /*leader*/, Group* /*group*/, BattlegroundTypeId /* bgTypeId */, PvPDifficultyEntry const* /* bracketEntry */,
        uint8 /* arenaType */, bool /* isRated */, bool /* isPremade */, uint32 /* arenaRating */, uint32 /* matchmakerRating */, uint32 /* arenaTeamId */, uint32 /* opponentsArenaTeamId */) override
    {
        if (!queue)
            return;

        if (sCFBG->IsEnableSystem() && !ginfo->ArenaType && !ginfo->IsRated)
            index = BG_QUEUE_CFBG;

        // After rework hook
        // sCFBG->OnAddGroupToBGQueue(ginfo, group);
    }

    bool CanFillPlayersToBG(BattlegroundQueue* queue, Battleground* bg, BattlegroundBracketId bracket_id) override
    {
        return !sCFBG->FillPlayersToCFBG(queue, bg, bracket_id);
    }

    bool IsCheckNormalMatch(BattlegroundQueue* queue, Battleground* bg, BattlegroundBracketId bracket_id, uint32 minPlayers, uint32 maxPlayers) override
    {
        if (!sCFBG->IsEnableSystem() || bg->isArena())
            return false;

        return sCFBG->CheckCrossFactionMatch(queue, bracket_id, minPlayers, maxPlayers);
    }

    bool CanSendMessageBGQueue(BattlegroundQueue* queue, Player* leader, Battleground* bg, PvPDifficultyEntry const* bracketEntry) override
    {
        if (bg->isArena() || !sCFBG->IsEnableSystem())
        {
            // if it's arena OR the CFBG is disabled, let the core handle the announcement
            return true;
        }

        // otherwise, let the CFBG module handle the announcement
        sCFBG->SendMessageQueue(queue, bg, bracketEntry, leader);
        return false;
    }
};

class CFBG_Player : public PlayerScript
{
public:
    CFBG_Player() : PlayerScript("CFBG_Player", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_CAN_JOIN_IN_BATTLEGROUND_QUEUE,
        PLAYERHOOK_ON_BEFORE_UPDATE,
        PLAYERHOOK_ON_BEFORE_SEND_CHAT_MESSAGE,
        PLAYERHOOK_ON_REPUTATION_CHANGE
    }) { }

    void OnPlayerLogin(Player* player) override
    {
        if (!sCFBG->IsEnableSystem())
            return;

        if (player->GetTeamId(true) != player->GetBgTeamId())
            sCFBG->FitPlayerInTeam(player, player->GetBattleground() && !player->GetBattleground()->isArena(), player->GetBattleground());
    }

    void OnPlayerLogout(Player* player) override
    {
        if (!sCFBG->IsEnableSystem() || !sCFBG->IsPlayerFake(player))
            return;

        // Only clear the WG fake state when the battlefield is not actively at
        // war.  During a running war the player may safely relog and rejoin
        // their assigned faction, so we leave the fake state intact for that
        // case.  BG fakes are always cleaned up by OnBattlegroundRemovePlayerAtLeave
        // and do not need to be handled here.
        Battlefield* bf = sBattlefieldMgr->GetBattlefieldToZoneId(player->GetZoneId());
        if (bf && bf->GetTypeId() == BATTLEFIELD_WG && !bf->IsWarTime())
            sCFBG->ClearFakePlayer(player);
    }

    bool OnPlayerCanJoinInBattlegroundQueue(Player* player, ObjectGuid /*BattlemasterGuid*/ , BattlegroundTypeId /*BGTypeID*/, uint8 joinAsGroup, GroupJoinBattlegroundResult& err) override
    {
        if (!sCFBG->IsEnableSystem())
            return true;

        if (joinAsGroup)
        {
            Group* group = player->GetGroup();
            if (!group)
                return true;

            if (group->isRaidGroup() || group->GetMembersCount() > sCFBG->GetMaxPlayersCountInGroup())
                err = ERR_BATTLEGROUND_JOIN_FAILED;

            return false;
        }

        return true;
    }

    void OnPlayerBeforeUpdate(Player* player, uint32 diff) override
    {
        if (timeCheck <= diff)
        {
            sCFBG->UpdateForget(player);
            timeCheck = 10000;
        }
        else
            timeCheck -= diff;
    }

    void OnPlayerBeforeSendChatMessage(Player* player, uint32& type, uint32& lang, std::string& /*msg*/) override
    {
        if (!player || !sCFBG->IsEnableSystem())
            return;

        Battleground* bg = player->GetBattleground();

        if (!bg || bg->isArena())
            return;

        // skip addon lang and universal
        if (lang == LANG_UNIVERSAL || lang == LANG_ADDON)
            return;

        // skip addon and system message
        if (type == CHAT_MSG_ADDON || type == CHAT_MSG_SYSTEM)
            return;

        // to gm lang
        lang = LANG_UNIVERSAL;
    }

    bool OnPlayerReputationChange(Player* player, uint32 factionID, int32& standing, bool /*incremental*/) override
    {
        uint32 repGain = player->GetReputation(factionID);
        TeamId teamId = player->GetTeamId(true);

        if ((factionID == FACTION_FROSTWOLF_CLAN && teamId == TEAM_ALLIANCE) ||
            (factionID == FACTION_STORMPIKE_GUARD && teamId == TEAM_HORDE))
        {
            uint32 diff = standing - repGain;
            player->GetReputationMgr().ModifyReputation(sFactionStore.LookupEntry(teamId == TEAM_ALLIANCE ? FACTION_STORMPIKE_GUARD : FACTION_FROSTWOLF_CLAN), diff);
            return false;
        }

        return true;
    }

private:
    uint32 timeCheck = 10000;
};

class CFBG_Battlefield : public BattlefieldScript
{
public:
    CFBG_Battlefield() : BattlefieldScript("CFBG_Battlefield", {
        BATTLEFIELDHOOK_ON_PLAYER_JOIN_WAR,
        BATTLEFIELDHOOK_ON_PLAYER_LEAVE_ZONE
    }) {}

    void OnBattlefieldPlayerJoinWar(Battlefield* bf, Player* player) override
    {
        if (!sCFBG->IsEnableSystem() || !sCFBG->IsEnableWGSystem())
            return;

        if (bf->GetTypeId() != BATTLEFIELD_WG)
            return;

        if (sCFBG->IsPlayerFake(player))
            return;

        // This hook fires at the very start of OnBattlefieldPlayerJoinWar, BEFORE the
        // player is inserted into any m_players[] bucket.  That means:
        //   1. GetPlayersInZoneCount reflects the current balanced distribution.
        //   2. SetFakeRaceAndMorphForBF changes player->GetTeamId() before the
        //      bucket insert, so the player lands in the correct (assigned) bucket.
        //   3. All subsequent Battlefield operations (queue, war invite, group
        //      assignment, leave cleanup) see the assigned team via GetTeamId().
        uint32 allianceCount = bf->GetPlayersInWarCount(TEAM_ALLIANCE);
        uint32 hordeCount    = bf->GetPlayersInWarCount(TEAM_HORDE);

        TeamId realTeam     = player->GetTeamId(true);
        TeamId assignedTeam = realTeam;

        LOG_ERROR("sql.sql", "Player {} entered WG with real team {}, alliance count {}, horde count {}", player->GetName(), realTeam, allianceCount, hordeCount);

        // Assign player to the team with fewer zone members to balance teams.
        if (realTeam == TEAM_ALLIANCE && allianceCount > hordeCount)
            assignedTeam = TEAM_HORDE;
        else if (realTeam == TEAM_HORDE && hordeCount > allianceCount)
            assignedTeam = TEAM_ALLIANCE;

        if (assignedTeam == realTeam)
            return;

        // Apply visual + faction transformation so the player looks and acts as
        // the assigned faction in-game (PvP targeting, phase shifts, etc.).
        // This also calls player->setTeamId(assignedTeam) so all subsequent
        // player->GetTeamId() calls return the assigned team.
        sCFBG->SetFakeRaceAndMorphForBF(player, assignedTeam);
    }

    void OnBattlefieldPlayerLeaveZone(Battlefield* bf, Player* player) override
    {
        if (!sCFBG->IsEnableSystem() || !sCFBG->IsEnableWGSystem())
            return;

        if (bf->GetTypeId() != BATTLEFIELD_WG)
            return;

        // HandlePlayerLeaveZone has already cleaned up all Battlefield data
        // structures using the assigned team (player->GetTeamId() still returns
        // assignedTeam at that point).  Now that cleanup is complete it is safe
        // to restore the player's real race/faction.
        if (sCFBG->IsPlayerFake(player))
            sCFBG->ClearFakePlayer(player);
    }
};

class CFBG_World : public WorldScript
{
public:
    CFBG_World() : WorldScript("CFBG_World", {
        WORLDHOOK_ON_AFTER_CONFIG_LOAD
    }) { }

    void OnAfterConfigLoad(bool /*Reload*/) override
    {
        sCFBG->LoadConfig();
    }
};

void AddSC_CFBG()
{
    new CFBG_BG();
    new CFBG_Player();
    new CFBG_Battlefield();
    new CFBG_World();
}
