# mod-dynamic-difficulty

Per-character SQL-persistent dynamic creature damage scaling for AzerothCore Playerbots.

Commands:
```text
.gamedifficulty
.gamedifficulty 0
.gamedifficulty 50
.gamedifficulty 100
.gamedifficulty reset
```

Example range 1.40 -> 2.00:
```text
0 = x1.40
50 = x1.70
100 = x2.00
```

The value is stored per character in `mod_dynamic_difficulty`.
In groups with multiple real players, the highest difficulty wins.
PlayerBots do not have their own difficulty, but their presence selects the `WithBots` config branch.

The module separately configures:
- outdoor / dungeon / raid
- with bots / without bots
- melee/direct damage / spell damage
- optional creature-rank extra multipliers

Periodic spell damage uses the SpellDamage range.

SQL migration:
`data/sql/db-characters/base/2026_09_10_00_mod_dynamic_difficulty.sql`

Docker install:
```powershell
cd C:\azerothcore-playerbots
docker stop ac-worldserver
```

Extract this ZIP so you get:
`C:\azerothcore-playerbots\modules\mod-dynamic-difficulty`

Then:
```powershell
cd C:\azerothcore-playerbots
docker compose --progress=plain build ac-worldserver
docker compose up -d --force-recreate ac-worldserver
docker compose logs -f ac-worldserver
```

Do not use `docker compose down -v`.

This source targets the verified Playerbot-branch UnitScript hooks:
`ModifyMeleeDamage`, `ModifySpellDamageTaken`, `ModifyPeriodicDamageAurasTick`.

Your local Docker build is still the definitive compile test.

## Debug build

Set:

```ini
DynamicDifficulty.Debug = 1
```

Then watch:

```powershell
docker compose logs -f ac-worldserver | Select-String "DynamicDifficulty DEBUG"
```

A physical hit should print `raw`, selected context/difficulty, multiplier, and `final`.
Disable debug after testing because it logs every matching hit.


## Debug chat build

With:

```ini
DynamicDifficulty.Debug = 1
```

every matching incoming creature hit is also printed directly in the affected real player's WoW chat:

```text
[DD DEBUG] MELEE mob=123 area=Outdoor bots=0 diff=100 raw=2 x1.700 => 3
```

This bypasses Docker/logger filtering and confirms whether the UnitScript hook fires and what value it writes.
Disable debug again after testing because this intentionally prints one chat line per matching hit.


## v1.2.1 debug-chat

Fixes in-game debug message formatting for AzerothCore's fmt-style `PSendSysMessage`.


## v1.3.0 - final damage hook

The previous build modified `ModifyMeleeDamage` / spell pre-processing hooks. Testing showed those
values were visible in debug but were not the final health damage actually applied to the player.

This build performs the scaling in `UnitScript::DealDamage`, whose returned value is the damage
passed through AzerothCore's final damage script chain.

Mapping:
- `DIRECT_DAMAGE` -> configured `Damage` range
- all other `DamageEffectType` values -> configured `SpellDamage` range

With `DynamicDifficulty.Debug = 1`, the affected player's chat shows:

```text
[DD FINAL] type=0 mob=15274 area=Outdoor bots=0 diff=100 raw=2 x1.700 => 3
```

The number after `=>` is the value returned by the final damage hook.
