#pragma once

#include <cstdint>
#include <glad/gl.h>

#include <filesystem>
#include <string>

namespace piko::rendering
{
    class Shader
    {
    public:
        Shader() = default;
        ~Shader();

        Shader( const Shader & ) = delete;
        Shader &operator=( const Shader & ) = delete;

        bool create_from_source( const char *vertex_source, const char *fragment_source, std::string *error = nullptr );
        bool create_from_files( const std::filesystem::path &vertex_path,
            const std::filesystem::path &fragment_path,
            std::string *error = nullptr );

        void destroy();

        void use() const;

        void set_int( const char *name, std::int32_t value ) const;
        void set_float( const char *name, float value ) const;
        void set_vec2( const char *name, float x, float y ) const;
        void set_vec3( const char *name, float x, float y, float z ) const;
        void set_vec4( const char *name, float x, float y, float z, float w ) const;

        [[nodiscard]] bool valid() const;
        [[nodiscard]] GLuint program() const;

    private:
        static void set_error( std::string *error, const std::string &message );
        static bool read_text_file( const std::filesystem::path &path, std::string &text, std::string &error );
        static bool compile_shader( GLuint shader, const char *source, std::string *error );
        static bool link_program( GLuint program, GLuint vertex_shader, GLuint fragment_shader, std::string *error );

    private:
        GLuint program_ = 0;
    };
}
