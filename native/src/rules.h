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

    inline void InsertNearest( int* slots, float* distances, int& count, int capacity, int slot, float distance )
    {
        if ( !std::isfinite( distance ) || distance < 0 )
            return;

        int pos = 0;

        while ( pos < count && ( distances[pos] < distance || ( distances[pos] == distance && slots[pos] < slot ) ) )
            ++pos;

        if ( pos >= capacity )
            return;

        for ( int i = std::min( count, capacity - 1 ); i > pos; --i )
        {
            slots[i] = slots[i - 1];
            distances[i] = distances[i - 1];
        }

        slots[pos] = slot;
        distances[pos] = distance;
        count = std::min( count + 1, capacity );
    }

    inline bool Withhold( uint32_t* primary, uint32_t* deletion, int index )
    {
        if ( !primary || !deletion || primary == deletion || index <= 0 || index >= EntityLimit )
            return false;

        auto bit = uint32_t( 1 ) << ( index & 31 );

        if ( !( primary[index >> 5] & bit ) )
            return false;

        deletion[index >> 5] |= bit;
        primary[index >> 5] &= ~bit;

        return true;
    }

    struct Settings
    {
        bool Enabled = true;
        int NearestEnemies = 5, VisibleGraceTicks = 20;
        float BoundsPadding = 48, PredictionSeconds = .2f, AlwaysVisibleDistance = 160;
        bool SetDontTransmitToZero = true;

        bool Valid() const
        {
            return NearestEnemies >= 1 && NearestEnemies <= 63 && VisibleGraceTicks >= 0 && VisibleGraceTicks <= 640 && std::isfinite( BoundsPadding ) && BoundsPadding >= 0 && BoundsPadding <= 256 && std::isfinite( PredictionSeconds ) && PredictionSeconds >= 0 && PredictionSeconds <= 1 && std::isfinite( AlwaysVisibleDistance ) && AlwaysVisibleDistance >= 0 && AlwaysVisibleDistance <= 16384;
        }
    };
}
