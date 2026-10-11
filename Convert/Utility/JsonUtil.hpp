#pragma once

#include <string>
#include <string_view>

namespace piko::convert::json_util
{
    enum class EscapeMode
    {
        Minimal,
        Text,
        Json,
        Ascii
    };

    std::string escape_string( std::string_view value, EscapeMode mode = EscapeMode::Json );
}
