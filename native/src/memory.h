#pragma once
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <link.h>
#include <dlfcn.h>
#include <cstring>
#endif

namespace awh
{
    inline bool Readable( const void* pointer, size_t size )
    {
        auto address = reinterpret_cast<uintptr_t>( pointer );
        
        if ( !address || size > UINTPTR_MAX - address )
            return false;

    #ifdef _WIN32
        MEMORY_BASIC_INFORMATION region{};
        if ( !VirtualQuery( pointer, &region, sizeof( region ) ) )
            return false;

        return region.State == MEM_COMMIT && !( region.Protect & ( PAGE_NOACCESS | PAGE_GUARD ) ) &&
            address + size <= reinterpret_cast<uintptr_t>( region.BaseAddress ) + region.RegionSize;
    #else
        static const auto ranges = []
        {
            std::vector<std::pair<uintptr_t, uintptr_t>> result;
            std::ifstream maps( "/proc/self/maps" );
            std::string line;
            while ( std::getline( maps, line ) )
            {
                std::istringstream input( line );
                std::string span, permissions;
                input >> span >> permissions;

                auto dash = span.find( '-' );
                if ( dash != std::string::npos && !permissions.empty() && permissions[0] == 'r' ) result.emplace_back( std::strtoull( span.substr( 0, dash ).c_str(), nullptr, 16 ), std::strtoull( span.substr( dash + 1 ).c_str(), nullptr, 16 ) );
            }
            return result;
        }();

        for ( auto range : ranges )
            if ( address >= range.first && address + size <= range.second )
                return true;

        return false;

    #endif
    }

    inline bool SchemaNameEquals( const char* name, const char* expected )
    {
        const size_t length = std::strlen( expected ) + 1;
        return Readable( name, length ) && !std::memcmp( name, expected, length );
    }

    struct Region
    {
        const uint8_t* start;
        size_t size;
    };

    inline void* FindServerFunction( const std::string& pattern, void* serverFunction )
    {
        if ( !serverFunction )
            return nullptr;

        std::vector<int> bytes;
        std::istringstream input( pattern );
        std::string token;

        while ( input >> token )
        {
            if ( token == "?" || token == "??" )
                bytes.push_back( -1 );
            else
            {
                if ( token.size() != 2 || token.find_first_not_of( "0123456789abcdefABCDEF" ) != std::string::npos )
                    return nullptr;

                bytes.push_back( static_cast<int>( std::strtoul( token.c_str(), nullptr, 16 ) ) );
            }
        }

        if ( bytes.empty() )
            return nullptr;

        std::vector<Region> regions;

    #ifdef _WIN32
        HMODULE module = nullptr;

        if ( !GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>( serverFunction ), &module ) )
            return nullptr;

        auto base = reinterpret_cast<const uint8_t*>( module );

        if ( !base )
            return nullptr;

        auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>( base );
        auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>( base + dos->e_lfanew );
        auto sections = IMAGE_FIRST_SECTION( nt );

        for ( unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i )

            if ( sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE )
                regions.push_back( { base + sections[i].VirtualAddress, sections[i].Misc.VirtualSize } );
    #else
        Dl_info module{};

        if ( !dladdr( serverFunction, &module ) || !module.dli_fbase )
            return nullptr;

        struct Search
        {
            void* base;
            std::vector<Region>* regions;
        } search{ module.dli_fbase, &regions };

        dl_iterate_phdr(
            []( dl_phdr_info* info, size_t, void* data )
            {
                auto& search = *static_cast<Search*>( data );

                if ( reinterpret_cast<void*>( info->dlpi_addr ) != search.base )
                    return 0;

                auto& out = *search.regions;

                for ( unsigned i = 0; i < info->dlpi_phnum; ++i )
                {
                    auto& p = info->dlpi_phdr[i];

                    if ( p.p_type == PT_LOAD && ( p.p_flags & PF_X ) )
                        out.push_back( { reinterpret_cast<const uint8_t*>( info->dlpi_addr + p.p_vaddr ), size_t( p.p_memsz ) } );
                }
                return 1;
            },
            &search );
    #endif
        const uint8_t* match = nullptr;

        for ( auto region : regions )
        {
            for ( size_t i = 0; i + bytes.size() <= region.size; ++i )
            {
                size_t j = 0;

                while ( j < bytes.size() && ( bytes[j] < 0 || region.start[i + j] == bytes[j] ) ) ++j;
                if ( j == bytes.size() )
                {
                    if ( match )
                        return nullptr; 
                        
                    match = region.start + i;
                }
            }
        }
        return const_cast<uint8_t*>( match );
    }
}
