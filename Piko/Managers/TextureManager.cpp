#include "TextureManager.hpp"
#include "Utils/StrUtil.hpp"

#include "Rendering/Renderer.hpp"

#include <cstdint>
#include <cstddef>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace piko::managers
{
    bool TextureHandle::valid() const
    {
        return id != 0;
    }

    TextureHandle::operator bool() const
    {
        return valid();
    }

    TextureManager::~TextureManager()
    {
        shutdown();
    }

    bool TextureManager::initialize( rendering::Renderer &renderer )
    {
        renderer_ = &renderer;

        initialized_ = true;
        return true;
    }

    void TextureManager::shutdown()
    {
        clear();
        renderer_ = nullptr;

        initialized_ = false;
    }

    bool TextureManager::initialized() const
    {
        return initialized_;
    }

    TextureHandle TextureManager::create_rgba( std::string_view name,
        const void *pixels,
        std::int32_t width,
        std::int32_t height )
    {
        if( renderer_ == nullptr )
        {
            throw std::runtime_error( "TextureManager::create_rgba called before initialize" );
        }

        const std::string key = normalize_name( name );

        if( key.empty() )
        {
            throw std::runtime_error( "TextureManager::create_rgba called with empty name" );
        }

        if( const auto existing = by_name_.find( key ); existing != by_name_.end() )
        {
            if( get( existing->second ) != nullptr )
            {
                return existing->second;
            }
        }

        rendering::Texture texture{};

        if( !renderer_->create_texture_rgba( pixels, width, height, texture ) )
        {
            throw std::runtime_error( "TextureManager::create_rgba failed to upload texture" );
        }

        return store_texture( key, {}, std::move( texture ) );
    }

    TextureHandle TextureManager::find( std::string_view name ) const
    {
        const std::string key = normalize_name( name );

        const auto it = by_name_.find( key );

        if( it == by_name_.end() )
        {
            return {};
        }

        return it->second;
    }

    const rendering::Texture *TextureManager::get( TextureHandle handle ) const
    {
        const Entry *entry = entry_for( handle );
        return entry != nullptr && entry->texture.valid() ? &entry->texture : nullptr;
    }

    const rendering::Texture *TextureManager::get( std::string_view name ) const
    {
        return get( find( name ) );
    }

    const TextureInfo *TextureManager::info( TextureHandle handle ) const
    {
        const Entry *entry = entry_for( handle );
        return entry != nullptr && entry->texture.valid() ? &entry->info : nullptr;
    }

    const TextureInfo *TextureManager::info( std::string_view name ) const
    {
        return info( find( name ) );
    }

    bool TextureManager::contains( std::string_view name ) const
    {
        return get( name ) != nullptr;
    }

    void TextureManager::unload( TextureHandle handle )
    {
        Entry *entry = entry_for( handle );

        if( entry == nullptr )
        {
            return;
        }

        if( !entry->info.name.empty() )
        {
            by_name_.erase( normalize_name( entry->info.name ) );
        }

        if( renderer_ != nullptr )
        {
            renderer_->destroy_texture( entry->texture );
        }
        else
        {
            entry->texture = {};
        }

        entry->info = {};
    }

    void TextureManager::unload( std::string_view name )
    {
        unload( find( name ) );
    }

    void TextureManager::clear()
    {
        if( renderer_ != nullptr )
        {
            for( Entry &entry : entries_ )
            {
                renderer_->destroy_texture( entry.texture );
            }
        }

        entries_.clear();
        by_name_.clear();
    }

    std::string TextureManager::normalize_name( std::string_view name )
    {
        return piko::utils::lowercase_ascii( piko::utils::normalize_slashes( std::string( name ) ) );
    }

    std::string TextureManager::default_name_for_path( const std::filesystem::path &path )
    {
        if( !path.filename().empty() )
        {
            return path.filename().generic_string();
        }

        return path.generic_string();
    }

    TextureHandle TextureManager::make_handle_for_index( std::size_t index ) const
    {
        return TextureHandle{ static_cast<std::uint32_t>( index + 1 ) };
    }

    TextureManager::Entry *TextureManager::entry_for( TextureHandle handle )
    {
        if( !handle )
        {
            return nullptr;
        }

        const std::size_t index = static_cast<std::size_t>( handle.id - 1 );

        if( index >= entries_.size() )
        {
            return nullptr;
        }

        if( !entries_[ index ].texture.valid() )
        {
            return nullptr;
        }

        return &entries_[ index ];
    }

    const TextureManager::Entry *TextureManager::entry_for( TextureHandle handle ) const
    {
        if( !handle )
        {
            return nullptr;
        }

        const std::size_t index = static_cast<std::size_t>( handle.id - 1 );

        if( index >= entries_.size() )
        {
            return nullptr;
        }

        if( !entries_[ index ].texture.valid() )
        {
            return nullptr;
        }

        return &entries_[ index ];
    }

    TextureHandle TextureManager::load_png( std::string_view name, const std::filesystem::path &path )
    {
        if( renderer_ == nullptr )
        {
            throw std::runtime_error( "TextureManager::load_png called before initialize" );
        }

        const std::string key = normalize_name( name );

        if( key.empty() )
        {
            throw std::runtime_error( "TextureManager::load_png called with empty name" );
        }

        if( const auto existing = by_name_.find( key ); existing != by_name_.end() )
        {
            if( get( existing->second ) != nullptr )
            {
                return existing->second;
            }
        }

        SDL_Surface *loaded = SDL_LoadPNG( path.string().c_str() );

        if( loaded == nullptr )
        {
            throw std::runtime_error( std::string( "Could not load runtime PNG: " ) + SDL_GetError() );
        }

        struct SurfaceGuard
        {
            SDL_Surface *value = nullptr;

            ~SurfaceGuard()
            {
                SDL_DestroySurface( value );
            }
        } loaded_guard{ loaded };

        SDL_Surface *rgba = SDL_ConvertSurface( loaded, SDL_PIXELFORMAT_RGBA32 );

        if( rgba == nullptr )
        {
            throw std::runtime_error( std::string( "Could not convert runtime PNG: " ) + SDL_GetError() );
        }

        SurfaceGuard rgba_guard{ rgba };

        std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>( rgba->w ) * static_cast<std::size_t>( rgba->h ) * 4u );

        for( std::int32_t y = 0; y < rgba->h; ++y )
        {
            std::memcpy( pixels.data() + static_cast<std::size_t>( y ) * static_cast<std::size_t>( rgba->w ) * 4u,
                static_cast<const std::uint8_t *>( rgba->pixels ) +
                    static_cast<std::size_t>( y ) * static_cast<std::size_t>( rgba->pitch ),
                static_cast<std::size_t>( rgba->w ) * 4u );
        }

        rendering::Texture texture{};

        if( !renderer_->create_texture_rgba( pixels.data(), rgba->w, rgba->h, texture ) )
        {
            throw std::runtime_error( "TextureManager failed to upload runtime PNG" );
        }

        const TextureHandle handle = store_texture( key, path, std::move( texture ) );
        Entry *entry = entry_for( handle );

        if( entry != nullptr )
        {
            entry->info.pixel_count = pixels.size() / 4u;

            entry->info.alpha_min = 255u;

            entry->info.alpha_max = 0u;

            for( std::size_t index = 3; index < pixels.size(); index += 4u )
            {
                const std::uint8_t alpha = pixels[ index ];
                entry->info.alpha_min = std::min( entry->info.alpha_min, alpha );

                entry->info.alpha_max = std::max( entry->info.alpha_max, alpha );

                if( alpha == 0u )
                {
                    ++entry->info.alpha_zero_pixels;
                }
                else if( alpha == 255u )
                {
                    ++entry->info.alpha_full_pixels;
                }
                else
                {
                    ++entry->info.alpha_partial_pixels;
                }
            }
        }

        return handle;
    }

    TextureHandle TextureManager::store_texture( std::string_view name,
        const std::filesystem::path &path,
        rendering::Texture &&texture )
    {
        Entry entry{};
        entry.info.name = std::string( name );

        entry.info.path = path;

        entry.info.width = texture.width;

        entry.info.height = texture.height;

        entry.texture = texture;

        entries_.push_back( std::move( entry ) );

        const TextureHandle handle = make_handle_for_index( entries_.size() - 1 );

        entries_.back().info.handle = handle;
        by_name_[ normalize_name( name ) ] = handle;

        return handle;
    }
}
