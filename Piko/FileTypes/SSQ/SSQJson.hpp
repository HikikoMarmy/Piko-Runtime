#pragma once

#include "SSQ.hpp"

namespace piko::filetypes::ssq
{
    [[nodiscard]] Scene load_json( const std::filesystem::path &path );
    void save_json( const Scene &scene, const std::filesystem::path &path );
}
