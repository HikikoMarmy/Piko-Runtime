#include "SaveData.hpp"

#include "Utils/Json.hpp"

#include <cstdint>
#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <system_error>

namespace piko::gamedata
{

    void SaveDataManager::load_progress_array( const utils::json::Value *value,
        std::array<SavedStageProgress, 100> &output )
    {
        if( value == nullptr || !value->is_array() )
        {
            return;
        }

        for( const utils::json::Value &entry : value->array() )
        {
            if( !entry.is_object() )
            {
                continue;
            }

            const std::int32_t stage = static_cast<std::int32_t>( entry.require( "stage" ).integer_or( -1 ) );

            if( stage < 0 || stage >= static_cast<std::int32_t>( output.size() ) )
            {
                continue;
            }

            SavedStageProgress &progress = output[ static_cast<std::size_t>( stage ) ];
            progress.cleared = entry.require( "cleared" ).boolean_or( false );

            progress.best_score = static_cast<std::uint32_t>(
                std::max<std::int64_t>( 0, entry.require( "best_score" ).integer_or( 0 ) ) );

            progress.best_rank = static_cast<std::int32_t>( entry.require( "best_rank" ).integer_or( 0 ) );

            progress.no_miss = entry.require( "no_miss" ).boolean_or( false );
        }
    }

    void SaveDataManager::write_progress_array( std::ostream &output,
        const std::array<SavedStageProgress, 100> &progress )
    {
        output << '[';
        bool first = true;

        for( std::size_t stage = 0; stage < progress.size(); ++stage )
        {
            const SavedStageProgress &entry = progress[ stage ];

            if( !entry.cleared && entry.best_score == 0u && entry.best_rank == 0 && !entry.no_miss )
            {
                continue;
            }

            if( !first )
            {
                output << ',';
            }

            first = false;
            output << "{\"stage\":" << stage << ",\"cleared\":" << ( entry.cleared ? "true" : "false" )
                   << ",\"best_score\":" << entry.best_score << ",\"best_rank\":" << entry.best_rank
                   << ",\"no_miss\":" << ( entry.no_miss ? "true" : "false" ) << '}';
        }

        output << ']';
    }

    std::int32_t SaveDataManager::rank_quality( std::int32_t rank )
    {
        switch( rank )
        {
            case 5:
                return 5;
            case 1:
                return 4;
            case 2:
                return 3;
            case 3:
                return 2;
            case 4:
                return 1;
            default:
                return 0;
        }
    }

    bool SaveDataManager::initialize( const std::filesystem::path &application_root )
    {
        save_directory_ = application_root / "Save";

        save_path_ = save_directory_ / "profiles.json";

        profiles_ = {};

        active_slot_ = -1;
        last_error_.clear();

        std::error_code error{};
        std::filesystem::create_directories( save_directory_, error );

        if( error )
        {
            last_error_ = "Could not create save directory: " + error.message();
            return false;
        }

        if( std::filesystem::is_regular_file( save_path_, error ) )
        {
            return load();
        }

        return true;
    }

    bool SaveDataManager::create_profile( std::int32_t slot )
    {
        if( !valid_slot( slot ) )
        {
            return false;
        }

        SaveProfile &profile = profiles_[ static_cast<std::size_t>( slot ) ];

        if( !profile.occupied )
        {
            profile = {};

            profile.occupied = true;

            profile.slot = slot;

            profile.name = "Piko " + std::to_string( slot + 1 );
        }

        active_slot_ = slot;
        return save();
    }

    bool SaveDataManager::select_profile( std::int32_t slot )
    {
        if( !valid_slot( slot ) || !profiles_[ static_cast<std::size_t>( slot ) ].occupied )
        {
            return false;
        }

        active_slot_ = slot;
        return save();
    }

    bool SaveDataManager::delete_profile( std::int32_t slot )
    {
        if( !valid_slot( slot ) || !profiles_[ static_cast<std::size_t>( slot ) ].occupied )
        {
            return false;
        }

        profiles_[ static_cast<std::size_t>( slot ) ] = {};

        profiles_[ static_cast<std::size_t>( slot ) ].slot = slot;

        if( active_slot_ == slot )
        {
            active_slot_ = -1;
        }

        return save();
    }

    bool SaveDataManager::record_stage_result( std::int32_t stage_number,
        SaveProgressMode mode,
        bool won,
        std::uint32_t score,
        std::int32_t rank,
        bool no_miss,
        ChartStyle chart_style )
    {
        if( stage_number < 0 || stage_number >= 100 || !won )
        {
            return false;
        }

        const auto previous_profiles = profiles_;
        const std::int32_t previous_active_slot = active_slot_;

        if( active_profile() == nullptr && !create_profile( 0 ) )
        {
            profiles_ = previous_profiles;

            active_slot_ = previous_active_slot;
            return false;
        }

        SaveProfile &profile = profiles_[ static_cast<std::size_t>( active_slot_ ) ];
        auto &chart = profile.charts[ utils::enum_index( chart_style ) ];
        auto &progress_array = mode == SaveProgressMode::Master ? chart.master
                               : mode == SaveProgressMode::Easy ? chart.easy
                                                                : chart.normal;

        SavedStageProgress &progress = progress_array[ static_cast<std::size_t>( stage_number ) ];
        progress.cleared = true;

        progress.best_score = std::max( progress.best_score, score );

        if( rank_quality( rank ) > rank_quality( progress.best_rank ) )
        {
            progress.best_rank = rank;
        }

        progress.no_miss = progress.no_miss || no_miss;

        if( save() )
        {
            return true;
        }

        profiles_ = previous_profiles;

        active_slot_ = previous_active_slot;
        return false;
    }

    const std::array<SaveProfile, SaveDataManager::profile_count> &SaveDataManager::profiles() const
    {
        return profiles_;
    }

    const SaveProfile *SaveDataManager::active_profile() const
    {
        if( !valid_slot( active_slot_ ) )
        {
            return nullptr;
        }

        const SaveProfile &profile = profiles_[ static_cast<std::size_t>( active_slot_ ) ];
        return profile.occupied ? &profile : nullptr;
    }

    std::int32_t SaveDataManager::active_slot() const
    {
        return active_slot_;
    }

    const SavedStageProgress *SaveDataManager::stage_progress( std::int32_t stage_number,
        SaveProgressMode mode,
        ChartStyle chart_style ) const
    {
        const SaveProfile *profile = active_profile();

        if( profile == nullptr || stage_number < 0 || stage_number >= 100 )
        {
            return nullptr;
        }

        const auto &chart = profile->chart( chart_style );

        const auto &progress = mode == SaveProgressMode::Master ? chart.master
                               : mode == SaveProgressMode::Easy ? chart.easy
                                                                : chart.normal;

        return &progress[ static_cast<std::size_t>( stage_number ) ];
    }

    std::int32_t SaveDataManager::clear_count( SaveProgressMode mode,
        std::int32_t maximum_stage,
        ChartStyle chart_style ) const
    {
        const SaveProfile *profile = active_profile();

        if( profile == nullptr )
        {
            return 0;
        }

        const auto &chart = profile->chart( chart_style );

        const auto &progress = mode == SaveProgressMode::Master ? chart.master
                               : mode == SaveProgressMode::Easy ? chart.easy
                                                                : chart.normal;

        const std::int32_t last = std::clamp( maximum_stage, 0, static_cast<std::int32_t>( progress.size() ) - 1 );
        std::int32_t count = 0;

        for( std::int32_t stage = 1; stage <= last; ++stage )
        {
            if( progress[ static_cast<std::size_t>( stage ) ].cleared )
            {
                ++count;
            }
        }

        return count;
    }

    std::int32_t SaveDataManager::highest_reached_stage() const
    {
        const SaveProfile *profile = active_profile();

        if( profile == nullptr )
        {
            return 1;
        }

        std::int32_t highest = 1;

        for( const auto &chart : profile->charts )
        {
            for( std::int32_t stage = 1; stage < static_cast<std::int32_t>( chart.normal.size() ); ++stage )
            {
                const auto index = static_cast<std::size_t>( stage );

                if( chart.easy[ index ].cleared || chart.normal[ index ].cleared || chart.master[ index ].cleared )
                {
                    highest = std::max( highest, stage + 1 );
                }
            }
        }

        return std::min( highest, 99 );
    }

    bool SaveDataManager::collection_unlocked( std::int32_t collection_index ) const
    {
        if( collection_index < 0 || collection_index >= static_cast<std::int32_t>( collection_requirements_.size() ) )
        {
            return false;
        }

        const CollectionRequirement requirement =
            collection_requirements_[ static_cast<std::size_t>( collection_index ) ];

        if( requirement.condition == CollectionUnlockCondition::Always )
        {
            return true;
        }

        if( requirement.condition == CollectionUnlockCondition::ReachStage )
        {
            return stage_reached( requirement.stage );
        }

        const SaveProgressMode mode = requirement.condition == CollectionUnlockCondition::ClearMaster
                                          ? SaveProgressMode::Master
                                          : SaveProgressMode::Normal;

        for( const auto style : { ChartStyle::GitarooMan, ChartStyle::GitarooManLives } )
        {
            const auto *progress = stage_progress( requirement.stage, mode, style );

            if( progress != nullptr && progress->cleared )
            {
                return true;
            }
        }

        return false;
    }

    const std::string &SaveDataManager::last_error() const
    {
        return last_error_;
    }

    bool SaveDataManager::load()
    {
        try
        {
            const utils::json::Value root = utils::json::load_file( save_path_ );

            if( root.require( "format" ).string() != "piko-save" || root.require( "version" ).integer_or( 0 ) != 2 )
            {
                throw std::runtime_error( "Unsupported save data format" );
            }

            profiles_ = {};

            for( std::int32_t slot = 0; slot < profile_count; ++slot )
            {
                profiles_[ static_cast<std::size_t>( slot ) ].slot = slot;
            }

            active_slot_ = static_cast<std::int32_t>( root.require( "active_slot" ).integer_or( -1 ) );
            const utils::json::Value *profiles = root.find( "profiles" );

            if( profiles != nullptr && profiles->is_array() )
            {
                for( const utils::json::Value &entry : profiles->array() )
                {
                    const std::int32_t slot = static_cast<std::int32_t>( entry.require( "slot" ).integer_or( -1 ) );

                    if( !valid_slot( slot ) )
                    {
                        continue;
                    }

                    SaveProfile &profile = profiles_[ static_cast<std::size_t>( slot ) ];
                    profile.occupied = true;

                    profile.name = entry.require( "name" ).string_or( "Piko " + std::to_string( slot + 1 ) );

                    const auto &charts = entry.require( "charts" );

                    for( std::size_t index = 0; index < chart_names_.size(); ++index )
                    {
                        if( const auto *chart = charts.find( chart_names_[ index ] ) )
                        {
                            load_progress_array( chart->find( "easy" ), profile.charts[ index ].easy );
                            load_progress_array( chart->find( "normal" ), profile.charts[ index ].normal );
                            load_progress_array( chart->find( "master" ), profile.charts[ index ].master );
                        }
                    }
                }
            }

            if( !valid_slot( active_slot_ ) || !profiles_[ static_cast<std::size_t>( active_slot_ ) ].occupied )
            {
                active_slot_ = -1;
            }

            last_error_.clear();
            return true;
        }
        catch( const std::exception &error )
        {
            profiles_ = {};

            active_slot_ = -1;

            last_error_ = "Could not load save data: " + std::string( error.what() );
            return false;
        }
    }

    bool SaveDataManager::save()
    {
        const std::filesystem::path temporary_path = save_path_.string() + ".tmp";
        const std::filesystem::path backup_path = save_path_.string() + ".bak";

        try
        {
            std::ofstream output( temporary_path, std::ios::binary | std::ios::trunc );

            if( !output )
            {
                throw std::runtime_error( "Could not open temporary save file" );
            }

            output << "{\n  \"format\": \"piko-save\",\n  \"version\": 2,\n  \"active_slot\": " << active_slot_
                   << ",\n  \"profiles\": [";

            bool first = true;

            for( const SaveProfile &profile : profiles_ )
            {
                if( !profile.occupied )
                {
                    continue;
                }

                if( !first )
                {
                    output << ',';
                }

                first = false;
                output << "\n    {\"slot\":" << profile.slot << ",\"name\":" << std::quoted( profile.name )
                       << ",\"charts\":{";

                for( std::size_t index = 0; index < chart_names_.size(); ++index )
                {
                    if( index > 0 )
                    {
                        output << ',';
                    }

                    const auto &chart = profile.charts[ index ];
                    output << std::quoted( chart_names_[ index ] ) << ":{\"easy\":";
                    write_progress_array( output, chart.easy );
                    output << ",\"normal\":";
                    write_progress_array( output, chart.normal );
                    output << ",\"master\":";
                    write_progress_array( output, chart.master );
                    output << '}';
                }

                output << "}}";
            }

            output << "\n  ]\n}\n";
            output.close();

            if( !output )
            {
                throw std::runtime_error( "Could not finish writing save data" );
            }

            std::error_code error{};
            std::filesystem::remove( backup_path, error );
            error.clear();

            if( std::filesystem::is_regular_file( save_path_, error ) )
            {
                error.clear();
                std::filesystem::rename( save_path_, backup_path, error );

                if( error )
                {
                    throw std::runtime_error( "Could not prepare save replacement: " + error.message() );
                }
            }

            error.clear();
            std::filesystem::rename( temporary_path, save_path_, error );

            if( error )
            {
                std::error_code restore_error{};

                if( std::filesystem::is_regular_file( backup_path, restore_error ) )
                {
                    std::filesystem::rename( backup_path, save_path_, restore_error );
                }

                throw std::runtime_error( "Could not replace save data: " + error.message() );
            }

            std::filesystem::remove( backup_path, error );
            last_error_.clear();
            return true;
        }
        catch( const std::exception &error )
        {
            std::error_code remove_error{};
            std::filesystem::remove( temporary_path, remove_error );
            last_error_ = "Could not save progress: " + std::string( error.what() );
            return false;
        }
    }

    bool SaveDataManager::valid_slot( std::int32_t slot ) const
    {
        return slot >= 0 && slot < profile_count;
    }

    bool SaveDataManager::stage_reached( std::int32_t stage_number ) const
    {
        if( stage_number <= 1 )
        {
            return true;
        }

        for( const auto style : { ChartStyle::GitarooMan, ChartStyle::GitarooManLives } )
        {
            for( const auto mode : { SaveProgressMode::Normal, SaveProgressMode::Easy } )
            {
                const auto *current = stage_progress( stage_number, mode, style );

                const auto *previous = stage_progress( stage_number - 1, mode, style );

                if( ( current != nullptr && current->cleared ) || ( previous != nullptr && previous->cleared ) )
                {
                    return true;
                }
            }
        }

        return false;
    }
}
