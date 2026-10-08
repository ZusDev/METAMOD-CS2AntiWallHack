#include <ISmmPlugin.h>
#include <iserver.h>
#include <entity2/entitysystem.h>
#include <gametrace.h>
#include <schemasystem/schemasystem.h>
#include <interfaces/interfaces.h>
#include <json.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <unordered_set>
#include <vector>
#include "rules.h"
#include "transmit.h"
#include "memory.h"
#include "jsonc.h"
#include "config.h"

PLUGIN_GLOBALVARS();
namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace awh;

namespace
{
    template <class T> T& Field( void* object, int offset )
    {
        return *reinterpret_cast<T*>( static_cast<uint8_t*>( object ) + offset );
    }

    CEntityInstance* EntityAt( CGameEntitySystem* system, CEntityIndex index )
    {
        const int value = index.Get();
        if ( !system || value < 0 || value >= MAX_TOTAL_ENTITIES - 1 )
            return nullptr;

        auto* chunk = system->m_EntityList.m_pIdentityChunks[value / MAX_ENTITIES_IN_LIST];
        if ( !chunk )
            return nullptr;

        auto& identity = chunk[value % MAX_ENTITIES_IN_LIST];

        return identity.GetEntityIndex() == index ? identity.m_pInstance : nullptr;
    }

    CEntityInstance* EntityAt( CGameEntitySystem* system, const CEntityHandle& handle )
    {
        if ( !handle.IsValid() )
            return nullptr;

        auto* entity = EntityAt( system, CEntityIndex( handle.GetEntryIndex() ) );

        return entity && entity->GetRefEHandle() == handle ? entity : nullptr;
    }

    bool Finite( const Vector& v )
    {
        return std::isfinite( v.x ) && std::isfinite( v.y ) && std::isfinite( v.z );
    }

    bool IsClass( CEntityInstance* entity, const char* name )
    {
        if ( !entity || !entity->m_pEntity )
            return false;

        auto* info = entity->Schema_DynamicBinding().m_pObj;

        for ( int depth = 0; info && depth < 32; ++depth )
        {
            if ( !std::strcmp( info->m_pszName, name ) )
                return true;

            info = info->m_nBaseClassCount ? info->m_pBaseClasses[0].m_pClass : nullptr;
        }

        return false;
    }

    struct Offsets
    {
        int pawn, currentPawn, alive, connected, flags, hltv, team, health, life, body, scene, collision, velocity, view, angles;
        int origin, child, sibling, nodeOwner, mins, maxs, owner, weapons, weaponHandles;
        int rules, warmup, freeze, winner;

        static int Find( CSchemaSystemTypeScope* scope, const char* cls, const char* name )
        {
            auto* info = scope->FindDeclaredClass( cls ).Get();

            for ( int depth = 0; info && depth < 32; ++depth )
            {
                if ( !Readable( info, sizeof( *info ) ) || info->m_nFieldCount > 4096 || info->m_nBaseClassCount > 32 || ( info->m_nFieldCount && !Readable( info->m_pFields, size_t( info->m_nFieldCount ) * sizeof( SchemaClassFieldData_t ) ) ) )
                {
                    META_CONPRINTF( "[AntiWallHack] Invalid schema metadata: %s\n", cls );
                    return -1;
                }

                for ( int i = 0; i < info->m_nFieldCount; ++i )
                    if ( SchemaNameEquals( info->m_pFields[i].m_pszName, name ) )
                    {
                        int offset = info->m_pFields[i].m_nSingleInheritanceOffset;
                        if ( offset < 0 || offset >= info->m_nSize )
                        {
                            META_CONPRINTF( "[AntiWallHack] Invalid schema offset: %s::%s\n", cls, name );
                            return -1;
                        }

                        return offset;
                    }
                if ( info->m_nBaseClassCount && !Readable( info->m_pBaseClasses, size_t( info->m_nBaseClassCount ) * sizeof( SchemaBaseClassInfoData_t ) ) )
                {
                    META_CONPRINTF( "[AntiWallHack] Invalid schema base metadata: %s\n", cls );
                    return -1;
                }

                info = info->m_nBaseClassCount ? info->m_pBaseClasses[0].m_pClass : nullptr;
            }

            META_CONPRINTF( "[AntiWallHack] Missing schema field: %s::%s\n", cls, name );

            return -1;
        }

        bool Init( CSchemaSystemTypeScope* s )
        {
            pawn = Find( s, "CCSPlayerController", "m_hPlayerPawn" );
            currentPawn = Find( s, "CBasePlayerController", "m_hPawn" );
            alive = Find( s, "CCSPlayerController", "m_bPawnIsAlive" );
            connected = Find( s, "CBasePlayerController", "m_iConnected" );
            flags = Find( s, "CBaseEntity", "m_fFlags" );
            hltv = Find( s, "CBasePlayerController", "m_bIsHLTV" );
            team = Find( s, "CBaseEntity", "m_iTeamNum" );
            health = Find( s, "CBaseEntity", "m_iHealth" );
            life = Find( s, "CBaseEntity", "m_lifeState" );
            body = Find( s, "CBaseEntity", "m_CBodyComponent" );
            scene = Find( s, "CBodyComponent", "m_pSceneNode" );
            collision = Find( s, "CBaseEntity", "m_pCollision" );
            velocity = Find( s, "CBaseEntity", "m_vecAbsVelocity" );
            view = Find( s, "CBaseModelEntity", "m_vecViewOffset" );
            angles = Find( s, "CCSPlayerPawn", "m_angEyeAngles" );
            origin = Find( s, "CGameSceneNode", "m_vecAbsOrigin" );
            child = Find( s, "CGameSceneNode", "m_pChild" );
            sibling = Find( s, "CGameSceneNode", "m_pNextSibling" );
            nodeOwner = Find( s, "CGameSceneNode", "m_pOwner" );
            mins = Find( s, "CCollisionProperty", "m_vecMins" );
            maxs = Find( s, "CCollisionProperty", "m_vecMaxs" );
            owner = Find( s, "CBaseEntity", "m_hOwnerEntity" );
            weapons = Find( s, "CBasePlayerPawn", "m_pWeaponServices" );
            weaponHandles = Find( s, "CPlayer_WeaponServices", "m_hMyWeapons" );
            rules = Find( s, "CCSGameRulesProxy", "m_pGameRules" );
            warmup = Find( s, "CCSGameRules", "m_bWarmupPeriod" );
            freeze = Find( s, "CCSGameRules", "m_bFreezePeriod" );
            winner = Find( s, "CCSGameRules", "m_iRoundWinStatus" );

            for ( int value : { pawn, currentPawn, alive, connected, flags, hltv, team, health, life, body, scene, collision, velocity, view, angles, origin, child, sibling, nodeOwner, mins, maxs, owner, weapons, weaponHandles, rules, warmup, freeze, winner } )
            if ( value < 0 )
                return false;

            return true;
        }
    };

    struct Snapshot
    {
        CEntityInstance* pawn = nullptr;
        uint32 controllerHandle = 0, pawnHandle = 0;
        uint8 team = 0;
        bool human = false, complete = false;
        Vector origin, eye, mins, maxs, velocity;
        QAngle angles;
        std::vector<CEntityHandle> entities;
        std::array<Vector, 12> samples;
        bool samplesReady = false;
    };

    struct Identity
    {
        uint32 controller = 0, pawn = 0;
        uint8 team = 0;

        bool operator==( const Identity& b ) const
        {
            return controller == b.controller && pawn == b.pawn && team == b.team;
        }
    };

    class SightFilter final : public CTraceFilter
    {
        const Snapshot& viewer;
        const Snapshot& target;

    public: SightFilter( const Snapshot& a, const Snapshot& b ) : CTraceFilter( uint64( 1 ) << LAYER_INDEX_CONTENTS_SOLID ), viewer( a ), target( b )
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
                return true; // world geometry

            if ( !e->m_pEntity || IsClass( e, "CBasePlayerPawn" ) || IsClass( e, "CBasePlayerController" ) || IsClass( e, "CEconEntity" ) )
                return false;

            auto handle = e->GetRefEHandle();

            return std::find( viewer.entities.begin(), viewer.entities.end(), handle ) == viewer.entities.end() && std::find( target.entities.begin(), target.entities.end(), handle ) == target.entities.end();
        }
    };
}

class AntiWallHack final : public ISmmPlugin, public IMetamodListener
{
    using TraceFunction = void ( * )( void*, Ray_t&, Vector&, Vector&, CTraceFilter*, CGameTrace* );
    using TraceHook = KHook::Function<void, void*, Ray_t&, Vector&, Vector&, CTraceFilter*, CGameTrace*>;
    ISource2GameEntities* gameEntities = nullptr;
    INetworkServerService* network = nullptr;
    void* resources = nullptr;
    CGameEntitySystem* entitySystem = nullptr;
    Offsets offsets;
    Settings settings;
    fs::path configPath;
    int entitySystemOffset = 0;
    std::unique_ptr<TraceHook> traceHook;
    TraceFunction traceFunction = nullptr;
    void* traceManager = nullptr;
    using TransmitHook = KHook::Virtual<ISource2GameEntities, void, CCheckTransmitInfo**, int, CBitVec<16384>&, CBitVec<16384>&, const Entity2Networkable_t**, const uint16*, int>;
    std::unique_ptr<TransmitHook> transmitHook;
    std::array<Snapshot, 64> players;
    std::array<Identity, 64> identities{};
    int deadlines[64][64]{};
    int preferred[64][64]{};
    std::array<std::vector<CEntityHandle>, 64> hidden;
    std::array<bool, 64> evaluated{};
    int frameTick = -1, traces = 0;
    bool faulted = false, hooked = false, convarChanged = false;
    std::string previousConvar;

    Json ReadGamedata( const fs::path& path )
    {
        std::ifstream file( path );
        if ( !file )
            throw std::runtime_error( "Cannot open " + path.string() );

        return ParseJsonc( std::string( std::istreambuf_iterator<char>( file ), {} ) );
    }

    void LoadSettings()
    {
        if ( !fs::exists( configPath ) )
        {
            fs::create_directories( configPath.parent_path() );
            std::ofstream out( configPath );
            out << DefaultConfig;
            out.close();

            if ( !out )
                throw std::runtime_error( "Cannot create " + configPath.string() );
        }

        std::ifstream file( configPath );

        if ( !file )
            throw std::runtime_error( "Cannot open " + configPath.string() );

        settings = ParseConfig( file );
    }

    void Reset()
    {
        frameTick = -1;
        traces = 0;
        std::memset( deadlines, 0, sizeof( deadlines ) );
        std::memset( preferred, 0, sizeof( preferred ) );
        identities.fill( {} );
        evaluated.fill( false );

        for ( auto& p : players )
            p = Snapshot{};

        for ( auto& row : hidden )
            row.clear();
    }

    void ConfigureConvar()
    {
        ConVarRefAbstract cvar( "sv_enable_donttransmit" );

        if ( !settings.SetDontTransmitToZero || !cvar.IsValidRef() )
            return;

        if ( !convarChanged )
        {
            previousConvar = cvar.GetString().Get();
            convarChanged = true;
        }

        cvar.SetBool( false );
    }

    bool CollectChildren( void* node, Snapshot& snapshot, std::unordered_set<void*>& seen, int depth )
    {
        while ( node )
        {
            if ( depth > 16 || seen.size() >= 128 || !seen.insert( node ).second )
                return false;

            auto* entity = Field<CEntityInstance*>( node, offsets.nodeOwner );

            if ( !entity || !entity->m_pEntity || IsClass( entity, "CBasePlayerPawn" ) || IsClass( entity, "CBasePlayerController" ) )
                return false;

            snapshot.entities.push_back( entity->GetRefEHandle() );

            if ( !CollectChildren( Field<void*>( node, offsets.child ), snapshot, seen, depth + 1 ) )
                return false;

            node = Field<void*>( node, offsets.sibling );
        }

        return true;
    }

    bool RoundActive()
    {
        for ( auto* id = entitySystem->m_EntityList.m_pFirstActiveEntity; id; id = id->m_pNext )
        {
            auto* e = id->m_pInstance;

            if ( !e || !IsClass( e, "CCSGameRulesProxy" ) )
                continue;

            auto* rules = Field<void*>( e, offsets.rules );

            return rules && !Field<bool>( rules, offsets.warmup ) && !Field<bool>( rules, offsets.freeze ) && Field<int>( rules, offsets.winner ) == 0;
        }

        return false;
    }

    void BeginFrame( int tick )
    {
        if ( frameTick == tick )
            return;

        if ( tick < frameTick )
            Reset();

        frameTick = tick;
        traces = 0;

        evaluated.fill( false );

        for ( auto& row : hidden )
            row.clear();

        for ( int slot = 0; slot < 64; ++slot )
        {
            auto& p = players[slot];
            p = Snapshot{};
            auto* controller = EntityAt( entitySystem, CEntityIndex( slot + 1 ) );

            if ( !controller || !IsClass( controller, "CCSPlayerController" ) || Field<int>( controller, offsets.connected ) != 0 || !Field<bool>( controller, offsets.alive ) )
            {
                identities[slot] = {};
                continue;
            }

            auto handle = Field<CEntityHandle>( controller, offsets.pawn );
            auto* pawn = EntityAt( entitySystem, handle );

            if ( !pawn || !IsClass( pawn, "CCSPlayerPawn" ) || Field<int>( pawn, offsets.health ) <= 0 || Field<uint8>( pawn, offsets.life ) != 0 )
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

            if ( !Finite( p.origin ) || !Finite( p.eye ) || !Finite( p.velocity ) || !Finite( p.mins ) || !Finite( p.maxs ) || !std::isfinite( p.angles.x ) || !std::isfinite( p.angles.y ) || !std::isfinite( p.angles.z ) || p.mins.x >= p.maxs.x || p.mins.y >= p.maxs.y || p.mins.z >= p.maxs.z )
            {
                identities[slot] = {};
                continue;
            }

            p.pawn = pawn;
            p.team = team;
            p.pawnHandle = handle.ToInt();
            p.controllerHandle = controller->GetRefEHandle().ToInt();

            // FL_FAKECLIENT and HLTV bypass, only filter the actual living pawn, not a possessed bot.
            p.human = !( Field<uint32>( controller, offsets.flags ) & FL_FAKECLIENT ) && !Field<bool>( controller, offsets.hltv ) && Field<CEntityHandle>( controller, offsets.currentPawn ) == handle;
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
            std::unordered_set<void*> seen;
            p.complete = CollectChildren( Field<void*>( scene, offsets.child ), p, seen, 0 );
            auto* services = Field<void*>( pawn, offsets.weapons );

            if ( !services )
                p.complete = false;
            else
            {
                const auto& weapons = Field<CUtlVector<CEntityHandle>>( services, offsets.weaponHandles );

                if ( weapons.Count() < 0 || weapons.Count() > 64 )
                    p.complete = false;
                else
                    for ( int i = 0; i < weapons.Count(); ++i )
                    {
                        if ( EntityAt( entitySystem, weapons[i] ) )
                            p.entities.push_back( weapons[i] );
                        else if ( weapons[i].IsValid() )
                            p.complete = false;
                    }
            }
        }
        // Ownership does not imply equipment: never include thrown grenades/projectiles.
        for ( auto* id = entitySystem->m_EntityList.m_pFirstActiveEntity; id; id = id->m_pNext )
        {
            auto* e = id->m_pInstance;

            if ( !e || !IsClass( e, "CEconEntity" ) )
                continue;

            auto owner = Field<CEntityHandle>( e, offsets.owner );

            if ( !owner.IsValid() )
                continue;

            for ( auto& p : players )
            if ( p.pawn && owner.ToInt() == p.pawnHandle )
            {
                p.entities.push_back( e->GetRefEHandle() );
                break;
            }
        }
    }

    void BuildSamples( Snapshot& target )
    {
        auto lo = target.mins - Vector( settings.BoundsPadding, settings.BoundsPadding, 0 );
        auto hi = target.maxs + Vector( settings.BoundsPadding, settings.BoundsPadding, settings.BoundsPadding );

        target.samples[0] = target.eye;
        target.samples[1] = target.origin + ( lo + hi ) * .5f;

        for ( int i = 0; i < 8; ++i ) target.samples[i + 2] = target.origin + Vector( ( i & 1 ) ? hi.x : lo.x, ( i & 2 ) ? hi.y : lo.y, ( i & 4 ) ? hi.z : lo.z + 4 );
        constexpr float radians = 3.14159265358979323846f / 180;
        float pitch = target.angles.x * radians, yaw = target.angles.y * radians;
        Vector forward( std::cos( pitch ) * std::cos( yaw ), std::cos( pitch ) * std::sin( yaw ), -std::sin( pitch ) );
        target.samples[10] = target.eye + forward * 64;
        target.samples[11] = target.origin + Vector( 0, 0, 32 ) + forward * 40;
        target.samplesReady = true;
    }

    bool IsHidden( int viewerSlot, int targetSlot )
    {
        auto& viewer = players[viewerSlot];
        auto& target = players[targetSlot];

        if ( !traceManager || !traceFunction )
            return false;

        if ( !target.samplesReady )
            BuildSamples( target );

        auto lead = target.velocity * settings.PredictionSeconds, eyeLead = viewer.velocity * settings.PredictionSeconds;
        int count = lead.LengthSqr() < 1 && eyeLead.LengthSqr() < 1 ? 12 : 24;
        int first = preferred[viewerSlot][targetSlot];

        if ( first >= count )
            first %= 12;

        SightFilter filter( viewer, target );

        for ( int order = 0; order < count; ++order )
        {
            if ( traces >= TraceBudget )
                return false;

            int index = OrderedSample( order, first );
            bool prediction = index >= 12;

            Vector start = viewer.eye + ( prediction ? eyeLead : Vector( 0, 0, 0 ) );
            Vector end = target.samples[index % 12] + ( prediction ? lead : Vector( 0, 0, 0 ) );

            if ( !Finite( start ) || !Finite( end ) )
                return false;

            Ray_t ray;
            alignas( 16 ) CGameTrace result;
            ++traces;

            traceFunction( traceManager, ray, start, end, &filter, &result );

            if ( !Occluded( result.m_flFraction, result.m_bStartInSolid ) )
            {
                if ( !result.m_bStartInSolid && std::isfinite( result.m_flFraction ) && result.m_flFraction >= .99f && result.m_flFraction <= 1 ) preferred[viewerSlot][targetSlot] = index;
                return false;
            }
            if ( result.m_pEnt && !filter.ShouldHitEntity( result.m_pEnt ) )
                return false;
        }

        return true;
    }

    const std::vector<CEntityHandle>& GetHidden( int slot, int tick )
    {
        if ( evaluated[slot] )
            return hidden[slot];

        evaluated[slot] = true;
        auto& viewer = players[slot];

        if ( !viewer.pawn || !viewer.human )
            return hidden[slot];

        int nearest[64];
        float distances[64];
        int count = 0;

        for ( int i = 0; i < 64; ++i )
            if ( players[i].pawn && i != slot && players[i].team != viewer.team )
                InsertNearest( nearest, distances, count, settings.NearestEnemies, i, ( players[i].origin - viewer.origin ).LengthSqr() );

        for ( int i = 0; i < count; ++i )
        {
            int targetSlot = nearest[i];
            auto& target = players[targetSlot];

            if ( !CanHide( tick, deadlines[slot][targetSlot] ) )
                continue;

            if ( !target.complete || distances[i] <= settings.AlwaysVisibleDistance * settings.AlwaysVisibleDistance || !IsHidden( slot, targetSlot ) )
            {
                deadlines[slot][targetSlot] = tick + settings.VisibleGraceTicks;
                continue;
            }

            bool valid = true;

            for ( auto h : target.entities )
                if ( h.GetEntryIndex() <= 0 || h.GetEntryIndex() >= EntityLimit || !EntityAt( entitySystem, h ) )
                {
                    valid = false;
                    break;
                }
            if ( valid )
                hidden[slot].insert( hidden[slot].end(), target.entities.begin(), target.entities.end() );
        }

        return hidden[slot];
    }

  public:
    AntiWallHack() = default;

    bool Load( PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late ) override
    {
        PLUGIN_SAVEVARS();

        if ( !KHook::__exported__khook )
        {
            ismm->Format( error, maxlen, "Metamod 2.0 KHook API is required" );
            return false;
        }

        GET_V_IFACE_CURRENT( GetServerFactory, gameEntities, ISource2GameEntities, INTERFACEVERSION_SERVERGAMEENTS );
        GET_V_IFACE_CURRENT( GetEngineFactory, network, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION );
        GET_V_IFACE_CURRENT( GetEngineFactory, resources, IGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION );
        GET_V_IFACE_CURRENT( GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION );
        ISchemaSystem* schema = static_cast<ISchemaSystem*>( ismm->GetEngineFactory()( SCHEMASYSTEM_INTERFACE_VERSION, nullptr ) );

        if ( !schema )
        {
            ismm->Format( error, maxlen, "SchemaSystem interface unavailable" );
            return false;
        }
        try
        {
            const fs::path baseDir( ismm->GetBaseDir() );
            configPath = baseDir / "cfg/AntiWallHack.cfg";
            LoadSettings();
            auto data = ReadGamedata( baseDir / "addons/antiwallhack/gamedata.jsonc" );
#ifdef _WIN32
            auto platform = data.at( "windows" );
            const char* module = "server.dll";
#else
            auto platform = data.at( "linux" );
            const char* module = "libserver.so";
#endif
            entitySystemOffset = platform.at( "GameEntitySystem" ).get<int>();

            if ( entitySystemOffset < 0 || entitySystemOffset > 1024 || entitySystemOffset % 8 )
                throw std::runtime_error( "Invalid GameEntitySystem offset" );

            auto* scope = schema->FindTypeScopeForModule( module );

            if ( !scope )
                throw std::runtime_error( "Server schema scope unavailable" );

            if ( !offsets.Init( scope ) )
            {
                ismm->Format( error, maxlen, "AntiWallHack: required schema fields unavailable, see console" );
                META_CONPRINTF( "[AntiWallHack] Startup rejected: required schema fields unavailable. No hooks installed.\n" );
                return false;
            }

            int index = KHook::GetVtableIndex( &ISource2GameEntities::CheckTransmit );

            if ( index < 0 || index >= 100 )
                throw std::runtime_error( "Invalid CheckTransmit vtable index" );

            auto* serverFunction = ( *reinterpret_cast<void***>( gameEntities ) )[index];

            traceFunction = reinterpret_cast<TraceFunction>( FindServerFunction( platform.at( "TraceShape" ).get<std::string>(), serverFunction ) );
            if ( !traceFunction )
                throw std::runtime_error( "TraceShape signature missing or ambiguous; update gamedata.jsonc" );

            traceHook = std::make_unique<TraceHook>( this, &AntiWallHack::OnTrace, nullptr );
            traceHook->Configure( traceFunction );
            transmitHook = std::make_unique<TransmitHook>( &ISource2GameEntities::CheckTransmit, this, nullptr, &AntiWallHack::OnTransmit );
            transmitHook->Add( gameEntities );
            hooked = true;
            ismm->AddListener( this, this );
            ConfigureConvar();
            return true;
        }
        catch ( const std::exception& ex )
        {
            if ( hooked && transmitHook )
            {
                transmitHook->Remove( gameEntities );
                hooked = false;
            }

            transmitHook.reset();
            traceHook.reset();

            META_CONPRINTF( "[AntiWallHack] Startup rejected: %s\n", ex.what() );
            ismm->Format( error, maxlen, "AntiWallHack: %s", ex.what() );

            return false;
        }
    }

    bool Unload( char*, size_t ) override
    {
        if ( hooked && transmitHook )
        {
            transmitHook->Remove( gameEntities );
            hooked = false;
        }

        transmitHook.reset();
        traceHook.reset();
        ConVarRefAbstract cvar( "sv_enable_donttransmit" );

        if ( convarChanged && cvar.IsValidRef() && !cvar.GetBool() )
            cvar.SetString( previousConvar.c_str() );

        convarChanged = false;
        traceManager = nullptr;
        entitySystem = nullptr;

        Reset();

        return true;
    }

    void OnLevelInit( const char*, const char*, const char*, const char*, bool, bool ) override
    {
        Reset();
        traceManager = nullptr;
        entitySystem = nullptr;
        ConfigureConvar();
    }

    void OnLevelShutdown() override
    {
        Reset();
        traceManager = nullptr;
        entitySystem = nullptr;
    }

    KHook::Return<void> OnTrace( void* manager, Ray_t&, Vector&, Vector&, CTraceFilter*, CGameTrace* )
    {
        traceManager = manager;
        return { KHook::Action::Ignore };
    }

    KHook::Return<void> OnTransmit( ISource2GameEntities*, CCheckTransmitInfo** infos, int count, CBitVec<16384>&, CBitVec<16384>&, const Entity2Networkable_t**, const uint16*, int )
    {
        if ( faulted || !settings.Enabled || !infos || count < 1 || count > 64 )
            return { KHook::Action::Ignore };
        try
        {
            auto* server = network->GetIGameServer();
            auto* globals = server ? server->GetGlobals() : nullptr;
            entitySystem = Field<CGameEntitySystem*>( resources, entitySystemOffset );
            ConVarRefAbstract cvar( "sv_enable_donttransmit" );
            if ( !globals || !entitySystem || !traceManager || ( cvar.IsValidRef() && cvar.GetBool() ) || !RoundActive() )
            {
                Reset();
                return { KHook::Action::Ignore };
            }
            BeginFrame( globals->tickcount );
            for ( int i = 0; i < count; ++i )
            {
                auto* info = reinterpret_cast<TransmitInfo*>( infos[i] );

                if ( !info || info->fullUpdate || !info->entities || !info->deletion || info->entities == info->deletion )
                    continue;

                int slot = info->slot.Get();

                if ( slot < 0 || slot >= 64 )
                    continue;

                for ( auto h : GetHidden( slot, globals->tickcount ) )
                    if ( EntityAt( entitySystem, h ) )
                        Withhold( info->entities->Base(), info->deletion->Base(), h.GetEntryIndex() );
            }
        }
        catch ( const std::exception& ex )
        {
            faulted = true;
            Reset();
            META_CONPRINTF( "[AntiWallHack] Filtering disabled after error: %s\n", ex.what() );
        }
        return { KHook::Action::Ignore };
    }

    const char* GetAuthor() override
    {
        return "M1K@c";
    }

    const char* GetName() override
    {
        return "AntiWallHack";
    }

    const char* GetDescription() override
    {
        return "Server-side enemy visibility filtering for CS2";
    }

    const char* GetURL() override
    {
        return "";
    }

    const char* GetLicense() override
    {
        return "AGPL-3.0";
    }

    const char* GetVersion() override
    {
        return "1.0.0";
    }

    const char* GetDate() override
    {
        return __DATE__;
    }

    const char* GetLogTag() override
    {
        return "AntiWallHack";
    }
};

AntiWallHack g_AntiWallHack;
PLUGIN_EXPOSE( AntiWallHack, g_AntiWallHack );
