#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Formulas.h"
#include "GameObject.h"
#include "Map.h"
#include "Player.h"
#include "Random.h"
#include "ScriptMgr.h"
#include "World.h"

#include <algorithm>

namespace
{
constexpr uint32 COPPER_CHEST_ENTRY = 900001;
constexpr uint32 SILVER_CHEST_ENTRY = 900002;
constexpr uint32 MOUNT_CHEST_ENTRY  = 900003;
constexpr char const* CHEST_SCRIPT_NAME = "go_mount_chest_v2";

bool IsOurChest(uint32 entry)
{
    return entry == COPPER_CHEST_ENTRY || entry == SILVER_CHEST_ENTRY || entry == MOUNT_CHEST_ENTRY;
}

void SendMessage(Player* player, char const* message)
{
    if (player && player->GetSession())
        ChatHandler(player->GetSession()).SendSysMessage(message);
}
}

class MountChestPlayerScript : public PlayerScript
{
public:
    MountChestPlayerScript() : PlayerScript("MountChestPlayerScript") { }

    void OnPlayerCreatureKill(Player* killer, Creature* killed) override
    {
        if (!killer || !killed)
            return;

        if (!sConfigMgr->GetOption<bool>("MountChest.Enable", true))
            return;

        Map* map = killer->GetMap();
        if (!map || map->IsDungeon() || map->IsRaid())
            return;

        // At max level the player cannot actually receive kill XP.
        if (killer->GetLevel() >= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
            return;

        // Only creatures that are actually XP-eligible for this player can trigger a chest.
        if (Acore::XP::Gain(killer, killed) == 0)
            return;

        // One fresh uniform random roll for every eligible kill.
        // These ranges are mutually exclusive, so the unconditional probabilities are exact:
        // mount 10/10000, silver 100/10000, copper 1000/10000.
        uint32 roll = urand(1, 10000);
        uint32 chestEntry = 0;

        if (roll <= 10)
            chestEntry = MOUNT_CHEST_ENTRY;
        else if (roll <= 110)
            chestEntry = SILVER_CHEST_ENTRY;
        else if (roll <= 1110)
            chestEntry = COPPER_CHEST_ENTRY;
        else
            return;

        uint32 lifetime = sConfigMgr->GetOption<uint32>("MountChest.LifetimeSeconds", 120);
        if (lifetime == 0)
            lifetime = 120;

        GameObject* chest = killer->SummonGameObject(
            chestEntry,
            killed->GetPositionX(),
            killed->GetPositionY(),
            killed->GetPositionZ(),
            killed->GetOrientation(),
            0.0f, 0.0f, 0.0f, 1.0f,
            lifetime);

        if (chest)
        {
            chest->ReplaceAllGameObjectFlags(static_cast<GameObjectFlags>(0));
            chest->SetLootState(GO_READY);
        }
    }
};

class MountChestGameObjectScript : public GameObjectScript
{
public:
    MountChestGameObjectScript() : GameObjectScript(CHEST_SCRIPT_NAME) { }

    bool OnGossipHello(Player* player, GameObject* go) override
    {
        if (!player || !go || !IsOurChest(go->GetEntry()))
            return false;

        if (!sConfigMgr->GetOption<bool>("MountChest.Enable", true))
            return true;

        bool ownerOnly = sConfigMgr->GetOption<bool>("MountChest.OwnerOnly", true);
        ObjectGuid ownerGuid = go->GetOwnerGUID();
        if (ownerOnly && !ownerGuid.IsEmpty() && ownerGuid != player->GetGUID())
        {
            SendMessage(player, "This Lucky Chest belongs to another player.");
            return true;
        }

        uint32 minRoll = sConfigMgr->GetOption<uint32>("MountChest.MoneyMin", 10);
        uint32 maxRoll = sConfigMgr->GetOption<uint32>("MountChest.MoneyMax", 50);
        if (minRoll > maxRoll)
            std::swap(minRoll, maxRoll);

        uint32 level = player->GetLevel();
        bool rewardGranted = false;

        switch (go->GetEntry())
        {
            case COPPER_CHEST_ENTRY:
            {
                uint32 amount = urand(minRoll, maxRoll) * level;
                player->ModifyMoney(static_cast<int32>(amount));
                SendMessage(player, "Lucky Copper Chest opened: money added.");
                rewardGranted = true;
                break;
            }
            case SILVER_CHEST_ENTRY:
            {
                uint32 amount = urand(minRoll, maxRoll) * level * 100u;
                player->ModifyMoney(static_cast<int32>(amount));
                SendMessage(player, "Lucky Silver Chest opened: money added.");
                rewardGranted = true;
                break;
            }
            case MOUNT_CHEST_ENTRY:
            {
                QueryResult result = WorldDatabase.Query(
                    "SELECT `entry` FROM `item_template` "
                    "WHERE `class` = 15 AND `subclass` = 5 "
                    "ORDER BY RAND() LIMIT 1");

                if (!result)
                {
                    SendMessage(player, "Lucky Mount Chest: no mount item was found in item_template.");
                    return true;
                }

                uint32 itemId = result->Fetch()[0].Get<uint32>();
                if (!player->AddItem(itemId, 1))
                {
                    SendMessage(player, "Lucky Mount Chest: your inventory is full. Make room and try again.");
                    return true;
                }

                SendMessage(player, "Lucky Mount Chest opened: a random mount was added to your inventory.");
                rewardGranted = true;
                break;
            }
            default:
                return false;
        }

        if (rewardGranted)
            go->DespawnOrUnsummon();

        return true;
    }
};

void AddMountChestScripts()
{
    new MountChestPlayerScript();
    new MountChestGameObjectScript();
}
