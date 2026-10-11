#pragma once

#include "CHC.hpp"

#include <filesystem>

namespace piko::filetypes::chc
{
    [[nodiscard]] Song load_json_file( const std::filesystem::path &path );
}
