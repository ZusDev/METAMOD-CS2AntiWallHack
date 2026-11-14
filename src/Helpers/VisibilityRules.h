#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace awh
{
    inline constexpr int PlayerLimit = 64;
    inline constexpr int EntityLimit = 16384;
    inline constexpr int TraceBudget = 4096;

    inline int OrderedSample( int order, int preferred )
    {
        return order == 0 ? preferred : order <= preferred ? order - 1 : order;
    }

    inline bool Occluded( float fraction, bool solid )
    {
        return !solid && std::isfinite( fraction ) && fraction > 0 && fraction < .99f;
    }

    inline bool CanHide( int tick, int deadline )
    {
        return tick > deadline;
    }

    // Controller m_bPawnIsAlive is a cached hint
    inline bool LivePawn( int health, uint8_t life, bool spawning, bool belongsToController )
    {
        return health > 0 && life == 0 && !spawning && belongsToController;
    }

    template <class CheckHidden> bool EvaluateHidden( int tick, int grace, int& deadline, CheckHidden check )
    {
        if ( !CanHide( tick, deadline ) )
        {
            return false;
        }

        if ( check() )
        {
            return true;
        }

        deadline = tick + grace;
        return false;
    }

    inline bool Withhold( uint32_t* primary, uint32_t* deletion, int index )
    {
        if ( !primary || !deletion || primary == deletion || index <= 0 || index >= EntityLimit )
        {
            return false;
        }

        auto bit = uint32_t( 1 ) << ( index & 31 );

        if ( !( primary[index >> 5] & bit ) )
        {
            return false;
        }

        deletion[index >> 5] |= bit;
        primary[index >> 5] &= ~bit;

        return true;
    }

    struct Settings
    {
        bool Enabled = true, SmokeOcclusion = true, Debug = false, AutoUpdate = true;
        bool FilterTeammates = false;
        int VisibleGraceTicks = 20;
        float BoundsPadding = 32;
        bool CornerPrediction = true;
        float CornerPredictionSeconds = .1f, CornerMaxDistance = 32;

        bool Valid() const
        {
            return VisibleGraceTicks >= 0 && VisibleGraceTicks <= 640 && std::isfinite( BoundsPadding ) && BoundsPadding >= 0 && BoundsPadding <= 256 &&
                   std::isfinite( CornerPredictionSeconds ) && CornerPredictionSeconds >= 0 && CornerPredictionSeconds <= .2f && std::isfinite( CornerMaxDistance ) &&
                   CornerMaxDistance >= 0 && CornerMaxDistance <= 64;
        }
    };
}
