#pragma once
#include <array>
#include <vector>
#include <utility>
#include "Helpers/Entities.h"

namespace awh
{
    struct Snapshot
    {
        CEntityInstance* pawn = nullptr;
        uint32 controllerHandle = 0, pawnHandle = 0;
        uint8 team = 0;
        bool human = false, bot = false, complete = false;
        Vector origin, eye, mins, maxs, velocity;
        QAngle angles;
        std::vector<CEntityHandle> entities;
        std::array<Vector, 12> samples, smokeSamples;
        bool samplesReady = false;
        int visCell = -1;
        uint64_t movementButtons = 0;
        Vector eyeLead{ 0, 0, 0 };
        bool eyeLeadReady = false;
        int visPeekCell = -1;
        int health = 0, life = -1;

        void Reset()
        {
            auto retained = std::move( entities );
            retained.clear();
            *this = Snapshot{};
            entities = std::move( retained );
        }
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

}
