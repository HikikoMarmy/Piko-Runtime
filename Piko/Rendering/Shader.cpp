#include "Shader.hpp"

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace piko::rendering
{
    void Shader::set_error( std::string *error, const std::string &message )
    {
        if( error != nullptr )
        {
            *error = message;
        }
    }

    bool Shader::read_text_file( const std::filesystem::path &path, std::string &text, std::string &error )
    {
        std::ifstream file( path, std::ios::in | std::ios::binary );

        if( !file )
        {
            error = "Could not open shader file: " + path.string();
            return false;
        }

        std::ostringstream stream;
        stream << file.rdbuf();
        text = stream.str();
        return true;
    }

    Shader::~Shader()
    {
        destroy();
    }

    bool Shader::create_from_source( const char *vertex_source, const char *fragment_source, std::string *error )
    {
        destroy();

        const GLuint vertex_shader = glCreateShader( GL_VERTEX_SHADER );
        const GLuint fragment_shader = glCreateShader( GL_FRAGMENT_SHADER );

        if( vertex_shader == 0 || fragment_shader == 0 )
        {
            if( vertex_shader != 0 )
            {
                glDeleteShader( vertex_shader );
            }

            if( fragment_shader != 0 )
            {
                glDeleteShader( fragment_shader );
            }

            set_error( error, "Failed to create shader objects." );
            return false;
        }

        if( !compile_shader( vertex_shader, vertex_source, error ) ||
            !compile_shader( fragment_shader, fragment_source, error ) )
        {
            glDeleteShader( vertex_shader );
            glDeleteShader( fragment_shader );
            return false;
        }

        const GLuint program = glCreateProgram();

        if( program == 0 )
        {
            glDeleteShader( vertex_shader );
            glDeleteShader( fragment_shader );
            set_error( error, "Failed to create shader program." );
            return false;
        }

        glAttachShader( program, vertex_shader );
        glAttachShader( program, fragment_shader );

        const bool linked = link_program( program, vertex_shader, fragment_shader, error );

        glDetachShader( program, vertex_shader );
        glDetachShader( program, fragment_shader );
        glDeleteShader( vertex_shader );
        glDeleteShader( fragment_shader );

        if( !linked )
        {
            glDeleteProgram( program );
            return false;
        }

        program_ = program;
        return true;
    }

    bool Shader::create_from_files( const std::filesystem::path &vertex_path,
        const std::filesystem::path &fragment_path,
        std::string *error )
    {
        std::string vertex_source;
        std::string fragment_source;
        std::string read_error;

        if( !read_text_file( vertex_path, vertex_source, read_error ) ||
            !read_text_file( fragment_path, fragment_source, read_error ) )
        {
            set_error( error, read_error );
            return false;
        }

        return create_from_source( vertex_source.c_str(), fragment_source.c_str(), error );
    }

    void Shader::destroy()
    {
        if( program_ != 0 )
        {
            glDeleteProgram( program_ );
            program_ = 0;
        }
    }

    void Shader::use() const
    {
        glUseProgram( program_ );
    }

    void Shader::set_int( const char *name, std::int32_t value ) const
    {
        const GLint location = glGetUniformLocation( program_, name );

        if( location >= 0 )
        {
            glUniform1i( location, value );
        }
    }

    void Shader::set_float( const char *name, float value ) const
    {
        const GLint location = glGetUniformLocation( program_, name );

        if( location >= 0 )
        {
            glUniform1f( location, value );
        }
    }

    void Shader::set_vec2( const char *name, float x, float y ) const
    {
        const GLint location = glGetUniformLocation( program_, name );

        if( location >= 0 )
        {
            glUniform2f( location, x, y );
        }
    }

    void Shader::set_vec3( const char *name, float x, float y, float z ) const
    {
        const GLint location = glGetUniformLocation( program_, name );

        if( location >= 0 )
        {
            glUniform3f( location, x, y, z );
        }
    }

    void Shader::set_vec4( const char *name, float x, float y, float z, float w ) const
    {
        const GLint location = glGetUniformLocation( program_, name );

        if( location >= 0 )
        {
            glUniform4f( location, x, y, z, w );
        }
    }

    bool Shader::valid() const
    {
        return program_ != 0;
    }

    GLuint Shader::program() const
    {
        return program_;
    }

    bool Shader::compile_shader( GLuint shader, const char *source, std::string *error )
    {
        glShaderSource( shader, 1, &source, nullptr );
        glCompileShader( shader );

        GLint success = GL_FALSE;
        glGetShaderiv( shader, GL_COMPILE_STATUS, &success );

        if( success == GL_TRUE )
        {
            return true;
        }

        GLint length = 0;
        glGetShaderiv( shader, GL_INFO_LOG_LENGTH, &length );

        std::vector<char> log( static_cast<std::size_t>( length > 0 ? length : 1 ) );
        glGetShaderInfoLog( shader, length, nullptr, log.data() );

        set_error( error, std::string( "Shader compilation failed: " ) + log.data() );
        return false;
    }

    bool Shader::link_program( GLuint program, GLuint vertex_shader, GLuint fragment_shader, std::string *error )
    {
        (void)vertex_shader;
        (void)fragment_shader;

        glLinkProgram( program );

        GLint success = GL_FALSE;
        glGetProgramiv( program, GL_LINK_STATUS, &success );

        if( success == GL_TRUE )
        {
            return true;
        }

        GLint length = 0;
        glGetProgramiv( program, GL_INFO_LOG_LENGTH, &length );

        std::vector<char> log( static_cast<std::size_t>( length > 0 ? length : 1 ) );
        glGetProgramInfoLog( program, length, nullptr, log.data() );

        set_error( error, std::string( "Shader link failed: " ) + log.data() );
        return false;
    }
}
