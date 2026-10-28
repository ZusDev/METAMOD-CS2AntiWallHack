#include "Plugin.h"

void AntiWallHack::Reset()
{
    frameTick = -1;
    traces = 0;
    std::memset( deadlines, 0, sizeof( deadlines ) );
    std::memset( preferred, 0, sizeof( preferred ) );
    identities.fill( {} );
    evaluated.fill( false );
    pairVisibility.Reset();

    for ( auto& p : players )
    {
        p.Reset();
    }

    for ( auto& row : hidden )
    {
        row.clear();
    }
}

bool AntiWallHack::CollectChildren( void* node, Snapshot& snapshot, AttachmentTraversal& seen, int depth )
{
    while ( node )
    {
        if ( depth > 16 || !seen.Insert( node ) )
        {
            return false;
        }

        auto* entity = Field<CEntityInstance*>( node, offsets.nodeOwner );

        if ( !entity || !entity->m_pEntity )
        {
            return false;
        }

        if ( IsClass( entity, "CBasePlayerPawn" ) || IsClass( entity, "CBasePlayerController" ) )
        {
            return false;
        }

        snapshot.entities.push_back( entity->GetRefEHandle() );

        if ( !CollectChildren( Field<void*>( node, offsets.child ), snapshot, seen, depth + 1 ) )
        {
            return false;
        }

        node = Field<void*>( node, offsets.sibling );
    }

    return true;
}

bool AntiWallHack::RoundActive()
{
    for ( auto* id = entitySystem->m_EntityList.m_pFirstActiveEntity; id; id = id->m_pNext )
    {
        auto* e = id->m_pInstance;

        if ( !e || !IsClass( e, "CCSGameRulesProxy" ) )
        {
            continue;
        }

        auto* rules = Field<void*>( e, offsets.rules );

        return rules && !Field<bool>( rules, offsets.warmup ) && !Field<bool>( rules, offsets.freeze ) && Field<int>( rules, offsets.winner ) == 0;
    }

    return false;
}

void AntiWallHack::BeginFrame( int tick, float now )
{
    if ( frameTick == tick )
    {
        return;
    }

    if ( tick < frameTick )
    {
        Reset();
        smokeExplosions.Reset();
    }

    frameTick = tick;
    frameTime = now;
    traces = 0;
    smokeCaptured = false;
    smokeVolumes.clear();
    smokeCellBudget = 262144;

    evaluated.fill( false );
    pairVisibility.Reset();

    for ( auto& row : hidden )
    {
        row.clear();
    }

    for ( int slot = 0; slot < 64; ++slot )
    {
        auto& p = players[slot];
        p.Reset();
        auto* controller = EntityAt( entitySystem, CEntityIndex( slot + 1 ) );

        if ( !controller || !IsClass( controller, "CCSPlayerController" ) )
        {
            identities[slot] = {};
            continue;
        }

        p.team = Field<uint8>( controller, offsets.team );
        p.controllerHandle = controller->GetRefEHandle().ToInt();

        if ( Field<int>( controller, offsets.connected ) != 0 )
        {
            identities[slot] = {};
            continue;
        }

        auto handle = Field<CEntityHandle>( controller, offsets.pawn );
        auto* pawn = EntityAt( entitySystem, handle );

        if ( !pawn || !IsClass( pawn, "CCSPlayerPawn" ) )
        {
            identities[slot] = {};
            continue;
        }

        p.pawnHandle = handle.ToInt();
        p.health = Field<int>( pawn, offsets.health );
        p.life = Field<uint8>( pawn, offsets.life );
        bool spawning = Field<bool>( pawn, offsets.spawning );
        bool belongs = Field<CEntityHandle>( pawn, offsets.pawnController ) == controller->GetRefEHandle();

        if ( !LivePawn( p.health, uint8_t( p.life ), spawning, belongs ) )
        {
            identities[slot] = {};
            continue;
        }

        auto team = Field<uint8>( pawn, offsets.team );
        auto* body = Field<void*>( pawn, offsets.body );
        auto* scene = body ? Field<void*>( body, offsets.scene ) : nullptr;
        auto* collision = Field<void*>( pawn, offsets.collision );

        if ( !scene || !collision || ( team != 2 && team != 3 ) )
        {
            identities[slot] = {};
            continue;
        }

        p.origin = Field<Vector>( scene, offsets.origin );
        p.eye = p.origin + Field<Vector>( pawn, offsets.view );
        p.velocity = Field<Vector>( pawn, offsets.velocity );
        p.mins = Field<Vector>( collision, offsets.mins );
        p.maxs = Field<Vector>( collision, offsets.maxs );
        p.angles = Field<QAngle>( pawn, offsets.angles );

        if ( !Finite( p.origin ) || !Finite( p.eye ) || !Finite( p.velocity ) || !Finite( p.mins ) || !Finite( p.maxs ) || !std::isfinite( p.angles.x ) ||
             !std::isfinite( p.angles.y ) || !std::isfinite( p.angles.z ) || p.mins.x >= p.maxs.x || p.mins.y >= p.maxs.y || p.mins.z >= p.maxs.z )
        {
            identities[slot] = {};
            continue;
        }

        p.pawn = pawn;
        p.team = team;
        p.pawnHandle = handle.ToInt();
        p.controllerHandle = controller->GetRefEHandle().ToInt();

        if ( settings.CornerPrediction && offsets.movement >= 0 && offsets.buttons >= 0 && offsets.buttonStates >= 0 )
        {
            if ( auto* movement = Field<void*>( pawn, offsets.movement ) )
            {
                p.movementButtons = Field<uint64_t>( movement, offsets.buttons + offsets.buttonStates );
            }
        }

        // Locate once per player/tick, not once per enemy pair. Nonstandard hulls use rays.
        if ( visibility.Ready() && settings.BoundsPadding <= 38 && p.maxs.z <= 72 && p.mins.z >= 0 && p.mins.x >= -16 && p.mins.y >= -16 && p.maxs.x <= 16 && p.maxs.y <= 16 &&
             p.eye.z - p.origin.z >= 40 && p.eye.z - p.origin.z <= 68 )
        {
            p.visCell = visibility.Cell( p.origin );
        }

        // FL_FAKECLIENT and HLTV bypass, only filter the actual living pawn, not a possessed bot.
        p.human = !( Field<uint32>( controller, offsets.flags ) & FL_FAKECLIENT ) && !Field<bool>( controller, offsets.hltv ) &&
                  Field<CEntityHandle>( controller, offsets.currentPawn ) == handle;
        p.bot = ( Field<uint32>( controller, offsets.flags ) & FL_FAKECLIENT ) != 0;
        Identity identity{ p.controllerHandle, p.pawnHandle, team };

        if ( !( identities[slot] == identity ) )
        {
            identities[slot] = identity;

            for ( int other = 0; other < 64; ++other )
            {
                deadlines[slot][other] = deadlines[other][slot] = tick + settings.VisibleGraceTicks;
                preferred[slot][other] = preferred[other][slot] = 0;
            }
        }

        p.entities.push_back( handle );
        AttachmentTraversal seen;
        p.complete = CollectChildren( Field<void*>( scene, offsets.child ), p, seen, 0 );
        auto* services = Field<void*>( pawn, offsets.weapons );

        if ( !services )
        {
            p.complete = false;
        }
        else
        {
            const auto& weapons = Field<CUtlVector<CEntityHandle>>( services, offsets.weaponHandles );

            if ( weapons.Count() < 0 || weapons.Count() > 64 )
            {
                p.complete = false;
            }
            else
            {
                for ( int i = 0; i < weapons.Count(); ++i )
                {
                    if ( EntityAt( entitySystem, weapons[i] ) )
                    {
                        p.entities.push_back( weapons[i] );
                    }
                    else if ( weapons[i].IsValid() )
                    {
                        p.complete = false;
                    }
                }
            }
        }
    }

    // Ownership does not imply equipment: never include thrown grenades/projectiles.
    for ( auto* id = entitySystem->m_EntityList.m_pFirstActiveEntity; id; id = id->m_pNext )
    {
        auto* e = id->m_pInstance;

        if ( !e || !IsClass( e, "CEconEntity" ) )
        {
            continue;
        }

        auto owner = Field<CEntityHandle>( e, offsets.owner );

        if ( !owner.IsValid() )
        {
            continue;
        }

        for ( auto& p : players )
        {
            if ( p.pawn && owner.ToInt() == p.pawnHandle )
            {
                p.entities.push_back( e->GetRefEHandle() );
                break;
            }
        }
    }
}

void AntiWallHack::EvaluateViewer( int slot, int tick )
{
    if ( evaluated[slot] )
    {
        return;
    }

    evaluated[slot] = true;
    auto& viewer = players[slot];

    if ( !viewer.pawn || !viewer.human )
    {
        return;
    }

    for ( int order = 0; order < PlayerLimit; ++order )
    {
        int targetSlot = RotatedIndex( order, uint64_t( uint32_t( tick ) ) + slot, PlayerLimit );

        if ( !Enemy( slot, targetSlot, identities ) )
        {
            continue;
        }

        pairVisibility.Evaluate( slot, targetSlot, tick, settings.VisibleGraceTicks, deadlines[slot][targetSlot], deadlines[targetSlot][slot],
                                 [&]( int from, int to )
                                 {
                                     return players[to].complete ? CheckVisibility( from, to ) : DirectVisibility::Unknown;
                                 } );

        if ( players[targetSlot].bot )
        {
            pairVisibility.EvaluateBot( slot, targetSlot, tick, settings.VisibleGraceTicks, deadlines[slot][targetSlot], deadlines[targetSlot][slot],
                                        [&]( int from, int to )
                                        {
                                            return players[to].complete ? CheckVisibility( from, to ) : DirectVisibility::Unknown;
                                        } );
        }
    }
}

const std::vector<CEntityHandle>& AntiWallHack::GetHidden( int slot, int tick )
{
    hidden[slot].clear();

    if ( !players[slot].pawn || !players[slot].human )
    {
        return hidden[slot];
    }

    for ( int targetSlot = 0; targetSlot < PlayerLimit; ++targetSlot )
    {
        if ( !Enemy( slot, targetSlot, identities ) || !pairVisibility.Hidden( slot, targetSlot, tick, deadlines[slot][targetSlot] ) )
        {
            continue;
        }

        auto& target = players[targetSlot];
        bool valid = true;

        for ( auto h : target.entities )
        {
            if ( h.GetEntryIndex() <= 0 || h.GetEntryIndex() >= EntityLimit || !EntityAt( entitySystem, h ) )
            {
                valid = false;
                break;
            }
        }

        if ( valid )
        {
            hidden[slot].insert( hidden[slot].end(), target.entities.begin(), target.entities.end() );
        }
    }

    return hidden[slot];
}
