#pragma once

#include "Managers/TextureManager.hpp"
#include "Modules/PikoModule.hpp"
#include "GameData/StageCatalog.hpp"

#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace piko::modules
{
    class LoadingProgress
    {
    public:
        void set_progress( float progress );
        [[nodiscard]] float progress() const;

        void set_status( std::string status );
        [[nodiscard]] std::string status() const;

        void request_cancel();
        [[nodiscard]] bool cancel_requested() const;

    private:
        mutable std::mutex status_mutex_;
        std::string status_{};

        std::atomic<std::int32_t> progress_per_thousand_ = 0;
        std::atomic_bool cancel_requested_ = false;
    };

    struct LoadingTask
    {
        std::string title = "@ui.now_loading";
        std::string initial_status = "@ui.preparing";
        std::string background_asset{};
        std::vector<std::string> background_candidates{};
        std::string fallback_background_asset{};
        std::int32_t stage_number = -1;

        std::function<void( LoadingProgress &progress )> work;

        std::function<std::unique_ptr<PikoModule>()> create_next_module;

        std::function<std::unique_ptr<PikoModule>()> create_cancel_module;
    };

    LoadingTask make_stage_loading_task( const gamedata::PlayableStageEntry &stage,
        std::function<std::unique_ptr<PikoModule>()> create_cancel_module );

    class LoadingModule final : public PikoModule
    {
    public:
        explicit LoadingModule( LoadingTask task );
        ~LoadingModule() override;

        LoadingModule( const LoadingModule & ) = delete;
        LoadingModule &operator=( const LoadingModule & ) = delete;

        void enter( App &app ) override;
        void leave( App &app ) override;
        void update( App &app, double delta_seconds ) override;
        void render( App &app, rendering::Renderer &renderer ) override;

    private:
        enum class ArtLayout
        {
            Other,
            Psp,
            Ps2Stage,
            Ps2Versus,
            Ps2Generic
        };

        void start_worker();
        void join_worker();
        void store_error( std::string message );
        [[nodiscard]] std::string error_message() const;
        void load_background( App &app );
        void unload_background( App &app );
        static void draw_ps2_letters( rendering::Renderer &renderer,
            const rendering::Texture &texture,
            float origin_x,
            float origin_y,
            float scale,
            float letter_x,
            float letter_y,
            float frame );

    private:
        LoadingTask task_{};
        LoadingProgress progress_{};

        std::thread worker_{};
        std::atomic_bool worker_started_ = false;
        std::atomic_bool worker_done_ = false;
        std::atomic_bool worker_failed_ = false;
        bool transition_requested_ = false;

        mutable std::mutex error_mutex_;
        std::string error_message_{};
        managers::TextureHandle background_texture_{};
        managers::TextureHandle loading_label_texture_{};
        ArtLayout art_layout_ = ArtLayout::Other;

        double elapsed_seconds_ = 0.0;
    };
}
