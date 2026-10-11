#include "RuntimeImporter.hpp"

#include <cstdint>
#include <Input/SourceFactory.hpp>
#include <Region/SourceScanner.hpp>
#include <Runtime/RuntimeBuilder.hpp>

#include <algorithm>
#include <array>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace piko::gamedata
{
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

    std::int32_t RuntimeImporter::source_priority( const ImportSource &source )
    {
        if( source.edition == "gitaroo_man" && source.platform == "ps2" )
        {
            return 0;
        }

        if( source.platform == "psp" )
        {
            return 1;
        }

        if( source.edition == "gitaroo_man_wan" )
        {
            return 2;
        }

        return 3;
    }

    std::int32_t RuntimeImporter::language_priority( const std::string &language )
    {
        if( language == "US" || language == "EN" || language == "JP" )
        {
            return 0;
        }

        if( language == "FR" )
        {
            return 1;
        }

        if( language == "GR" )
        {
            return 2;
        }

        return 9;
    }

    std::string RuntimeImporter::default_language_for_region( const std::string &region )
    {
        if( region == "jp" )
        {
            return "JP";
        }

        if( region == "eu" )
        {
            return "EN";
        }

        return "US";
    }

    std::vector<std::string> RuntimeImporter::source_languages( const convert::SourceProfile &profile )
    {
        std::vector<std::string> languages = profile.language_folders;

        if( languages.empty() && profile.edition == "gitaroo_man" && profile.platform == "ps2" )
        {
            languages.push_back( default_language_for_region( profile.region ) );
        }

        std::sort( languages.begin(),
            languages.end(),
            []( const std::string &left, const std::string &right )
            {
                const std::int32_t left_priority = language_priority( left );
                const std::int32_t right_priority = language_priority( right );

                if( left_priority != right_priority )
                {
                    return left_priority < right_priority;
                }

                return left < right;
            } );
        languages.erase( std::unique( languages.begin(), languages.end() ), languages.end() );
        return languages;
    }

    std::vector<RuntimeImporter::ImportSource> RuntimeImporter::inspect_sources(
        const std::vector<std::filesystem::path> &source_paths )
    {
        std::vector<ImportSource> sources;
        sources.reserve( source_paths.size() );

        for( const std::filesystem::path &path : source_paths )
        {
            auto source = convert::open_game_source( path );
            const convert::SourceProfile profile = convert::SourceScanner{}.scan( *source );

            if( profile.platform != "ps2" && profile.platform != "psp" )
            {
                throw std::runtime_error(
                    "Could not identify this as a Gitaroo Man PS2 or PSP disc: " + path.string() );
            }

            sources.push_back( ImportSource{
                .path = path,
                .platform = profile.platform,
                .edition = profile.edition,
                .region = profile.region,
                .languages = source_languages( profile )
            } );
        }

        std::stable_sort( sources.begin(),
            sources.end(),
            []( const ImportSource &left, const ImportSource &right )
            {
                return source_priority( left ) < source_priority( right );
            } );

        return sources;
    }

    std::set<std::string> RuntimeImporter::installed_ps2_languages( const std::filesystem::path &application_root )
    {
        std::set<std::string> languages;
        const std::filesystem::path runtime_root = application_root / "GAME_DATA";

        for( const std::string &language : { "US", "EN", "FR", "GR", "JP" } )
        {
            std::error_code error{};

            if( std::filesystem::is_regular_file( runtime_root / language / "manifest.json", error ) )
            {
                languages.insert( language );
            }
        }

        return languages;
    }

    std::vector<RuntimeImporter::ImportJob> RuntimeImporter::make_jobs( const std::vector<ImportSource> &sources,
        const std::filesystem::path &application_root )
    {
        std::vector<ImportJob> jobs;
        std::set<std::string> ps2_languages = installed_ps2_languages( application_root );

        for( const ImportSource &source : sources )
        {
            if( source.edition == "gitaroo_man" && source.platform == "ps2" )
            {
                for( const std::string &language : source.languages )
                {
                    jobs.push_back( ImportJob{
                        .path = source.path,
                        .platform = source.platform,
                        .edition = source.edition,
                        .language = language
                    } );

                    ps2_languages.insert( language );
                }
            }
        }

        for( const ImportSource &source : sources )
        {
            if( source.platform != "psp" )
            {
                continue;
            }

            if( ps2_languages.empty() )
            {
                jobs.push_back( ImportJob{
                    .path = source.path,
                    .platform = source.platform,
                    .edition = source.edition,
                    .language = "auto"
                } );
                continue;
            }

            for( const std::string &language : ps2_languages )
            {
                jobs.push_back( ImportJob{
                    .path = source.path,
                    .platform = source.platform,
                    .edition = source.edition,
                    .language = language
                } );
            }
        }

        for( const ImportSource &source : sources )
        {
            if( source.edition != "gitaroo_man_wan" )
            {
                continue;
            }

            jobs.push_back( ImportJob{
                .path = source.path,
                .platform = source.platform,
                .edition = source.edition,
                .language = "JP"
            } );
        }

        return jobs;
    }

    std::string RuntimeImporter::job_label( const ImportJob &job )
    {
        if( job.edition == "gitaroo_man_wan" )
        {
            return "Gitaroo Man Wan bonus stage";
        }

        if( job.platform == "psp" )
        {
            return job.language == "auto" ? "Gitaroo Man Lives! chart/audio"
                                          : "Gitaroo Man Lives! chart/audio (" + job.language + ")";
        }

        if( job.platform == "ps2" )
        {
            return "PlayStation 2 (" + job.language + ")";
        }

        return "Unknown disc";
    }

    RuntimeImportResult RuntimeImporter::import_sources( const std::vector<std::filesystem::path> &source_paths,
        const std::filesystem::path &application_root,
        convert::AudioCompression audio_compression,
        RuntimeImportProgressCallback progress_callback )
    {
        RuntimeImportResult output;

        if( source_paths.empty() )
        {
            output.error = "No ISO files were selected.";
            return output;
        }

        try
        {
            if( progress_callback )
            {
                progress_callback( RuntimeImportProgress{
                    .fraction = 0.0f,
                    .source_index = 0u,
                    .source_count = source_paths.size(),
                    .source_label = "Disc images",
                    .status = "Checking selected game data",
                    .current_file = {}
                } );
            }

            const std::vector<ImportSource> sources = inspect_sources( source_paths );
            const std::vector<ImportJob> jobs = make_jobs( sources, application_root );

            if( jobs.empty() )
            {
                throw std::runtime_error( "The selected discs did not contain importable runtime content" );
            }

            for( std::size_t index = 0u; index < jobs.size(); ++index )
            {
                const ImportJob &job = jobs[ index ];
                const std::string source_label = job_label( job );

                convert::RuntimeBuildOptions options;
                options.platform = job.platform;

                options.region = "auto";

                options.language = job.language;

                options.input_path = job.path;

                options.output_path = application_root;

                options.audio_compression = audio_compression;

                options.overwrite_existing = true;

                options.progress_callback = [ progress_callback, index, source_count = jobs.size(), source_label ](
                                                const convert::RuntimeBuildProgress &progress )
                {
                    if( !progress_callback )
                    {
                        return;
                    }

                    const float total_fraction = ( static_cast<float>( index ) + progress.fraction ) /
                                                 static_cast<float>( std::max<std::size_t>( source_count, 1u ) );

                    progress_callback( RuntimeImportProgress{
                        .fraction = std::clamp( total_fraction, 0.0f, 1.0f ),
                        .source_index = index,
                        .source_count = source_count,
                        .source_label = source_label,
                        .status = progress.status,
                        .current_file = progress.current_file
                    } );
                };

                const convert::RuntimeBuildResult result = convert::RuntimeBuilder{}.build( options );
                output.files_written += result.files_written;

                output.bytes_written += result.bytes_written;
                output.warnings.insert( output.warnings.end(), result.warnings.begin(), result.warnings.end() );

                if( !result.success )
                {
                    std::ostringstream error;
                    error << source_label << " import failed";

                    for( const std::string &message : result.errors )
                    {
                        error << "\n\n" << message;
                    }

                    output.error = error.str();
                    return output;
                }
            }

            if( progress_callback )
            {
                progress_callback( RuntimeImportProgress{
                    .fraction = 1.0f,
                    .source_index = jobs.size(),
                    .source_count = jobs.size(),
                    .source_label = "Runtime",
                    .status = "Import complete",
                    .current_file = {}
                } );
            }

            output.success = true;
            return output;
        }
        catch( const std::exception &error )
        {
            output.error = error.what();
            return output;
        }
    }
}
