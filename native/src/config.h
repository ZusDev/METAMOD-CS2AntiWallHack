#pragma once
#include "rules.h"
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace awh
{
inline constexpr const char* DefaultConfig = R"cfg(// AntiWallHack configuration.

Enabled                      1       // Enable visibility filtering.
NearestEnemies               5       // Number of enemies checked.
VisibleGraceTicks            20      // Visibility grace period.
BoundsPadding                48      // Extra bounds padding.
PredictionSeconds            0.2     // Movement prediction.
AlwaysVisibleDistance        160     // Reveal nearby enemies.
SetDontTransmitToZero        1       // Set sv_enable_donttransmit to 0.)cfg";

inline Settings ParseConfig( std::istream& input )
{
    Settings result;
    std::string line;
    int lineNumber = 0;

    while ( std::getline( input, line ) )
    {
        ++lineNumber;
        std::istringstream tokens( line );
        tokens >> std::ws;

        if ( tokens.eof() || tokens.peek() == '/' || tokens.peek() == '#' )
            continue;

        std::string key, value, extra;

        if ( !( tokens >> std::quoted( key ) >> std::quoted( value ) ) )
            throw std::runtime_error( "Missing setting value at cfg line " + std::to_string( lineNumber ) );

        if ( tokens >> extra && extra.rfind( "//", 0 ) != 0 && extra.rfind( "#", 0 ) != 0 )
            throw std::runtime_error( "Unexpected text at cfg line " + std::to_string( lineNumber ) );

        auto boolean = [&]()
        {
            if ( value == "1" || value == "true" )
                return true;

            if ( value == "0" || value == "false" )
                return false;

            throw std::runtime_error( "Invalid boolean for " + key );
        };

        auto number = [&]()
        {
            size_t used = 0;

            double parsed = std::stod( value, &used );

            if ( used != value.size() || !std::isfinite( parsed ) )
                throw std::runtime_error( "Invalid number for " + key );

            return parsed;
        };
        auto integer = [&]()
        {
            double parsed = number();

            if ( parsed != std::floor( parsed ) || parsed < 0 || parsed > 640 )
                throw std::runtime_error( "Invalid integer for " + key );

            return static_cast<int>( parsed );
        };
        if ( key == "Enabled" )
            result.Enabled = boolean();
        else if ( key == "NearestEnemies" )
            result.NearestEnemies = integer();
        else if ( key == "VisibleGraceTicks" )
            result.VisibleGraceTicks = integer();
        else if ( key == "BoundsPadding" )
            result.BoundsPadding = static_cast<float>( number() );
        else if ( key == "PredictionSeconds" )
            result.PredictionSeconds = static_cast<float>( number() );
        else if ( key == "AlwaysVisibleDistance" )
            result.AlwaysVisibleDistance = static_cast<float>( number() );
        else if ( key == "SetDontTransmitToZero" )
            result.SetDontTransmitToZero = boolean();
        else
            throw std::runtime_error( "Unknown setting: " + key );
    }
    if ( input.bad() )
        throw std::runtime_error( "Cannot read configuration" );

    if ( !result.Valid() )
        throw std::runtime_error( "Configuration values are outside the documented bounds" );

    return result;
}

inline void WriteConfig( std::ostream& output, const Settings& settings )
{
    output << "// AntiWallHack configuration. Reload the plugin after editing.\n\n"
           << "Enabled                      " << settings.Enabled << '\n'
           << "NearestEnemies               " << settings.NearestEnemies << '\n'
           << "VisibleGraceTicks             " << settings.VisibleGraceTicks << '\n'
           << "BoundsPadding                " << settings.BoundsPadding << '\n'
           << "PredictionSeconds            " << settings.PredictionSeconds << '\n'
           << "AlwaysVisibleDistance        " << settings.AlwaysVisibleDistance << '\n'
           << "SetDontTransmitToZero         " << settings.SetDontTransmitToZero << '\n';
}
} 
