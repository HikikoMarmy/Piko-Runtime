#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace piko::utils
{
    std::string lowercase_ascii( std::string value );
    std::string uppercase_ascii( std::string value );
    std::string normalize_slashes( std::string value );
    std::string lowercase_filename( std::string value );
    std::vector<std::string> split_lines( std::string_view text );
    std::string safe_ascii_stem( std::string value, std::string fallback, bool lowercase = false );
}
