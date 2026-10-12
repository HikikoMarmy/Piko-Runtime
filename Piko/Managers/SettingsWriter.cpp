#include "SettingsWriter.hpp"

#include <iomanip>
#include <locale>
#include <string>

namespace piko::managers
{
    SettingsWriter::SettingsWriter( std::ostream &stream ) : stream_( stream )
    {
        stream_.imbue( std::locale::classic() );
        stream_ << std::boolalpha << std::fixed << std::setprecision( decimal_places );
    }

    void SettingsWriter::begin_object()
    {
        stream_ << '{';
        field_counts_.push_back( 0 );
    }

    void SettingsWriter::begin_object( std::string_view key )
    {
        write_key( key );
        begin_object();
    }

    void SettingsWriter::end_object()
    {
        if( field_counts_.back() > 0 )
        {
            stream_ << '\n' << std::string( ( field_counts_.size() - 1 ) * indent_width, ' ' );
        }

        stream_ << '}';
        field_counts_.pop_back();
    }

    void SettingsWriter::write_key( std::string_view key )
    {
        if( field_counts_.back() > 0 )
        {
            stream_ << ',';
        }

        stream_ << '\n' << std::string( field_counts_.size() * indent_width, ' ' );
        write_value( key );
        stream_ << ": ";
        ++field_counts_.back();
    }

    void SettingsWriter::write_value( std::string_view value )
    {
        stream_ << '"';

        for( const unsigned char character : value )
        {
            if( character == '"' || character == '\\' )
            {
                stream_ << '\\' << static_cast<char>( character );
            }
            else if( character < 0x20 )
            {
                stream_ << "\\u00" << hex_digits[ character >> 4 ] << hex_digits[ character & 0x0f ];
            }
            else
            {
                stream_ << static_cast<char>( character );
            }
        }

        stream_ << '"';
    }
}
