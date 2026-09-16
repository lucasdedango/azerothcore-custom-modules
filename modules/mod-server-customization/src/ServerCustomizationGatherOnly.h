#pragma once

#include "DataMap.h"
#include "Define.h"
#include "ObjectGuid.h"

#include <unordered_map>

namespace ServerCustomizationGatherOnly
{
    inline constexpr char DataKey[] = "ServerCustomization.AutomationMode";

    enum class Mode : uint8
    {
        None = 0,
        Mining = 1,
        Herbalism = 2,
        Gathering = 3,
        Grind = 4
    };

    inline bool IsGatherMode(Mode mode)
    {
        return mode == Mode::Mining || mode == Mode::Herbalism || mode == Mode::Gathering;
    }

    class State : public DataMap::Base
    {
    public:
        Mode mode = Mode::None;
        ObjectGuid::LowType targetSpawnId = 0;
        float targetX = 0.0f;
        float targetY = 0.0f;
        float targetZ = 0.0f;
        uint32 updateTimerMs = 0;
        uint32 targetAgeMs = 0;
        uint32 mountWaitMs = 0;
        uint32 recentDamageMs = 0;
        uint32 grindCreatureEntry = 0;
        bool restHold = false;
        uint32 recentFallMs = 0;
        bool suppressMobLoot = false;
        bool mountSuppressedNearTarget = false;
        float lastApproachDistance = 0.0f;
        uint32 stuckChecks = 0;
        bool routeAvoidanceEnable = true;
        float routeAvoidanceBuffer = 8.0f;
        float routeAvoidanceWeight = 60.0f;
        float routeAvoidanceEliteMultiplier = 2.5f;
        bool mountGraceUsed = false;
        bool ignoreMountedAggroActive = false;
        bool combatPaused = false;
        std::unordered_map<ObjectGuid::LowType, uint32> blacklistMs;
    };
}
