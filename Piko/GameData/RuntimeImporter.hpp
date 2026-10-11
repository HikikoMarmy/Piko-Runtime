#pragma once

#include "Audio/ImcConverter.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace piko::convert
{
    struct SourceProfile;
}

namespace piko::gamedata
{
    struct RuntimeImportProgress
    {
        float fraction = 0.0f;
        std::size_t source_index = 0u;
        std::size_t source_count = 0u;
        std::string source_label;
        std::string status;
        std::string current_file;
    };

    using RuntimeImportProgressCallback = std::function<void( const RuntimeImportProgress &progress )>;

    struct RuntimeImportResult
    {
        bool success = false;
        std::size_t files_written = 0u;
        std::uint64_t bytes_written = 0u;
        std::vector<std::string> warnings;
        std::string error;
    };

    class RuntimeImporter
    {
    public:
        [[nodiscard]] static RuntimeImportResult import_sources( const std::vector<std::filesystem::path> &source_paths,
            const std::filesystem::path &application_root,
            convert::AudioCompression audio_compression,
            RuntimeImportProgressCallback progress_callback = {} );

    private:
        struct ImportSource
        {
            std::filesystem::path path;
            std::string platform;
            std::string edition;
            std::string region;
            std::vector<std::string> languages;
        };

        struct ImportJob
        {
            std::filesystem::path path;
            std::string platform;
            std::string edition;
            std::string language;
        };

        [[nodiscard]] static std::int32_t source_priority( const ImportSource &source );
        [[nodiscard]] static std::int32_t language_priority( const std::string &language );
        [[nodiscard]] static std::string default_language_for_region( const std::string &region );
        [[nodiscard]] static std::vector<std::string> source_languages( const convert::SourceProfile &profile );
        [[nodiscard]] static std::vector<ImportSource> inspect_sources(
            const std::vector<std::filesystem::path> &source_paths );

        [[nodiscard]] static std::set<std::string> installed_ps2_languages(
            const std::filesystem::path &application_root );

        [[nodiscard]] static std::vector<ImportJob> make_jobs( const std::vector<ImportSource> &sources,
            const std::filesystem::path &application_root );

        [[nodiscard]] static std::string job_label( const ImportJob &job );
    };
}
