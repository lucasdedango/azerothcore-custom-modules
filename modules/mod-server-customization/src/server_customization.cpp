#include "AiObjectContext.h"
#include "Chat.h"
#include "CellImpl.h"
#include "CommandScript.h"
#include "Creature.h"
#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "GameObjectData.h"
#include "GridNotifiers.h"
#include "Map.h"
#include "MotionMaster.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "PlayerbotRepository.h"
#include "Playerbots.h"
#include "PoolMgr.h"
#include "ScriptMgr.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "WorldPacket.h"
#include "ServerCustomizationGatherOnly.h"
#include "SharedDefines.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <list>
#include <limits>
#include <sstream>
#include <vector>

using namespace Acore::ChatCommands;

namespace ServerCustomization
{
    struct Config
    {
        bool StarterEnable = true;
        bool GiveRiding = true;
        bool GiveRacialMount = true;
        uint32 MoneyCopper = 100;
        bool ActionBarEnable = true;
        uint8 PreferredSlot = 11;
        bool AnnounceGrant = true;

        bool XpRateEnable = true;
        bool XpRateAnnounceOnLogin = true;
        float XpRateDefault = 1.0f;
        float XpRateMaximum = 3.0f;
        bool ProfessionGainEnable = true;
        uint32 GatheringSkillGainDefault = 1;
        uint32 CraftingSkillGainDefault = 3;

        uint32 Human = 458;
        uint32 Orc = 6654;
        uint32 Dwarf = 6899;
        uint32 NightElf = 10789;
        uint32 Undead = 17464;
        uint32 Tauren = 18990;
        uint32 Gnome = 17453;
        uint32 Troll = 10796;
        uint32 BloodElf = 35018;
        uint32 Draenei = 34406;

        bool GatherRouteEnable = true;
        bool GatherRouteSameZoneOnly = true;
        float GatherRouteSearchRadius = 250.0f;
        uint32 GatherRouteUpdateIntervalMs = 1500;
        uint32 GatherRoutePathCandidates = 12;
        float GatherRouteApproachDistance = 3.5f;
        uint32 GatherRouteTargetTimeoutMs = 120000;
        uint32 GatherRouteBlacklistMs = 45000;
        uint32 GatherRouteApproachRings = 3;
        uint32 GatherRouteApproachPointsPerRing = 12;
        float GatherRouteInteractionDistance = 3.25f;
        bool GatherRouteStuckEnable = true;
        float GatherRouteStuckNearDistance = 20.0f;
        float GatherRouteStuckMinProgress = 0.75f;
        uint32 GatherRouteStuckMaxChecks = 5;
        uint32 GatherRouteStuckBlacklistMs = 90000;
        float GatherRouteMountSuppressDistance = 12.0f;
        bool GatherRouteMountEnable = true;
        float GatherRouteMountMinDistance = 45.0f;
        uint32 GatherRouteMountWaitMs = 2500;
        float GatherRouteDismountDistance = 7.0f;
        bool GatherRouteIgnoreAggroUntilDamage = true;
        uint32 GatherRouteDamageCombatHoldMs = 3000;
        bool RouteAvoidanceEnable = true;
        float RouteAvoidanceBuffer = 8.0f;
        float RouteAvoidanceWeight = 60.0f;
        float RouteAvoidanceEliteMultiplier = 2.5f;

        bool AutomationRestEnable = true;
        float AutomationRestHealthStartPct = 55.0f;
        float AutomationRestHealthResumePct = 90.0f;
        float AutomationRestManaStartPct = 35.0f;
        float AutomationRestManaResumePct = 85.0f;

        bool AutomationDisableFallDamage = true;
        uint32 AutomationFallDamageGraceMs = 5000;
    };

    Config g_Config;

    inline constexpr char CharacterRatesDataKey[] = "ServerCustomization.CharacterRates";

    class CharacterRatesState : public DataMap::Base
    {
    public:
        float xpRate = 1.0f;
        uint32 gatheringSkillGain = 1;
        uint32 craftingSkillGain = 3;
    };

    static float ClampXpRate(float rate)
    {
        return std::clamp(rate, 1.0f, std::max(1.0f, g_Config.XpRateMaximum));
    }

    static CharacterRatesState* GetCharacterRates(Player* player)
    {
        return player ? player->CustomData.GetDefault<CharacterRatesState>(CharacterRatesDataKey) : nullptr;
    }

    static void SaveCharacterRates(Player* player)
    {
        if (!player)
            return;

        if (CharacterRatesState* state = player->CustomData.Get<CharacterRatesState>(CharacterRatesDataKey))
        {
            CharacterDatabase.DirectExecute(
                "REPLACE INTO `server_customization_character_rates` "
                "(`CharacterGUID`, `XPRate`, `GatheringSkillGain`, `CraftingSkillGain`) VALUES ({}, {}, {}, {})",
                player->GetGUID().GetCounter(), state->xpRate, state->gatheringSkillGain, state->craftingSkillGain);
        }
    }

    static void SendXpStatus(ChatHandler* handler, Player* player)
    {
        if (!handler || !player)
            return;

        if (player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_NO_XP_GAIN))
        {
            handler->SendSysMessage("|cffffff00[XP]|r Gain d'experience desactive. Utilisez .xp enable pour le reactiver.");
            return;
        }

        std::ostringstream message;
        message << "|cffffff00[XP]|r Multiplicateur actuel : x" << GetCharacterRates(player)->xpRate
                << " (maximum x" << g_Config.XpRateMaximum << ").";
        handler->SendSysMessage(message.str().c_str());
    }

    static void SendProfessionStatus(ChatHandler* handler, Player* player)
    {
        if (!handler || !player)
            return;

        CharacterRatesState* state = GetCharacterRates(player);
        handler->PSendSysMessage("|cffffff00[Metiers]|r Recolte minerai/plantes : {} point(s) ; craft : {} point(s).",
                                 state->gatheringSkillGain, state->craftingSkillGain);
    }

    inline constexpr char DamageDebugDataKey[] = "ServerCustomization.DamageDebug";

    class DamageDebugState : public DataMap::Base
    {
    public:
        bool enabled = false;
    };

    static DamageDebugState* GetDamageDebugState(Player* player, bool create)
    {
        if (!player)
            return nullptr;

        if (create)
            return player->CustomData.GetDefault<DamageDebugState>(DamageDebugDataKey);

        return player->CustomData.Get<DamageDebugState>(DamageDebugDataKey);
    }

    static bool IsDamageDebugEnabled(Player* player)
    {
        if (auto* state = GetDamageDebugState(player, false))
            return state->enabled;
        return false;
    }

    static std::string DamageSourceLabel(Unit* attacker)
    {
        if (!attacker)
            return "environment/no attacker";

        std::ostringstream out;
        out << attacker->GetName();

        if (attacker->GetEntry())
            out << " (entry " << attacker->GetEntry() << ")";

        out << ", lvl " << uint32(attacker->GetLevel());
        return out.str();
    }

    static bool ShouldSuppressAutomationFallDamage(Player* player, Unit* attacker)
    {
        if (!g_Config.AutomationDisableFallDamage || !player || attacker)
            return false;

        auto* state = player->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey);
        if (!state || state->mode == ServerCustomizationGatherOnly::Mode::None)
            return false;

        // AzerothCore does not expose the environmental damage type through UnitScript::OnDamage.
        // Restrict suppression to source-less damage occurring during/recently after a falling state.
        // This catches delayed movement-generated fall damage without globally disabling environment damage.
        return player->IsFalling() || state->recentFallMs > 0;
    }

    static void MarkGatherDamage(Player* player, uint32 damage)
    {
        if (!player || damage == 0)
            return;

        auto* state = player->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey);
        if (!state || !ServerCustomizationGatherOnly::IsGatherMode(state->mode))
            return;

        state->recentDamageMs = std::max(state->recentDamageMs, g_Config.GatherRouteDamageCombatHoldMs);
        state->ignoreMountedAggroActive = false;
    }

    static void SendDamageDebug(Player* player, std::string const& kind, Unit* attacker, uint32 damage,
                                SpellInfo const* spellInfo = nullptr)
    {
        if (!player || !player->GetSession() || !IsDamageDebugEnabled(player))
            return;

        uint32 hp = player->GetHealth();
        uint32 maxHp = player->GetMaxHealth();
        bool lethal = damage >= hp;

        std::ostringstream msg;
        msg << "|cffff6666[DAMAGE DEBUG]|r " << kind
            << " |cffffff00" << damage << "|r from " << DamageSourceLabel(attacker);

        if (spellInfo)
            msg << " |cff66ccffspell " << spellInfo->SpellName[0] << " (" << spellInfo->Id << ")|r";

        msg << " |cffaaaaaaHP " << hp << "/" << maxHp;
        if (lethal)
            msg << " - LETHAL";
        msg << "|r";

        ChatHandler(player->GetSession()).SendSysMessage(msg.str().c_str());
        LOG_INFO("module", "[ServerCustomization DamageDebug] player={} damage={} source={} spell={} hp={}/{} lethal={}",
                 player->GetName(), damage, DamageSourceLabel(attacker),
                 spellInfo ? uint32(spellInfo->Id) : 0, hp, maxHp, lethal ? 1 : 0);
    }

    static uint32 GetRacialMountSpell(uint8 race)
    {
        switch (race)
        {
            case RACE_HUMAN:         return g_Config.Human;
            case RACE_ORC:           return g_Config.Orc;
            case RACE_DWARF:         return g_Config.Dwarf;
            case RACE_NIGHTELF:      return g_Config.NightElf;
            case RACE_UNDEAD_PLAYER: return g_Config.Undead;
            case RACE_TAUREN:        return g_Config.Tauren;
            case RACE_GNOME:         return g_Config.Gnome;
            case RACE_TROLL:         return g_Config.Troll;
            case RACE_BLOODELF:      return g_Config.BloodElf;
            case RACE_DRAENEI:       return g_Config.Draenei;
            default:                 return 0;
        }
    }

    static void PutSpellOnPrimaryBar(Player* player, uint32 spellId);

    static bool HasKnownMountSpell(Player* player)
    {
        if (!player)
            return false;

        for (auto const& entry : player->GetSpellMap())
        {
            if (entry.second->State == PLAYERSPELL_REMOVED || !entry.second->Active)
                continue;

            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(entry.first);
            if (!spellInfo || spellInfo->IsPassive())
                continue;

            for (uint8 effectIndex = 0; effectIndex < MAX_SPELL_EFFECTS; ++effectIndex)
            {
                if (spellInfo->Effects[effectIndex].ApplyAuraName == SPELL_AURA_MOUNTED)
                    return true;
            }
        }

        return false;
    }

    static bool EnsureStarterRidingAndMount(Player* player, bool grantMoneyAndAnnounce)
    {
        if (!player || !g_Config.StarterEnable)
            return false;

        bool changed = false;
        bool hadRiding = player->HasSpell(33388);

        if (g_Config.GiveRiding && !hadRiding)
        {
            player->learnSpell(33388);
            player->SetSkill(SKILL_RIDING, 1, 75, 75);
            changed = true;
        }

        uint32 mountSpell = GetRacialMountSpell(player->getRace());
        if (g_Config.GiveRacialMount && mountSpell && !HasKnownMountSpell(player))
        {
            player->learnSpell(mountSpell);
            PutSpellOnPrimaryBar(player, mountSpell);
            changed = true;
        }

        // Preserve the old "starter package once" behaviour for money/messages:
        // only first-time Apprentice Riding grant gets the cash package.
        if (grantMoneyAndAnnounce && !hadRiding)
        {
            if (g_Config.MoneyCopper > 0)
                player->ModifyMoney(int32(g_Config.MoneyCopper));

            if (g_Config.AnnounceGrant && player->GetSession())
            {
                ChatHandler(player->GetSession()).SendSysMessage(
                    "|cff00ff00Server Customization:|r starter package granted.");
            }
        }
        else if (grantMoneyAndAnnounce && changed && player->GetSession())
        {
            ChatHandler(player->GetSession()).SendSysMessage(
                "|cff00ff00Server Customization:|r missing starter mount/riding repaired.");
        }

        return changed;
    }

    static void CancelMount(Player* player)
    {
        if (!player || !player->IsMounted())
            return;

        player->StopMoving();

        if (player->GetSession())
        {
            WorldPacket emptyPacket;
            player->GetSession()->HandleCancelMountAuraOpcode(emptyPacket);
        }
        else
        {
            player->Dismount();
        }
    }

    static bool HasLiveAttackers(PlayerbotAI* botAI)
    {
        if (!botAI || !botAI->GetAiObjectContext())
            return false;

        auto const& attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        for (ObjectGuid const& guid : attackers)
        {
            Unit* attacker = botAI->GetUnit(guid);
            if (attacker && attacker->IsAlive())
                return true;
        }

        return false;
    }

    static void PutSpellOnPrimaryBar(Player* player, uint32 spellId)
    {
        if (!player || !spellId || !g_Config.ActionBarEnable)
            return;

        uint8 preferred = g_Config.PreferredSlot > 11 ? 11 : g_Config.PreferredSlot;

        for (int32 slot = preferred; slot >= 0; --slot)
        {
            if (!player->GetActionButton(uint8(slot)))
            {
                player->addActionButton(uint8(slot), spellId, ACTION_BUTTON_SPELL);
                return;
            }
        }

        for (uint8 slot = preferred + 1; slot < 12; ++slot)
        {
            if (!player->GetActionButton(slot))
            {
                player->addActionButton(slot, spellId, ACTION_BUTTON_SPELL);
                return;
            }
        }
    }

    class ConfigScript : public WorldScript
    {
    public:
        ConfigScript() : WorldScript("ServerCustomizationConfig", { WORLDHOOK_ON_BEFORE_CONFIG_LOAD }) {}

        void OnBeforeConfigLoad(bool) override
        {
            g_Config.StarterEnable = sConfigMgr->GetOption<bool>("ServerCustomization.Starter.Enable", true);
            g_Config.GiveRiding = sConfigMgr->GetOption<bool>("ServerCustomization.Starter.GiveRiding", true);
            g_Config.GiveRacialMount = sConfigMgr->GetOption<bool>("ServerCustomization.Starter.GiveRacialMount", true);
            g_Config.MoneyCopper = sConfigMgr->GetOption<uint32>("ServerCustomization.Starter.MoneyCopper", 100);
            g_Config.ActionBarEnable = sConfigMgr->GetOption<bool>("ServerCustomization.Starter.MountActionBar.Enable", true);
            g_Config.PreferredSlot = uint8(sConfigMgr->GetOption<uint32>("ServerCustomization.Starter.MountActionBar.PreferredSlot", 11));
            g_Config.AnnounceGrant = sConfigMgr->GetOption<bool>("ServerCustomization.Starter.AnnounceGrant", true);

            g_Config.XpRateEnable = sConfigMgr->GetOption<bool>("ServerCustomization.XP.Enable", true);
            g_Config.XpRateAnnounceOnLogin = sConfigMgr->GetOption<bool>("ServerCustomization.XP.AnnounceOnLogin", true);
            g_Config.XpRateMaximum = std::max(1.0f, sConfigMgr->GetOption<float>("ServerCustomization.XP.MaxRate", 3.0f));
            g_Config.XpRateDefault = ClampXpRate(sConfigMgr->GetOption<float>("ServerCustomization.XP.DefaultRate", 1.0f));
            g_Config.ProfessionGainEnable = sConfigMgr->GetOption<bool>("ServerCustomization.Profession.Enable", true);
            g_Config.GatheringSkillGainDefault = std::clamp(sConfigMgr->GetOption<uint32>("ServerCustomization.Profession.DefaultGatheringSkillGain", 1), 1u, 3u);
            g_Config.CraftingSkillGainDefault = std::clamp(sConfigMgr->GetOption<uint32>("ServerCustomization.Profession.DefaultCraftingSkillGain", 3), 1u, 3u);

            g_Config.Human = sConfigMgr->GetOption<uint32>("ServerCustomization.Starter.Mount.Human", 458);
            g_Config.Orc = sConfigMgr->GetOption<uint32>("ServerCustomization.Starter.Mount.Orc", 6654);
            g_Config.Dwarf = sConfigMgr->GetOption<uint32>("ServerCustomization.Starter.Mount.Dwarf", 6899);
            g_Config.NightElf = sConfigMgr->GetOption<uint32>("ServerCustomization.Starter.Mount.NightElf", 10789);
            g_Config.Undead = sConfigMgr->GetOption<uint32>("ServerCustomization.Starter.Mount.Undead", 17464);
            g_Config.Tauren = sConfigMgr->GetOption<uint32>("ServerCustomization.Starter.Mount.Tauren", 18990);
            g_Config.Gnome = sConfigMgr->GetOption<uint32>("ServerCustomization.Starter.Mount.Gnome", 17453);
            g_Config.Troll = sConfigMgr->GetOption<uint32>("ServerCustomization.Starter.Mount.Troll", 10796);
            g_Config.BloodElf = sConfigMgr->GetOption<uint32>("ServerCustomization.Starter.Mount.BloodElf", 35018);
            g_Config.Draenei = sConfigMgr->GetOption<uint32>("ServerCustomization.Starter.Mount.Draenei", 34406);

            g_Config.GatherRouteEnable = sConfigMgr->GetOption<bool>("ServerCustomization.GatherRoute.Enable", true);
            g_Config.GatherRouteSameZoneOnly = sConfigMgr->GetOption<bool>("ServerCustomization.GatherRoute.SameZoneOnly", true);
            g_Config.GatherRouteSearchRadius = sConfigMgr->GetOption<float>("ServerCustomization.GatherRoute.SearchRadius", 250.0f);
            g_Config.GatherRouteUpdateIntervalMs = sConfigMgr->GetOption<uint32>("ServerCustomization.GatherRoute.UpdateIntervalMs", 1500);
            g_Config.GatherRoutePathCandidates = sConfigMgr->GetOption<uint32>("ServerCustomization.GatherRoute.PathCandidates", 12);
            g_Config.GatherRouteApproachDistance = sConfigMgr->GetOption<float>("ServerCustomization.GatherRoute.ApproachDistance", 3.5f);
            g_Config.GatherRouteTargetTimeoutMs = sConfigMgr->GetOption<uint32>("ServerCustomization.GatherRoute.TargetTimeoutMs", 120000);
            g_Config.GatherRouteBlacklistMs = sConfigMgr->GetOption<uint32>("ServerCustomization.GatherRoute.BlacklistMs", 45000);
            g_Config.GatherRouteApproachRings = sConfigMgr->GetOption<uint32>("ServerCustomization.GatherRoute.ApproachRings", 3);
            g_Config.GatherRouteApproachPointsPerRing = sConfigMgr->GetOption<uint32>("ServerCustomization.GatherRoute.ApproachPointsPerRing", 12);
            g_Config.GatherRouteInteractionDistance = sConfigMgr->GetOption<float>("ServerCustomization.GatherRoute.InteractionDistance", 3.25f);
            g_Config.GatherRouteStuckEnable = sConfigMgr->GetOption<bool>("ServerCustomization.GatherRoute.Stuck.Enable", true);
            g_Config.GatherRouteStuckNearDistance = sConfigMgr->GetOption<float>("ServerCustomization.GatherRoute.Stuck.NearDistance", 20.0f);
            g_Config.GatherRouteStuckMinProgress = sConfigMgr->GetOption<float>("ServerCustomization.GatherRoute.Stuck.MinProgress", 0.75f);
            g_Config.GatherRouteStuckMaxChecks = sConfigMgr->GetOption<uint32>("ServerCustomization.GatherRoute.Stuck.MaxChecks", 5);
            g_Config.GatherRouteStuckBlacklistMs = sConfigMgr->GetOption<uint32>("ServerCustomization.GatherRoute.Stuck.BlacklistMs", 90000);
            g_Config.GatherRouteMountSuppressDistance = sConfigMgr->GetOption<float>("ServerCustomization.GatherRoute.Mount.SuppressNearTargetDistance", 12.0f);
            g_Config.GatherRouteMountEnable = sConfigMgr->GetOption<bool>("ServerCustomization.GatherRoute.Mount.Enable", true);
            g_Config.GatherRouteMountMinDistance = sConfigMgr->GetOption<float>("ServerCustomization.GatherRoute.Mount.MinDistance", 45.0f);
            g_Config.GatherRouteMountWaitMs = sConfigMgr->GetOption<uint32>("ServerCustomization.GatherRoute.Mount.WaitMs", 2500);
            g_Config.GatherRouteDismountDistance = sConfigMgr->GetOption<float>("ServerCustomization.GatherRoute.Mount.DismountDistance", 7.0f);
            g_Config.GatherRouteIgnoreAggroUntilDamage = sConfigMgr->GetOption<bool>("ServerCustomization.GatherRoute.Mount.IgnoreAggroUntilDamage", true);
            g_Config.GatherRouteDamageCombatHoldMs = sConfigMgr->GetOption<uint32>("ServerCustomization.GatherRoute.Mount.DamageCombatHoldMs", 3000);
            g_Config.RouteAvoidanceEnable = sConfigMgr->GetOption<bool>("ServerCustomization.RouteAvoidance.Enable", true);
            g_Config.RouteAvoidanceBuffer = sConfigMgr->GetOption<float>("ServerCustomization.RouteAvoidance.Buffer", 8.0f);
            g_Config.RouteAvoidanceWeight = sConfigMgr->GetOption<float>("ServerCustomization.RouteAvoidance.Weight", 60.0f);
            g_Config.RouteAvoidanceEliteMultiplier = sConfigMgr->GetOption<float>("ServerCustomization.RouteAvoidance.EliteMultiplier", 2.5f);

            g_Config.AutomationRestEnable = sConfigMgr->GetOption<bool>("ServerCustomization.Automation.Rest.Enable", true);
            g_Config.AutomationRestHealthStartPct = sConfigMgr->GetOption<float>("ServerCustomization.Automation.Rest.HealthStartPct", 55.0f);
            g_Config.AutomationRestHealthResumePct = sConfigMgr->GetOption<float>("ServerCustomization.Automation.Rest.HealthResumePct", 90.0f);
            g_Config.AutomationRestManaStartPct = sConfigMgr->GetOption<float>("ServerCustomization.Automation.Rest.ManaStartPct", 35.0f);
            g_Config.AutomationRestManaResumePct = sConfigMgr->GetOption<float>("ServerCustomization.Automation.Rest.ManaResumePct", 85.0f);

            g_Config.AutomationDisableFallDamage = sConfigMgr->GetOption<bool>("ServerCustomization.Automation.DisableFallDamage", true);
            g_Config.AutomationFallDamageGraceMs = sConfigMgr->GetOption<uint32>("ServerCustomization.Automation.FallDamageGraceMs", 5000);
        }
    };

    static PlayerbotAI* EnsureRestrictedSelfbot(ChatHandler* handler)
    {
        if (!handler)
            return nullptr;

        Player* player = handler->GetPlayer();
        if (!player)
            return nullptr;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);

        if (!botAI)
        {
            PlayerbotsMgr::instance().AddPlayerbotData(player, true);

            botAI = GET_PLAYERBOT_AI(player);
            if (!botAI)
            {
                handler->SendSysMessage("|cffff4444Automation:|r unable to enable selfbot AI.");
                return nullptr;
            }

            botAI->SetMaster(player);
            PlayerbotRepository::instance().Load(botAI);
        }
        else
        {
            botAI->SetMaster(player);
        }

        return botAI;
    }

    static bool SendSelfbotCommand(ChatHandler* handler, std::string const& command)
    {
        PlayerbotAI* botAI = EnsureRestrictedSelfbot(handler);
        if (!botAI)
            return false;

        Player* player = handler->GetPlayer();
        botAI->HandleCommand(CHAT_MSG_WHISPER, command, player);
        return true;
    }

    static void SendAutomationMode(ChatHandler* handler, char const* mode)
    {
        handler->PSendSysMessage("|cff00ff00Automation:|r mode |cffffff00{}|r enabled.", mode);
    }

    static void DisableRestrictedSelfbot(ChatHandler* handler)
    {
        if (!handler)
            return;

        Player* player = handler->GetPlayer();
        if (!player)
            return;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
        if (!botAI)
            return;

        PlayerbotRepository::instance().Save(botAI);
        player->StopMoving();
        player->GetMotionMaster()->Clear();
        PlayerbotsMgr::instance().RemovePlayerBotData(player->GetGUID(), true);
    }

    static ServerCustomizationGatherOnly::State* GetAutomationState(Player* player, bool create)
    {
        if (!player)
            return nullptr;

        if (create)
            return player->CustomData.GetDefault<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey);

        return player->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey);
    }

    static void SetAutomationMode(Player* player, ServerCustomizationGatherOnly::Mode mode)
    {
        if (!player)
            return;

        if (mode == ServerCustomizationGatherOnly::Mode::None)
        {
            player->CustomData.Erase(ServerCustomizationGatherOnly::DataKey);
            return;
        }

        auto* state = GetAutomationState(player, true);
        state->mode = mode;
        state->targetSpawnId = 0;
        state->targetX = 0.0f;
        state->targetY = 0.0f;
        state->targetZ = 0.0f;
        state->updateTimerMs = 0;
        state->targetAgeMs = 0;
        state->mountWaitMs = 0;
        state->recentDamageMs = 0;
        state->grindCreatureEntry = 0;
        state->restHold = false;
        state->recentFallMs = 0;
        state->suppressMobLoot = false;
        state->mountSuppressedNearTarget = false;
        state->lastApproachDistance = 0.0f;
        state->stuckChecks = 0;
        state->routeAvoidanceEnable = g_Config.RouteAvoidanceEnable;
        state->routeAvoidanceBuffer = g_Config.RouteAvoidanceBuffer;
        state->routeAvoidanceWeight = g_Config.RouteAvoidanceWeight;
        state->routeAvoidanceEliteMultiplier = g_Config.RouteAvoidanceEliteMultiplier;
        state->mountGraceUsed = false;
        state->ignoreMountedAggroActive = false;
        state->combatPaused = false;
        state->blacklistMs.clear();
    }

    static ServerCustomizationGatherOnly::Mode GetAutomationMode(Player* player)
    {
        if (auto* state = GetAutomationState(player, false))
            return state->mode;
        return ServerCustomizationGatherOnly::Mode::None;
    }

    static bool ModeAcceptsSkill(ServerCustomizationGatherOnly::Mode mode, uint32 skillId)
    {
        if (mode == ServerCustomizationGatherOnly::Mode::Mining)
            return skillId == SKILL_MINING;
        if (mode == ServerCustomizationGatherOnly::Mode::Herbalism)
            return skillId == SKILL_HERBALISM;
        if (mode == ServerCustomizationGatherOnly::Mode::Gathering)
            return skillId == SKILL_MINING || skillId == SKILL_HERBALISM;
        return false;
    }

    static bool GetGatherSkillForSpawn(Player* player, GameObjectData const& data, ServerCustomizationGatherOnly::Mode mode,
                                       uint32& skillId, uint32& requiredSkill)
    {
        skillId = SKILL_NONE;
        requiredSkill = 0;

        GameObjectTemplate const* goInfo = sObjectMgr->GetGameObjectTemplate(data.id);
        if (!goInfo || goInfo->type != GAMEOBJECT_TYPE_CHEST)
            return false;

        uint32 lockId = goInfo->GetLockId();
        if (!lockId)
            return false;

        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);
        if (!lockInfo)
            return false;

        for (uint8 i = 0; i < 8; ++i)
        {
            if (lockInfo->Type[i] != LOCK_KEY_SKILL)
                continue;

            uint32 candidateSkill = SkillByLockType(LockType(lockInfo->Index[i]));
            if (!ModeAcceptsSkill(mode, candidateSkill))
                continue;

            uint32 req = std::max(2u, lockInfo->Skill[i]);
            if (player->GetSkillValue(candidateSkill) < req)
                continue;

            skillId = candidateSkill;
            requiredSkill = req;
            return true;
        }

        return false;
    }

    static bool IsSpawnAvailable(Player* player, ObjectGuid::LowType spawnId, GameObjectData const& data)
    {
        if (!player || !player->IsInWorld())
            return false;

        Map* map = player->GetMap();
        if (!map || data.mapid != player->GetMapId())
            return false;

        if (!(data.spawnMask & (1u << map->GetSpawnMode())))
            return false;

        if (!map->IsSpawnGroupActive(data.spawnGroupId))
            return false;

        if ((data.phaseMask & player->GetPhaseMask()) == 0)
            return false;

        if (uint32 poolId = sPoolMgr->IsPartOfAPool<GameObject>(spawnId))
        {
            (void)poolId;
            if (!map->GetPoolData().IsSpawnedObject<GameObject>(spawnId))
                return false;
        }

        time_t respawn = map->GetGORespawnTime(spawnId);
        if (respawn > std::time(nullptr))
            return false;

        // If the grid is loaded, trust the live object too. If it is not loaded yet,
        // the spawn/pool/respawn data above is still enough to route toward its DB position.
        auto bounds = map->GetGameObjectBySpawnIdStore().equal_range(spawnId);
        if (bounds.first != bounds.second)
        {
            bool anySpawned = false;
            for (auto itr = bounds.first; itr != bounds.second; ++itr)
            {
                GameObject* go = itr->second;
                if (go && go->isSpawned())
                {
                    anySpawned = true;
                    break;
                }
            }
            if (!anySpawned)
                return false;
        }

        return true;
    }

    struct RouteHazard
    {
        Creature* creature = nullptr;
        float dangerRadius = 0.0f;
        float severity = 1.0f;
    };

    static std::vector<RouteHazard> CollectRouteHazards(Player* player, float scanRadius, uint32 ignoreEntry = 0)
    {
        std::vector<RouteHazard> hazards;
        if (!player || !g_Config.RouteAvoidanceEnable || scanRadius <= 0.0f)
            return hazards;

        std::list<Unit*> units;
        Acore::AnyUnfriendlyUnitInObjectRangeCheck check(player, player, scanRadius);
        Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(player, units, check);
        Cell::VisitObjects(player, searcher, scanRadius);

        hazards.reserve(units.size());

        for (Unit* unit : units)
        {
            Creature* creature = unit ? unit->ToCreature() : nullptr;
            if (!creature || !creature->IsAlive() || creature->IsDuringRemoveFromWorld())
                continue;

            if (ignoreEntry && creature->GetEntry() == ignoreEntry)
                continue;

            if (!creature->CanCreatureAttack(player, true))
                continue;

            float aggro = std::max(0.0f, creature->GetAggroRange(player));
            if (aggro <= 0.0f)
                continue;

            float severity = 1.0f;

            int32 levelDelta = int32(creature->GetLevel()) - int32(player->GetLevel());
            if (levelDelta > 0)
                severity += std::min(2.0f, float(levelDelta) * 0.20f);

            if (creature->isElite() || creature->isWorldBoss())
                severity *= std::max(1.0f, g_Config.RouteAvoidanceEliteMultiplier);

            hazards.push_back({ creature, aggro + std::max(0.0f, g_Config.RouteAvoidanceBuffer), severity });
        }

        return hazards;
    }

    static float ScorePathRisk(Movement::PointsArray const& points, std::vector<RouteHazard> const& hazards)
    {
        if (!g_Config.RouteAvoidanceEnable || points.empty() || hazards.empty())
            return 0.0f;

        float risk = 0.0f;

        for (RouteHazard const& hazard : hazards)
        {
            if (!hazard.creature || hazard.dangerRadius <= 0.0f)
                continue;

            float minDist2 = std::numeric_limits<float>::max();
            float hx = hazard.creature->GetPositionX();
            float hy = hazard.creature->GetPositionY();

            for (G3D::Vector3 const& point : points)
            {
                float dx = point.x - hx;
                float dy = point.y - hy;
                float d2 = dx * dx + dy * dy;
                if (d2 < minDist2)
                    minDist2 = d2;
            }

            float minDist = std::sqrt(minDist2);
            if (minDist >= hazard.dangerRadius)
                continue;

            float penetration = 1.0f - (minDist / std::max(0.1f, hazard.dangerRadius));
            risk += g_Config.RouteAvoidanceWeight * hazard.severity * (1.0f + penetration);
        }

        return risk;
    }

    struct GatherCandidate
    {
        ObjectGuid::LowType spawnId = 0;
        GameObjectData const* data = nullptr;
        float distance = 0.0f;
        uint32 skillId = SKILL_NONE;
    };

    struct GatherRouteChoice
    {
        ObjectGuid::LowType spawnId = 0;
        GameObjectData const* data = nullptr;
        float approachX = 0.0f;
        float approachY = 0.0f;
        float approachZ = 0.0f;
        float pathLength = 0.0f;
        float routeRisk = 0.0f;
        float routeScore = 0.0f;
    };

    static bool FindBestApproachPoint(Player* player, GameObjectData const& data,
                                      std::vector<RouteHazard> const& hazards,
                                      float& xOut, float& yOut, float& zOut,
                                      float& pathLengthOut, float& routeRiskOut, float& routeScoreOut)
    {
        if (!player)
            return false;

        constexpr float TwoPi = 6.28318530717958647692f;
        uint32 ringCount = std::max(1u, g_Config.GatherRouteApproachRings);
        uint32 pointsPerRing = std::max(8u, g_Config.GatherRouteApproachPointsPerRing);

        // Multiple rings matter on cliff-side nodes: one XY point can be navmesh-valid
        // on the lower shelf while another point around the same node is the actual
        // mineable upper ledge. Never accept an approach whose FINAL 3-D distance from
        // the node is outside Playerbots' usable gather range.
        float maxRadius = std::max(1.5f, std::min(g_Config.GatherRouteApproachDistance,
                                                  g_Config.GatherRouteInteractionDistance));
        float minRadius = std::min(1.5f, maxRadius);

        bool found = false;
        float bestScore = 0.0f;

        for (uint32 ring = 0; ring < ringCount; ++ring)
        {
            float t = ringCount == 1 ? 1.0f : float(ring) / float(ringCount - 1);
            float radius = minRadius + (maxRadius - minRadius) * t;

            for (uint32 i = 0; i < pointsPerRing; ++i)
            {
                // Offset alternate rings by half a segment so they do not test the exact
                // same radial lines.
                float ringOffset = (ring & 1u) ? (0.5f / float(pointsPerRing)) : 0.0f;
                float angle = TwoPi * (float(i) / float(pointsPerRing) + ringOffset);

                float x = data.posX + std::cos(angle) * radius;
                float y = data.posY + std::sin(angle) * radius;
                float z = data.posZ;

                // Snap each candidate independently to the walkable terrain at that XY.
                // This is what differentiates a lower cliff shelf from an upper ledge.
                player->UpdateAllowedPositionZ(x, y, z);

                float dxNode = x - data.posX;
                float dyNode = y - data.posY;
                float dzNode = z - data.posZ;
                float interactionDistance = std::sqrt(dxNode * dxNode + dyNode * dyNode + dzNode * dzNode);

                if (interactionDistance > std::max(1.0f, g_Config.GatherRouteInteractionDistance))
                    continue;

                PathGenerator path(player);
                if (!path.CalculatePath(x, y, z, false) || path.GetPathType() != PATHFIND_NORMAL)
                    continue;

                Movement::PointsArray const& points = path.GetPath();
                if (points.empty())
                    continue;

                float pathLength = path.getPathLength();
                if (pathLength <= 0.0f)
                    continue;

                float routeRisk = ScorePathRisk(points, hazards);
                float routeScore = pathLength + routeRisk;

                if (!found || routeScore < bestScore)
                {
                    found = true;
                    bestScore = routeScore;
                    xOut = x;
                    yOut = y;
                    zOut = z;
                    pathLengthOut = pathLength;
                    routeRiskOut = routeRisk;
                    routeScoreOut = routeScore;
                }
            }
        }

        return found;
    }

    static bool FindNextGatherTarget(Player* player, ServerCustomizationGatherOnly::State& state,
                                     GatherRouteChoice& choiceOut)
    {
        if (!player || !g_Config.GatherRouteEnable)
            return false;

        uint32 currentZone = player->GetZoneId();
        uint32 phaseMask = player->GetPhaseMask();
        Map* map = player->GetMap();
        if (!map)
            return false;

        std::vector<GatherCandidate> candidates;
        candidates.reserve(64);

        float maxDist2 = g_Config.GatherRouteSearchRadius * g_Config.GatherRouteSearchRadius;

        for (auto const& pair : sObjectMgr->GetAllGOData())
        {
            ObjectGuid::LowType spawnId = pair.first;
            GameObjectData const& data = pair.second;

            if (data.mapid != player->GetMapId())
                continue;

            if (state.blacklistMs.find(spawnId) != state.blacklistMs.end())
                continue;

            float dx = data.posX - player->GetPositionX();
            float dy = data.posY - player->GetPositionY();
            float dz = data.posZ - player->GetPositionZ();
            float dist2 = dx * dx + dy * dy + dz * dz;
            if (dist2 > maxDist2)
                continue;

            if (g_Config.GatherRouteSameZoneOnly)
            {
                uint32 spawnZone = map->GetZoneId(phaseMask, data.posX, data.posY, data.posZ);
                if (spawnZone != currentZone)
                    continue;
            }

            uint32 skillId = SKILL_NONE;
            uint32 requiredSkill = 0;
            if (!GetGatherSkillForSpawn(player, data, state.mode, skillId, requiredSkill))
                continue;

            if (!IsSpawnAvailable(player, spawnId, data))
                continue;

            candidates.push_back({ spawnId, &data, std::sqrt(dist2), skillId });
        }

        if (candidates.empty())
            return false;

        float hazardScanRadius = g_Config.GatherRouteSearchRadius +
                                 std::max(20.0f, g_Config.RouteAvoidanceBuffer + 12.0f);
        std::vector<RouteHazard> hazards = CollectRouteHazards(player, hazardScanRadius);

        // Only spend navmesh time on the nearest N nodes by straight-line distance...
        std::sort(candidates.begin(), candidates.end(), [](GatherCandidate const& a, GatherCandidate const& b)
        {
            return a.distance < b.distance;
        });

        uint32 maxTests = std::max(1u, g_Config.GatherRoutePathCandidates);
        maxTests = std::min<uint32>(maxTests, uint32(candidates.size()));

        bool found = false;
        float bestRouteScore = 0.0f;

        // Among the nearest N nodes, choose the lowest-cost real navmesh route.
        // Cost = actual navmesh length + hostile-aggro risk penalty.
        for (uint32 i = 0; i < maxTests; ++i)
        {
            GatherCandidate const& candidate = candidates[i];

            float approachX = 0.0f;
            float approachY = 0.0f;
            float approachZ = 0.0f;
            float pathLength = 0.0f;
            float routeRisk = 0.0f;
            float routeScore = 0.0f;

            if (!FindBestApproachPoint(player, *candidate.data, hazards,
                                       approachX, approachY, approachZ,
                                       pathLength, routeRisk, routeScore))
            {
                state.blacklistMs[candidate.spawnId] = g_Config.GatherRouteBlacklistMs;
                continue;
            }

            if (!found || routeScore < bestRouteScore)
            {
                found = true;
                bestRouteScore = routeScore;
                choiceOut.spawnId = candidate.spawnId;
                choiceOut.data = candidate.data;
                choiceOut.approachX = approachX;
                choiceOut.approachY = approachY;
                choiceOut.approachZ = approachZ;
                choiceOut.pathLength = pathLength;
                choiceOut.routeRisk = routeRisk;
                choiceOut.routeScore = routeScore;
            }
        }

        return found;
    }

    static void TickBlacklist(ServerCustomizationGatherOnly::State& state, uint32 diff)
    {
        for (auto itr = state.blacklistMs.begin(); itr != state.blacklistMs.end();)
        {
            if (itr->second <= diff)
                itr = state.blacklistMs.erase(itr);
            else
            {
                itr->second -= diff;
                ++itr;
            }
        }
    }

    static void AbandonGatherTarget(ServerCustomizationGatherOnly::State& state, uint32 blacklistMs)
    {
        if (state.targetSpawnId)
            state.blacklistMs[state.targetSpawnId] = blacklistMs;

        state.targetSpawnId = 0;
        state.targetX = state.targetY = state.targetZ = 0.0f;
        state.targetAgeMs = 0;
        state.mountWaitMs = 0;
        state.mountGraceUsed = false;
        state.mountSuppressedNearTarget = false;
        state.lastApproachDistance = 0.0f;
        state.stuckChecks = 0;
        state.updateTimerMs = 0;
    }

    static void UpdateAutomationRecovery(Player* player, uint32 diff)
    {
        auto* state = GetAutomationState(player, false);
        if (!state || state->mode == ServerCustomizationGatherOnly::Mode::None || !player)
            return;

        // Remember recent falling for a few seconds. Player movement can report landing and
        // apply fall damage on different updates; this grace window lets the damage hook
        // recognize delayed fall damage caused during our automation route.
        if (player->IsFalling())
            state->recentFallMs = std::max(state->recentFallMs, g_Config.AutomationFallDamageGraceMs);
        else if (state->recentFallMs > diff)
            state->recentFallMs -= diff;
        else
            state->recentFallMs = 0;

        if (!g_Config.AutomationRestEnable || !player->IsAlive())
        {
            state->restHold = false;
            return;
        }

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
        bool inCombat = player->IsInCombat() || (botAI && botAI->GetState() == BOT_STATE_COMBAT);

        // Never block self-defense. As soon as combat ends, the low HP/mana test below
        // can put the character back into recovery before a new voluntary target is chosen.
        if (inCombat)
        {
            state->restHold = false;
            return;
        }

        float hpPct = player->GetHealthPct();
        bool usesMana = player->GetMaxPower(POWER_MANA) > 0;
        float manaPct = usesMana ? player->GetPowerPct(POWER_MANA) : 100.0f;

        if (!state->restHold)
        {
            bool lowHealth = hpPct <= g_Config.AutomationRestHealthStartPct;
            bool lowMana = usesMana && manaPct <= g_Config.AutomationRestManaStartPct;

            if (lowHealth || lowMana)
            {
                state->restHold = true;
                player->StopMoving();
                player->GetMotionMaster()->Clear();
            }
            return;
        }

        bool healthRecovered = hpPct >= g_Config.AutomationRestHealthResumePct;
        bool manaRecovered = !usesMana || manaPct >= g_Config.AutomationRestManaResumePct;

        if (healthRecovered && manaRecovered)
        {
            state->restHold = false;
            state->updateTimerMs = 0;
        }
    }

    static void UpdateGatherRoute(Player* player, uint32 diff)
    {
        auto* state = GetAutomationState(player, false);
        if (!state || !ServerCustomizationGatherOnly::IsGatherMode(state->mode))
            return;

        TickBlacklist(*state, diff);

        if (state->recentDamageMs > diff)
            state->recentDamageMs -= diff;
        else
            state->recentDamageMs = 0;

        if (!player->IsAlive() || player->IsBeingTeleported() || player->IsInFlight())
            return;

        PlayerbotAI* botAI = GET_PLAYERBOT_AI(player);
        if (!botAI)
            return;

        // Do not fight our own eat/drink action. The recovery controller uses hysteresis:
        // once rest starts, routing remains frozen until both HP and mana reach the resume thresholds.
        if (state->restHold)
            return;

        // Mining/herbalism opens a real loot window. Never re-path or re-open the node while
        // Playerbots is still processing that loot packet.
        if (player->GetLootGUID())
        {
            if (player->isMoving())
                player->StopMoving();
            return;
        }

        bool hasAttackers = HasLiveAttackers(botAI);
        bool combatNow = hasAttackers || player->IsInCombat() || botAI->GetState() == BOT_STATE_COMBAT;
        bool hardControlled = player->HasUnitState(UNIT_STATE_STUNNED | UNIT_STATE_ROOT | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING);

        // While mounted we can intentionally ride through a simple aggro. The exception is any
        // recent incoming damage, hard control, or forced dismount. Damage immediately clears this
        // flag from the UnitScript hook, so Playerbots can take combat control on the next update.
        state->ignoreMountedAggroActive =
            g_Config.GatherRouteIgnoreAggroUntilDamage &&
            player->IsMounted() &&
            state->recentDamageMs == 0 &&
            !hardControlled;

        bool shouldYieldToCombat = combatNow && !state->ignoreMountedAggroActive;

        if (shouldYieldToCombat)
        {
            if (!state->combatPaused)
            {
                state->combatPaused = true;
                state->mountWaitMs = 0;
                state->mountGraceUsed = false;
                state->ignoreMountedAggroActive = false;

                // Cancel only our travel movement once. Do not keep clearing MotionMaster every tick,
                // otherwise native Playerbots combat movement would never get a chance to take over.
                player->StopMoving();
                player->GetMotionMaster()->Clear();

                // Dismount immediately so the combat engine does not have to wait for the normal
                // mount-state action to notice the attacker.
                CancelMount(player);
            }

            return;
        }

        // Combat just ended: preserve the previous resource target if it is still valid and allow
        // the route controller to resume it immediately.
        if (state->combatPaused)
        {
            state->combatPaused = false;
            state->updateTimerMs = 0;
        }

        if (state->mountWaitMs > diff)
            state->mountWaitMs -= diff;
        else
            state->mountWaitMs = 0;

        if (state->updateTimerMs > diff)
        {
            state->updateTimerMs -= diff;
            return;
        }
        state->updateTimerMs = std::max(250u, g_Config.GatherRouteUpdateIntervalMs);

        GameObjectData const* targetData = nullptr;
        if (state->targetSpawnId)
            targetData = sObjectMgr->GetGameObjectData(state->targetSpawnId);

        bool targetValid = false;
        if (targetData)
        {
            uint32 dummySkill = 0;
            uint32 dummyReq = 0;
            targetValid = GetGatherSkillForSpawn(player, *targetData, state->mode, dummySkill, dummyReq) &&
                          IsSpawnAvailable(player, state->targetSpawnId, *targetData);
        }

        if (targetValid)
        {
            state->targetAgeMs += g_Config.GatherRouteUpdateIntervalMs;

            float distanceToNode = player->GetDistance(targetData->posX, targetData->posY, targetData->posZ);
            float distanceToApproach = player->GetDistance(state->targetX, state->targetY, state->targetZ);

            // Once we are close to the chosen approach, do not allow native Playerbots mount
            // logic to start another mount cycle. This specifically prevents mount/dismount
            // loops under cliff-side resources.
            state->mountSuppressedNearTarget =
                distanceToApproach <= std::max(4.0f, g_Config.GatherRouteMountSuppressDistance);

            if (state->mountSuppressedNearTarget && player->IsMounted())
            {
                CancelMount(player);
                state->mountWaitMs = 0;
                return;
            }

            // A navmesh path can occasionally report success but make no useful progress near
            // geometry seams. Detect that only near the final approach and blacklist the node
            // temporarily instead of retrying forever.
            if (g_Config.GatherRouteStuckEnable &&
                distanceToApproach <= g_Config.GatherRouteStuckNearDistance &&
                !player->IsNonMeleeSpellCast(true))
            {
                if (state->lastApproachDistance <= 0.0f ||
                    distanceToApproach + g_Config.GatherRouteStuckMinProgress < state->lastApproachDistance)
                {
                    state->lastApproachDistance = distanceToApproach;
                    state->stuckChecks = 0;
                }
                else
                {
                    ++state->stuckChecks;
                    if (state->stuckChecks >= std::max(1u, g_Config.GatherRouteStuckMaxChecks))
                    {
                        player->StopMoving();
                        player->GetMotionMaster()->Clear();
                        AbandonGatherTarget(*state, g_Config.GatherRouteStuckBlacklistMs);
                        return;
                    }
                }
            }
            else if (distanceToApproach > g_Config.GatherRouteStuckNearDistance)
            {
                state->lastApproachDistance = distanceToApproach;
                state->stuckChecks = 0;
            }

            // Dismount early enough for native gather/loot to perform the final interaction cleanly.
            if (player->IsMounted() && distanceToNode <= std::max(
                    g_Config.GatherRouteDismountDistance,
                    g_Config.GatherRouteInteractionDistance + 0.5f))
            {
                CancelMount(player);
                state->mountWaitMs = 0;
                state->mountSuppressedNearTarget = true;
                return;
            }

            if (distanceToNode <= g_Config.GatherRouteInteractionDistance)
            {
                // Native Playerbots gather/loot now sees and opens the node. Do not override
                // its final interaction movement.
                state->mountSuppressedNearTarget = true;
                return;
            }

            if (state->targetAgeMs <= g_Config.GatherRouteTargetTimeoutMs)
            {
                // For longer legs, give native Playerbots mount logic a short grace period before
                // starting direct movement. If the character has no mount, routing continues once
                // the grace expires instead of getting stuck forever.
                if (g_Config.GatherRouteMountEnable &&
                    distanceToNode >= g_Config.GatherRouteMountMinDistance &&
                    !player->IsMounted() &&
                    HasKnownMountSpell(player))
                {
                    if (!state->mountGraceUsed)
                    {
                        state->mountGraceUsed = true;
                        state->mountWaitMs = std::max(500u, g_Config.GatherRouteMountWaitMs);
                        return;
                    }

                    // While the one-shot grace timer is active, leave MotionMaster alone so
                    // CheckMountStateAction can cast the mount. Once it expires, move anyway.
                    if (state->mountWaitMs > 0)
                        return;
                }

                // Re-issue the same path after combat/AI interruptions, but never random-wander.
                if (!player->isMoving())
                {
                    player->GetMotionMaster()->MovePoint(
                        0x53434752, state->targetX, state->targetY, state->targetZ,
                        FORCED_MOVEMENT_NONE, 0.0f, 0.0f, true, false);
                }
                return;
            }

            AbandonGatherTarget(*state, g_Config.GatherRouteBlacklistMs);
        }
        else
        {
            state->targetSpawnId = 0;
            state->targetX = state->targetY = state->targetZ = 0.0f;
            state->targetAgeMs = 0;
            state->mountWaitMs = 0;
            state->mountGraceUsed = false;
            state->mountSuppressedNearTarget = false;
            state->lastApproachDistance = 0.0f;
            state->stuckChecks = 0;
        }

        GatherRouteChoice choice;
        if (!FindNextGatherTarget(player, *state, choice) || !choice.data)
            return;

        state->targetSpawnId = choice.spawnId;
        state->targetX = choice.approachX;
        state->targetY = choice.approachY;
        state->targetZ = choice.approachZ;
        state->targetAgeMs = 0;
        state->mountWaitMs = 0;
        state->mountGraceUsed = false;
        state->mountSuppressedNearTarget = false;
        state->lastApproachDistance = player->GetDistance(choice.approachX, choice.approachY, choice.approachZ);
        state->stuckChecks = 0;

        float distanceToNode = player->GetDistance(choice.data->posX, choice.data->posY, choice.data->posZ);

        if (g_Config.GatherRouteMountEnable &&
            distanceToNode >= g_Config.GatherRouteMountMinDistance &&
            !player->IsMounted() &&
            HasKnownMountSpell(player))
        {
            state->mountGraceUsed = true;
            state->mountWaitMs = std::max(500u, g_Config.GatherRouteMountWaitMs);
            return;
        }

        player->GetMotionMaster()->MovePoint(
            0x53434752, state->targetX, state->targetY, state->targetZ,
            FORCED_MOVEMENT_NONE, 0.0f, 0.0f, true, false);
    }

    static bool StartGatherRoute(ChatHandler* handler, ServerCustomizationGatherOnly::Mode mode, char const* modeName,
                                 bool suppressMobLoot = false)
    {
        Player* player = handler ? handler->GetPlayer() : nullptr;
        if (!player)
            return false;

        PlayerbotAI* botAI = EnsureRestrictedSelfbot(handler);
        if (!botAI)
            return false;

        if (mode == ServerCustomizationGatherOnly::Mode::Mining && player->GetSkillValue(SKILL_MINING) == 0)
        {
            handler->SendSysMessage("|cffff4444Automation:|r you do not know Mining.");
            return false;
        }

        if (mode == ServerCustomizationGatherOnly::Mode::Herbalism && player->GetSkillValue(SKILL_HERBALISM) == 0)
        {
            handler->SendSysMessage("|cffff4444Automation:|r you do not know Herbalism.");
            return false;
        }

        if (mode == ServerCustomizationGatherOnly::Mode::Gathering &&
            player->GetSkillValue(SKILL_MINING) == 0 && player->GetSkillValue(SKILL_HERBALISM) == 0)
        {
            handler->SendSysMessage("|cffff4444Automation:|r you know neither Mining nor Herbalism.");
            return false;
        }

        // Repair an old/player-created character that already had Riding but never received
        // the starter mount. No starter money is granted from an automation command.
        EnsureStarterRidingAndMount(player, false);

        SetAutomationMode(player, mode);
        if (auto* state = GetAutomationState(player, false))
            state->suppressMobLoot = suppressMobLoot;

        // No New RPG / wander random here: mod-server-customization chooses a real spawned
        // resource node, compares navmesh routes, and drives MotionMaster toward the best nearby approach point.
        botAI->HandleCommand(
            CHAT_MSG_WHISPER,
            "nc +loot,+gather,+mount,-new rpg,-grind,-follow,-stay,-passive",
            player);
        botAI->HandleCommand(CHAT_MSG_WHISPER, "rpg status idle", player);

        SendAutomationMode(handler, modeName);
        if (suppressMobLoot)
            handler->SendSysMessage("|cffaaaaaaMob loot:|r OFF for creatures killed while travelling; resource-node loot remains enabled.");
        handler->SendSysMessage(
            "|cffaaaaaaDirected gathering:|r multi-approach cliff-aware routing; failed final approaches are temporarily blacklisted instead of retried forever.");
        return true;
    }

    static bool StartGrind(ChatHandler* handler, uint32 targetEntry = 0, char const* targetName = nullptr)
    {
        Player* player = handler ? handler->GetPlayer() : nullptr;
        if (!player)
            return false;

        if (!EnsureRestrictedSelfbot(handler))
            return false;

        SetAutomationMode(player, ServerCustomizationGatherOnly::Mode::Grind);
        if (auto* state = GetAutomationState(player, false))
            state->grindCreatureEntry = targetEntry;

        SendSelfbotCommand(handler, "nc +grind,+loot,-new rpg,-gather,-reveal,-follow,-stay,-passive");
        SendSelfbotCommand(handler, "rpg status idle");

        if (targetEntry)
        {
            handler->PSendSysMessage("|cff00ff00Automation:|r grind target enabled - {} (entry {}).",
                                     targetName ? targetName : "creature", targetEntry);
            handler->SendSysMessage("|cffaaaaaaGrind target:|r attackers are still defended against, but voluntary grind targets are restricted to this creature entry.");
        }
        else
        {
            SendAutomationMode(handler, "grind");
            handler->SendSysMessage("|cffaaaaaaGrind:|r attacks suitable nearby mobs; it does not use the gather routing system.");
        }

        return true;
    }

    static bool StartGrindTarget(ChatHandler* handler)
    {
        Player* player = handler ? handler->GetPlayer() : nullptr;
        if (!player)
            return false;

        Unit* selected = handler->getSelectedUnit();
        Creature* creature = selected ? selected->ToCreature() : nullptr;
        if (!creature || !creature->IsAlive() || !player->IsHostileTo(creature))
        {
            handler->SendSysMessage("|cffff4444Grind target:|r select a living hostile creature first.");
            return false;
        }

        if (!StartGrind(handler, creature->GetEntry(), creature->GetName().c_str()))
            return false;

        // The selected creature is only the configuration input. Leaving it in
        // Playerbots' current-target cache prevents the native "no target"
        // trigger from running "attack anything" and selecting a grind target.
        player->SetSelection(ObjectGuid::Empty);
        if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(player))
        {
            botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Set(nullptr);
            botAI->GetAiObjectContext()->GetValue<ObjectGuid>("pull target")->Set(ObjectGuid::Empty);
        }

        return true;
    }

    static bool ParseGatherCommandOptions(ChatHandler* handler, std::string_view args, bool& suppressMobLoot)
    {
        suppressMobLoot = false;
        std::string value(args);
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });

        // Trim simple whitespace.
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
            value.erase(value.begin());
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
            value.pop_back();

        if (value.empty() || value == "loot")
            return true;

        if (value == "noloot" || value == "no-loot" || value == "nloot")
        {
            suppressMobLoot = true;
            return true;
        }

        if (handler)
            handler->SendSysMessage("|cffff4444Option invalide.|r Utilise sans option, |cffffff00loot|r ou |cffffff00noloot|r.");
        return false;
    }

    class AutomationCommandScript : public CommandScript
    {
    public:
        AutomationCommandScript() : CommandScript("ServerCustomizationAutomationCommands") {}

        ChatCommandTable GetCommands() const override
        {
            static ChatCommandTable commandTable =
            {
                { "bothelp",    HandleBotHelp,    SEC_PLAYER, Console::No },
                { "autohelp",   HandleBotHelp,    SEC_PLAYER, Console::No },
                { "bh",         HandleBotHelp,    SEC_PLAYER, Console::No },

                { "mine",       HandleMine,       SEC_PLAYER, Console::No },
                { "herb",       HandleHerb,       SEC_PLAYER, Console::No },
                { "gather",     HandleGather,     SEC_PLAYER, Console::No },
                { "grind",      HandleGrind,      SEC_PLAYER, Console::No },
                { "grindtarget", HandleGrindTarget, SEC_PLAYER, Console::No },

                { "damagedebug", HandleDamageDebug, SEC_PLAYER, Console::No },

                { "autostatus", HandleAutoStatus, SEC_PLAYER, Console::No },
                { "as",         HandleAutoStatus, SEC_PLAYER, Console::No },

                { "autostop",   HandleAutoStop,   SEC_PLAYER, Console::No },
                { "astop",      HandleAutoStop,   SEC_PLAYER, Console::No }
            };
            return commandTable;
        }

        static bool HandleBotHelp(ChatHandler* handler)
        {
            if (!handler)
                return false;

            handler->SendSysMessage("|cff00ff00=== Automation ===|r");
            handler->SendSysMessage("|cffffff00.mine [noloot]|r - mine nodes; noloot ignores creature corpses killed on the way");
            handler->SendSysMessage("|cffffff00.herb [noloot]|r - herb nodes; noloot ignores creature corpses killed on the way");
            handler->SendSysMessage("|cffffff00.gather [noloot]|r - ore + herbs; noloot ignores creature corpses killed on the way");
            handler->SendSysMessage("|cffffff00.grind|r - normal grind of suitable nearby mobs");
            handler->SendSysMessage("|cffffff00.grindtarget|r - focus voluntary grind on the selected living hostile creature type");
            handler->SendSysMessage("|cffffff00.damagedebug on|r / |cffffff00off|r / |cffffff00status|r - trace incoming damage");
            handler->SendSysMessage("|cffffff00.autostatus|r / |cffffff00.as|r - show current automation mode");
            handler->SendSysMessage("|cffffff00.autostop|r / |cffffff00.astop|r - stop automation and detach selfbot AI");
            handler->SendSysMessage("|cffaaaaaaGather combat rule:|r routing stops immediately on attacker/combat detection, Playerbots fights, then the resource route resumes.");
            handler->SendSysMessage("|cffaaaaaaGather mount rule:|r mounted simple aggro is ignored until damage/control/dismount; then combat takes priority.");
            handler->SendSysMessage("|cffaaaaaaRoute avoidance:|r gather tests multiple mineable approach points, rejects wrong-height cliff shelves, and avoids hostile aggro ranges.");
            return true;
        }

        static bool HandleMine(ChatHandler* handler, std::string_view args)
        {
            bool suppressMobLoot = false;
            if (!ParseGatherCommandOptions(handler, args, suppressMobLoot))
                return false;
            return StartGatherRoute(handler, ServerCustomizationGatherOnly::Mode::Mining, "mine", suppressMobLoot);
        }

        static bool HandleHerb(ChatHandler* handler, std::string_view args)
        {
            bool suppressMobLoot = false;
            if (!ParseGatherCommandOptions(handler, args, suppressMobLoot))
                return false;
            return StartGatherRoute(handler, ServerCustomizationGatherOnly::Mode::Herbalism, "herb", suppressMobLoot);
        }

        static bool HandleGather(ChatHandler* handler, std::string_view args)
        {
            bool suppressMobLoot = false;
            if (!ParseGatherCommandOptions(handler, args, suppressMobLoot))
                return false;
            return StartGatherRoute(handler, ServerCustomizationGatherOnly::Mode::Gathering, "gather", suppressMobLoot);
        }

        static bool HandleGrind(ChatHandler* handler)
        {
            return StartGrind(handler);
        }

        static bool HandleGrindTarget(ChatHandler* handler)
        {
            return StartGrindTarget(handler);
        }

        static bool HandleDamageDebug(ChatHandler* handler, std::string_view args)
        {
            Player* player = handler ? handler->GetPlayer() : nullptr;
            if (!player)
                return false;

            std::string value(args);
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return char(std::tolower(c)); });

            auto* state = GetDamageDebugState(player, true);

            if (value.empty() || value == "status")
            {
                handler->PSendSysMessage("|cffffff00Damage debug:|r {}.", state->enabled ? "|cff00ff00ON|r" : "|cffff4444OFF|r");
                return true;
            }

            if (value == "on" || value == "1")
            {
                state->enabled = true;
                handler->SendSysMessage("|cff00ff00Damage debug enabled.|r Incoming damage will be shown in chat and worldserver logs.");
                return true;
            }

            if (value == "off" || value == "0")
            {
                state->enabled = false;
                handler->SendSysMessage("|cffffff00Damage debug disabled.|r");
                return true;
            }

            handler->SendSysMessage("|cffff4444Syntax:|r .damagedebug on | off | status");
            return false;
        }

        static bool HandleAutoStatus(ChatHandler* handler)
        {
            Player* player = handler ? handler->GetPlayer() : nullptr;
            if (!player)
                return false;

            if (!GET_PLAYERBOT_AI(player))
            {
                handler->SendSysMessage("|cffffff00Automation:|r inactive.");
                return true;
            }

            auto* state = GetAutomationState(player, false);
            switch (GetAutomationMode(player))
            {
                case ServerCustomizationGatherOnly::Mode::Mining:
                    handler->PSendSysMessage("|cff00ff00Automation:|r active - mine{}. Target spawn: {}", (state && state->suppressMobLoot) ? " noloot" : "", state ? uint32(state->targetSpawnId) : 0);
                    break;
                case ServerCustomizationGatherOnly::Mode::Herbalism:
                    handler->PSendSysMessage("|cff00ff00Automation:|r active - herb{}. Target spawn: {}", (state && state->suppressMobLoot) ? " noloot" : "", state ? uint32(state->targetSpawnId) : 0);
                    break;
                case ServerCustomizationGatherOnly::Mode::Gathering:
                    handler->PSendSysMessage("|cff00ff00Automation:|r active - gather{}. Target spawn: {}", (state && state->suppressMobLoot) ? " noloot" : "", state ? uint32(state->targetSpawnId) : 0);
                    break;
                case ServerCustomizationGatherOnly::Mode::Grind:
                    if (state && state->grindCreatureEntry)
                        handler->PSendSysMessage("|cff00ff00Automation:|r active - grind target entry {}.", state->grindCreatureEntry);
                    else
                        handler->SendSysMessage("|cff00ff00Automation:|r active - grind.");
                    break;
                default:
                    handler->SendSysMessage("|cffffff00Automation:|r selfbot AI active, custom mode not set.");
                    break;
            }

            return true;
        }

        static bool HandleAutoStop(ChatHandler* handler)
        {
            Player* player = handler ? handler->GetPlayer() : nullptr;
            if (!player)
                return false;

            SetAutomationMode(player, ServerCustomizationGatherOnly::Mode::None);

            if (!GET_PLAYERBOT_AI(player))
            {
                handler->SendSysMessage("|cffffff00Automation:|r already stopped.");
                return true;
            }

            SendSelfbotCommand(handler, "rpg status idle");
            SendSelfbotCommand(handler, "nc -new rpg,-grind,-loot,-gather,-mount,-reveal,-follow,+stay,+passive");
            DisableRestrictedSelfbot(handler);

            handler->SendSysMessage("|cff00ff00Automation:|r stopped and selfbot AI disabled.");
            return true;
        }
    };

    class XpCommandScript : public CommandScript
    {
    public:
        XpCommandScript() : CommandScript("ServerCustomizationXpCommands") {}

        ChatCommandTable GetCommands() const override
        {
            static ChatCommandTable xpCommands =
            {
                { "enable",  HandleEnable,  SEC_PLAYER, Console::No },
                { "disable", HandleDisable, SEC_PLAYER, Console::No },
                { "view",    HandleView,    SEC_PLAYER, Console::No },
                { "set",     HandleSet,     SEC_PLAYER, Console::No },
                { "default", HandleDefault, SEC_PLAYER, Console::No }
            };
            static ChatCommandTable root = { { "xp", xpCommands } };
            return root;
        }

        static bool IsAvailable(ChatHandler* handler)
        {
            if (g_Config.XpRateEnable)
                return true;

            handler->SendSysMessage("|cffff4444[XP]|r La personnalisation d'experience est desactivee sur ce serveur.");
            return false;
        }

        static bool HandleView(ChatHandler* handler)
        {
            if (!IsAvailable(handler))
                return false;

            SendXpStatus(handler, handler->GetPlayer());
            return true;
        }

        static bool HandleSet(ChatHandler* handler, float rate)
        {
            if (!IsAvailable(handler))
                return false;

            Player* player = handler->GetPlayer();
            if (!player || !std::isfinite(rate) || rate < 1.0f || rate > g_Config.XpRateMaximum)
            {
                handler->PSendSysMessage("|cffff4444Syntaxe :|r .xp set X (X entre 1 et {:.2f})", g_Config.XpRateMaximum);
                return false;
            }

            GetCharacterRates(player)->xpRate = rate;
            SaveCharacterRates(player);
            SendXpStatus(handler, player);
            return true;
        }

        static bool HandleDefault(ChatHandler* handler)
        {
            if (!IsAvailable(handler))
                return false;

            Player* player = handler->GetPlayer();
            if (!player)
                return false;

            GetCharacterRates(player)->xpRate = g_Config.XpRateDefault;
            SaveCharacterRates(player);
            SendXpStatus(handler, player);
            return true;
        }

        static bool HandleDisable(ChatHandler* handler)
        {
            if (!IsAvailable(handler))
                return false;

            Player* player = handler->GetPlayer();
            if (!player)
                return false;

            player->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_NO_XP_GAIN);
            handler->SendSysMessage("|cffffff00[XP]|r Gain d'experience desactive.");
            return true;
        }

        static bool HandleEnable(ChatHandler* handler)
        {
            if (!IsAvailable(handler))
                return false;

            Player* player = handler->GetPlayer();
            if (!player)
                return false;

            player->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_NO_XP_GAIN);
            handler->SendSysMessage("|cff00ff00[XP]|r Gain d'experience reactive.");
            SendXpStatus(handler, player);
            return true;
        }
    };

    class ProfessionCommandScript : public CommandScript
    {
    public:
        ProfessionCommandScript() : CommandScript("ServerCustomizationProfessionCommands") {}

        ChatCommandTable GetCommands() const override
        {
            static ChatCommandTable professionCommands =
            {
                { "gathering", HandleGathering, SEC_PLAYER, Console::No },
                { "crafting",  HandleCrafting,  SEC_PLAYER, Console::No },
                { "view",      HandleView,      SEC_PLAYER, Console::No },
                { "default",   HandleDefault,   SEC_PLAYER, Console::No }
            };
            static ChatCommandTable root = { { "profession", professionCommands } };
            return root;
        }

        static bool IsAvailable(ChatHandler* handler)
        {
            if (g_Config.ProfessionGainEnable)
                return true;

            handler->SendSysMessage("|cffff4444[Metiers]|r La personnalisation des gains est desactivee sur ce serveur.");
            return false;
        }

        static bool SetGain(ChatHandler* handler, uint32 gain, bool gathering)
        {
            if (!IsAvailable(handler))
                return false;

            Player* player = handler->GetPlayer();
            if (!player || gain < 1 || gain > 3)
            {
                handler->SendSysMessage(gathering
                    ? "|cffff4444Syntaxe :|r .profession gathering X (X entre 1 et 3)"
                    : "|cffff4444Syntaxe :|r .profession crafting X (X entre 1 et 3)");
                return false;
            }

            CharacterRatesState* state = GetCharacterRates(player);
            if (gathering)
                state->gatheringSkillGain = gain;
            else
                state->craftingSkillGain = gain;
            SaveCharacterRates(player);
            SendProfessionStatus(handler, player);
            return true;
        }

        static bool HandleGathering(ChatHandler* handler, uint32 gain) { return SetGain(handler, gain, true); }
        static bool HandleCrafting(ChatHandler* handler, uint32 gain) { return SetGain(handler, gain, false); }

        static bool HandleView(ChatHandler* handler)
        {
            if (!IsAvailable(handler))
                return false;
            SendProfessionStatus(handler, handler->GetPlayer());
            return true;
        }

        static bool HandleDefault(ChatHandler* handler)
        {
            if (!IsAvailable(handler))
                return false;

            Player* player = handler->GetPlayer();
            if (!player)
                return false;

            CharacterRatesState* state = GetCharacterRates(player);
            state->gatheringSkillGain = g_Config.GatheringSkillGainDefault;
            state->craftingSkillGain = g_Config.CraftingSkillGainDefault;
            SaveCharacterRates(player);
            SendProfessionStatus(handler, player);
            return true;
        }
    };

    class DamageDebugUnitScript : public UnitScript
    {
    public:
        DamageDebugUnitScript()
            : UnitScript("ServerCustomizationDamageDebugUnitScript", true,
                         { UNITHOOK_ON_DAMAGE, UNITHOOK_MODIFY_PERIODIC_DAMAGE_AURAS_TICK,
                           UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN }) {}

        void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override
        {
            Player* player = victim ? victim->ToPlayer() : nullptr;
            if (!player)
                return;

            if (ShouldSuppressAutomationFallDamage(player, attacker))
            {
                if (IsDamageDebugEnabled(player))
                    SendDamageDebug(player, "fall-suppressed", attacker, damage, nullptr);
                damage = 0;
                return;
            }

            MarkGatherDamage(player, damage);
            if (IsDamageDebugEnabled(player))
                SendDamageDebug(player, "incoming", attacker, damage, nullptr);
        }

        void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* spellInfo) override
        {
            Player* player = target ? target->ToPlayer() : nullptr;
            if (!player)
                return;

            MarkGatherDamage(player, damage);
            if (IsDamageDebugEnabled(player))
                SendDamageDebug(player, "periodic", attacker, damage, spellInfo);
        }

        void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
        {
            if (damage <= 0)
                return;

            Player* player = target ? target->ToPlayer() : nullptr;
            if (!player)
                return;

            MarkGatherDamage(player, uint32(damage));
            if (IsDamageDebugEnabled(player))
                SendDamageDebug(player, "spell", attacker, uint32(damage), spellInfo);
        }
    };

    class PlayerScriptImpl : public PlayerScript
    {
    public:
        PlayerScriptImpl()
            : PlayerScript("ServerCustomizationPlayerScript",
                           { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT, PLAYERHOOK_ON_UPDATE,
                             PLAYERHOOK_ON_GIVE_EXP, PLAYERHOOK_ON_UPDATE_GATHERING_SKILL,
                             PLAYERHOOK_ON_UPDATE_CRAFTING_SKILL }) {}

        void OnPlayerUpdate(Player* player, uint32 diff) override
        {
            UpdateAutomationRecovery(player, diff);
            UpdateGatherRoute(player, diff);
        }

        void OnPlayerLogin(Player* player) override
        {
            EnsureStarterRidingAndMount(player, true);

            CharacterRatesState* state = GetCharacterRates(player);
            state->xpRate = g_Config.XpRateDefault;
            state->gatheringSkillGain = g_Config.GatheringSkillGainDefault;
            state->craftingSkillGain = g_Config.CraftingSkillGainDefault;
            if (QueryResult result = CharacterDatabase.Query(
                    "SELECT `XPRate`, `GatheringSkillGain`, `CraftingSkillGain` "
                    "FROM `server_customization_character_rates` WHERE `CharacterGUID` = {}",
                    player->GetGUID().GetCounter()))
            {
                Field* fields = result->Fetch();
                state->xpRate = ClampXpRate(fields[0].Get<float>());
                state->gatheringSkillGain = std::clamp(fields[1].Get<uint32>(), 1u, 3u);
                state->craftingSkillGain = std::clamp(fields[2].Get<uint32>(), 1u, 3u);
            }

            if (g_Config.XpRateEnable && g_Config.XpRateAnnounceOnLogin && player->GetSession())
            {
                ChatHandler handler(player->GetSession());
                SendXpStatus(&handler, player);
            }
        }

        void OnPlayerLogout(Player* player) override
        {
            SaveCharacterRates(player);
        }

        void OnPlayerGiveXP(Player* player, uint32& amount, Unit*, uint8) override
        {
            if (g_Config.XpRateEnable)
                amount = uint32(std::round(float(amount) * GetCharacterRates(player)->xpRate));
        }

        void OnPlayerUpdateGatheringSkill(Player* player, uint32 skillId, uint32, uint32, uint32, uint32, uint32& gain) override
        {
            if (g_Config.ProfessionGainEnable && (skillId == SKILL_HERBALISM || skillId == SKILL_MINING))
                gain = GetCharacterRates(player)->gatheringSkillGain;
        }

        void OnPlayerUpdateCraftingSkill(Player* player, SkillLineAbilityEntry const*, uint32, uint32& gain) override
        {
            if (g_Config.ProfessionGainEnable)
                gain = GetCharacterRates(player)->craftingSkillGain;
        }
    };
}

void AddServerCustomizationScripts()
{
    new ServerCustomization::ConfigScript();
    new ServerCustomization::PlayerScriptImpl();
    new ServerCustomization::DamageDebugUnitScript();
    new ServerCustomization::AutomationCommandScript();
    new ServerCustomization::XpCommandScript();
    new ServerCustomization::ProfessionCommandScript();
}
