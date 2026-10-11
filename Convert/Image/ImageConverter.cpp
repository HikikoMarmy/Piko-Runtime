#include "Image/ImageConverter.hpp"
#include "Utils/StrUtil.hpp"

#include "Utility/JsonUtil.hpp"

#include "Image/PngWriter.hpp"
#include "Utils/BattleTexturePixels.hpp"
#include "Runtime/Stage/StageGameplayInterfaceAtlas.hpp"
#include "FileTypes/IMX/IMX.hpp"
#include "FileTypes/XGM/XGM.hpp"

#include <cstdint>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace piko::convert::image_convert
{
    bool is_indexed( filetypes::imx::PixelStorageMode mode )
    {
        return mode == filetypes::imx::PixelStorageMode::Indexed4 || mode == filetypes::imx::PixelStorageMode::Indexed8;
    }

    std::string storage_mode_name( filetypes::imx::PixelStorageMode mode )
    {
        switch( mode )
        {
            case filetypes::imx::PixelStorageMode::Indexed4:
                return "indexed4";
            case filetypes::imx::PixelStorageMode::Indexed8:
                return "indexed8";
            case filetypes::imx::PixelStorageMode::Rgb888:
                return "rgb888";
            case filetypes::imx::PixelStorageMode::Rgba8888:
                return "rgba8888";
        }

        return "unknown";
    }

    std::vector<std::uint8_t> smooth_indexed_rgba( const filetypes::imx::Image &image,
        const ImageProcessingOptions &options )
    {
        if( !is_indexed( image.storage_mode ) || image.width < 2 || image.height < 2 ||
            image.rgba.size() !=
                static_cast<std::size_t>( image.width ) * static_cast<std::size_t>( image.height ) * 4u )
        {
            return image.rgba;
        }

        const float default_threshold =
            image.storage_mode == filetypes::imx::PixelStorageMode::Indexed4 ? 0.28f : 0.18f;

        const float color_threshold = default_threshold * std::clamp( options.threshold_scale, 0.0f, 2.0f );
        const float default_strength = image.storage_mode == filetypes::imx::PixelStorageMode::Indexed4 ? 0.46f : 0.34f;
        const float smoothing_strength = std::clamp( default_strength * options.strength_scale, 0.0f, 1.0f );

        std::vector<std::uint8_t> filtered = image.rgba;

        if( options.alpha_cleanup )
        {
            for( std::size_t index = 3u; index < filtered.size(); index += 4u )
            {
                if( filtered[ index ] <= options.transparent_cutoff )
                {
                    filtered[ index ] = 0u;
                }
                else if( filtered[ index ] >= options.opaque_cutoff )
                {
                    filtered[ index ] = 255u;
                }
            }
        }

        if( !options.smoothing || smoothing_strength <= 0.0f || color_threshold <= 0.0f )
        {
            return filtered;
        }

        for( std::uint32_t y = 0; y < image.height; ++y )
        {
            for( std::uint32_t x = 0; x < image.width; ++x )
            {
                const std::size_t center_index = ( static_cast<std::size_t>( y ) * image.width + x ) * 4u;

                if( image.rgba[ center_index + 3u ] < 250u )
                {
                    continue;
                }

                const float center_r = static_cast<float>( image.rgba[ center_index + 0u ] ) / 255.0f;
                const float center_g = static_cast<float>( image.rgba[ center_index + 1u ] ) / 255.0f;
                const float center_b = static_cast<float>( image.rgba[ center_index + 2u ] ) / 255.0f;
                float sum_r = center_r;
                float sum_g = center_g;
                float sum_b = center_b;
                float sum_weight = 1.0f;
                std::int32_t accepted_neighbors = 0;

                for( std::int32_t offset_y = -1; offset_y <= 1; ++offset_y )
                {
                    for( std::int32_t offset_x = -1; offset_x <= 1; ++offset_x )
                    {
                        if( offset_x == 0 && offset_y == 0 )
                        {
                            continue;
                        }

                        const std::int32_t sample_x = static_cast<std::int32_t>( x ) + offset_x;
                        const std::int32_t sample_y = static_cast<std::int32_t>( y ) + offset_y;

                        if( sample_x < 0 || sample_y < 0 || sample_x >= static_cast<std::int32_t>( image.width ) ||
                            sample_y >= static_cast<std::int32_t>( image.height ) )
                        {
                            continue;
                        }

                        const std::size_t sample_index = ( static_cast<std::size_t>( sample_y ) * image.width +
                                                             static_cast<std::size_t>( sample_x ) ) *
                                                         4u;

                        if( image.rgba[ sample_index + 3u ] < 250u )
                        {
                            continue;
                        }

                        const float sample_r = static_cast<float>( image.rgba[ sample_index + 0u ] ) / 255.0f;
                        const float sample_g = static_cast<float>( image.rgba[ sample_index + 1u ] ) / 255.0f;
                        const float sample_b = static_cast<float>( image.rgba[ sample_index + 2u ] ) / 255.0f;
                        const float color_delta = std::max( std::abs( sample_r - center_r ),
                            std::max( std::abs( sample_g - center_g ), std::abs( sample_b - center_b ) ) );

                        if( color_delta >= color_threshold )
                        {
                            continue;
                        }

                        const float spatial_weight = offset_x != 0 && offset_y != 0 ? 0.52f : 0.78f;
                        const float color_weight = 1.0f - color_delta / color_threshold;
                        const float weight = spatial_weight * color_weight;
                        sum_r += sample_r * weight;

                        sum_g += sample_g * weight;

                        sum_b += sample_b * weight;

                        sum_weight += weight;
                        ++accepted_neighbors;
                    }
                }

                if( accepted_neighbors < 2 || sum_weight <= 1.0f )
                {
                    continue;
                }

                const float result_r = std::lerp( center_r, sum_r / sum_weight, smoothing_strength );
                const float result_g = std::lerp( center_g, sum_g / sum_weight, smoothing_strength );
                const float result_b = std::lerp( center_b, sum_b / sum_weight, smoothing_strength );
                filtered[ center_index + 0u ] =
                    static_cast<std::uint8_t>( std::clamp( std::lround( result_r * 255.0f ), 0l, 255l ) );

                filtered[ center_index + 1u ] =
                    static_cast<std::uint8_t>( std::clamp( std::lround( result_g * 255.0f ), 0l, 255l ) );

                filtered[ center_index + 2u ] =
                    static_cast<std::uint8_t>( std::clamp( std::lround( result_b * 255.0f ), 0l, 255l ) );
            }
        }

        return filtered;
    }

    void write_texture_manifest( const std::filesystem::path &path, const std::vector<ConvertedTexture> &textures )
    {
        std::ofstream file( path, std::ios::binary );

        if( !file )
        {
            throw std::runtime_error( "Could not create texture manifest: " + path.string() );
        }

        file << "{\n  \"format\": \"piko-textures\",\n  \"version\": 1,\n  \"textures\": [\n";

        for( std::size_t i = 0; i < textures.size(); ++i )
        {
            const ConvertedTexture &texture = textures[ i ];
            file << "    {\n";
            file << "      \"index\": " << texture.source_index << ",\n";
            file << "      \"source_name\": \""
                 << json_util::escape_string( texture.source_name, json_util::EscapeMode::Text ) << "\",\n";

            file << "      \"file\": \"" << json_util::escape_string( texture.file_name, json_util::EscapeMode::Text )
                 << "\",\n";

            file << "      \"width\": " << texture.width << ",\n";
            file << "      \"height\": " << texture.height << ",\n";
            file << "      \"storage\": \"" << texture.storage_mode << "\",\n";
            file << "      \"usage_hint\": " << texture.usage_hint << "\n";
            file << "    }" << ( i + 1u == textures.size() ? "\n" : ",\n" );
        }

        file << "  ]\n}\n";

        if( !file )
        {
            throw std::runtime_error( "Could not write texture manifest: " + path.string() );
        }
    }
}

namespace piko::convert
{
    std::vector<std::uint8_t> process_texture_rgba( const filetypes::imx::Image &image,
        const ImageProcessingOptions &options )
    {
        return image_convert::smooth_indexed_rgba( image, options );
    }

    void export_note_sprites( std::span<const std::uint8_t> rgba,
        std::uint32_t width,
        std::uint32_t height,
        const std::filesystem::path &directory )
    {
        using namespace runtime::stage::gameplay_interface_atlas;
        const ui::BattleImage atlas{
            static_cast<std::int32_t>( width ),
            static_cast<std::int32_t>( height ),
            std::vector<std::uint8_t>( rgba.begin(), rgba.end() )
        };

        const std::array<rendering::Rect, 4>
            sources{
            phrase_tube_body,
            phrase_tube_cap,
            note_begin_icon,
            trace_line_body
        };

        const std::array<const char *, 4> filenames{
            "ribbon.png",
            "cap.png",
            "note.png",
            "trace.png"
        };

        for( std::size_t index = 0; index < sources.size(); ++index )
        {
            const auto path = directory / filenames[ index ];

            auto image = ui::crop_battle_image( atlas, sources[ index ] );

            if( filenames[ index ] == std::string_view( "trace.png" ) )
            {
                // The trace sprite runs vertically in the atlas.
                ui::BattleImage rotated{ image.height, image.width, std::vector<std::uint8_t>( image.pixels.size() ) };

                for( std::int32_t y = 0; y < image.height; ++y )
                {
                    for( std::int32_t x = 0; x < image.width; ++x )
                    {
                        const auto source = ( static_cast<std::size_t>( y ) * image.width + x ) * 4u;

                        const auto destination =
                            ( static_cast<std::size_t>( x ) * rotated.width + image.height - 1 - y ) * 4u;

                        std::copy_n( image.pixels.data() + source, 4, rotated.pixels.data() + destination );
                    }
                }

                image = std::move( rotated );
            }

            if( std::filesystem::is_regular_file( path ) )
            {
                continue;
            }

            // Keep brightness when removing colour.
            for( std::size_t pixel = 0; pixel < image.pixels.size(); pixel += 4u )
            {
                const auto red = image.pixels[ pixel ];

                const auto green = image.pixels[ pixel + 1u ];

                const auto blue = image.pixels[ pixel + 2u ];

                const auto gray = std::max( { red, green, blue } );
                image.pixels[ pixel ] = gray;

                image.pixels[ pixel + 1u ] = gray;

                image.pixels[ pixel + 2u ] = gray;
            }

            write_rgba_png( path, image.width, image.height, image.pixels );
        }
    }

    void export_aim_sprites( std::span<const std::uint8_t> rgba,
        std::uint32_t width,
        std::uint32_t height,
        const std::filesystem::path &directory )
    {
        using namespace runtime::stage::gameplay_interface_atlas;
        const std::array<rendering::Rect, 2> sources{
            aim_direction_arrow,
            wing_arrow
        };

        const std::array<const char *, 2> filenames{
            "aim.png",
            "wing.png"
        };

        if( rgba.size() != static_cast<std::size_t>( width ) * height * 4u )
        {
            return;
        }

        for( const auto &source : sources )
        {
            if( width < source.x + source.w || height < source.y + source.h )
            {
                return;
            }
        }

        const ui::BattleImage atlas{
            static_cast<std::int32_t>( width ),
            static_cast<std::int32_t>( height ),
            std::vector<std::uint8_t>( rgba.begin(), rgba.end() )
        };

        for( std::size_t index = 0; index < sources.size(); ++index )
        {
            const auto path = directory / filenames[ index ];

            if( std::filesystem::is_regular_file( path ) )
            {
                continue;
            }

            const auto image = ui::crop_battle_image( atlas, sources[ index ] );
            write_rgba_png( path, image.width, image.height, image.pixels );
        }
    }

    void export_battle_sprites( std::span<const std::uint8_t> rgba,
        std::uint32_t width,
        std::uint32_t height,
        const std::filesystem::path &atlas_path,
        const std::filesystem::path &centre_directory )
    {
        const std::string stem = utils::uppercase_ascii( atlas_path.stem().string() );
        const bool portable_hud =
            atlas_path.parent_path().filename() == "HUD" && stem.size() == 4 && stem.starts_with( "ST" );

        if( centre_directory.empty() || ( stem != "ITFC" && !portable_hud ) )
        {
            return;
        }

        const auto battle_directory = centre_directory.parent_path().parent_path();
        export_aim_sprites( rgba, width, height, battle_directory / "aim/default" );

        const auto &trace = runtime::stage::gameplay_interface_atlas::trace_line_body;

        if( width >= trace.x + trace.w && height >= trace.y + trace.h &&
            rgba.size() == static_cast<std::size_t>( width ) * height * 4u )
        {
            export_note_sprites( rgba, width, height, battle_directory / "notes/default" );
        }

        const auto &source = runtime::stage::gameplay_interface_atlas::center_icon;

        const auto left = static_cast<std::uint32_t>( source.x );

        const auto top = static_cast<std::uint32_t>( source.y );

        const auto icon_width = static_cast<std::uint32_t>( source.w );

        const auto icon_height = static_cast<std::uint32_t>( source.h );
        const std::size_t required_bytes = static_cast<std::size_t>( width ) * height * 4;

        if( width < left + icon_width || height < top + icon_height || rgba.size() < required_bytes )
        {
            return;
        }

        std::vector<std::uint8_t> icon( static_cast<std::size_t>( icon_width ) * icon_height * 4 );

        for( bool pressed : { false, true } )
        {
            const auto output_path = centre_directory / ( pressed ? "pressed.png" : "normal.png" );

            if( std::filesystem::is_regular_file( output_path ) )
            {
                continue;
            }

            const auto &tint = pressed ? runtime::stage::gameplay_interface_atlas::center_icon_pressed_tint
                                       : runtime::stage::gameplay_interface_atlas::center_icon_idle_tint;

            for( std::uint32_t y = 0; y < icon_height; ++y )
            {
                for( std::uint32_t x = 0; x < icon_width; ++x )
                {
                    const std::size_t input = ( static_cast<std::size_t>( top + y ) * width + left + x ) * 4;
                    const std::size_t output = ( static_cast<std::size_t>( y ) * icon_width + x ) * 4;
                    icon[ output ] = rgba[ input ];

                    icon[ output + 1 ] =
                        static_cast<std::uint8_t>( static_cast<float>( rgba[ input + 1 ] ) * tint.g + 0.5f );

                    icon[ output + 2 ] = rgba[ input + 2 ];

                    icon[ output + 3 ] = rgba[ input + 3 ];
                }
            }

            write_rgba_png( output_path, icon_width, icon_height, icon );
        }
    }

    ConvertedTexture convert_imx_to_png( std::span<const std::uint8_t> bytes,
        const std::filesystem::path &output_path,
        std::string source_name,
        const std::filesystem::path &centre_directory )
    {
        const filetypes::imx::Image image = filetypes::imx::decode( bytes );
        const std::vector<std::uint8_t> rgba = process_texture_rgba( image );
        write_rgba_png( output_path, image.width, image.height, rgba );
        export_battle_sprites( rgba, image.width, image.height, output_path, centre_directory );

        return ConvertedTexture{
            .source_name = std::move( source_name ),
            .file_name = output_path.filename().generic_string(),
            .width = image.width,
            .height = image.height,
            .storage_mode = image_convert::storage_mode_name( image.storage_mode )
        };
    }

    std::vector<ConvertedTexture> convert_xgm_textures_to_png( std::span<const std::uint8_t> bytes,
        const std::filesystem::path &output_directory,
        const std::filesystem::path &centre_directory )
    {
        const filetypes::xgm::Metadata metadata = filetypes::xgm::decode_metadata( bytes );
        std::vector<ConvertedTexture> textures;
        textures.reserve( metadata.textures.size() );
        std::unordered_set<std::string> used_names;

        std::filesystem::create_directories( output_directory );

        for( std::size_t index = 0; index < metadata.textures.size(); ++index )
        {
            const filetypes::xgm::TextureMetadata &texture_metadata = metadata.textures[ index ];

            if( texture_metadata.data_offset > bytes.size() ||
                texture_metadata.size > bytes.size() - texture_metadata.data_offset )
            {
                throw std::runtime_error( "Embedded XGM texture extends beyond its source data" );
            }

            const std::span<const std::uint8_t> imx_bytes =
                bytes.subspan( texture_metadata.data_offset, texture_metadata.size );

            const filetypes::imx::Image image = filetypes::imx::decode( imx_bytes );
            std::string stem =
                piko::utils::safe_ascii_stem( texture_metadata.filename, "TEXTURE_" + std::to_string( index ) );

            std::string candidate = stem + ".png";

            for( std::size_t suffix = 1; used_names.contains( candidate ); ++suffix )
            {
                candidate = stem + "_" + std::to_string( suffix ) + ".png";
            }

            used_names.insert( candidate );
            const std::filesystem::path output_path = output_directory / candidate;
            const std::vector<std::uint8_t> rgba = process_texture_rgba( image );
            write_rgba_png( output_path, image.width, image.height, rgba );
            export_battle_sprites( rgba, image.width, image.height, output_path, centre_directory );

            textures.push_back( ConvertedTexture{
                .source_name = texture_metadata.filename,
                .file_name = candidate,
                .width = image.width,
                .height = image.height,
                .storage_mode = image_convert::storage_mode_name( image.storage_mode ),
                .source_index = static_cast<std::uint32_t>( index ),
                .usage_hint = texture_metadata.usage_hint
            } );
        }

        image_convert::write_texture_manifest( output_directory / "textures.json", textures );
        return textures;
    }
}
