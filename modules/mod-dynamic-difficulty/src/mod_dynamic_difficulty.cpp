#include "ScriptMgr.h"
#include "Configuration/Config.h"
#include "Chat.h"
#include "Creature.h"
#include "DataMap.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "WorldSession.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

using namespace Acore::ChatCommands;

namespace DynamicDifficulty
{
enum class AreaKind : uint8 { Outdoor, Dungeon, Raid };

struct Range { float Min = 1.0f; float Max = 1.0f; };
struct ContextRanges { Range Damage; Range SpellDamage; };

struct Config
{
    bool Enable = true;
    uint8 DefaultDifficulty = 0;
    bool AnnounceOnLogin = true;
    bool Debug = false;
    ContextRanges OutdoorWithBots, OutdoorWithoutBots;
    ContextRanges DungeonWithBots, DungeonWithoutBots;
    ContextRanges RaidWithBots, RaidWithoutBots;
    float NormalCreatureExtra = 1.0f;
    float EliteCreatureExtra = 1.0f;
    float RareEliteCreatureExtra = 1.0f;
    float WorldBossCreatureExtra = 1.0f;
    float RareCreatureExtra = 1.0f;
    float FinalMultiplierCap = 10.0f;
};

static Config g_Config;

class DifficultyData : public DataMap::Base
{
public:
    explicit DifficultyData(uint8 value = 0) : Value(value) {}
    uint8 Value = 0;
};

static constexpr char const* DATA_KEY = "DynamicDifficulty";

static uint8 ClampDifficulty(int32 value)
{
    return static_cast<uint8>(std::clamp<int32>(value, 0, 100));
}

static float Interpolate(Range const& range, uint8 difficulty)
{
    float t = static_cast<float>(difficulty) / 100.0f;
    return range.Min + ((range.Max - range.Min) * t);
}

static Range LoadRange(char const* prefix, char const* suffix, float defaultMin, float defaultMax)
{
    Range r;
    std::string minName = std::string(prefix) + "." + suffix + ".Min";
    std::string maxName = std::string(prefix) + "." + suffix + ".Max";
    r.Min = std::max(0.0f, sConfigMgr->GetOption<float>(minName, defaultMin));
    r.Max = std::max(0.0f, sConfigMgr->GetOption<float>(maxName, defaultMax));
    if (r.Max < r.Min)
        std::swap(r.Min, r.Max);
    return r;
}

static ContextRanges LoadContext(char const* prefix, float dMin, float dMax, float sMin, float sMax)
{
    ContextRanges r;
    r.Damage = LoadRange(prefix, "Damage", dMin, dMax);
    r.SpellDamage = LoadRange(prefix, "SpellDamage", sMin, sMax);
    return r;
}

static bool IsBot(Player const* player)
{
    return player && player->GetSession() && player->GetSession()->IsBot();
}

static uint8 GetPlayerDifficulty(Player* player)
{
    if (!player)
        return g_Config.DefaultDifficulty;

    if (DifficultyData* data = player->CustomData.Get<DifficultyData>(DATA_KEY))
        return data->Value;

    player->CustomData.Set(DATA_KEY, new DifficultyData(g_Config.DefaultDifficulty));
    return g_Config.DefaultDifficulty;
}

static void SetPlayerDifficulty(Player* player, uint8 value)
{
    if (!player)
        return;

    if (DifficultyData* data = player->CustomData.Get<DifficultyData>(DATA_KEY))
    {
        data->Value = value;
        return;
    }

    player->CustomData.Set(DATA_KEY, new DifficultyData(value));
}

struct GroupContext
{
    bool HasRealPlayer = false;
    bool HasBots = false;
    uint8 Difficulty = 0;
};

static GroupContext ResolveGroupContext(Player* victim)
{
    GroupContext result;
    if (!victim)
        return result;

    Group* group = victim->GetGroup();
    if (!group)
    {
        if (IsBot(victim))
        {
            result.HasBots = true;
            return result;
        }

        result.HasRealPlayer = true;
        result.Difficulty = GetPlayerDifficulty(victim);
        return result;
    }

    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (!member || !member->GetSession())
            continue;

        if (IsBot(member))
            result.HasBots = true;
        else
        {
            result.HasRealPlayer = true;
            result.Difficulty = std::max(result.Difficulty, GetPlayerDifficulty(member));
        }
    }
    return result;
}

static AreaKind GetAreaKind(Map const* map)
{
    if (!map) return AreaKind::Outdoor;
    if (map->IsRaid()) return AreaKind::Raid;
    if (map->IsDungeon()) return AreaKind::Dungeon;
    return AreaKind::Outdoor;
}

static ContextRanges const& GetRanges(AreaKind area, bool bots)
{
    if (area == AreaKind::Raid)
        return bots ? g_Config.RaidWithBots : g_Config.RaidWithoutBots;
    if (area == AreaKind::Dungeon)
        return bots ? g_Config.DungeonWithBots : g_Config.DungeonWithoutBots;
    return bots ? g_Config.OutdoorWithBots : g_Config.OutdoorWithoutBots;
}

static char const* AreaName(AreaKind area)
{
    if (area == AreaKind::Raid) return "Raid";
    if (area == AreaKind::Dungeon) return "Dungeon";
    return "Outdoor";
}

static void SendDamageDebugToPlayer(
    Player* victim,
    char const* kind,
    uint32 creatureEntry,
    AreaKind area,
    GroupContext const& group,
    int64 before,
    float multiplier,
    int64 after)
{
    if (!g_Config.Debug || !victim || !victim->GetSession() || IsBot(victim))
        return;

    ChatHandler(victim->GetSession()).PSendSysMessage(
        "|cffffcc00[DD DEBUG]|r {} mob={} area={} bots={} diff={} raw={} x{:.3f} => {}",
        kind,
        creatureEntry,
        AreaName(area),
        group.HasBots ? 1u : 0u,
        static_cast<uint32>(group.Difficulty),
        before,
        multiplier,
        after);
}

static float RankExtra(Creature const* creature)
{
    if (!creature || !creature->GetCreatureTemplate())
        return 1.0f;

    switch (creature->GetCreatureTemplate()->rank)
    {
        case CREATURE_ELITE_NORMAL: return g_Config.NormalCreatureExtra;
        case CREATURE_ELITE_ELITE: return g_Config.EliteCreatureExtra;
        case CREATURE_ELITE_RAREELITE: return g_Config.RareEliteCreatureExtra;
        case CREATURE_ELITE_WORLDBOSS: return g_Config.WorldBossCreatureExtra;
        case CREATURE_ELITE_RARE: return g_Config.RareCreatureExtra;
        default: return 1.0f;
    }
}

static float EffectiveMultiplier(Unit* target, Unit* attacker, bool spell)
{
    if (!g_Config.Enable || !target || !attacker)
        return 1.0f;

    Creature* creature = attacker->ToCreature();
    Player* victim = target->ToPlayer();
    if (!creature || !victim)
        return 1.0f;

    GroupContext group = ResolveGroupContext(victim);
    if (!group.HasRealPlayer)
        return 1.0f;

    ContextRanges const& ranges = GetRanges(GetAreaKind(target->GetMap()), group.HasBots);
    float base = Interpolate(spell ? ranges.SpellDamage : ranges.Damage, group.Difficulty);
    return std::clamp(base * RankExtra(creature), 0.0f, g_Config.FinalMultiplierCap);
}

static uint32 ScaleDamage(uint32 damage, float multiplier)
{
    if (!damage || multiplier == 1.0f)
        return damage;
    double v = std::round(static_cast<double>(damage) * multiplier);
    if (v <= 0.0) return 0;
    if (v >= static_cast<double>(UINT32_MAX)) return UINT32_MAX;
    return static_cast<uint32>(v);
}

static int32 ScaleDamage(int32 damage, float multiplier)
{
    if (damage <= 0 || multiplier == 1.0f)
        return damage;
    double v = std::round(static_cast<double>(damage) * multiplier);
    if (v <= 0.0) return 0;
    if (v >= static_cast<double>(INT32_MAX)) return INT32_MAX;
    return static_cast<int32>(v);
}

static void SaveDifficulty(Player* player)
{
    if (!player || IsBot(player))
        return;

    CharacterDatabase.DirectExecute(
        "REPLACE INTO `mod_dynamic_difficulty` (`guid`, `difficulty`) VALUES ({}, {})",
        player->GetGUID().GetCounter(),
        static_cast<uint32>(GetPlayerDifficulty(player)));
}

static void SendStatus(ChatHandler* handler, Player* player)
{
    uint8 personal = GetPlayerDifficulty(player);
    GroupContext group = ResolveGroupContext(player);
    AreaKind area = GetAreaKind(player->GetMap());
    uint8 effective = group.HasRealPlayer ? group.Difficulty : personal;
    ContextRanges const& ranges = GetRanges(area, group.HasBots);

    handler->PSendSysMessage(
        "|cff00ff96[DynamicDifficulty]|r Personal: %u%%. Effective group: %u%%.",
        static_cast<uint32>(personal), static_cast<uint32>(effective));

    handler->PSendSysMessage(
        "Context: %s / %s bots. Damage x%.3f, spell damage x%.3f before creature-rank modifier.",
        AreaName(area), group.HasBots ? "with" : "without",
        Interpolate(ranges.Damage, effective),
        Interpolate(ranges.SpellDamage, effective));
}

class DynamicDifficultyWorldScript : public WorldScript
{
public:
    DynamicDifficultyWorldScript()
        : WorldScript("DynamicDifficultyWorldScript", { WORLDHOOK_ON_BEFORE_CONFIG_LOAD }) {}

    void OnBeforeConfigLoad(bool /*reload*/) override
    {
        g_Config.Enable = sConfigMgr->GetOption<bool>("DynamicDifficulty.Enable", true);
        g_Config.DefaultDifficulty = ClampDifficulty(
            sConfigMgr->GetOption<int32>("DynamicDifficulty.DefaultDifficulty", 0));
        g_Config.AnnounceOnLogin =
            sConfigMgr->GetOption<bool>("DynamicDifficulty.AnnounceOnLogin", true);
        g_Config.Debug = sConfigMgr->GetOption<bool>("DynamicDifficulty.Debug", false);

        g_Config.FinalMultiplierCap =
            std::max(1.0f, sConfigMgr->GetOption<float>("DynamicDifficulty.FinalMultiplierCap", 10.0f));

        g_Config.OutdoorWithBots = LoadContext("DynamicDifficulty.Outdoor.WithBots", 1.40f, 2.00f, 1.40f, 2.00f);
        g_Config.OutdoorWithoutBots = LoadContext("DynamicDifficulty.Outdoor.WithoutBots", 1.00f, 1.30f, 1.00f, 1.30f);
        g_Config.DungeonWithBots = LoadContext("DynamicDifficulty.Dungeon.WithBots", 1.20f, 1.70f, 1.20f, 1.70f);
        g_Config.DungeonWithoutBots = LoadContext("DynamicDifficulty.Dungeon.WithoutBots", 1.00f, 1.20f, 1.00f, 1.20f);
        g_Config.RaidWithBots = LoadContext("DynamicDifficulty.Raid.WithBots", 1.10f, 1.50f, 1.10f, 1.50f);
        g_Config.RaidWithoutBots = LoadContext("DynamicDifficulty.Raid.WithoutBots", 1.00f, 1.15f, 1.00f, 1.15f);

        g_Config.NormalCreatureExtra = std::max(0.0f, sConfigMgr->GetOption<float>("DynamicDifficulty.CreatureRank.Normal", 1.0f));
        g_Config.EliteCreatureExtra = std::max(0.0f, sConfigMgr->GetOption<float>("DynamicDifficulty.CreatureRank.Elite", 1.0f));
        g_Config.RareEliteCreatureExtra = std::max(0.0f, sConfigMgr->GetOption<float>("DynamicDifficulty.CreatureRank.RareElite", 1.0f));
        g_Config.WorldBossCreatureExtra = std::max(0.0f, sConfigMgr->GetOption<float>("DynamicDifficulty.CreatureRank.WorldBoss", 1.0f));
        g_Config.RareCreatureExtra = std::max(0.0f, sConfigMgr->GetOption<float>("DynamicDifficulty.CreatureRank.Rare", 1.0f));
    }
};

class DynamicDifficultyPlayerScript : public PlayerScript
{
public:
    DynamicDifficultyPlayerScript()
        : PlayerScript("DynamicDifficultyPlayerScript", { PLAYERHOOK_ON_LOGIN, PLAYERHOOK_ON_LOGOUT }) {}

    void OnPlayerLogin(Player* player) override
    {
        if (!player || IsBot(player))
            return;

        uint8 value = g_Config.DefaultDifficulty;
        QueryResult result = CharacterDatabase.Query(
            "SELECT `difficulty` FROM `mod_dynamic_difficulty` WHERE `guid` = {}",
            player->GetGUID().GetCounter());

        if (result)
            value = ClampDifficulty(result->Fetch()[0].Get<int32>());

        player->CustomData.Set(DATA_KEY, new DifficultyData(value));

        if (g_Config.Enable && g_Config.AnnounceOnLogin)
            ChatHandler(player->GetSession()).PSendSysMessage(
                "|cff00ff96[DynamicDifficulty]|r Loaded: %u%%. Use .gamedifficulty for status.",
                static_cast<uint32>(value));
    }

    void OnPlayerLogout(Player* player) override
    {
        SaveDifficulty(player);
    }
};

class DynamicDifficultyUnitScript : public UnitScript
{
public:
    DynamicDifficultyUnitScript()
        : UnitScript("DynamicDifficultyUnitScript") {}

    uint32 DealDamage(Unit* attacker, Unit* victimUnit, uint32 damage, DamageEffectType damageType) override
    {
        if (!g_Config.Enable || !attacker || !victimUnit || !damage)
            return damage;

        Creature* creature = attacker->ToCreature();
        Player* victim = victimUnit->ToPlayer();

        if (!creature || !victim)
            return damage;

        GroupContext group = ResolveGroupContext(victim);
        if (!group.HasRealPlayer)
            return damage;

        // DIRECT_DAMAGE is normal weapon/melee damage.
        // Everything else is treated with the SpellDamage range so spells,
        // class abilities and periodic damage keep their own configured scaling.
        bool spell = damageType != DIRECT_DAMAGE;
        float multiplier = EffectiveMultiplier(victimUnit, attacker, spell);

        uint32 before = damage;
        uint32 after = ScaleDamage(damage, multiplier);

        if (g_Config.Debug && victim->GetSession() && !IsBot(victim))
        {
            AreaKind area = GetAreaKind(victim->GetMap());

            ChatHandler(victim->GetSession()).PSendSysMessage(
                "|cffffcc00[DD FINAL]|r type={} mob={} area={} bots={} diff={} raw={} x{:.3f} => {}",
                static_cast<uint32>(damageType),
                creature->GetEntry(),
                AreaName(area),
                group.HasBots ? 1u : 0u,
                static_cast<uint32>(group.Difficulty),
                before,
                multiplier,
                after);

            LOG_INFO("module",
                "[DynamicDifficulty FINAL] type={} creatureEntry={} playerGuid={} area={} bots={} difficulty={} raw={} multiplier={:.3f} final={}",
                static_cast<uint32>(damageType),
                creature->GetEntry(),
                victim->GetGUID().GetCounter(),
                AreaName(area),
                group.HasBots ? 1 : 0,
                static_cast<uint32>(group.Difficulty),
                before,
                multiplier,
                after);
        }

        return after;
    }
};

class DynamicDifficultyCommandScript : public CommandScript
{
public:
    DynamicDifficultyCommandScript() : CommandScript("DynamicDifficultyCommandScript") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable table =
        {
            { "gamedifficulty", HandleGameDifficulty, SEC_PLAYER, Console::No }
        };
        return table;
    }

    static bool HandleGameDifficulty(ChatHandler* handler, char const* args)
    {
        if (!handler || !handler->GetSession())
            return false;

        Player* player = handler->GetSession()->GetPlayer();
        if (!player || IsBot(player))
            return false;

        std::string value = args ? args : "";
        auto first = value.find_first_not_of(" \t\r\n");
        auto last = value.find_last_not_of(" \t\r\n");
        value = (first == std::string::npos) ? "" : value.substr(first, last - first + 1);

        if (value.empty() || value == "status" || value == "view")
        {
            SendStatus(handler, player);
            return true;
        }

        if (value == "reset" || value == "default")
        {
            SetPlayerDifficulty(player, g_Config.DefaultDifficulty);
            SaveDifficulty(player);
            handler->PSendSysMessage(
                "|cff00ff96[DynamicDifficulty]|r Reset to %u%% and saved.",
                static_cast<uint32>(g_Config.DefaultDifficulty));
            SendStatus(handler, player);
            return true;
        }

        char* end = nullptr;
        long parsed = std::strtol(value.c_str(), &end, 10);
        if (!end || *end != '\0' || parsed < 0 || parsed > 100)
        {
            handler->SendSysMessage("Usage: .gamedifficulty <0-100> | status | reset");
            handler->SetSentErrorMessage(true);
            return false;
        }

        SetPlayerDifficulty(player, static_cast<uint8>(parsed));
        SaveDifficulty(player);
        handler->PSendSysMessage(
            "|cff00ff96[DynamicDifficulty]|r Set to %ld%% and saved in SQL.", parsed);
        SendStatus(handler, player);
        return true;
    }
};
}

void AddDynamicDifficultyScripts()
{
    using namespace DynamicDifficulty;
    new DynamicDifficultyWorldScript();
    new DynamicDifficultyPlayerScript();
    new DynamicDifficultyUnitScript();
    new DynamicDifficultyCommandScript();
}
