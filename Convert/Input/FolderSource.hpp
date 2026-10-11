#pragma once

#include "Input/IGameSource.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace piko::convert
{
    class FolderSource final : public IGameSource
    {
    public:
        explicit FolderSource( std::filesystem::path root_path );

        bool exists( std::string_view virtual_path ) const override;
        bool is_directory( std::string_view virtual_path ) const override;
        std::vector<std::uint8_t> get_file( std::string_view virtual_path ) const override;
        std::vector<std::string> list_directory( std::string_view virtual_path ) const override;
        std::vector<SourceFileInfo> list_all_files() const override;

        const std::filesystem::path &root_path() const noexcept;

    private:
        void build_index();
        void add_directory_entry( const std::string &child_path );

        std::filesystem::path root_path_;
        std::unordered_map<std::string, std::filesystem::path> files_;
        std::unordered_map<std::string, std::filesystem::path> directories_;
        std::unordered_map<std::string, std::vector<std::string>> directory_entries_;
    };
}
