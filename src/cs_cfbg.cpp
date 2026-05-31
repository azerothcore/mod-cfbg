/*
 * Copyright (С) since 2019+ AzerothCore <www.azerothcore.org>
 * Licence MIT https://opensource.org/MIT
 */

#include "Battlefield.h"
#include "BattlefieldMgr.h"
#include "Chat.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "CFBG.h"

using namespace Acore::ChatCommands;

class cfbg_commandscript : public CommandScript
{
public:
    cfbg_commandscript() : CommandScript("cfbg_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable cfbgCommands =
        {
            { "race",  HandleCFBGChooseRace, SEC_PLAYER,        Console::No  },
            { "debug", HandleCFBGDebug,      SEC_MODERATOR,     Console::Yes },
        };

        static ChatCommandTable commandTable =
        {
            { "cfbg",  cfbgCommands },
        };

        return commandTable;
    }

    static bool HandleCFBGChooseRace(ChatHandler* handler, std::string raceInput)
    {
        Player* player = handler->GetPlayer();

        uint8 raceId = 0;

        if (sCFBG->RandomizeRaces())
        {
            handler->SendSysMessage("Race selection is currently disabled.");
            handler->SetSentErrorMessage(true);
            return true;
        }

        for (auto const& raceVariable : *sCFBG->GetRaceInfo())
        {
            if (raceInput == raceVariable.RaceName)
            {
                if (player->GetTeamId(true) == raceVariable.TeamId)
                {
                    raceId = raceVariable.RaceId;
                }
                else
                {
                    handler->SendSysMessage("Race not available to your faction.");
                    handler->SetSentErrorMessage(true);
                    return true;
                }
                
                if (!IsRaceValidForClass(player, raceId))
                {
                    handler->SendSysMessage("Race not available to your class.");
                    handler->SetSentErrorMessage(true);
                    return true;
                }

                if (raceId == RACE_NIGHTELF)
                {
                    handler->SendSysMessage("Night elf models are not available as the female model is missing and the male one causes client crashes.");
                    handler->SetSentErrorMessage(true);
                    return true;
                }

                if (player->getGender() == GENDER_FEMALE && (raceId == RACE_TROLL || raceId == RACE_DWARF))
                {
                    handler->SendSysMessage("Female models are not available for the following races: troll, dwarf.");
                    handler->SetSentErrorMessage(true);
                    return true;
                }
            }
        }

        player->UpdatePlayerSetting("mod-cfbg", SETTING_CFBG_RACE, raceId);

        if (!raceId)
        {
            handler->SendSysMessage("Race unavailable. CFBG selected race set to random. You will be morphed into a random race when you enter a battleground on the opposite team.");
        }
        else
        {
            handler->PSendSysMessage("CFBG selected race set to {}", raceInput);
        }

        return true;
    }

    static char const* TeamIdName(TeamId t)
    {
        switch (t)
        {
            case TEAM_ALLIANCE: return "Alliance";
            case TEAM_HORDE:    return "Horde";
            default:            return "Neutral";
        }
    }

    static bool HandleCFBGDebug(ChatHandler* handler, Optional<PlayerIdentifier> player)
    {
        if (!player)
            player = PlayerIdentifier::FromTargetOrSelf(handler);

        if (!player || !player->IsConnected())
        {
            handler->SendErrorMessage(LANG_PLAYER_NOT_FOUND);
            return false;
        }

        Player* target = player->GetConnectedPlayer();

        bool const isFake     = sCFBG->IsPlayerFake(target);
        bool const native     = sCFBG->IsPlayingNative(target);
        bool const forgetBG   = sCFBG->ShouldForgetBGPlayers(target);
        bool const forgetList = sCFBG->ShouldForgetInListPlayers(target);

        uint8 const preferredRace = target->GetPlayerSetting("mod-cfbg", SETTING_CFBG_RACE).value;

        handler->PSendSysMessage("CFBG debug: {} [GUID: {}]",
            target->GetName(), target->GetGUID().ToString());
        handler->PSendSysMessage("  System enabled: {}  WG enabled: {}",
            sCFBG->IsEnableSystem() ? "yes" : "no",
            sCFBG->IsEnableWGSystem() ? "yes" : "no");
        handler->PSendSysMessage("  Faked: {}  PlayingNative: {}",
            isFake ? "yes" : "no", native ? "yes" : "no");
        handler->PSendSysMessage("  ForgetBGPlayers: {}  ForgetInListPlayers: {}",
            forgetBG ? "yes" : "no", forgetList ? "yes" : "no");
        handler->PSendSysMessage("  Class: {}  Gender: {}",
            uint32(target->getClass()), uint32(target->getGender()));
        handler->PSendSysMessage("  Native : race={}  team={}",
            uint32(target->getRace(true)), TeamIdName(target->GetTeamId(true)));
        handler->PSendSysMessage("  Current: race={}  team={}  display={}/{} (current/native)",
            uint32(target->getRace()), TeamIdName(target->GetTeamId()),
            target->GetDisplayId(), target->GetNativeDisplayId());
        handler->PSendSysMessage("  BgTeamId: {}  InBattleground: {}",
            TeamIdName(target->GetBgTeamId()),
            target->InBattleground() ? "yes" : "no");
        handler->PSendSysMessage("  PreferredRace setting: {}", uint32(preferredRace));

        FakePlayer const* fake = sCFBG->GetFakePlayer(target);
        if (fake)
        {
            handler->SendSysMessage("  Fake record:");
            handler->PSendSysMessage("    Fake race={}  morph={}  team={}",
                uint32(fake->FakeRace), fake->FakeMorph, TeamIdName(fake->FakeTeamID));
            handler->PSendSysMessage("    Real race={}  morph={}  nativeMorph={}  team={}",
                uint32(fake->RealRace), fake->RealMorph, fake->RealNativeMorph,
                TeamIdName(fake->RealTeamID));
        }

        // === Wintergrasp diagnostics ===
        ObjectGuid const guid = target->GetGUID();
        uint32 const zoneId   = target->GetZoneId();
        Battlefield* bf = sBattlefieldMgr->GetBattlefieldToZoneId(zoneId);
        bool const inWGZone = bf && bf->GetTypeId() == BATTLEFIELD_WG;

        // Locate any WG battlefield (whether the player is in its zone or not),
        // so we can detect stale fakes that linger outside the zone.
        Battlefield* wg = inWGZone ? bf : nullptr;
        if (!wg)
        {
            // BattleId 1 is WG by registration order; safer to look up by zone.
            // Use the well-known WG zone id (Wintergrasp = 4197).
            if (Battlefield* candidate = sBattlefieldMgr->GetBattlefieldToZoneId(4197))
                if (candidate->GetTypeId() == BATTLEFIELD_WG)
                    wg = candidate;
        }

        handler->SendSysMessage("  --- Wintergrasp ---");
        handler->PSendSysMessage("    InWGZone: {}  (zone={})",
            inWGZone ? "yes" : "no", zoneId);

        if (!wg)
        {
            handler->SendSysMessage("    No WG battlefield instance found.");
        }
        else
        {
            handler->PSendSysMessage("    WarTime: {}", wg->IsWarTime() ? "yes" : "no");

            bool const inQueueA   = wg->GetPlayersQueueSet(TEAM_ALLIANCE).count(guid) > 0;
            bool const inQueueH   = wg->GetPlayersQueueSet(TEAM_HORDE).count(guid) > 0;
            bool const invitedA   = wg->GetInvitedPlayersMap(TEAM_ALLIANCE).count(guid) > 0;
            bool const invitedH   = wg->GetInvitedPlayersMap(TEAM_HORDE).count(guid) > 0;
            bool const inWarA     = wg->GetPlayersInWarSet(TEAM_ALLIANCE).count(guid) > 0;
            bool const inWarH     = wg->GetPlayersInWarSet(TEAM_HORDE).count(guid) > 0;

            handler->PSendSysMessage("    Player in sets:  Queue A/H={}/{}  Invited A/H={}/{}  InWar A/H={}/{}",
                inQueueA ? "Y" : "-", inQueueH ? "Y" : "-",
                invitedA ? "Y" : "-", invitedH ? "Y" : "-",
                inWarA   ? "Y" : "-", inWarH   ? "Y" : "-");

            // Which core-tracked team does the player belong to right now, if any?
            std::optional<TeamId> coreTeam;
            if (inWarA || invitedA || inQueueA) coreTeam = TEAM_ALLIANCE;
            if (inWarH || invitedH || inQueueH) coreTeam = TEAM_HORDE;
            bool const inBothSides = (inWarA || invitedA || inQueueA) && (inWarH || invitedH || inQueueH);

            // ---- Oddity checks ----
            std::vector<std::string> issues;

            // 1. Player appears on both teams simultaneously in any WG set.
            if (inBothSides)
                issues.emplace_back("Player tracked in BOTH Alliance and Horde WG sets (set leak)");

            // 2. Fake record exists but FakeTeamID == RealTeamID — stale/no-op fake.
            if (fake && fake->FakeTeamID == fake->RealTeamID)
                issues.emplace_back(Acore::StringFormat(
                    "Fake record present but FakeTeamID == RealTeamID ({}) — stale fake",
                    TeamIdName(fake->FakeTeamID)));

            // 3. GetTeamId() disagrees with the side the core has the player on.
            if (coreTeam && !inBothSides && target->GetTeamId() != *coreTeam)
                issues.emplace_back(Acore::StringFormat(
                    "Current team ({}) does not match core WG side ({})",
                    TeamIdName(target->GetTeamId()), TeamIdName(*coreTeam)));

            // 4. Fake record's FakeTeamID disagrees with the core WG side.
            if (fake && coreTeam && !inBothSides && fake->FakeTeamID != *coreTeam)
                issues.emplace_back(Acore::StringFormat(
                    "Fake team ({}) does not match core WG side ({})",
                    TeamIdName(fake->FakeTeamID), TeamIdName(*coreTeam)));

            // 5. Player is faked and in war, but is sitting in the war set of
            //    their REAL team instead of the assigned/fake team.
            if (isFake && fake)
            {
                bool const inRealWarSet = (fake->RealTeamID == TEAM_ALLIANCE) ? inWarA : inWarH;
                bool const inFakeWarSet = (fake->FakeTeamID == TEAM_ALLIANCE) ? inWarA : inWarH;
                if (inRealWarSet && !inFakeWarSet && fake->FakeTeamID != fake->RealTeamID)
                    issues.emplace_back(Acore::StringFormat(
                        "Faked {} but sitting in core {} war set (battlegroup desync)",
                        TeamIdName(fake->FakeTeamID), TeamIdName(fake->RealTeamID)));
            }

            // 6. War is active and player is in zone, faked, but not in either war set.
            if (inWGZone && wg->IsWarTime() && isFake && !inWarA && !inWarH && !invitedA && !invitedH)
                issues.emplace_back("Faked in WG zone during war, but absent from war/invited sets");

            // 7. Player is faked but currently outside WG zone and not in a BG —
            //    cleanup leak (the OnPlayerUpdateZone path should have fired).
            if (isFake && !inWGZone && !target->InBattleground())
                issues.emplace_back("Faked while outside WG zone and not in a BG (cleanup leak)");

            // 8. WG hook is disabled in config but a WG-style fake is present in
            //    the WG zone — config flipped mid-life.
            if (inWGZone && isFake && !sCFBG->IsEnableWGSystem())
                issues.emplace_back("Faked in WG zone but CFBG.Battlefield.Enable=0");

            if (issues.empty())
            {
                handler->SendSysMessage("    Oddities: none");
            }
            else
            {
                handler->SendSysMessage("    Oddities:");
                for (std::string const& msg : issues)
                    handler->PSendSysMessage("      ! {}", msg);
            }
        }

        return true;
    }

    static bool IsRaceValidForClass(Player* player, uint8 fakeRace)
    {
        auto raceData{ *sCFBG->GetRaceData() };

        std::vector<uint8> availableRacesForClass = player->GetTeamId(true) == TEAM_HORDE ?
            raceData[player->getClass()].availableRacesA : raceData[player->getClass()].availableRacesH;

        for (auto const& races : availableRacesForClass)
        {
            if (races == fakeRace)
            {
                return true;
            }
        }

        return false;
    }
};

void AddSC_cfbg_commandscript()
{
    new cfbg_commandscript();
}

class cfbg_bf_commandscript : public CommandScript
{
public:
    cfbg_bf_commandscript() : CommandScript("cfbg_bf_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable bfSubCommands =
        {
            { "list", HandleBFList, SEC_ADMINISTRATOR, Console::No },
        };

        static ChatCommandTable commandTable =
        {
            { "bf", bfSubCommands },
        };

        return commandTable;
    }

    static bool HandleBFList(ChatHandler* handler, uint32 battleId)
    {
        Battlefield* bf = sBattlefieldMgr->GetBattlefieldByBattleId(battleId);

        if (!bf)
        {
            handler->SendErrorMessage("Battlefield {} not found.", battleId);
            return false;
        }

        uint32 const allianceZone = bf->GetPlayersInZoneCount(TEAM_ALLIANCE);
        uint32 const hordeZone    = bf->GetPlayersInZoneCount(TEAM_HORDE);
        uint32 const allianceWar  = bf->GetPlayersInWarCount(TEAM_ALLIANCE);
        uint32 const hordeWar     = bf->GetPlayersInWarCount(TEAM_HORDE);
        uint32 const maxPerTeam   = bf->GetMaxPlayersPerTeam();

        handler->SendSysMessage(Acore::StringFormat("Battlefield {} | {}", battleId,
            bf->IsWarTime() ? "WAR" : "PEACE").c_str());
        handler->SendSysMessage(Acore::StringFormat("  Alliance: {} in zone, {} in war / {} max",
            allianceZone, allianceWar, maxPerTeam).c_str());
        handler->SendSysMessage(Acore::StringFormat("  Horde:    {} in zone, {} in war / {} max",
            hordeZone, hordeWar, maxPerTeam).c_str());

        return true;
    }
};

void AddSC_cfbg_bf_commandscript()
{
    new cfbg_bf_commandscript();
}
