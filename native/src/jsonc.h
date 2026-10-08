#pragma once
#include <json.hpp>
#include <cctype>
#include <string>

namespace awh
{
inline nlohmann::json ParseJsonc( std::string text )
{
    bool quoted = false, escaped = false;

    for ( size_t i = 0; i < text.size(); ++i )
    {
        char c = text[i];

        if ( quoted )
        {
            if ( escaped )
                escaped = false;
            else if ( c == '\\' )
                escaped = true;
            else if ( c == '"' )
                quoted = false;

            continue;
        }

        if ( c == '"' )
        {
            quoted = true;

            continue;
        }

        if ( c != '/' || i + 1 >= text.size() )
            continue;

        if ( text[i + 1] == '/' )
        {
            while ( i < text.size() && text[i] != '\n' && text[i] != '\r' )
                text[i++] = ' ';

            if ( i == text.size() )
                break;
        }

        else if ( text[i + 1] == '*' )
        {
            text[i++] = ' ';
            text[i++] = ' ';
            bool closed = false;

            for ( ; i < text.size(); ++i )
            {
                if ( text[i] == '*' && i + 1 < text.size() && text[i + 1] == '/' )
                {
                    text[i] = text[i + 1] = ' ';
                    ++i;
                    closed = true;
                    break;
                }
                if ( text[i] != '\n' && text[i] != '\r' )
                    text[i] = ' ';
            }

            if ( !closed )
                throw std::runtime_error( "Unterminated JSONC block comment" );
        }
    }
    quoted = escaped = false;

    for ( size_t i = 0; i < text.size(); ++i )
    {
        char c = text[i];
        
        if ( quoted )
        {
            if ( escaped )
                escaped = false;
            else if ( c == '\\' )
                escaped = true;
            else if ( c == '"' )
                quoted = false;
        }
        else if ( c == '"' )
            quoted = true;
        else if ( c == ',' )
        {
            size_t j = i + 1;
            while ( j < text.size() && std::isspace( static_cast<unsigned char>( text[j] ) ) ) ++j;

            if ( j < text.size() && ( text[j] == '}' || text[j] == ']' ) ) text[i] = ' ';
        }
    }

    return nlohmann::json::parse( text );
}
} 
