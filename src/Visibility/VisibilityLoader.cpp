#include "Plugin.h"

void AntiWallHack::NotifyMissingVisibility()
{
    if ( !settings.Enabled || visMapName.empty() )
    {
        return;
    }

    if ( visibility.Ready() )
    {
        visNextNotice = {};
        return;
    }

    const auto now = std::chrono::steady_clock::now();

    if ( now < visNextNotice )
    {
        return;
    }

    visNextNotice = now + std::chrono::minutes( 5 );
    META_CONPRINTF( "[AntiWallHack] Checks disabled for %s: %s. Will resume automatically when VIS is ready.\n", visMapName.c_str(), visStatus.c_str() );
}

void AntiWallHack::LoadVisibility( const char* name, bool startValidation )
{
    StopVisibilityWorker();
    DebugStage( "LoadVisibility: enter" );
    visibility = {};
    visMapName = name && *name ? name : "<unknown>";
    ++visGeneration;
    visObserved = false;
    visNextPoll = {};
    visNextNotice = {};
    visStatus = "waiting for map physics before VIS validation";

    if ( startValidation )
    {
        UpdateVisibility();
    }
    else
    {
        DebugStage( "VIS: validation deferred until ready frame or transmit" );
    }

    DebugStage( "LoadVisibility: returned" );
}

void AntiWallHack::UpdateVisibility()
{
    using namespace std::chrono;

    if ( visPending.valid() )
    {
        if ( visPending.wait_for( seconds( 0 ) ) != std::future_status::ready )
        {
            return;
        }

        try
        {
            auto loaded = visPending.get();

            if ( visLoadingMap == visMapName && visLoadingGeneration == visGeneration )
            {
                visibility = std::move( loaded );
                frameTick = -1;
                META_CONPRINTF( "[AntiWallHack] VIS activated for %s: %zu cells, all enemies eligible.\n", visMapName.c_str(), visibility.CellCount() );
            }
        }
        catch ( const std::exception& error )
        {
            if ( visLoadingMap == visMapName && visLoadingGeneration == visGeneration )
            {
                visibility = {};
                visStatus = "VIS validation failed: " + std::string( error.what() );
                DebugStage( error.what() );
                // Workshop downloads may finish after the first validation attempt.
                visObserved = false;
                visNextPoll = steady_clock::now() + seconds( 30 );
            }
        }
    }

    auto now = steady_clock::now();

    if ( now < visNextPoll )
    {
        return;
    }

    visNextPoll = now + seconds( 5 );

    if ( visMapName.empty() || visMapName != MapIdentity( visMapName ) )
    {
        return;
    }

    try
    {
        auto path = gamePath / "addons/antiwallhack/vis" / ( visMapName + ".awvis" );
        bool exists = fs::is_regular_file( path );
        auto time = exists ? fs::last_write_time( path ) : fs::file_time_type{};
        auto size = exists ? fs::file_size( path ) : 0;

        if ( visObserved && exists == visObservedExists && time == visObservedTime && size == visObservedSize )
        {
            return;
        }

        if ( !exists )
        {
            visibility = {};
            visStatus = "baked VIS table missing: " + path.string();
            visObserved = true;
            visObservedExists = false;
            visObservedTime = time;
            visObservedSize = size;
            return;
        }

        auto root = gamePath;
        auto map = visMapName;
        visStatus = "validating baked VIS table: " + path.string();
        visLoadingMap = map;
        visLoadingGeneration = visGeneration;
        visCancel = std::make_shared<std::atomic<bool>>( false );
        auto cancelled = visCancel;
        const int gameCpu = sched_getcpu();
        DebugStage( "VIS: launching validation worker" );
        visPending = std::async( std::launch::async,
                                 [this, path, root, map, cancelled, gameCpu]
                                 {
                                     DebugStage( SetWorkerAffinity( gameCpu ) ? "VIS worker: background CPU affinity enabled" : "VIS worker: CPU affinity unchanged" );
                                     const auto tid = static_cast<id_t>( syscall( SYS_gettid ) );
                                     errno = 0;
                                     const int currentNice = getpriority( PRIO_PROCESS, tid );

                                     if ( errno == 0 && ( currentNice >= 15 || setpriority( PRIO_PROCESS, tid, 15 ) == 0 ) )
                                     {
                                         DebugStage( "VIS worker: low priority enabled" );
                                     }
                                     else
                                     {
                                         DebugStage( "VIS worker: priority unchanged, throttled I/O still enabled" );
                                     }

                                     DebugStage( "VIS worker: entered" );
                                     VisibilityMap loaded;

                                     try
                                     {
                                         const bool completed = loaded.Load(
                                             path, root, map,
                                             [this]( const char* progress )
                                             {
                                                 DebugStage( progress );
                                             },
                                             [cancelled]
                                             {
                                                 return cancelled->load();
                                             } );
                                         DebugStage( completed ? "VIS worker: validation completed" : "VIS worker: validation cancelled" );
                                     }
                                     catch ( ... )
                                     {
                                         DebugStage( "VIS worker: validation threw" );
                                         throw;
                                     }

                                     return loaded;
                                 } );
        DebugStage( "VIS: validation worker launched" );
        visObserved = true;
        visObservedExists = exists;
        visObservedTime = time;
        visObservedSize = size;
    }
    catch ( const std::exception& error )
    {
        visibility = {};
        visStatus = "VIS unavailable: " + std::string( error.what() );
        DebugStage( error.what() );
    }
}

void AntiWallHack::StopVisibilityWorker() noexcept
{
    if ( visCancel )
    {
        visCancel->store( true );
    }

    if ( visPending.valid() )
    {
        DebugStage( "VIS: waiting for validation worker to stop" );

        try
        {
            visPending.get();
        }
        catch ( const std::exception& error )
        {
            DebugStage( error.what() );
        }
        catch ( ... )
        {
            DebugStage( "VIS: discarded validation error during shutdown" );
        }

        DebugStage( "VIS: validation worker stopped" );
    }

    visCancel.reset();
    visLoadingMap.clear();
}
