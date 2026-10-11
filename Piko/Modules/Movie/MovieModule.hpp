#pragma once

#include "Managers/AudioManager.hpp"
#include "Modules/PikoModule.hpp"
#include "Rendering/RenderTypes.hpp"
#include "Runtime/Movie/MediaFoundationMoviePlayer.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace piko::modules
{
    class MovieModule final : public PikoModule
    {
    public:
        MovieModule( std::int32_t album_page_id, std::filesystem::path movie_path );

        void enter( App &app ) override;
        void leave( App &app ) override;
        void update( App &app, double delta_seconds ) override;
        void render( App &app, rendering::Renderer &renderer ) override;

    private:
        void advance( App &app );
        bool present_next_frame();

    private:
        std::int32_t album_page_id_ = -1;
        std::filesystem::path movie_path_{};
        runtime::movie::MediaFoundationMoviePlayer player_{};
        managers::AudioChannelHandle audio_channel_{};
        rendering::Texture texture_{};
        runtime::movie::MovieFrame pending_frame_{};
        std::string audio_name_{};
        std::string status_{};
        double frame_accumulator_ = 0.0;
        double elapsed_seconds_ = 0.0;
        bool frame_dirty_ = false;
        bool open_ok_ = false;
        bool advancing_ = false;
    };
}
