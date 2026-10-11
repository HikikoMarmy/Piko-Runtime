#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace piko::convert
{
    std::string normalize_virtual_path( std::string_view path );
    std::vector<std::string> split_virtual_path( std::string_view path );
    std::string virtual_parent_path( std::string_view normalized_path );
    std::string virtual_filename( std::string_view normalized_path );
    bool virtual_path_starts_with( std::string_view normalized_path, std::string_view normalized_prefix );
}
