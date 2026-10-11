#pragma once

#include "Input/IGameSource.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace piko::convert
{
    class Iso9660Source final : public IGameSource
    {
    public:
        explicit Iso9660Source( std::filesystem::path iso_path );

        bool exists( std::string_view virtual_path ) const override;
        bool is_directory( std::string_view virtual_path ) const override;
        std::vector<std::uint8_t> get_file( std::string_view virtual_path ) const override;
        std::vector<std::string> list_directory( std::string_view virtual_path ) const override;
        std::vector<SourceFileInfo> list_all_files() const override;

        const std::filesystem::path &iso_path() const noexcept;
        std::size_t file_count() const noexcept;

    private:
        struct Entry
        {
            std::string path;
            std::uint32_t extent = 0;
            std::uint32_t size = 0;
            bool directory = false;
        };

        static constexpr std::uint32_t sector_size = 2048;

        static std::string decode_iso_identifier( const std::uint8_t *data, std::size_t length );

        void parse_primary_volume_descriptor();
        void parse_directory( const Entry &directory_entry, std::unordered_set<std::uint32_t> &visited_extents );

        std::vector<std::uint8_t> read_extent( std::uint32_t extent, std::uint32_t size ) const;
        Entry parse_directory_record( const std::vector<std::uint8_t> &data,
            std::size_t offset,
            const std::string &parent_path ) const;

        void add_directory_child( const std::string &parent, const std::string &child_name );

        std::filesystem::path iso_path_;
        mutable std::ifstream iso_stream_;
        std::unordered_map<std::string, Entry> files_;
        std::unordered_map<std::string, Entry> directories_;
        std::unordered_map<std::string, std::vector<std::string>> directory_entries_;
    };
}
