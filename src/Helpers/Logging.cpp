#include "Plugin.h"

void AntiWallHack::DebugStage( const char* stage ) noexcept
{
    if ( !settings.Debug )
    {
        return;
    }

    try
    {
        std::lock_guard<std::mutex> lock( debugMutex );
        auto sequence = ++debugSequence;
        std::fprintf( stderr, "[AntiWallHack][Debug] 1.0.8 #%llu %s\n", sequence, stage );
        std::fflush( stderr );

        if ( gamePath.empty() )
        {
            return;
        }

        auto path = gamePath / "addons/antiwallhack/startup-debug.log";

        if ( auto* log = std::fopen( path.string().c_str(), "a" ) )
        {
            std::fprintf( log, "[AntiWallHack][Debug] 1.0.8 #%llu %s\n", sequence, stage );
            std::fflush( log );
            std::fclose( log );
        }
    }
    catch ( ... )
    {
    } // Diagnostics must never interrupt engine callbacks.
}
