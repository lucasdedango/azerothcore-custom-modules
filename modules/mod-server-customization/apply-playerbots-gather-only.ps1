param(
    [string]$RepoRoot = "C:\azerothcore-playerbots"
)

$ErrorActionPreference = "Stop"

$playerbotsRoot = Join-Path $RepoRoot "modules\mod-playerbots"
$sharedHeaderSource = Join-Path $RepoRoot "modules\mod-server-customization\src\ServerCustomizationGatherOnly.h"
$sharedHeaderTarget = Join-Path $playerbotsRoot "src\ServerCustomizationGatherOnly.h"
$grindPath = Join-Path $playerbotsRoot "src\Ai\Base\Value\GrindTargetValue.cpp"
$lootPath = Join-Path $playerbotsRoot "src\Ai\Base\Actions\AddLootAction.cpp"
$mountPath = Join-Path $playerbotsRoot "src\Ai\Base\Actions\CheckMountStateAction.cpp"
$openLootPath = Join-Path $playerbotsRoot "src\Ai\Base\Actions\LootAction.cpp"

foreach ($path in @($sharedHeaderSource, $grindPath, $lootPath, $mountPath, $openLootPath)) {
    if (!(Test-Path $path)) {
        throw "Required file not found: $path"
    }
}

Copy-Item -LiteralPath $sharedHeaderSource -Destination $sharedHeaderTarget -Force
Write-Host "Installed shared automation state header in mod-playerbots." -ForegroundColor Green

# -----------------------------------------------------------------------------
# GrindTargetValue
# Attackers are returned first by native Playerbots. For directed gather modes,
# stop immediately after that attacker loop so the bot NEVER selects a nearby mob
# voluntarily. Grind mode must remain native and is therefore NOT blocked.
# -----------------------------------------------------------------------------
$grind = Get-Content -Raw -LiteralPath $grindPath

if (!$grind.Contains('#include "ServerCustomizationGatherOnly.h"')) {
    $anchor = @'
#include "GrindTargetValue.h"
#include "NewRpgInfo.h"
'@
    if (!$grind.Contains($anchor)) {
        throw "Expected include block not found in GrindTargetValue.cpp. Patch aborted."
    }
    $grind = $grind.Replace($anchor, @'
#include "GrindTargetValue.h"
#include "NewRpgInfo.h"
#include "ServerCustomizationGatherOnly.h"
'@)
}


# v2.1 route-risk includes for targeted grind.
foreach ($includePair in @(
    @('#include "GrindTargetValue.h"', "#include `"GrindTargetValue.h`"`r`n#include `"CellImpl.h`""),
    @('#include "NewRpgInfo.h"', "#include `"NewRpgInfo.h`"`r`n#include `"GridNotifiers.h`"`r`n#include `"PathGenerator.h`"")
)) {
    $needle = $includePair[0]
    $replacement = $includePair[1]
    if ($grind.Contains($needle) -and !$grind.Contains($replacement)) {
        $grind = $grind.Replace($needle, $replacement)
    }
}

$oldV15 = @'
    // ServerCustomization gather-only mode deliberately stops target selection here.
    // The attacker loop above remains untouched, so mobs that actually aggro the
    // character are still fought normally; nearby neutral/hostile mobs are ignored.
    if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
    {
        if (gatherState->mode != ServerCustomizationGatherOnly::Mode::None)
            return nullptr;
    }

'@

$newBlock = @'
    // ServerCustomization directed-gather mode deliberately stops voluntary target
    // selection here. Custom Grind remains native, with an optional creature-entry filter.
    if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
    {
        if (ServerCustomizationGatherOnly::IsGatherMode(gatherState->mode))
            return nullptr;
    }

'@

if ($grind.Contains($oldV15)) {
    $grind = $grind.Replace($oldV15, $newBlock)
    Write-Host "Upgraded v1.5 gather combat restriction to v1.6." -ForegroundColor Green
}
elseif (!$grind.Contains($newBlock)) {
    $anchor = @'
    GuidVector targets = *context->GetValue<GuidVector>("possible targets");
    if (targets.empty())
        return nullptr;
'@
    if (!$grind.Contains($anchor)) {
        throw "Expected GrindTargetValue target block not found. mod-playerbots may have changed; patch aborted."
    }
    $grind = $grind.Replace($anchor, $newBlock + $anchor)
    Write-Host "Applied directed-gather combat targeting patch." -ForegroundColor Green
}
else {
    Write-Host "Directed-gather combat targeting patch is already current."
}





$restAnchor = @'
    // ServerCustomization directed-gather mode deliberately stops voluntary target
'@

$restBlock = @'
    // During automation recovery, attackers above have already had first priority,
    // but no new voluntary grind/gather target may be selected until HP/mana recover.
    if (auto* recoveryState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
    {
        if (recoveryState->restHold)
            return nullptr;
    }

    // ServerCustomization directed-gather mode deliberately stops voluntary target
'@

if ($grind.Contains($restAnchor) -and !$grind.Contains("recoveryState->restHold")) {
    $grind = $grind.Replace($restAnchor, $restBlock)
    Write-Host "Applied automation recovery target hold." -ForegroundColor Green
}
elseif ($grind.Contains("recoveryState->restHold")) {
    Write-Host "Automation recovery target hold is already current."
}
else {
    throw "Could not place automation recovery target hold in GrindTargetValue.cpp."
}


if (!$grind.Contains("#include <limits>")) {
    $firstFunction = "Unit* GrindTargetValue::Calculate()"
    $pos = $grind.IndexOf($firstFunction)
    if ($pos -lt 0) { throw "Could not place GrindTargetValue standard includes." }
    $grind = $grind.Insert($pos, "#include <limits>`r`n#include <list>`r`n#include <vector>`r`n`r`n")
}

$grindRiskHelperAnchor = @'
Unit* GrindTargetValue::Calculate()
'@

$grindRiskHelper = @'
namespace
{
    struct ServerCustomizationGrindHazard
    {
        Creature* creature = nullptr;
        float dangerRadius = 0.0f;
        float severity = 1.0f;
    };

    static std::vector<ServerCustomizationGrindHazard> CollectServerCustomizationGrindHazards(
        Player* bot, uint32 ignoreEntry, float scanRadius,
        float buffer, float eliteMultiplier)
    {
        std::vector<ServerCustomizationGrindHazard> hazards;
        if (!bot || scanRadius <= 0.0f)
            return hazards;

        std::list<Unit*> units;
        Acore::AnyUnfriendlyUnitInObjectRangeCheck check(bot, bot, scanRadius);
        Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(bot, units, check);
        Cell::VisitObjects(bot, searcher, scanRadius);

        for (Unit* unit : units)
        {
            Creature* creature = unit ? unit->ToCreature() : nullptr;
            if (!creature || !creature->IsAlive() || creature->IsDuringRemoveFromWorld())
                continue;

            if (ignoreEntry && creature->GetEntry() == ignoreEntry)
                continue;

            if (!creature->CanCreatureAttack(bot, true))
                continue;

            float aggro = std::max(0.0f, creature->GetAggroRange(bot));
            if (aggro <= 0.0f)
                continue;

            float severity = 1.0f;
            int32 levelDelta = int32(creature->GetLevel()) - int32(bot->GetLevel());
            if (levelDelta > 0)
                severity += std::min(2.0f, float(levelDelta) * 0.20f);

            if (creature->isElite() || creature->isWorldBoss())
                severity *= std::max(1.0f, eliteMultiplier);

            hazards.push_back({ creature, aggro + std::max(0.0f, buffer), severity });
        }

        return hazards;
    }

    static float ServerCustomizationTargetRouteScore(
        Player* bot, Unit* target,
        ServerCustomizationGatherOnly::State const* state)
    {
        if (!bot || !target || !state || !state->routeAvoidanceEnable)
            return bot && target ? bot->GetDistance(target) : 0.0f;

        PathGenerator path(bot);
        if (!path.CalculatePath(target->GetPositionX(), target->GetPositionY(), target->GetPositionZ(), false) ||
            path.GetPathType() != PATHFIND_NORMAL ||
            path.GetPath().empty())
            return std::numeric_limits<float>::max();

        float pathLength = path.getPathLength();
        float scanRadius = std::min(300.0f, std::max(60.0f, pathLength + 30.0f));
        auto hazards = CollectServerCustomizationGrindHazards(
            bot, state->grindCreatureEntry, scanRadius,
            state->routeAvoidanceBuffer, state->routeAvoidanceEliteMultiplier);

        float risk = 0.0f;
        Movement::PointsArray const& points = path.GetPath();

        for (ServerCustomizationGrindHazard const& hazard : hazards)
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
            risk += state->routeAvoidanceWeight * hazard.severity * (1.0f + penetration);
        }

        return pathLength + risk;
    }
}

Unit* GrindTargetValue::Calculate()
'@

if ($grind.Contains($grindRiskHelperAnchor) -and !$grind.Contains("ServerCustomizationTargetRouteScore(")) {
    $grind = $grind.Replace($grindRiskHelperAnchor, $grindRiskHelper)
    Write-Host "Applied .grindtarget route-risk helper." -ForegroundColor Green
}
elseif ($grind.Contains("ServerCustomizationTargetRouteScore(")) {
    Write-Host ".grindtarget route-risk helper is already current."
}
else {
    throw "Could not locate GrindTargetValue::Calculate for route-risk helper."
}


$nativeAttackerBlock = @'
    GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || !unit->IsAlive())
            continue;

        return unit;
    }

'@

$mountedIgnoreAttackerBlock = @'
    bool ignoreMountedGatherAggro = false;
    if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
        ignoreMountedGatherAggro = ServerCustomizationGatherOnly::IsGatherMode(gatherState->mode) &&
                                   gatherState->ignoreMountedAggroActive;

    if (!ignoreMountedGatherAggro)
    {
        GuidVector attackers = context->GetValue<GuidVector>("attackers")->Get();
        for (ObjectGuid const guid : attackers)
        {
            Unit* unit = botAI->GetUnit(guid);
            if (!unit || !unit->IsAlive())
                continue;

            return unit;
        }
    }

'@

if ($grind.Contains($nativeAttackerBlock)) {
    $grind = $grind.Replace($nativeAttackerBlock, $mountedIgnoreAttackerBlock)
    Write-Host "Applied mounted gather aggro-ignore patch." -ForegroundColor Green
}
elseif ($grind.Contains($mountedIgnoreAttackerBlock)) {
    Write-Host "Mounted gather aggro-ignore patch is already current."
}
else {
    throw "Expected GrindTargetValue attacker block not found. Patch aborted."
}

$targetLoopAnchor = @'
    for (ObjectGuid const guid : targets)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

'@

$targetedLoopBlock = @'
    for (ObjectGuid const guid : targets)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        // .grindtarget: attackers were handled above, but voluntary grind targets must
        // match the selected creature entry. If none are nearby, return no grind target
        // instead of attacking unrelated mobs.
        bool customGrindTarget = false;
        if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
        {
            if (gatherState->mode == ServerCustomizationGatherOnly::Mode::Grind)
            {
                customGrindTarget = true;
                if (gatherState->grindCreatureEntry != 0 && unit->GetEntry() != gatherState->grindCreatureEntry)
                    continue;
            }
        }

'@

$v26TargetedLoopBlock = @'
    for (ObjectGuid const guid : targets)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        // .grindtarget: attackers were handled above, but voluntary grind targets must
        // match the selected creature entry. If none are nearby, return no grind target
        // instead of attacking unrelated mobs.
        bool configuredGrindTarget = false;
        if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
        {
            if (gatherState->mode == ServerCustomizationGatherOnly::Mode::Grind && gatherState->grindCreatureEntry != 0)
            {
                configuredGrindTarget = unit->GetEntry() == gatherState->grindCreatureEntry;
                if (!configuredGrindTarget)
                    continue;
            }
        }

'@

$v25TargetedLoopBlock = @'
    for (ObjectGuid const guid : targets)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit)
            continue;

        // .grindtarget: attackers were handled above, but voluntary grind targets must
        // match the selected creature entry. If none are nearby, return no grind target
        // instead of attacking unrelated mobs.
        if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
        {
            if (gatherState->mode == ServerCustomizationGatherOnly::Mode::Grind &&
                gatherState->grindCreatureEntry != 0 &&
                unit->GetEntry() != gatherState->grindCreatureEntry)
                continue;
        }

'@

if ($grind.Contains($targetedLoopBlock)) {
    Write-Host "Custom grind creature-entry filter is already current."
}
elseif ($grind.Contains($v26TargetedLoopBlock)) {
    $grind = $grind.Replace($v26TargetedLoopBlock, $targetedLoopBlock)
    Write-Host "Upgraded custom grind filter to allow grey mobs in both .grind and .grindtarget." -ForegroundColor Green
}
elseif ($grind.Contains($v25TargetedLoopBlock)) {
    $grind = $grind.Replace($v25TargetedLoopBlock, $targetedLoopBlock)
    Write-Host "Upgraded custom grind filter to allow grey mobs in both .grind and .grindtarget." -ForegroundColor Green
}
elseif ($grind.Contains($targetLoopAnchor)) {
    $grind = $grind.Replace($targetLoopAnchor, $targetedLoopBlock)
    Write-Host "Applied .grindtarget creature-entry filter." -ForegroundColor Green
}
else {
    throw "Expected GrindTargetValue target loop not found. Patch aborted."
}

$nativeXpTargetCheck = @'
        if (!bot->isHonorOrXPTarget(unit))
            continue;
'@

$customGrindXpTargetCheck = @'
        // Custom .grind and .grindtarget may intentionally attack grey creatures.
        // Keep native XP/honor suitability outside these explicit automation modes.
        if (!customGrindTarget && !bot->isHonorOrXPTarget(unit))
            continue;
'@

$v26XpTargetCheck = @'
        // A creature explicitly chosen with .grindtarget may be grey to the player.
        // Keep native XP/honor suitability for normal grind, but do not silently reject
        // the requested entry solely because it no longer grants experience.
        if (!configuredGrindTarget && !bot->isHonorOrXPTarget(unit))
            continue;
'@

if ($grind.Contains($v26XpTargetCheck)) {
    $grind = $grind.Replace($v26XpTargetCheck, $customGrindXpTargetCheck)
    Write-Host "Upgraded grey-creature selection override for .grind and .grindtarget." -ForegroundColor Green
}
elseif ($grind.Contains($nativeXpTargetCheck)) {
    $grind = $grind.Replace($nativeXpTargetCheck, $customGrindXpTargetCheck)
    Write-Host "Applied grey-creature selection override for .grind and .grindtarget." -ForegroundColor Green
}
elseif ($grind.Contains($customGrindXpTargetCheck)) {
    Write-Host "Custom grind grey-creature selection override is already current."
}
else {
    throw "Expected GrindTargetValue XP/honor target check not found. Patch aborted."
}

$nativeDistanceBlock = @'
        else
        {
            float newdistance = bot->GetDistance(unit);
            if (!result || (newdistance < distance))
            {
                distance = newdistance;
                result = unit;
            }
        }
'@

$riskDistanceBlock = @'
        else
        {
            float newdistance = bot->GetDistance(unit);

            if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
            {
                if (gatherState->mode == ServerCustomizationGatherOnly::Mode::Grind &&
                    gatherState->grindCreatureEntry != 0 &&
                    gatherState->routeAvoidanceEnable)
                    newdistance = ServerCustomizationTargetRouteScore(bot, unit, gatherState);
            }

            if (!result || (newdistance < distance))
            {
                distance = newdistance;
                result = unit;
            }
        }
'@

if ($grind.Contains($nativeDistanceBlock)) {
    $grind = $grind.Replace($nativeDistanceBlock, $riskDistanceBlock)
    Write-Host "Applied risk-aware .grindtarget target selection." -ForegroundColor Green
}
elseif ($grind.Contains($riskDistanceBlock)) {
    Write-Host "Risk-aware .grindtarget target selection is already current."
}
else {
    throw "Expected GrindTargetValue distance-selection block not found. Patch aborted."
}


Set-Content -LiteralPath $grindPath -Value $grind -NoNewline

# -----------------------------------------------------------------------------
# AddLootAction
# While one of the directed gather modes is active, generic `loot` is prevented
# from enqueueing corpses/unrelated objects and .mine/.herb stay strictly filtered.
# Grind mode is not filtered.
# -----------------------------------------------------------------------------
$loot = Get-Content -Raw -LiteralPath $lootPath

if (!$loot.Contains('#include "ServerCustomizationGatherOnly.h"')) {
    $anchor = @'
#include "LootObjectStack.h"
#include "Playerbots.h"
'@
    if (!$loot.Contains($anchor)) {
        throw "Expected include block not found in AddLootAction.cpp. Patch aborted."
    }
    $loot = $loot.Replace($anchor, @'
#include "LootObjectStack.h"
#include "Playerbots.h"
#include "ServerCustomizationGatherOnly.h"
'@)
}

$nativeAllLoot = @'
bool AddAllLootAction::AddLoot(ObjectGuid guid) { return AI_VALUE(LootObjectStack*, "available loot")->Add(guid); }
'@

$v15AllLoot = @'
bool AddAllLootAction::AddLoot(ObjectGuid guid)
{
    if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
    {
        if (gatherState->mode != ServerCustomizationGatherOnly::Mode::None)
        {
            LootObject loot(bot, guid);
            WorldObject* wo = loot.GetWorldObject(bot);
            if (loot.IsEmpty() || !wo || loot.skillId == SKILL_NONE || !loot.IsLootPossible(bot))
                return false;

            if (gatherState->mode == ServerCustomizationGatherOnly::Mode::Mining && loot.skillId != SKILL_MINING)
                return false;

            if (gatherState->mode == ServerCustomizationGatherOnly::Mode::Herbalism && loot.skillId != SKILL_HERBALISM)
                return false;
        }
    }

    return AI_VALUE(LootObjectStack*, "available loot")->Add(guid);
}
'@

$currentAllLoot = @'
bool AddAllLootAction::AddLoot(ObjectGuid guid)
{
    if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
    {
        if (ServerCustomizationGatherOnly::IsGatherMode(gatherState->mode))
        {
            LootObject loot(bot, guid);
            WorldObject* wo = loot.GetWorldObject(bot);
            if (loot.IsEmpty() || !wo || loot.skillId == SKILL_NONE || !loot.IsLootPossible(bot))
                return false;

            if (gatherState->mode == ServerCustomizationGatherOnly::Mode::Mining && loot.skillId != SKILL_MINING)
                return false;

            if (gatherState->mode == ServerCustomizationGatherOnly::Mode::Herbalism && loot.skillId != SKILL_HERBALISM)
                return false;

            if (gatherState->mode == ServerCustomizationGatherOnly::Mode::Gathering &&
                loot.skillId != SKILL_MINING && loot.skillId != SKILL_HERBALISM)
                return false;
        }
    }

    return AI_VALUE(LootObjectStack*, "available loot")->Add(guid);
}
'@

if ($loot.Contains($v15AllLoot)) {
    $loot = $loot.Replace($v15AllLoot, $currentAllLoot)
    Write-Host "Upgraded v1.5 generic loot filter to v1.6." -ForegroundColor Green
}
elseif ($loot.Contains($nativeAllLoot)) {
    $loot = $loot.Replace($nativeAllLoot, $currentAllLoot)
    Write-Host "Applied directed-gather generic loot filter." -ForegroundColor Green
}
elseif ($loot.Contains($currentAllLoot)) {
    Write-Host "Directed-gather generic loot filter is already current."
}
else {
    throw "Expected AddAllLootAction::AddLoot block not found. mod-playerbots may have changed; patch aborted."
}

$nativeGatherBlock = @'
    if (loot.skillId == SKILL_NONE)
        return false;

    if (!loot.IsLootPossible(bot))
        return false;
'@

$v15GatherBlock = @'
    if (loot.skillId == SKILL_NONE)
        return false;

    // Strict gather-only filtering requested by mod-server-customization.
    // Mining mode accepts only Mining nodes; Herbalism mode accepts only Herb nodes.
    // Gathering mode leaves native skill-based gathering untouched.
    if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
    {
        if (gatherState->mode == ServerCustomizationGatherOnly::Mode::Mining && loot.skillId != SKILL_MINING)
            return false;

        if (gatherState->mode == ServerCustomizationGatherOnly::Mode::Herbalism && loot.skillId != SKILL_HERBALISM)
            return false;
    }

    if (!loot.IsLootPossible(bot))
        return false;
'@

$currentGatherBlock = @'
    if (loot.skillId == SKILL_NONE)
        return false;

    // Strict directed-gather filtering requested by mod-server-customization.
    if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
    {
        if (gatherState->mode == ServerCustomizationGatherOnly::Mode::Mining && loot.skillId != SKILL_MINING)
            return false;

        if (gatherState->mode == ServerCustomizationGatherOnly::Mode::Herbalism && loot.skillId != SKILL_HERBALISM)
            return false;

        if (gatherState->mode == ServerCustomizationGatherOnly::Mode::Gathering &&
            loot.skillId != SKILL_MINING && loot.skillId != SKILL_HERBALISM)
            return false;
    }

    if (!loot.IsLootPossible(bot))
        return false;
'@

if ($loot.Contains($v15GatherBlock)) {
    $loot = $loot.Replace($v15GatherBlock, $currentGatherBlock)
    Write-Host "Upgraded v1.5 strict gather filter to v1.6." -ForegroundColor Green
}
elseif ($loot.Contains($nativeGatherBlock)) {
    $loot = $loot.Replace($nativeGatherBlock, $currentGatherBlock)
    Write-Host "Applied strict Mining/Herbalism gather filter." -ForegroundColor Green
}
elseif ($loot.Contains($currentGatherBlock)) {
    Write-Host "Strict gather filter is already current."
}
else {
    throw "Expected AddGatheringLootAction block not found. mod-playerbots may have changed; patch aborted."
}

Set-Content -LiteralPath $lootPath -Value $loot -NoNewline

# -----------------------------------------------------------------------------
# CheckMountStateAction
# While directed gathering is intentionally riding through a simple aggro with
# no received damage, keep Playerbots from voluntarily dismounting.
# -----------------------------------------------------------------------------
$mount = Get-Content -Raw -LiteralPath $mountPath

$oldMountUsefulV22 = @'
bool CheckMountStateAction::isUseful()
{
    // During directed gathering, a mounted character may intentionally ride through
'@

$newMountUsefulV23 = @'
bool CheckMountStateAction::isUseful()
{
    // Close to the final custom approach point, the route controller owns mount state.
    // Returning false here prevents the native mount strategy from remounting forever
    // underneath an inaccessible/cliff-side node.
    if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
    {
        if (ServerCustomizationGatherOnly::IsGatherMode(gatherState->mode) &&
            gatherState->mountSuppressedNearTarget)
            return false;
    }

    // During directed gathering, a mounted character may intentionally ride through
'@

if ($mount.Contains($oldMountUsefulV22) -and !$mount.Contains("gatherState->mountSuppressedNearTarget")) {
    $mount = $mount.Replace($oldMountUsefulV22, $newMountUsefulV23)
    Write-Host "Upgraded mount near-target suppression to v2.3." -ForegroundColor Green
}


if (!$mount.Contains('#include "ServerCustomizationGatherOnly.h"')) {
    $anchor = @'
#include "ServerFacade.h"
#include "SpellAuraEffects.h"
'@
    if (!$mount.Contains($anchor)) {
        throw "Expected include block not found in CheckMountStateAction.cpp. Patch aborted."
    }
    $mount = $mount.Replace($anchor, @'
#include "ServerFacade.h"
#include "SpellAuraEffects.h"
#include "ServerCustomizationGatherOnly.h"
'@)
}

$mountUsefulAnchor = @'
bool CheckMountStateAction::isUseful()
{
    // Not useful when:
'@

$mountUsefulPatched = @'
bool CheckMountStateAction::isUseful()
{
    // Close to the final custom approach point, the route controller owns mount state.
    // Returning false here prevents the native mount strategy from remounting forever
    // underneath an inaccessible/cliff-side node.
    if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
    {
        if (ServerCustomizationGatherOnly::IsGatherMode(gatherState->mode) &&
            gatherState->mountSuppressedNearTarget)
            return false;
    }

    // During directed gathering, a mounted character may intentionally ride through
    // simple aggro until actual damage/control/dismount occurs.
    if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
    {
        if (ServerCustomizationGatherOnly::IsGatherMode(gatherState->mode) &&
            gatherState->ignoreMountedAggroActive && bot->IsMounted())
            return false;
    }

    // Not useful when:
'@

if ($mount.Contains($mountUsefulAnchor)) {
    $mount = $mount.Replace($mountUsefulAnchor, $mountUsefulPatched)
    Write-Host "Applied mounted gather keep-riding patch." -ForegroundColor Green
}
elseif ($mount.Contains($mountUsefulPatched)) {
    Write-Host "Mounted gather keep-riding patch is already current."
}
else {
    throw "Expected CheckMountStateAction::isUseful block not found. Patch aborted."
}


# v2.2: native Playerbots normally refuses ground mounts below its configured minimum
# level. Directed gather may intentionally run on a character that already KNOWS a
# usable mount and Riding earlier than that setting, so bypass only this level gate
# while one of our gather modes is active.
$nativeMountLevelGate = @'
    // Not useful when level lower than minimum required
    if (bot->GetLevel() < sPlayerbotAIConfig.useGroundMountAtMinLevel)
        return false;
'@

$customMountLevelGate = @'
    // Native minimum mount level, except directed gather: if the character knows a
    // mount already, Mount() will perform the normal spell/item usability checks.
    bool directedGatherMount = false;
    if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
        directedGatherMount = ServerCustomizationGatherOnly::IsGatherMode(gatherState->mode);

    if (bot->GetLevel() < sPlayerbotAIConfig.useGroundMountAtMinLevel && !directedGatherMount)
        return false;
'@

if ($mount.Contains($nativeMountLevelGate)) {
    $mount = $mount.Replace($nativeMountLevelGate, $customMountLevelGate)
    Write-Host "Applied directed-gather mount-level bypass." -ForegroundColor Green
}
elseif ($mount.Contains($customMountLevelGate)) {
    Write-Host "Directed-gather mount-level bypass is already current."
}
else {
    throw "Expected CheckMountStateAction minimum-level block not found. Patch aborted."
}


Set-Content -LiteralPath $mountPath -Value $mount -NoNewline

# -----------------------------------------------------------------------------
# LootAction / StoreLootAction
# Directed resource gathering should take the contents of the mining/herb loot
# window even when the normal Playerbots item-usage filter would skip them.
# StoreLootAction already queues CMSG_AUTOSTORE_LOOT_ITEM and CMSG_LOOT_RELEASE;
# this patch only removes that inappropriate filter for directed gather.
# -----------------------------------------------------------------------------
$openLoot = Get-Content -Raw -LiteralPath $openLootPath

if (!$openLoot.Contains('#include "ServerCustomizationGatherOnly.h"')) {
    $includeAnchor = '#include "ServerFacade.h"'
    if (!$openLoot.Contains($includeAnchor)) {
        throw "Expected ServerFacade include not found in LootAction.cpp. Patch aborted."
    }
    $openLoot = $openLoot.Replace($includeAnchor, $includeAnchor + "`r`n" + '#include "ServerCustomizationGatherOnly.h"')
}


$lootExecuteAnchor = @'
    if (lootObject.guid.IsGameObject() &&
'@

$lootExecuteNoMob = @'
    // Optional .mine/.herb/.gather noloot: combat still works normally, but corpses
    // killed on the way are removed from Playerbots' available-loot queue. Resource
    // GameObjects are untouched and continue through the normal gather/store pipeline.
    if (lootObject.guid.IsCreature())
    {
        if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
        {
            if (ServerCustomizationGatherOnly::IsGatherMode(gatherState->mode) &&
                gatherState->suppressMobLoot)
            {
                AI_VALUE(LootObjectStack*, "available loot")->Remove(lootObject.guid);
                context->GetValue<LootObject>("loot target")->Set(LootObject());
                return true;
            }
        }
    }

    if (lootObject.guid.IsGameObject() &&
'@

if ($openLoot.Contains($lootExecuteAnchor) -and !$openLoot.Contains("gatherState->suppressMobLoot")) {
    $openLoot = $openLoot.Replace($lootExecuteAnchor, $lootExecuteNoMob)
    Write-Host "Applied optional gather no-mob-loot filter." -ForegroundColor Green
}
elseif ($openLoot.Contains("gatherState->suppressMobLoot")) {
    Write-Host "Optional gather no-mob-loot filter is already current."
}
else {
    throw "Could not place optional gather no-mob-loot filter in LootAction.cpp."
}


$nativeStoreFilter = @'
        if (loot_type != LOOT_SKINNING && !IsLootAllowed(itemid, botAI))
            continue;
'@

$gatherStoreFilter = @'
        bool directedGatherLoot = false;
        if (auto* gatherState = bot->CustomData.Get<ServerCustomizationGatherOnly::State>(ServerCustomizationGatherOnly::DataKey))
            directedGatherLoot = ServerCustomizationGatherOnly::IsGatherMode(gatherState->mode);

        if (loot_type != LOOT_SKINNING && !directedGatherLoot && !IsLootAllowed(itemid, botAI))
            continue;
'@

if ($openLoot.Contains($nativeStoreFilter)) {
    $openLoot = $openLoot.Replace($nativeStoreFilter, $gatherStoreFilter)
    Write-Host "Applied directed-gather loot-store bypass." -ForegroundColor Green
}
elseif ($openLoot.Contains($gatherStoreFilter)) {
    Write-Host "Directed-gather loot-store bypass is already current."
}
else {
    throw "Expected StoreLootAction item filter not found. Patch aborted."
}

Set-Content -LiteralPath $openLootPath -Value $openLoot -NoNewline


Write-Host "Playerbots directed-gather patch complete. Rebuild ac-worldserver before testing." -ForegroundColor Green
