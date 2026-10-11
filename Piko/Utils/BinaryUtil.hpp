#pragma once

#include <cstdint>

namespace piko::utils
{
    std::uint16_t read_le_u16( const std::uint8_t *data );
    std::uint32_t read_le_u32( const std::uint8_t *data );
    std::uint16_t read_be_u16( const std::uint8_t *data );
    std::uint32_t read_be_u32( const std::uint8_t *data );
}
