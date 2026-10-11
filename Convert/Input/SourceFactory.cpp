#include "Input/SourceFactory.hpp"

#include "Input/FolderSource.hpp"
#include "Input/Iso9660Source.hpp"

namespace piko::convert
{
    std::unique_ptr<IGameSource> open_game_source( const std::filesystem::path &path )
    {
        if( std::filesystem::is_directory( path ) )
        {
            return std::make_unique<FolderSource>( path );
        }

        return std::make_unique<Iso9660Source>( path );
    }
}
