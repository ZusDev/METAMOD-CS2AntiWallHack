#pragma once
#include <algorithm>
#include <array>
#include <cstddef>

namespace awh
{
    class AttachmentTraversal
    {
        std::array<const void*, 128> nodes;
        size_t count = 0;

      public:
        bool Insert( const void* node )
        {
            if ( !node || count == nodes.size() || std::find( nodes.begin(), nodes.begin() + count, node ) != nodes.begin() + count )
            {
                return false;
            }

            nodes[count++] = node;
            return true;
        }
    };
}
