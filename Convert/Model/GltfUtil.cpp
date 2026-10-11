#include "Model/GltfUtil.hpp"

#include <cstdint>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <ostream>

namespace piko::convert::gltf
{
    void align_buffer( std::vector<std::uint8_t> &buffer, std::size_t alignment )
    {
        if( alignment == 0u )
        {
            return;
        }

        while( buffer.size() % alignment != 0u )
        {
            buffer.push_back( 0u );
        }
    }

    void write_int_array( std::ostream &output, const std::vector<std::int32_t> &values )
    {
        output << '[';

        for( std::size_t index = 0; index < values.size(); ++index )
        {
            if( index != 0u )
            {
                output << ',';
            }

            output << values[ index ];
        }

        output << ']';
    }

    void write_float_array( std::ostream &output, const std::vector<float> &values )
    {
        output << '[';

        for( std::size_t index = 0; index < values.size(); ++index )
        {
            if( index != 0u )
            {
                output << ',';
            }

            output << std::setprecision( 9 ) << values[ index ];
        }

        output << ']';
    }

    void write_double_array( std::ostream &output, const std::vector<double> &values )
    {
        output << '[';

        for( std::size_t index = 0; index < values.size(); ++index )
        {
            if( index != 0u )
            {
                output << ',';
            }

            output << std::setprecision( 9 ) << values[ index ];
        }

        output << ']';
    }

    void write_double_array( std::ostream &output, const std::array<double, 3> &values )
    {
        output << '[';

        for( std::size_t index = 0; index < values.size(); ++index )
        {
            if( index != 0u )
            {
                output << ',';
            }

            output << std::setprecision( 9 ) << values[ index ];
        }

        output << ']';
    }

    void write_double_array( std::ostream &output, const std::array<double, 4> &values )
    {
        output << '[';

        for( std::size_t index = 0; index < values.size(); ++index )
        {
            if( index != 0u )
            {
                output << ',';
            }

            output << std::setprecision( 9 ) << values[ index ];
        }

        output << ']';
    }

    std::pair<std::vector<double>, std::vector<double>> min_max_vec3( const std::vector<float> &values )
    {
        std::vector<double> minimum( 3u, std::numeric_limits<double>::max() );
        std::vector<double> maximum( 3u, std::numeric_limits<double>::lowest() );

        for( std::size_t index = 0; index + 2u < values.size(); index += 3u )
        {
            for( std::size_t component = 0; component < 3u; ++component )
            {
                minimum[ component ] =
                    std::min( minimum[ component ], static_cast<double>( values[ index + component ] ) );

                maximum[ component ] =
                    std::max( maximum[ component ], static_cast<double>( values[ index + component ] ) );
            }
        }

        if( values.empty() )
        {
            minimum.assign( 3u, 0.0 );
            maximum.assign( 3u, 0.0 );
        }

        return { minimum, maximum };
    }

    std::pair<std::vector<double>, std::vector<double>> min_max_scalar( const std::vector<float> &values )
    {
        if( values.empty() )
        {
            return { { 0.0 }, { 0.0 } };
        }

        const auto [ minimum, maximum ] = std::minmax_element( values.begin(), values.end() );
        return { { static_cast<double>( *minimum ) }, { static_cast<double>( *maximum ) } };
    }
}
