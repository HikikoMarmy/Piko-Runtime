#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace piko::convert::process
{
    struct RunOptions
    {
        bool quiet = false;
    };

    std::int32_t run_process( const std::filesystem::path &executable,
        const std::vector<std::filesystem::path> &arguments,
        RunOptions options = {} );
}
