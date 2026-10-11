#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace piko::managers
{
    struct FrameProfile
    {
        double events_ms = 0.0;
        double update_ms = 0.0;
        double audio_ms = 0.0;
        double render_ms = 0.0;
        double present_ms = 0.0;
        std::uint64_t colored_draw_calls = 0;
        std::uint64_t textured_draw_calls = 0;
        std::uint64_t text_draw_calls = 0;
        std::uint64_t glyph_quads = 0;
        std::uint64_t vertices = 0;
    };

    class FrameStats
    {
    public:
        static constexpr std::size_t max_samples = 120;

        void reset();
        void record_frame( double delta_seconds );
        void record_profile( const FrameProfile &profile );

        [[nodiscard]] double frame_seconds() const;
        [[nodiscard]] double frame_ms() const;
        [[nodiscard]] double instantaneous_fps() const;

        [[nodiscard]] double average_frame_seconds() const;
        [[nodiscard]] double average_frame_ms() const;
        [[nodiscard]] double average_fps() const;

        [[nodiscard]] double worst_frame_seconds() const;
        [[nodiscard]] double worst_frame_ms() const;

        [[nodiscard]] std::size_t sample_count() const;
        [[nodiscard]] const FrameProfile &profile() const;

    private:
        std::array<double, max_samples> samples_{};
        std::size_t sample_count_ = 0;
        std::size_t next_sample_ = 0;
        double frame_seconds_ = 0.0;
        double average_frame_seconds_ = 0.0;
        double worst_frame_seconds_ = 0.0;
        FrameProfile profile_{};
    };
}
