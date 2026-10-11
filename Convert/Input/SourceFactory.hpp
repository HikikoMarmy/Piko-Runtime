#pragma once

#include "Input/IGameSource.hpp"

#include <filesystem>
#include <memory>

namespace piko::convert
{
    std::unique_ptr<IGameSource> open_game_source( const std::filesystem::path &path );
}
