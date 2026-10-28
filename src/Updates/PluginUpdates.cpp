#include "Plugin.h"

void AntiWallHack::PollUpdates()
{
    if ( updatePending.valid() && updatePending.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready )
    {
        auto result = updatePending.get();
        updateInstalled = result.installed;

        if ( !result.message.empty() )
        {
            META_CONPRINTF( "[AntiWallHack] %s.\n", result.message.c_str() );
        }
    }

    if ( !settings.AutoUpdate || updateInstalled || updatePending.valid() || std::chrono::steady_clock::now() < updateNextCheck )
    {
        return;
    }

    updateNextCheck = std::chrono::steady_clock::now() + std::chrono::hours( 6 );
    auto cancel = updateCancel;
    const auto path = gamePath;
    const std::string current = GetVersion();
    const int gameCpu = sched_getcpu();

    try
    {
        updatePending = std::async( std::launch::async,
                                    [this, path, current, cancel, gameCpu]()
                                    {
                                        DebugStage( SetWorkerAffinity( gameCpu ) ? "Update worker: background CPU affinity enabled" : "Update worker: CPU affinity unchanged" );
                                        return updates::CheckAndInstall( path, current, cancel );
                                    } );
    }
    catch ( const std::exception& )
    {
        META_CONPRINTF( "[AntiWallHack] Could not start release update check, retrying in six hours.\n" );
    }
}
