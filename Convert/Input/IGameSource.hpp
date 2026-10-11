#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace piko::convert
{
    struct SourceFileInfo
    {
        std::string path;
        std::uint64_t size = 0;
        bool directory = false;
    };

    class IGameSource
    {
    public:
        virtual ~IGameSource() = default;

        virtual bool exists( std::string_view virtual_path ) const = 0;
        virtual bool is_directory( std::string_view virtual_path ) const = 0;
        virtual std::vector<std::uint8_t> get_file( std::string_view virtual_path ) const = 0;
        virtual std::vector<std::string> list_directory( std::string_view virtual_path ) const = 0;
        virtual std::vector<SourceFileInfo> list_all_files() const = 0;
    };
}
