#pragma once
#include <checktransmitinfo.h>
#include <playerslot.h>
#include <tier1/utlvector.h>
#include <cstddef>

// The second mask carries deletion deltas, it is not an always-transmit mask.
struct VisibilityInfo
{
    uint32 size;
    SpawnGroupHandle_t spawnGroup;
    CBitVec<4096> bits;
};

struct TransmitInfo
{
    CBitVec<16384>* entities;
    CBitVec<16384>* deletion;
    CBitVec<16384>* outOfPVS;
    CBitVec<16384>* always;
    CUtlVector<CPlayerSlot> targetSlots;
    VisibilityInfo visibility;
    CPlayerSlot slot;
    bool fullUpdate;
};

static_assert( sizeof( VisibilityInfo ) == 520 );
static_assert( sizeof( TransmitInfo ) == 584 );
static_assert( offsetof( TransmitInfo, slot ) == 0x240 );
static_assert( offsetof( TransmitInfo, fullUpdate ) == 0x244 );
