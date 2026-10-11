#pragma once

#include "Audio/ImcConverter.hpp"
#include "Input/IGameSource.hpp"
#include "Region/SourceScanner.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace piko::convert
{
    struct RuntimeBuildProgress
    {
        float fraction = 0.0f;
        std::string status;
        std::string current_file;
    };

    using RuntimeBuildProgressCallback = std::function<void( const RuntimeBuildProgress &progress )>;

    struct RuntimeBuildOptions
    {
        std::filesystem::path input_path;
        std::filesystem::path output_path;
        std::string platform = "auto";
        std::string region = "auto";
        std::string language = "auto";
        AudioCompression audio_compression = AudioCompression::Flac;
        bool overwrite_existing = false;
        RuntimeBuildProgressCallback progress_callback;
    };

    struct RuntimeBuildResult
    {
        bool success = false;
        std::size_t files_written = 0;
        std::uint64_t bytes_written = 0;
        std::vector<std::string> warnings;
        std::vector<std::string> errors;
    };

    class RuntimeBuilder
    {
    public:
        RuntimeBuildResult build( const RuntimeBuildOptions &options ) const;

    private:
        RuntimeBuildResult build_ps2_runtime( const IGameSource &source,
            const SourceProfile &profile,
            const RuntimeBuildOptions &options ) const;

        RuntimeBuildResult build_psp_runtime( const IGameSource &source,
            const SourceProfile &profile,
            const RuntimeBuildOptions &options ) const;

        RuntimeBuildResult build_wan_runtime( const IGameSource &source,
            const SourceProfile &profile,
            const RuntimeBuildOptions &options ) const;
    };
}
