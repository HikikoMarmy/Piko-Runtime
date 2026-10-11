#include "Utils/BinaryUtil.hpp"

namespace piko::utils
{
    std::uint16_t read_le_u16( const std::uint8_t *data )
    {
        return static_cast<std::uint16_t>( data[ 0 ] ) |
               static_cast<std::uint16_t>( static_cast<std::uint16_t>( data[ 1 ] ) << 8u );
    }

    std::uint32_t read_le_u32( const std::uint8_t *data )
    {
        return static_cast<std::uint32_t>( data[ 0 ] ) | ( static_cast<std::uint32_t>( data[ 1 ] ) << 8u ) |
               ( static_cast<std::uint32_t>( data[ 2 ] ) << 16u ) | ( static_cast<std::uint32_t>( data[ 3 ] ) << 24u );
    }

    std::uint16_t read_be_u16( const std::uint8_t *data )
    {
        return static_cast<std::uint16_t>( static_cast<std::uint16_t>( data[ 0 ] ) << 8u ) |
               static_cast<std::uint16_t>( data[ 1 ] );
    }

    std::uint32_t read_be_u32( const std::uint8_t *data )
    {
        return ( static_cast<std::uint32_t>( data[ 0 ] ) << 24u ) | ( static_cast<std::uint32_t>( data[ 1 ] ) << 16u ) |
               ( static_cast<std::uint32_t>( data[ 2 ] ) << 8u ) | static_cast<std::uint32_t>( data[ 3 ] );
    }
}
