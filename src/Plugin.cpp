#include "Plugin.h"

bool AntiWallHack::Load( PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late )
{
    PLUGIN_SAVEVARS();
    DebugStage( "Load: entered" );

    if ( !KHook::__exported__khook )
    {
        ismm->Format( error, maxlen, "Metamod 2.0 KHook API is required" );
        return false;
    }

    DebugStage( "Load: acquiring engine/server interfaces" );
    GET_V_IFACE_CURRENT( GetServerFactory, gameEntities, ISource2GameEntities, INTERFACEVERSION_SERVERGAMEENTS );
    GET_V_IFACE_CURRENT( GetServerFactory, serverDll, ISource2Server, INTERFACEVERSION_SERVERGAMEDLL );
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
        DebugStage( "Load: interfaces acquired" );
        const fs::path baseDir( ismm->GetBaseDir() );
        gamePath = baseDir;
        configPath = baseDir / "cfg/AntiWallHack.cfg";
        LoadSettings();
        updateCancel = std::make_shared<std::atomic<bool>>( false );
        updatePending = {};
        updateInstalled = false;
        updateNextCheck = std::chrono::steady_clock::now() + std::chrono::seconds( 30 );
        DebugStage( "Load: configuration loaded" );
        auto data = ReadGamedata( baseDir / "addons/antiwallhack/gamedata.txt" );
        auto platform = data.at( "linux" );
        const char* module = "libserver.so";
        entitySystemOffset = platform.at( "GameEntitySystem" ).get<int>();

        if ( entitySystemOffset < 0 || entitySystemOffset > 1024 || entitySystemOffset % 8 )
        {
            throw std::runtime_error( "Invalid GameEntitySystem offset" );
        }

        auto* scope = schema->FindTypeScopeForModule( module );

        if ( !scope )
        {
            throw std::runtime_error( "Server schema scope unavailable" );
        }

        DebugStage( "Load: resolving schema offsets" );

        if ( !offsets.Init( scope ) )
        {
            ismm->Format( error, maxlen, "AntiWallHack: required schema fields unavailable, see console" );
            META_CONPRINTF( "[AntiWallHack] Startup rejected: required schema fields unavailable. No hooks installed.\n" );
            return false;
        }

        if ( settings.CornerPrediction )
        {
            offsets.movement = Offsets::Find( scope, "CBasePlayerPawn", "m_pMovementServices" );
            offsets.buttons = Offsets::Find( scope, "CPlayer_MovementServices", "m_nButtons" );

            if ( auto* globalScope = schema->GlobalTypeScope() )
            {
                offsets.buttonStates = Offsets::Find( globalScope, "CInButtonState", "m_pButtonStates" );
            }

            if ( offsets.movement < 0 || offsets.buttons < 0 || offsets.buttonStates < 0 )
            {
                META_CONPRINTF( "[AntiWallHack] Movement input schema unavailable, corner prediction uses bounded "
                                "velocity only.\n" );
            }
        }

        int index = KHook::GetVtableIndex( &ISource2GameEntities::CheckTransmit );
        DebugStage( "Load: schema ready, resolving trace signature" );

        if ( index < 0 || index >= 100 )
        {
            throw std::runtime_error( "Invalid CheckTransmit vtable index" );
        }

        void* moduleAnchor = InterfaceModuleAnchor( gameEntities );
        traceFunction = reinterpret_cast<TraceFunction>( FindServerFunction( platform.at( "TraceShape" ).get<std::string>(), moduleAnchor ) );

        if ( !traceFunction )
        {
            META_CONPRINTF( "[AntiWallHack] Startup rejected, TraceShape signature missing or ambiguous, update gamedata.txt. No hooks installed.\n" );
            ismm->Format( error, maxlen, "AntiWallHack: TraceShape signature missing or ambiguous, update gamedata.txt" );
            return false;
        }

        traceHook = std::make_unique<TraceHook>( this, &AntiWallHack::OnTrace, nullptr );
        DebugStage( "Load: configuring trace hook" );
        traceHook->Configure( traceFunction );
        transmitHook = std::make_unique<TransmitHook>( &ISource2GameEntities::CheckTransmit, this, nullptr, &AntiWallHack::OnTransmit );
        DebugStage( "Load: installing transmit hook" );
        transmitHook->Add( gameEntities );
        hooked = true;
        using FrameCallback = KHook::Return<void> ( AntiWallHack::* )( ISource2Server*, bool, bool, bool );
        frameHook = std::make_unique<FrameHook>( &ISource2Server::GameFrame, this, static_cast<FrameCallback>( nullptr ), &AntiWallHack::OnFrame );
        frameHook->Add( serverDll );
        ismm->AddListener( this, this );
        DebugStage( "Load: hooks installed, configuring convar" );
        ConfigureConvar();
        SetupSmoke( moduleAnchor, scope, platform );
        DebugStage( "Load: completed" );
        return true;
    }
    catch ( const std::exception& ex )
    {
        if ( frameHook )
        {
            frameHook->Remove( serverDll );
            frameHook.reset();
        }

        if ( hooked && transmitHook )
        {
            transmitHook->Remove( gameEntities );
            hooked = false;
        }

        RemoveEventHook();
        transmitHook.reset();
        traceHook.reset();

        META_CONPRINTF( "[AntiWallHack] Startup rejected: %s\n", ex.what() );
        ismm->Format( error, maxlen, "AntiWallHack: %s", ex.what() );

        return false;
    }
}

bool AntiWallHack::Unload( char*, size_t )
{
    updateCancel->store( true );

    if ( updatePending.valid() )
    {
        updatePending.wait();
    }

    if ( frameHook )
    {
        frameHook->Remove( serverDll );
        frameHook.reset();
    }

    DebugStage( "Unload: entered, waiting for VIS worker" );
    StopVisibilityWorker();

    DebugStage( "Unload: VIS worker finished, removing hooks" );

    if ( hooked && transmitHook )
    {
        transmitHook->Remove( gameEntities );
        hooked = false;
    }

    RemoveEventHook();
    smokeAvailable = false;
    smokeVolumes.clear();
    smokeExplosions.Reset();
    transmitHook.reset();
    traceHook.reset();
    ConVarRefAbstract cvar( "sv_enable_donttransmit" );

    if ( convarChanged && cvar.IsValidRef() && !cvar.GetBool() )
    {
        cvar.SetString( previousConvar.c_str() );
    }

    convarChanged = false;
    visibility = {};
    visMapName.clear();
    traceManager = nullptr;
    entitySystem = nullptr;

    Reset();

    DebugStage( "Unload: completed" );
    return true;
}

const char* AntiWallHack::GetAuthor()
{
    return "M1K@c";
}

const char* AntiWallHack::GetName()
{
    return "AntiWallHack";
}

const char* AntiWallHack::GetDescription()
{
    return "Server-side enemy visibility filtering for CS2";
}

const char* AntiWallHack::GetURL()
{
    return "https://discord.gg/d5uvMmUpuE";
}

const char* AntiWallHack::GetLicense()
{
    return "AGPL-3.0";
}

const char* AntiWallHack::GetVersion()
{
    return "1.0.9";
}

const char* AntiWallHack::GetDate()
{
    return __DATE__;
}

const char* AntiWallHack::GetLogTag()
{
    return "AntiWallHack";
}

AntiWallHack g_AntiWallHack;
PLUGIN_EXPOSE( AntiWallHack, g_AntiWallHack );
