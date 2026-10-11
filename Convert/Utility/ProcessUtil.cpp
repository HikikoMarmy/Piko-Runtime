#include "Utility/ProcessUtil.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace piko::convert::process
{
    std::string quote_shell_argument( const std::filesystem::path &path )
    {
        const std::string value = path.string();
        std::string result = "\"";

        for( const char character : value )
        {
            if( character == '"' )
            {
                result += "\\\"";
            }
            else
            {
                result.push_back( character );
            }
        }

        result.push_back( '"' );
        return result;
    }

#ifdef _WIN32
    std::wstring quote_windows_argument( std::wstring_view value )
    {
        if( value.empty() )
        {
            return L"\"\"";
        }

        const bool requires_quotes = value.find_first_of( L" \t\n\v\"" ) != std::wstring_view::npos;

        if( !requires_quotes )
        {
            return std::wstring( value );
        }

        std::wstring result;
        result.push_back( L'"' );
        std::size_t backslashes = 0;

        for( const wchar_t character : value )
        {
            if( character == L'\\' )
            {
                ++backslashes;
                continue;
            }

            if( character == L'"' )
            {
                result.append( backslashes * 2u + 1u, L'\\' );
                result.push_back( L'"' );
            }
            else
            {
                result.append( backslashes, L'\\' );
                result.push_back( character );
            }

            backslashes = 0;
        }

        result.append( backslashes * 2u, L'\\' );
        result.push_back( L'"' );
        return result;
    }

    std::int32_t run_process( const std::filesystem::path &executable,
        const std::vector<std::filesystem::path> &arguments,
        RunOptions options )
    {
        std::wstring command_line = quote_windows_argument( executable.wstring() );

        for( const std::filesystem::path &argument : arguments )
        {
            command_line.push_back( L' ' );
            command_line += quote_windows_argument( argument.wstring() );
        }

        std::vector<wchar_t> mutable_command_line( command_line.begin(), command_line.end() );
        mutable_command_line.push_back( L'\0' );

        SECURITY_ATTRIBUTES security{};
        security.nLength = sizeof( security );

        security.bInheritHandle = TRUE;

        HANDLE null_handle = INVALID_HANDLE_VALUE;

        if( options.quiet )
        {
            null_handle = CreateFileW( L"NUL",
                GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                &security,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr );
        }

        STARTUPINFOW startup{};
        startup.cb = sizeof( startup );
        const bool inherit_standard_handles = options.quiet;

        if( inherit_standard_handles )
        {
            startup.dwFlags = STARTF_USESTDHANDLES;

            startup.hStdInput = GetStdHandle( STD_INPUT_HANDLE );

            startup.hStdOutput =
                options.quiet && null_handle != INVALID_HANDLE_VALUE ? null_handle : GetStdHandle( STD_OUTPUT_HANDLE );

            startup.hStdError =
                options.quiet && null_handle != INVALID_HANDLE_VALUE ? null_handle : GetStdHandle( STD_ERROR_HANDLE );
        }

        PROCESS_INFORMATION process{};
        const BOOL created = CreateProcessW( nullptr,
            mutable_command_line.data(),
            nullptr,
            nullptr,
            inherit_standard_handles ? TRUE : FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process );

        if( null_handle != INVALID_HANDLE_VALUE )
        {
            CloseHandle( null_handle );
        }

        if( created == FALSE )
        {
            return -1;
        }

        WaitForSingleObject( process.hProcess, INFINITE );
        DWORD exit_code = 1u;
        GetExitCodeProcess( process.hProcess, &exit_code );
        CloseHandle( process.hThread );
        CloseHandle( process.hProcess );
        return static_cast<std::int32_t>( exit_code );
    }
#else
    std::int32_t run_process( const std::filesystem::path &executable,
        const std::vector<std::filesystem::path> &arguments,
        RunOptions options )
    {
        std::string command = quote_shell_argument( executable );

        for( const std::filesystem::path &argument : arguments )
        {
            command.push_back( ' ' );
            command += quote_shell_argument( argument );
        }

        if( options.quiet )
        {
            command += " >/dev/null 2>&1";
        }

        return std::system( command.c_str() );
    }
#endif
}
