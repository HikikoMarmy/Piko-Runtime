#pragma once

#include "Runtime/Audio/Pcm16.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace piko::runtime::movie
{
    struct MovieFrame
    {
        std::uint64_t index = 0;
        double presentation_seconds = 0.0;
        std::vector<std::uint8_t> rgba;
    };

    class MediaFoundationMoviePlayer
    {
    public:
        MediaFoundationMoviePlayer() = default;
        ~MediaFoundationMoviePlayer();

        MediaFoundationMoviePlayer( const MediaFoundationMoviePlayer & ) = delete;
        MediaFoundationMoviePlayer &operator=( const MediaFoundationMoviePlayer & ) = delete;

        bool open( const std::filesystem::path &movie_path );
        void close();
        [[nodiscard]] bool pop_frame( MovieFrame &frame );
        [[nodiscard]] bool video_finished() const;
        [[nodiscard]] bool failed() const;
        [[nodiscard]] std::string last_error() const;
        [[nodiscard]] runtime::audio::Pcm16 take_audio();
        [[nodiscard]] std::int32_t width() const;
        [[nodiscard]] std::int32_t height() const;
        [[nodiscard]] double frames_per_second() const;

    private:
        bool read_video_format();
        bool decode_audio();
        void video_worker();
        void set_error( std::string message );

    private:
        static constexpr std::size_t max_queued_frames = 8;

        std::filesystem::path movie_path_{};
        runtime::audio::Pcm16 audio_{};
        std::thread video_thread_{};
        mutable std::mutex mutex_{};
        std::condition_variable condition_{};
        std::deque<MovieFrame> frames_{};
        std::atomic<bool> stop_requested_{ false };
        std::atomic<bool> video_finished_{ true };
        std::atomic<bool> failed_{ false };
        std::string last_error_{};
        std::int32_t width_ = 0;
        std::int32_t height_ = 0;
        double frames_per_second_ = 30.0;
        bool media_foundation_started_ = false;
    };
}
