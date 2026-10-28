#pragma once
#include <sched.h>

namespace awh
{
    inline bool SetWorkerAffinity( int gameCpu )
    {
        if ( gameCpu < 0 || gameCpu >= CPU_SETSIZE )
        {
            return false;
        }

        cpu_set_t allowed;
        CPU_ZERO( &allowed );

        if ( sched_getaffinity( 0, sizeof( allowed ), &allowed ) != 0 )
        {
            return false;
        }

        cpu_set_t selected;
        CPU_ZERO( &selected );

        for ( int cpu = 3; cpu <= 4; ++cpu )
        {
            if ( cpu != gameCpu && CPU_ISSET( cpu, &allowed ) )
            {
                CPU_SET( cpu, &selected );
            }
        }

        if ( CPU_COUNT( &selected ) == 0 )
        {
            for ( int cpu = 0; cpu < CPU_SETSIZE; ++cpu )
            {
                if ( cpu != gameCpu && CPU_ISSET( cpu, &allowed ) )
                {
                    CPU_SET( cpu, &selected );
                }
            }
        }

        return CPU_COUNT( &selected ) > 0 && sched_setaffinity( 0, sizeof( selected ), &selected ) == 0;
    }
}