#pragma once

#include <array>
#include <cstddef>
#include <iosfwd>
#include <cstdint>
#include <utility>
#include <vector>

namespace piko::convert::gltf
{
    void align_buffer( std::vector<std::uint8_t> &buffer, std::size_t alignment = 4u );

    void write_int_array( std::ostream &output, const std::vector<std::int32_t> &values );
    void write_float_array( std::ostream &output, const std::vector<float> &values );
    void write_double_array( std::ostream &output, const std::vector<double> &values );
    void write_double_array( std::ostream &output, const std::array<double, 3> &values );
    void write_double_array( std::ostream &output, const std::array<double, 4> &values );

    [[nodiscard]] std::pair<std::vector<double>, std::vector<double>> min_max_vec3( const std::vector<float> &values );

    [[nodiscard]] std::pair<std::vector<double>, std::vector<double>> min_max_scalar(
        const std::vector<float> &values );
}
