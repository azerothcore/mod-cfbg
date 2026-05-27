/*
 *  Copyright (С) since 2019 Andrei Guluaev (Winfidonarleyan/Kargatum) https://github.com/Winfidonarleyan
 *  Copyright (С) since 2019+ AzerothCore <www.azerothcore.org>
 */

#include "CFBG.h"
#include "Battlefield.h"
#include "BattlefieldMgr.h"
#include "Group.h"
#include "ObjectAccessor.h"
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
        PLAYERHOOK_ON_LOGOUT,
        PLAYERHOOK_ON_UPDATE_ZONE,
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

    // Fires after Player::UpdateZone has finished all Battlefield/OutdoorPvP/WorldState
    // leave+enter calls. This is where we restore a WG fake player's real faction:
    // doing it earlier (in OnBattlefieldPlayerLeaveZone) flips m_team before core's
    // Battlefield::HandlePlayerLeaveZone runs, which erases PlayersInWar using the
    // (now wrong) team and orphans the assigned-team entry.
    void OnPlayerUpdateZone(Player* player, uint32 newZone, uint32 /*newArea*/) override
    {
        if (!sCFBG->IsEnableSystem() || !sCFBG->IsEnableWGSystem())
            return;

        if (!sCFBG->IsPlayerFake(player))
            return;

        // Battleground fakes are owned by the BG hooks (OnBattlegroundRemovePlayerAtLeave
        // / OnBattlegroundEndReward), not this WG cleanup. A battleground zone is not a
        // WG battlefield, so without this guard entering a BG would clear a cross-faction
        // player's fake right after the entry morph (Battleground::AddPlayer runs before
        // UpdateZone), leaving GetTeamId() on the real faction while bgTeamId stays on the
        // assigned side -- the flag-capture/win desync. WG players are not InBattleground().
        if (player->InBattleground())
            return;

        Battlefield* bf = sBattlefieldMgr->GetBattlefieldToZoneId(newZone);
        if (!bf || bf->GetTypeId() != BATTLEFIELD_WG)
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

// WG constants duplicated here to avoid pulling in BattlefieldWG.h
static constexpr uint32 WG_SPELL_LIEUTENANT            = 55629;
static constexpr uint32 WG_NPC_QUEST_PVP_KILL_ALLIANCE = 31086;
static constexpr uint32 WG_NPC_QUEST_PVP_KILL_HORDE    = 39019;

class CFBG_Battlefield : public BattlefieldScript
{
public:
    CFBG_Battlefield() : BattlefieldScript("CFBG_Battlefield", {
        BATTLEFIELDHOOK_ON_PLAYER_JOIN_WAR,
        BATTLEFIELDHOOK_ON_PLAYER_LEAVE_WAR,
        BATTLEFIELDHOOK_ON_PLAYER_LEAVE_ZONE,
        BATTLEFIELDHOOK_ON_WAR_END,
        BATTLEFIELDHOOK_ON_PLAYER_KILL
    }) {}

    void OnBattlefieldPlayerJoinWar(Battlefield* bf, Player* player) override
    {
        if (!sCFBG->IsEnableSystem() || !sCFBG->IsEnableWGSystem())
            return;

        if (bf->GetTypeId() != BATTLEFIELD_WG)
            return;

        if (sCFBG->IsPlayerFake(player))
            return;

        uint32 allianceCount = static_cast<uint32>(_wgWarPlayers[TEAM_ALLIANCE].size());
        uint32 hordeCount    = static_cast<uint32>(_wgWarPlayers[TEAM_HORDE].size());

        TeamId realTeam     = player->GetTeamId(true);
        TeamId assignedTeam = realTeam;

        if (realTeam == TEAM_ALLIANCE && allianceCount > hordeCount)
            assignedTeam = TEAM_HORDE;
        else if (realTeam == TEAM_HORDE && hordeCount > allianceCount)
            assignedTeam = TEAM_ALLIANCE;

        if (assignedTeam != realTeam)
            sCFBG->SetFakeRaceAndMorphForBF(player, assignedTeam);

        _wgWarPlayers[assignedTeam].insert(player->GetGUID());
    }

    void OnBattlefieldPlayerLeaveWar(Battlefield* bf, Player* player) override
    {
        if (!sCFBG->IsEnableSystem() || !sCFBG->IsEnableWGSystem())
            return;

        if (bf->GetTypeId() != BATTLEFIELD_WG)
            return;

        // player->GetTeamId() still returns the assigned team here; ClearFakePlayer
        // is not called until OnPlayerUpdateZone fires at the end of Player::UpdateZone.
        _wgWarPlayers[player->GetTeamId()].erase(player->GetGUID());
    }

    void OnBattlefieldPlayerLeaveZone(Battlefield* bf, Player* player) override
    {
        if (!sCFBG->IsEnableSystem() || !sCFBG->IsEnableWGSystem())
            return;

        if (bf->GetTypeId() != BATTLEFIELD_WG)
            return;

        // Safety catch-all: if the player leaves the zone while war is not
        // active (or if LeaveWar somehow did not fire), remove them from the
        // war tracking now.  A GUID erase on a set that does not contain the
        // key is a guaranteed no-op, so double-removal is safe.
        // m_team is still the assigned team here: this hook fires from
        // BattlefieldMgr::HandlePlayerLeaveZone BEFORE core's cleanup runs,
        // and OnPlayerUpdateZone (where ClearFakePlayer now lives) only
        // fires after UpdateZone completes.
        _wgWarPlayers[player->GetTeamId()].erase(player->GetGUID());
    }

    void OnBattlefieldPlayerKill(Battlefield* bf, Player* killer, Player* victim) override
    {
        if (!sCFBG->IsEnableSystem() || !sCFBG->IsEnableWGSystem())
            return;

        if (bf->GetTypeId() != BATTLEFIELD_WG)
            return;

        // Credit the killer directly on any WG-zone kill, war or not. Granting
        // both PvP-kill credit NPCs covers crossfaction players whose held quest
        // does not match their assigned team — KilledMonsterCredit on a quest
        // the player does not hold is a no-op.
        //
        // Assist credit for nearby allies stays on core's existing lieutenant
        // path (BattlefieldWG::HandleKill iterates PlayersInWar[killerTeam] when
        // victim has SPELL_LIEUTENANT). We deliberately do NOT replicate that
        // iteration for every kill: the per-kill cost would scale with war
        // population (100+ players).
        //
        // To avoid double-crediting the killer when core's lieutenant path will
        // fire, skip coreCredit if the victim is a lieutenant AND the killer is
        // actually in PlayersInWar[killerTeam] (the only set core iterates).
        TeamId killerTeam = killer->GetTeamId();
        uint32 coreCredit  = (killerTeam == TEAM_HORDE) ? WG_NPC_QUEST_PVP_KILL_ALLIANCE
                                                        : WG_NPC_QUEST_PVP_KILL_HORDE;
        uint32 otherCredit = (killerTeam == TEAM_HORDE) ? WG_NPC_QUEST_PVP_KILL_HORDE
                                                        : WG_NPC_QUEST_PVP_KILL_ALLIANCE;

        bool coreWillCreditKiller = victim->HasAura(WG_SPELL_LIEUTENANT)
                                 && bf->GetPlayersInWarSet(killerTeam).count(killer->GetGUID()) > 0;

        if (!coreWillCreditKiller)
            killer->KilledMonsterCredit(coreCredit);
        killer->KilledMonsterCredit(otherCredit);
    }

    void OnBattlefieldWarEnd(Battlefield* bf, bool /*endByTimer*/) override
    {
        if (!sCFBG->IsEnableSystem() || !sCFBG->IsEnableWGSystem())
            return;

        if (bf->GetTypeId() != BATTLEFIELD_WG)
            return;

        // When the war ends, the core clears PlayersInWar in bulk without
        // firing per-player LeaveWar hooks.  Players staying in the WG zone
        // never trigger LeaveZone either, so their fake faction would persist
        // indefinitely.  Iterate our own tracking and restore each player now.
        for (uint8 team = 0; team < PVP_TEAMS_COUNT; ++team)
        {
            for (ObjectGuid const& guid : _wgWarPlayers[team])
            {
                Player* player = ObjectAccessor::FindPlayer(guid);
                if (player && sCFBG->IsPlayerFake(player))
                    sCFBG->ClearFakePlayer(player);
            }
            _wgWarPlayers[team].clear();
        }
    }

private:
    // Module-owned WG war-player tracking, indexed by the CFBG-assigned TeamId.
    // Populated when a player accepts a war invitation (JoinWar) and drained
    // when they leave the war (LeaveWar) or zone (LeaveZone catch-all).
    // Kept separately from the core's m_PlayersInWar / m_InvitedPlayers so that
    // balance decisions during active war are always based on clean state.
    GuidUnorderedSet _wgWarPlayers[PVP_TEAMS_COUNT];
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
