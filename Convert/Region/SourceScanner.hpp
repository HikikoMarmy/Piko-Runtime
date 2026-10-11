#pragma once

#include "Input/IGameSource.hpp"

#include <string>
#include <vector>

namespace piko::convert
{
    struct SourceProfile
    {
        std::string platform = "unknown"; // ps2, psp, unknown
        std::string region = "unknown";   // us, jp, eu, unknown
        std::string edition = "gitaroo_man";
        std::string executable_path;
        std::string disc_id;

        std::vector<std::string> content_roots;
        std::vector<std::string> language_folders;
        std::vector<std::string> stage_ids;
    };

    class SourceScanner
    {
    public:
        SourceProfile scan( const IGameSource &source ) const;

    private:
        static std::string detect_region_from_executable( std::string_view executable_path );
        static std::string extract_boot_executable( const std::vector<std::uint8_t> &system_cnf );
        static std::string extract_disc_id( std::string_view executable_path );
        static std::string bytes_to_text( const std::vector<std::uint8_t> &data );
        static bool looks_like_stage_id( std::string_view value );
        static bool contains_string( const std::vector<std::string> &list, const std::string &value );
    };
}
