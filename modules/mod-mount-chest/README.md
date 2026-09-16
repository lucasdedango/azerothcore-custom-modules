# mod-mount-chest V2

AzerothCore WotLK module for a solo / small-group server.

## Behaviour

A fresh random roll is made independently for every XP-eligible creature kill in the open world:

- Lucky Copper Chest: 10% (1 in 10)
  - Gives a random 10-50 copper multiplied by the player's level.
- Lucky Silver Chest: 1% (1 in 100)
  - Gives a random 10-50 silver multiplied by the player's level.
- Lucky Mount Chest: 0.1% (1 in 1000)
  - Gives one random item from `item_template` class 15 / subclass 5 (mounts).
- No chest: 88.9%.

These are probabilities, not counters. A player can get two chests in a row or have a long dry streak.

Only kills for which `Acore::XP::Gain(player, creature)` is non-zero are eligible. Dungeon and raid maps are excluded.

## Interaction / ownership

All three chest templates are bound to `go_mount_chest_v2`. Right-click is handled by a `GameObjectScript`, so rewards do not depend on the normal chest loot state.

`MountChest.OwnerOnly = 1` means a summoned chest can only be opened by the player who spawned it. A manually spawned GM test chest has no owner and can still be opened.

After a successful reward the chest despawns immediately. If nobody opens it, the summoned game object expires after `MountChest.LifetimeSeconds` (default 120 seconds).

For a mount chest, if the player's inventory is full, the chest stays in the world so the player can make room and try again.

## Configuration

```ini
MountChest.Enable = 1
MountChest.OwnerOnly = 1
MountChest.LifetimeSeconds = 120
MountChest.MoneyMin = 10
MountChest.MoneyMax = 50
```

Docker environment equivalents used by AzerothCore:

```yaml
AC_MOUNT_CHEST_ENABLE: "1"
AC_MOUNT_CHEST_OWNER_ONLY: "1"
AC_MOUNT_CHEST_LIFETIME_SECONDS: "120"
AC_MOUNT_CHEST_MONEY_MIN: "10"
AC_MOUNT_CHEST_MONEY_MAX: "50"
```

The old V1 variable `AC_MOUNT_CHEST_CHANCE_PERCENT` is no longer used.

## SQL upgrade

`sql/install.sql` is idempotent for entries 900001-900003. It first deletes V1/V2 definitions from:

- `gameobject_loot_template`
- `gameobject_template_addon`
- `gameobject_template`

Then it recreates the three V2 gameobject templates. V2 does not use `gameobject_loot_template`; rewards are granted by the C++ script.
