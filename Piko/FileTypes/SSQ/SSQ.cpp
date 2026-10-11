#include "SSQ.hpp"
#include "Utils/BinaryUtil.hpp"
#include "Utils/StrUtil.hpp"
#include "SSQJson.hpp"

#include <cctype>

#include <cstddef>
#include <cstring>
#include <fstream>
#include <sstream>

namespace piko::filetypes::ssq
{
    namespace parser
    {
        class Reader
        {
        public:
            explicit Reader( std::span<const std::uint8_t> bytes ) : bytes_( bytes )
            {
            }

            [[nodiscard]] std::size_t offset() const
            {
                return offset_;
            }

            [[nodiscard]] std::size_t remaining() const
            {
                return bytes_.size() - offset_;
            }

            void require( std::size_t size, const char *context ) const
            {
                if( size > remaining() )
                {
                    throw DecodeError( std::string( "Unexpected end of SSQ while reading " ) + context );
                }
            }

            void skip( std::size_t size, const char *context )
            {
                require( size, context );
                offset_ += size;
            }

            std::vector<std::uint8_t> read_bytes( std::size_t size, const char *context )
            {
                require( size, context );
                std::vector<std::uint8_t> out( bytes_.begin() + static_cast<std::ptrdiff_t>( offset_ ),
                    bytes_.begin() + static_cast<std::ptrdiff_t>( offset_ + size ) );

                offset_ += size;
                return out;
            }

            bool magic_is( const char *magic ) const
            {
                return remaining() >= 4 && std::memcmp( bytes_.data() + offset_, magic, 4 ) == 0;
            }

            void expect_magic( const char *magic, const char *context )
            {
                require( 4, context );

                if( std::memcmp( bytes_.data() + offset_, magic, 4 ) != 0 )
                {
                    std::ostringstream ss;
                    ss << "SSQ expected " << magic << " while reading " << context << " at 0x" << std::hex << offset_;
                    throw DecodeError( ss.str() );
                }

                offset_ += 4;
            }

            std::uint32_t read_u32( const char *context )
            {
                require( 4, context );
                const std::uint8_t *p = bytes_.data() + offset_;
                offset_ += 4;
                return piko::utils::read_le_u32( p );
            }

            float read_f32( const char *context )
            {
                const std::uint32_t bits = read_u32( context );
                float value = 0.0f;
                std::memcpy( &value, &bits, sizeof( value ) );
                return value;
            }

            std::string read_fixed_string( std::size_t size, const char *context )
            {
                require( size, context );
                const char *begin = reinterpret_cast<const char *>( bytes_.data() + offset_ );
                const char *end = begin + size;
                const char *nul = begin;
                while( nul < end && *nul != '\0' )
                {
                    ++nul;
                }

                offset_ += size;
                return std::string( begin, nul );
            }

            xg::Vec3 read_vec3( const char *context )
            {
                return xg::Vec3{ read_f32( context ), read_f32( context ), read_f32( context ) };
            }

            xg::Quat read_quat( const char *context )
            {
                return xg::Quat{ read_f32( context ), read_f32( context ), read_f32( context ), read_f32( context ) };
            }

        private:
            std::span<const std::uint8_t> bytes_;
            std::size_t offset_ = 0;
        };

        xg::Vec3 convert_vec3_lh_to_rh( xg::Vec3 v )
        {
            v.z = -v.z;
            return v;
        }

        xg::Quat convert_quat_lh_to_rh( xg::Quat q )
        {

            q.x = -q.x;

            q.y = -q.y;
            return q;
        }

        float word_as_float( std::uint32_t word )
        {
            float value = 0.0f;
            std::memcpy( &value, &word, sizeof( value ) );
            return value;
        }

        std::string fixed_string_from_words( const std::array<std::uint32_t, 17> &words, std::size_t first_word )
        {
            std::string value;
            value.reserve( 16 );

            for( std::size_t i = 0; i < 4; ++i )
            {
                const std::uint32_t word = words[ first_word + i ];

                for( std::size_t byte = 0; byte < 4; ++byte )
                {
                    const char ch = static_cast<char>( ( word >> ( byte * 8u ) ) & 0xFFu );

                    if( ch == '\0' )
                    {
                        return value;
                    }

                    value.push_back( ch );
                }
            }

            return value;
        }

        void read_gmpx_prefix( Reader &reader, std::uint32_t &version, const char *context )
        {
            reader.expect_magic( "GMPX", context );
            version = reader.read_u32( "GMPX version" );
            (void)reader.read_u32( "GMPX size-ish" );
            reader.skip( 0x08, "GMPX zero/ignored" );
            reader.skip( 0x10, "GMPX garbage/ignored" );
        }

        Vec3Key read_position_key( Reader &reader )
        {
            Vec3Key key{};
            key.frame = reader.read_f32( "position frame" );

            key.reciprocal_frames_to_next = reader.read_f32( "position reciprocal" );

            key.value = convert_vec3_lh_to_rh( reader.read_vec3( "position xyz" ) );

            key.interpolate = reader.read_u32( "position interpolate" ) != 0;
            (void)reader.read_u32( "position frame160" );
            (void)reader.read_u32( "position unknown" );
            return key;
        }

        QuatKey read_rotation_key( Reader &reader )
        {
            QuatKey key{};
            key.frame = reader.read_f32( "rotation frame" );

            key.reciprocal_frames_to_next = reader.read_f32( "rotation reciprocal" );

            key.raw_value = reader.read_quat( "rotation quat" );

            key.value = convert_quat_lh_to_rh( key.raw_value );

            key.interpolate = reader.read_u32( "rotation interpolate" ) != 0;
            (void)reader.read_u32( "rotation frame160" );
            return key;
        }

        ScaleKey read_scale_key( Reader &reader )
        {
            ScaleKey key{};
            key.frame = reader.read_f32( "scale frame" );

            key.reciprocal_frames_to_next = reader.read_f32( "scale reciprocal" );

            key.value = reader.read_vec3( "scale xyz" );
            (void)reader.read_u32( "scale unknown0" );
            key.interpolate = reader.read_u32( "scale interpolate" ) != 0;
            (void)reader.read_u32( "scale unknown1" );
            return key;
        }

        rendering::Color read_rgb_u32_color( Reader &reader, const char *context )
        {
            const float r = static_cast<float>( reader.read_u32( context ) ) / 255.0f;
            const float g = static_cast<float>( reader.read_u32( context ) ) / 255.0f;
            const float b = static_cast<float>( reader.read_u32( context ) ) / 255.0f;
            return rendering::Color{ r, g, b, 1.0f };
        }

        ColorKey read_color_key( Reader &reader )
        {
            ColorKey key{};
            key.frame = reader.read_f32( "ambient color frame" );

            key.reciprocal_frames_to_next = reader.read_f32( "ambient color reciprocal" );

            key.value = rendering::Color{
                reader.read_f32( "ambient color red" ),
                reader.read_f32( "ambient color green" ),
                reader.read_f32( "ambient color blue" ),
                1.0f
            };

            key.interpolate = reader.read_u32( "ambient color interpolate" ) != 0;
            (void)reader.read_u32( "ambient color frame160" );
            (void)reader.read_u32( "ambient color unknown" );
            return key;
        }

        std::uint32_t raw_u32( const std::vector<std::uint8_t> &bytes, std::size_t offset )
        {
            if( offset + 4 > bytes.size() )
            {
                return 0;
            }

            return piko::utils::read_le_u32( bytes.data() + offset );
        }

        float raw_f32( const std::vector<std::uint8_t> &bytes, std::size_t offset )
        {
            const std::uint32_t bits = raw_u32( bytes, offset );
            float value = 0.0f;
            std::memcpy( &value, &bits, sizeof( value ) );
            return value;
        }

        rendering::Color raw_rgb_f32_color( const std::vector<std::uint8_t> &bytes, std::size_t offset )
        {
            return rendering::Color{
                raw_f32( bytes, offset + 0x00 ),
                raw_f32( bytes, offset + 0x04 ),
                raw_f32( bytes, offset + 0x08 ),
                1.0f
            };
        }

        xg::Vec3 raw_vec3_f32( const std::vector<std::uint8_t> &bytes, std::size_t offset )
        {
            return xg::Vec3{
                raw_f32( bytes, offset + 0x00 ),
                raw_f32( bytes, offset + 0x04 ),
                raw_f32( bytes, offset + 0x08 )
            };
        }

        xg::Quat raw_quat_f32( const std::vector<std::uint8_t> &bytes, std::size_t offset )
        {
            return xg::Quat{
                raw_f32( bytes, offset + 0x00 ),
                raw_f32( bytes, offset + 0x04 ),
                raw_f32( bytes, offset + 0x08 ),
                raw_f32( bytes, offset + 0x0C )
            };
        }

        ModelAnimKey read_model_anim_key( Reader &reader )
        {
            ModelAnimKey key{};
            key.frame = reader.read_f32( "model anim frame" );

            key.reciprocal_frames_to_next = reader.read_f32( "model anim reciprocal" );

            key.animation_index = reader.read_u32( "model anim index" );

            key.use_current_player_time_origin = reader.read_u32( "model anim current-player-time origin" ) != 0;

            key.hidden = reader.read_u32( "model anim hide flag" ) != 0;

            key.state_driven = reader.read_u32( "model anim state driven" ) != 0;

            key.loop = reader.read_u32( "model anim loop" ) != 0;

            key.model_property = reader.read_u32( "model anim runtime property" );

            key.transition = reader.read_u32( "model anim transition" ) != 0;

            key.drop_shadow = reader.read_u32( "model anim drop shadow" ) != 0;

            key.unknown_28 = reader.read_u32( "model anim unknown 28" );

            key.frame_times_160 = reader.read_u32( "model anim frame160" );
            return key;
        }

        ModelAnimKey read_v1000_model_anim_key( Reader &reader )
        {
            ModelAnimKey key{};
            key.frame = reader.read_f32( "v1000 model anim frame" );

            key.reciprocal_frames_to_next = reader.read_f32( "v1000 model anim reciprocal" );

            key.animation_index = reader.read_u32( "v1000 model anim index" );

            key.use_current_player_time_origin = reader.read_u32( "v1000 model anim time origin" ) != 0;

            key.hidden = reader.read_u32( "v1000 model anim hidden" ) != 0;

            key.state_driven = reader.read_u32( "v1000 model anim state" ) != 0;

            key.loop = reader.read_u32( "v1000 model anim loop" ) != 0;

            key.model_property = reader.read_u32( "v1000 model anim property" );
            return key;
        }

        void read_model_setup( Reader &reader, ModelEntry &model, std::uint32_t stage_version )
        {
            const bool stage_v1000 = stage_version == 0x1000;
            std::uint32_t version = 0;
            read_gmpx_prefix( reader, version, "ModelSetup" );

            const std::uint32_t num_positions = reader.read_u32( "model num positions" );
            const std::uint32_t num_rotations = reader.read_u32( "model num rotations" );

            model.position_keys.reserve( num_positions );

            for( std::uint32_t i = 0; i < num_positions; ++i )
            {
                Vec3Key key = read_position_key( reader );

                if( i == 0 )
                {
                    model.position = key.value;
                }

                model.position_keys.push_back( key );
            }

            model.rotation_keys.reserve( num_rotations );

            for( std::uint32_t i = 0; i < num_rotations; ++i )
            {
                QuatKey key = read_rotation_key( reader );

                if( i == 0 )
                {
                    model.rotation = key.value;

                    model.raw_rotation = key.raw_value;
                }

                model.rotation_keys.push_back( key );
            }

            const std::uint32_t num_anims = reader.read_u32( "model num anims" );
            bool visible_from_first_anim = true;

            if( num_anims >= ( stage_v1000 ? 1u : 2u ) )
            {
                model.anim_keys.reserve( num_anims );

                for( std::uint32_t i = 0; i < num_anims; ++i )
                {
                    ModelAnimKey key =
                        stage_v1000 ? read_v1000_model_anim_key( reader ) : read_model_anim_key( reader );

                    if( i == 0 )
                    {
                        visible_from_first_anim = !key.hidden;
                    }

                    model.anim_keys.push_back( key );
                }

                model.visible = visible_from_first_anim;
            }

            if( version >= 0x1100 )
            {
                const std::uint32_t num_scalars = reader.read_u32( "model num scalars" );

                if( stage_v1000 )
                {
                    reader.skip( static_cast<std::size_t>( num_scalars ) * 0x20u, "v1000 model scalar payload" );
                    const std::uint32_t v1000_scalar_count = num_scalars == 0u ? 1u : num_scalars;
                    reader.skip( static_cast<std::size_t>( v1000_scalar_count ) * 0x10u, "v1000 model scalar footer" );

                    model.scale = xg::Vec3{ 1.0f, 1.0f, 1.0f };
                    model.scale_keys.clear();
                }
                else
                {
                    if( num_scalars >= 2u )
                    {
                        model.scale_keys.reserve( num_scalars );

                        for( std::uint32_t i = 0; i < num_scalars; ++i )
                        {
                            ScaleKey key = read_scale_key( reader );

                            if( i == 0 )
                            {
                                model.scale = key.value;
                            }

                            model.scale_keys.push_back( key );
                        }
                    }

                    reader.skip( 0x40u, "model ignored scalar footer" );
                }
            }

            if( ( model.model_type == 1 || model.model_type == 2 || model.model_type == 3 ) && version >= 0x1300 )
            {
                const std::uint32_t num_controllables = reader.read_u32( "player model controllable count" );
                model.controllables.clear();
                model.controllables.reserve( num_controllables );

                for( std::uint32_t i = 0; i < num_controllables; ++i )
                {
                    Controllable controllable{};
                    controllable.min_angle = reader.read_f32( "player model controllable min angle" );

                    controllable.max_angle = reader.read_f32( "player model controllable max angle" );

                    controllable.descriptor = reader.read_u32( "player model controllable descriptor" );

                    controllable.event_flag = reader.read_u32( "player model controllable event flag" );

                    controllable.animation_index = reader.read_u32( "player model controllable anim index" );

                    controllable.hold_last_frame_count = reader.read_u32( "player model controllable hold frames" );

                    controllable.forward_playback = reader.read_u32( "player model controllable direction" ) != 0;

                    controllable.loop_playback = reader.read_u32( "player model controllable loop playback" ) != 0;

                    controllable.use_current_global_time_origin =
                        reader.read_u32( "player model controllable current-global-time origin" ) != 0;

                    controllable.random_connection = reader.read_u32( "player model controllable random" ) != 0;
                    reader.skip( 0x08, "player model controllable padding" );
                    model.controllables.push_back( std::move( controllable ) );
                }

                for( std::uint32_t i = 0; i < num_controllables; ++i )
                {
                    const std::uint32_t connections = reader.read_u32( "player model connection count" );

                    if( i < model.controllables.size() )
                    {
                        model.controllables[ i ].connections.reserve( connections );

                        for( std::uint32_t j = 0; j < connections; ++j )
                        {
                            model.controllables[ i ].connections.push_back(
                                reader.read_u32( "player model connection index" ) );
                        }
                    }
                    else
                    {
                        reader.skip( static_cast<std::size_t>( connections ) * 4u, "player model connections" );
                    }
                }

                for( std::uint32_t i = 0; i < num_controllables; ++i )
                {
                    const std::uint32_t raw_default = reader.read_u32( "player model event default" );

                    if( i < model.controllables.size() )
                    {
                        model.controllables[ i ].default_event = static_cast<std::int32_t>( raw_default );
                    }
                }
            }
            else if( ( model.model_type == 5 || model.model_type == 6 || model.model_type == 7 ||
                         model.model_type == 8 ) &&
                     version >= 0x1200 )
            {
                model.has_effect_control = true;

                for( std::uint32_t &word : model.effect_control_words )
                {
                    word = reader.read_u32( "attack/defense model control" );
                }

                // sub_1A5C90
                model.effect_control.reference_length = word_as_float( model.effect_control_words[ 0 ] );

                model.effect_control.source_offset = convert_vec3_lh_to_rh( xg::Vec3{
                    word_as_float( model.effect_control_words[ 1 ] ),
                    word_as_float( model.effect_control_words[ 2 ] ),
                    word_as_float( model.effect_control_words[ 3 ] )
                } );

                model.effect_control.target_offset = convert_vec3_lh_to_rh( xg::Vec3{
                    word_as_float( model.effect_control_words[ 4 ] ),
                    word_as_float( model.effect_control_words[ 5 ] ),
                    word_as_float( model.effect_control_words[ 6 ] )
                } );

                model.effect_control.source_model_name = fixed_string_from_words( model.effect_control_words, 7 );

                model.effect_control.target_model_name = fixed_string_from_words( model.effect_control_words, 11 );
            }
        }

        LightSetup read_light_setup( Reader &reader )
        {
            LightSetup setup{};
            setup.prelude = reader.read_bytes( 0x50, "light prelude" );
            reader.expect_magic( "GMLT", "LightSetup" );
            setup.version = reader.read_u32( "light version" );
            (void)reader.read_u32( "light size-ish" );
            reader.skip( 0x08, "light zero/ignored" );
            reader.skip( 0x10, "light garbage/ignored" );
            setup.num20 = reader.read_u32( "light num20" );

            setup.num30 = reader.read_u32( "light num30" );

            if( setup.num20 >= 2 )
            {
                setup.block20 =
                    reader.read_bytes( static_cast<std::size_t>( setup.num20 ) * 0x20u, "light num20 entries" );
            }

            if( setup.num30 >= 2 )
            {
                setup.block30 =
                    reader.read_bytes( static_cast<std::size_t>( setup.num30 ) * 0x30u, "light num30 entries" );
            }

            if( setup.prelude.size() >= 0x28u )
            {
                // Retail stores the light type at +0x00 and the evaluated
                // direction vector at +0x1C.  The latter is not a quaternion.
                setup.light_type = raw_u32( setup.prelude, 0x00 );

                setup.enabled = setup.light_type == 1u;

                setup.primary_color = raw_rgb_f32_color( setup.prelude, 0x04 );

                setup.secondary_color = raw_rgb_f32_color( setup.prelude, 0x10 );

                setup.raw_direction = raw_vec3_f32( setup.prelude, 0x1C );

                setup.direction = convert_vec3_lh_to_rh( setup.raw_direction );
            }

            if( setup.num20 >= 2 && setup.block20.size() >= static_cast<std::size_t>( setup.num20 ) * 0x20u )
            {
                setup.rotation_keys.reserve( setup.num20 );

                for( std::uint32_t i = 0; i < setup.num20; ++i )
                {
                    const std::size_t off = static_cast<std::size_t>( i ) * 0x20u;
                    LightRotationKey key{};
                    key.frame = raw_f32( setup.block20, off + 0x00 );

                    key.reciprocal_frames_to_next = raw_f32( setup.block20, off + 0x04 );

                    key.raw_value = raw_quat_f32( setup.block20, off + 0x08 );

                    key.value = convert_quat_lh_to_rh( key.raw_value );

                    key.interpolate = raw_u32( setup.block20, off + 0x18 ) != 0;
                    setup.rotation_keys.push_back( key );
                }
            }

            if( setup.num30 >= 2 && setup.block30.size() >= static_cast<std::size_t>( setup.num30 ) * 0x30u )
            {
                setup.color_keys.reserve( setup.num30 );

                for( std::uint32_t i = 0; i < setup.num30; ++i )
                {
                    const std::size_t off = static_cast<std::size_t>( i ) * 0x30u;
                    LightColorKey key{};
                    key.frame = raw_f32( setup.block30, off + 0x00 );

                    key.reciprocal_frames_to_next = raw_f32( setup.block30, off + 0x04 );

                    key.primary = raw_rgb_f32_color( setup.block30, off + 0x08 );

                    key.secondary = raw_rgb_f32_color( setup.block30, off + 0x14 );

                    key.interpolate = raw_u32( setup.block30, off + 0x20 ) != 0;
                    setup.color_keys.push_back( key );
                }
            }

            return setup;
        }

        Camera read_camera_setup( Reader &reader )
        {
            std::uint32_t version = 0;
            read_gmpx_prefix( reader, version, "CameraSetup" );

            Camera camera{};
            camera.fog_color = rendering::Color{
                reader.read_f32( "camera fog red" ),
                reader.read_f32( "camera fog green" ),
                reader.read_f32( "camera fog blue" ),
                reader.read_f32( "camera fog alpha" )
            };

            camera.base_ambient_color = read_rgb_u32_color( reader, "camera base ambient rgb" );

            camera.ambient_color = camera.base_ambient_color;
            (void)reader.read_u32( "camera base diffuse enable" );
            camera.base_diffuse_color = read_rgb_u32_color( reader, "camera base diffuse rgb" );

            camera.fov_degrees = reader.read_f32( "camera base fov" );

            camera.aspect = reader.read_f32( "camera base aspect" );

            camera.z_near = reader.read_f32( "camera base znear" );

            camera.z_far = reader.read_f32( "camera base zfar" );
            (void)reader.read_u32( "camera base unknown" );

            const std::uint32_t num_positions = reader.read_u32( "camera num positions" );
            const std::uint32_t num_rotations = reader.read_u32( "camera num rotations" );

            camera.position_keys.reserve( num_positions );

            for( std::uint32_t i = 0; i < num_positions; ++i )
            {
                Vec3Key key = read_position_key( reader );

                if( i == 0 )
                {
                    camera.position = key.value;
                }

                camera.position_keys.push_back( key );
            }

            camera.rotation_keys.reserve( num_rotations );

            for( std::uint32_t i = 0; i < num_rotations; ++i )
            {
                QuatKey key = read_rotation_key( reader );

                if( i == 0 )
                {
                    camera.rotation = key.value;

                    camera.raw_rotation = key.raw_value;
                }

                camera.rotation_keys.push_back( key );
            }

            const std::uint32_t num_projections = reader.read_u32( "camera num projections" );

            if( num_projections >= 2 )
            {
                camera.projection_keys.reserve( num_projections );

                for( std::uint32_t i = 0; i < num_projections; ++i )
                {
                    ProjectionKey key{};
                    key.frame = reader.read_f32( "projection frame" );

                    key.reciprocal_frames_to_next = reader.read_f32( "projection reciprocal" );

                    key.fov_degrees = reader.read_f32( "projection fov" );

                    key.aspect = reader.read_f32( "projection aspect" );

                    key.z_near = reader.read_f32( "projection znear" );

                    key.z_far = reader.read_f32( "projection zfar" );
                    (void)reader.read_u32( "projection unknown" );
                    key.interpolate = reader.read_u32( "projection interpolate" ) != 0;

                    if( i == 0 )
                    {
                        camera.fov_degrees = key.fov_degrees;

                        camera.aspect = key.aspect;

                        camera.z_near = key.z_near;

                        camera.z_far = key.z_far;
                    }

                    camera.projection_keys.push_back( key );
                }
            }

            const std::uint32_t num_ambient = reader.read_u32( "camera num ambient colors" );

            if( num_ambient >= 2 )
            {
                camera.ambient_keys.reserve( num_ambient );

                for( std::uint32_t i = 0; i < num_ambient; ++i )
                {
                    ColorKey key = read_color_key( reader );

                    if( i == 0 )
                    {
                        camera.ambient_color = key.value;
                    }

                    camera.ambient_keys.push_back( key );
                }
            }

            const std::uint32_t num_lights = reader.read_u32( "camera num lights" );
            camera.light_setups.reserve( num_lights );

            for( std::uint32_t i = 0; i < num_lights; ++i )
            {
                camera.light_setups.push_back( read_light_setup( reader ) );
            }

            if( version >= 0x1200 )
            {
                const std::uint32_t num_unknown = reader.read_u32( "camera num unknown 0x40" );

                if( num_unknown >= 2 )
                {
                    reader.skip( static_cast<std::size_t>( num_unknown ) * 0x40u, "camera unknown 0x40 entries" );
                }
            }

            return camera;
        }

        FixedSpriteDescriptor read_fixed_sprite_descriptor( Reader &reader )
        {
            FixedSpriteDescriptor descriptor{};
            descriptor.texture_index = reader.read_u32( "fixed sprite texture index" );

            descriptor.source_x = reader.read_f32( "fixed sprite source x" );

            descriptor.source_y = reader.read_f32( "fixed sprite source y" );

            descriptor.source_width = reader.read_f32( "fixed sprite source width" );

            descriptor.source_height = reader.read_f32( "fixed sprite source height" );

            descriptor.position = convert_vec3_lh_to_rh( reader.read_vec3( "fixed sprite position" ) );

            descriptor.width = reader.read_f32( "fixed sprite width" );

            descriptor.height = reader.read_f32( "fixed sprite height" );

            for( std::uint32_t &word : descriptor.render_words )
            {
                word = reader.read_u32( "fixed sprite render word" );
            }

            descriptor.owner_selector = descriptor.render_words[ 7 ];
            return descriptor;
        }

        FixedSpriteTransformKey read_fixed_sprite_transform_key( Reader &reader )
        {
            FixedSpriteTransformKey key{};
            key.frame = reader.read_f32( "fixed sprite transform frame" );

            key.reciprocal_frames_to_next = reader.read_f32( "fixed sprite transform reciprocal" );

            key.position = convert_vec3_lh_to_rh( reader.read_vec3( "fixed sprite transform position" ) );

            key.width = reader.read_f32( "fixed sprite transform width" );

            key.height = reader.read_f32( "fixed sprite transform height" );

            key.hidden = reader.read_u32( "fixed sprite transform hidden" ) != 0;

            key.unknown_20 = reader.read_u32( "fixed sprite transform unknown 20" );

            key.interpolate = reader.read_u32( "fixed sprite transform interpolate" ) != 0;

            key.external_visibility_control = reader.read_u32( "fixed sprite transform external visibility" ) != 0;

            key.frame_times_160 = reader.read_u32( "fixed sprite transform frame160" );
            return key;
        }

        ColorKey read_fixed_sprite_color_key( Reader &reader )
        {
            ColorKey key{};
            key.frame = reader.read_f32( "fixed sprite color frame" );

            key.reciprocal_frames_to_next = reader.read_f32( "fixed sprite color reciprocal" );

            key.value = rendering::Color{
                reader.read_f32( "fixed sprite color red" ),
                reader.read_f32( "fixed sprite color green" ),
                reader.read_f32( "fixed sprite color blue" ),
                reader.read_f32( "fixed sprite color alpha" )
            };

            key.interpolate = reader.read_u32( "fixed sprite color interpolate" ) != 0;
            (void)reader.read_u32( "fixed sprite color frame160" );
            return key;
        }

        FixedSpriteFrameKey read_fixed_sprite_frame_key( Reader &reader )
        {
            FixedSpriteFrameKey key{};
            key.frame = reader.read_f32( "fixed sprite frame position" );

            key.reciprocal_frames_to_next = reader.read_f32( "fixed sprite frame reciprocal" );

            key.source_x = reader.read_f32( "fixed sprite frame source x" );

            key.source_y = reader.read_f32( "fixed sprite frame source y" );

            key.source_width = reader.read_f32( "fixed sprite frame source width" );

            key.source_height = reader.read_f32( "fixed sprite frame source height" );

            key.interpolate = reader.read_u32( "fixed sprite frame interpolate" ) != 0;

            key.frame_times_160 = reader.read_u32( "fixed sprite frame frame160" );
            return key;
        }

        FixedSprite read_fixed_sprite( Reader &reader, const FixedSpriteDescriptor &descriptor )
        {
            reader.expect_magic( "GMSP", "FixedSprite" );
            FixedSprite sprite{};
            sprite.version = reader.read_u32( "fixed sprite version" );

            sprite.descriptor = descriptor;
            reader.skip( 0x0C, "fixed sprite zero/ignored" );
            reader.skip( 0x10, "fixed sprite garbage/ignored" );

            for( std::uint32_t &word : sprite.base_words )
            {
                word = reader.read_u32( "fixed sprite base word" );
            }

            const std::uint32_t num_transforms = reader.read_u32( "fixed sprite transform count" );
            const std::uint32_t num_colors = reader.read_u32( "fixed sprite color count" );
            const std::uint32_t num_frames = reader.read_u32( "fixed sprite frame count" );

            if( num_transforms >= 2 )
            {
                sprite.transform_keys.reserve( num_transforms );

                for( std::uint32_t i = 0; i < num_transforms; ++i )
                {
                    sprite.transform_keys.push_back( read_fixed_sprite_transform_key( reader ) );
                }
            }

            if( num_colors >= 2 )
            {
                sprite.color_keys.reserve( num_colors );

                for( std::uint32_t i = 0; i < num_colors; ++i )
                {
                    sprite.color_keys.push_back( read_fixed_sprite_color_key( reader ) );
                }
            }

            if( num_frames >= 2 )
            {
                sprite.frame_keys.reserve( num_frames );

                for( std::uint32_t i = 0; i < num_frames; ++i )
                {
                    sprite.frame_keys.push_back( read_fixed_sprite_frame_key( reader ) );
                }
            }

            return sprite;
        }

        std::vector<FixedSprite> read_sprites_setup( Reader &reader )
        {
            reader.expect_magic( "GMSP", "SpritesSetup" );
            (void)reader.read_u32( "sprites version" );
            reader.skip( 0x0C, "sprites zero/ignored" );
            reader.skip( 0x10, "sprites garbage/ignored" );
            const std::uint32_t num_fixed = reader.read_u32( "sprites fixed count" );
            const std::uint32_t num_unk1 = reader.read_u32( "sprites unk1 count" );
            const std::uint32_t num_unk2 = reader.read_u32( "sprites unk2 count" );
            (void)reader.read_u32( "sprites reserved" );

            std::vector<FixedSprite> sprites;

            if( num_fixed > 0 )
            {
                reader.expect_magic( "GMF0", "FixedSpritesSetup" );
                (void)reader.read_u32( "fixed sprite setup version" );
                reader.skip( 0x0C, "fixed sprite setup zero/ignored" );
                reader.skip( 0x10, "fixed sprite setup garbage/ignored" );
                const std::uint32_t fixed_count_again = reader.read_u32( "fixed sprite setup count" );

                if( fixed_count_again != num_fixed )
                {
                    throw DecodeError( "SSQ fixed sprite counts do not match" );
                }

                std::vector<FixedSpriteDescriptor> descriptors;
                descriptors.reserve( fixed_count_again );

                for( std::uint32_t i = 0; i < fixed_count_again; ++i )
                {
                    descriptors.push_back( read_fixed_sprite_descriptor( reader ) );
                }

                sprites.reserve( fixed_count_again );

                for( const FixedSpriteDescriptor &descriptor : descriptors )
                {
                    sprites.push_back( read_fixed_sprite( reader, descriptor ) );
                }
            }

            if( num_unk1 != 0 || num_unk2 != 0 )
            {
                throw DecodeError( "SSQ contains unsupported non-fixed sprite setup" );
            }

            return sprites;
        }

        TextureAnimation read_tex_anim( Reader &reader )
        {
            reader.skip( 4, "texanim magic/zero" );
            TextureAnimation animation{};
            animation.version = reader.read_u32( "texanim version" );
            reader.skip( 0x0C, "texanim zero/ignored" );
            reader.skip( 0x10, "texanim garbage/ignored" );
            animation.paste_x = reader.read_u32( "texanim paste x" );

            animation.paste_y = reader.read_u32( "texanim paste y" );

            animation.texture_name = reader.read_fixed_string( 0x10, "texanim texture name" );

            animation.texture_words[ 0 ] = reader.read_u32( "texanim texture word 0" );

            animation.texture_words[ 1 ] = reader.read_u32( "texanim texture word 1" );
            const std::uint32_t num_cutouts = reader.read_u32( "texanim cutout count" );
            animation.cutouts.reserve( num_cutouts );

            for( std::uint32_t i = 0; i < num_cutouts; ++i )
            {
                TextureCutout cutout{};
                cutout.x0 = reader.read_f32( "texanim cutout x0" );

                cutout.y0 = reader.read_f32( "texanim cutout y0" );

                cutout.x1 = reader.read_f32( "texanim cutout x1" );

                cutout.y1 = reader.read_f32( "texanim cutout y1" );
                animation.cutouts.push_back( cutout );
            }

            const std::uint32_t num_frames = reader.read_u32( "texanim frame count" );
            animation.frames.reserve( num_frames );

            for( std::uint32_t i = 0; i < num_frames; ++i )
            {
                TextureAnimFrame frame{};
                frame.frame = reader.read_f32( "texanim frame" );

                frame.reciprocal_frames_to_next = reader.read_f32( "texanim reciprocal" );

                frame.cutout_index = reader.read_u32( "texanim cutout index" );

                frame.interpolate = reader.read_u32( "texanim interpolate" );
                animation.frames.push_back( frame );
            }

            return animation;
        }
    }

    DecodeError::DecodeError( const std::string &message ) : std::runtime_error( message )
    {
    }

    Scene decode( std::span<const std::uint8_t> bytes )
    {
        parser::Reader reader( bytes );
        reader.expect_magic( "GMSX", "Stage" );

        Scene scene{};
        scene.version = reader.read_u32( "stage version" );
        reader.skip( 0x0C, "stage zero/ignored" );
        reader.skip( 0x10, "stage garbage/ignored" );
        (void)reader.read_u32( "stage xg related count" );

        const std::uint32_t num_imx = reader.read_u32( "stage imx count" );
        scene.imx_entries.reserve( num_imx );

        for( std::uint32_t i = 0; i < num_imx; ++i )
        {
            scene.imx_entries.push_back( reader.read_fixed_string( 0x10, "stage imx name" ) );
            reader.skip( 0x10, "stage imx reserved" );
        }

        const std::uint32_t num_xg = reader.read_u32( "stage xg count" );
        scene.models.reserve( num_xg );

        for( std::uint32_t i = 0; i < num_xg; ++i )
        {
            ModelEntry entry{};
            entry.xg_name = reader.read_fixed_string( 0x10, "stage xg name" );

            entry.is_clone = reader.read_u32( "stage xg is clone" );

            entry.clone_id = reader.read_u32( "stage xg clone id" );
            (void)reader.read_u32( "stage xg reserved" );
            entry.model_type = reader.read_u32( "stage xg model type" );

            entry.xg_animation_length = reader.read_f32( "stage xg anim length" );

            entry.xg_animation_speed = reader.read_f32( "stage xg anim speed" );

            entry.xg_animation_framerate = reader.read_f32( "stage xg framerate" );
            (void)reader.read_u32( "stage xg reserved trailing" );

            if( scene.version == 0x1000 )
            {
                entry.xg_animation_length = 0.0f;

                entry.xg_animation_speed = 1.0f;

                entry.xg_animation_framerate = 60.0f;
            }

            scene.models.push_back( std::move( entry ) );
        }

        for( ModelEntry &entry : scene.models )
        {
            read_model_setup( reader, entry, scene.version );
        }

        scene.camera = read_camera_setup( reader );

        scene.fixed_sprites = read_sprites_setup( reader );

        if( scene.version >= 0x1100 )
        {
            const std::uint32_t num_texanims = reader.read_u32( "stage texanim count" );
            scene.texture_animations.reserve( num_texanims );

            for( std::uint32_t i = 0; i < num_texanims; ++i )
            {
                scene.texture_animations.push_back( read_tex_anim( reader ) );
            }
        }

        if( scene.version >= 0x1200 && reader.remaining() >= 4 )
        {
            if( reader.magic_is( "PSTP" ) )
            {
                reader.expect_magic( "PSTP", "PSetup" );
                (void)reader.read_u32( "PSetup version" );
                reader.skip( 0x0C, "PSetup zero/ignored" );
                reader.skip( 0x10, "PSetup garbage/ignored" );
                const std::uint32_t count = reader.read_u32( "PSetup mystery count" );

                if( count != 0 )
                {
                    throw DecodeError( "SSQ PSetup mystery payload is unsupported" );
                }
            }
        }

        return scene;
    }

    Scene load_file( const std::filesystem::path &path )
    {
        const std::string extension = piko::utils::uppercase_ascii( path.extension().string() );

        if( extension == ".JSON" )
        {
            Scene scene = load_json( path );

            if( scene.version == 0x1000 )
            {
                for( ModelEntry &model : scene.models )
                {
                    model.scale = xg::Vec3{ 1.0f, 1.0f, 1.0f };
                    model.scale_keys.clear();
                }
            }

            return scene;
        }

        std::ifstream file( path, std::ios::binary );

        if( !file )
        {
            throw DecodeError( "Could not open SSQ file: " + path.string() );
        }

        std::vector<std::uint8_t> bytes( ( std::istreambuf_iterator<char>( file ) ), std::istreambuf_iterator<char>() );
        return decode( bytes );
    }
}
