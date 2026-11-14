#pragma once
#include "Models/PlayerSnapshot.h"

namespace awh
{
    class SightFilter final : public CTraceFilter
    {
        const Snapshot& viewer;
        const Snapshot& target;

      public:
        SightFilter( const Snapshot& a, const Snapshot& b ) : CTraceFilter( uint64( 1 ) << LAYER_INDEX_CONTENTS_SOLID ), viewer( a ), target( b )
        {
            m_nInteractsExclude = ( uint64( 1 ) << LAYER_INDEX_CONTENTS_PLAYER ) | ( uint64( 1 ) << LAYER_INDEX_CONTENTS_HITBOX ) | ( uint64( 1 ) << LAYER_INDEX_CONTENTS_TRIGGER ) | ( uint64( 1 ) << LAYER_INDEX_CONTENTS_PLAYER_CLIP ) | ( uint64( 1 ) << LAYER_INDEX_CONTENTS_NPC_CLIP ) | ( uint64( 1 ) << LAYER_INDEX_CONTENTS_WINDOW ) | ( uint64( 1 ) << LAYER_INDEX_CONTENTS_CARRIED_OBJECT ) | ( uint64( 1 ) << LAYER_INDEX_CONTENTS_CARRIED_WEAPON );
            SetPassEntity1( a.pawn );
            SetPassEntity2( b.pawn );
            SetPassEntityOwner1( a.pawn );
            SetPassEntityOwner2( b.pawn );
        }

        bool ShouldHitEntity( CEntityInstance* e ) override
        {
            if ( !e )
            {
                return true; // world geometry
            }

            if ( !e->m_pEntity || IsClass( e, "CBasePlayerPawn" ) || IsClass( e, "CBasePlayerController" ) || IsClass( e, "CEconEntity" ) )
            {
                return false;
            }

            auto handle = e->GetRefEHandle();

            return std::find( viewer.entities.begin(), viewer.entities.end(), handle ) == viewer.entities.end() && std::find( target.entities.begin(), target.entities.end(), handle ) == target.entities.end();
        }
    };
}
