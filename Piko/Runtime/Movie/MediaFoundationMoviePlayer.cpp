#include "MediaFoundationMoviePlayer.hpp"

#ifdef _WIN32

#include <Windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace piko::runtime::movie::media_foundation
{
    using Microsoft::WRL::ComPtr;

    class ComApartment
    {
    public:
        ComApartment()
        {
            result_ = CoInitializeEx( nullptr, COINIT_MULTITHREADED );

            owns_initialization_ = result_ == S_OK || result_ == S_FALSE;
        }

        ~ComApartment()
        {
            if( owns_initialization_ )
            {
                CoUninitialize();
            }
        }

        [[nodiscard]] bool available() const
        {
            return SUCCEEDED( result_ ) || result_ == RPC_E_CHANGED_MODE;
        }

        [[nodiscard]] HRESULT result() const
        {
            return result_;
        }

    private:
        HRESULT result_ = E_FAIL;
        bool owns_initialization_ = false;
    };

    struct VideoFormat
    {
        std::int32_t width = 0;
        std::int32_t height = 0;
        long stride = 0;
        double frames_per_second = 30.0;
    };

    struct AudioFormat
    {
        std::int32_t sample_rate = 0;
        std::int32_t channels = 0;
        std::int32_t bits_per_sample = 0;
        std::int32_t block_alignment = 0;
    };

    std::string hresult_string( HRESULT result )
    {
        std::ostringstream text;
        text << "0x" << std::uppercase << std::hex << std::setw( 8 ) << std::setfill( '0' )
             << static_cast<unsigned long>( result );

        return text.str();
    }

    void require( HRESULT result, const char *operation )
    {
        if( FAILED( result ) )
        {
            throw std::runtime_error( std::string( operation ) + " failed (" + hresult_string( result ) + ")" );
        }
    }

    ComPtr<IMFSourceReader> create_reader( const std::filesystem::path &path, bool video_processing )
    {
        ComPtr<IMFAttributes> attributes;
        require( MFCreateAttributes( &attributes, 2 ), "MFCreateAttributes" );
        require( attributes->SetUINT32( MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, video_processing ? TRUE : FALSE ),
            "Enable Media Foundation video processing" );

        ComPtr<IMFSourceReader> reader;
        require( MFCreateSourceReaderFromURL( path.c_str(), attributes.Get(), &reader ),
            "Open MP4 with Media Foundation" );

        return reader;
    }

    VideoFormat configure_video_reader( IMFSourceReader *reader )
    {
        require( reader->SetStreamSelection( MF_SOURCE_READER_ALL_STREAMS, FALSE ),
            "Disable Media Foundation source streams" );

        require( reader->SetStreamSelection( MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE ), "Select MP4 video stream" );

        ComPtr<IMFMediaType> output_type;
        require( MFCreateMediaType( &output_type ), "Create Media Foundation video type" );
        require( output_type->SetGUID( MF_MT_MAJOR_TYPE, MFMediaType_Video ), "Set video major type" );
        require( output_type->SetGUID( MF_MT_SUBTYPE, MFVideoFormat_RGB32 ), "Set video pixel format" );
        require( reader->SetCurrentMediaType( MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output_type.Get() ),
            "Configure MP4 video decoder" );

        ComPtr<IMFMediaType> actual_type;
        require( reader->GetCurrentMediaType( MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual_type ),
            "Read decoded video format" );

        std::uint32_t width = 0;
        std::uint32_t height = 0;
        require( MFGetAttributeSize( actual_type.Get(), MF_MT_FRAME_SIZE, &width, &height ), "Read video dimensions" );

        std::uint32_t frame_rate_numerator = 0;
        std::uint32_t frame_rate_denominator = 0;
        const HRESULT frame_rate_result =
            MFGetAttributeRatio( actual_type.Get(), MF_MT_FRAME_RATE, &frame_rate_numerator, &frame_rate_denominator );

        std::uint32_t stride_value = 0;
        long stride = static_cast<long>( width * 4u );

        if( SUCCEEDED( actual_type->GetUINT32( MF_MT_DEFAULT_STRIDE, &stride_value ) ) )
        {
            stride = static_cast<long>( static_cast<std::int32_t>( stride_value ) );
        }

        VideoFormat format{};
        format.width = static_cast<std::int32_t>( width );

        format.height = static_cast<std::int32_t>( height );

        format.stride = stride;

        if( SUCCEEDED( frame_rate_result ) && frame_rate_numerator > 0u && frame_rate_denominator > 0u )
        {
            format.frames_per_second =
                static_cast<double>( frame_rate_numerator ) / static_cast<double>( frame_rate_denominator );
        }

        return format;
    }

    bool configure_audio_reader( IMFSourceReader *reader, AudioFormat &format )
    {
        ComPtr<IMFMediaType> native_type;
        const HRESULT native_result =
            reader->GetNativeMediaType( MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &native_type );

        if( native_result == MF_E_INVALIDSTREAMNUMBER || native_result == MF_E_NO_MORE_TYPES )
        {
            return false;
        }

        require( native_result, "Find MP4 audio stream" );

        require( reader->SetStreamSelection( MF_SOURCE_READER_ALL_STREAMS, FALSE ),
            "Disable Media Foundation source streams" );

        require( reader->SetStreamSelection( MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE ), "Select MP4 audio stream" );

        ComPtr<IMFMediaType> output_type;
        require( MFCreateMediaType( &output_type ), "Create Media Foundation audio type" );
        require( output_type->SetGUID( MF_MT_MAJOR_TYPE, MFMediaType_Audio ), "Set audio major type" );
        require( output_type->SetGUID( MF_MT_SUBTYPE, MFAudioFormat_PCM ), "Set PCM audio format" );
        require( reader->SetCurrentMediaType( MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, output_type.Get() ),
            "Configure MP4 audio decoder" );

        ComPtr<IMFMediaType> actual_type;
        require( reader->GetCurrentMediaType( MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual_type ),
            "Read decoded audio format" );

        std::uint32_t sample_rate = 0;
        std::uint32_t channels = 0;
        std::uint32_t bits_per_sample = 0;
        std::uint32_t block_alignment = 0;
        require( actual_type->GetUINT32( MF_MT_AUDIO_SAMPLES_PER_SECOND, &sample_rate ), "Read audio sample rate" );
        require( actual_type->GetUINT32( MF_MT_AUDIO_NUM_CHANNELS, &channels ), "Read audio channel count" );
        require( actual_type->GetUINT32( MF_MT_AUDIO_BITS_PER_SAMPLE, &bits_per_sample ), "Read audio sample size" );
        require( actual_type->GetUINT32( MF_MT_AUDIO_BLOCK_ALIGNMENT, &block_alignment ),
            "Read audio block alignment" );

        format.sample_rate = static_cast<std::int32_t>( sample_rate );

        format.channels = static_cast<std::int32_t>( channels );

        format.bits_per_sample = static_cast<std::int32_t>( bits_per_sample );

        format.block_alignment = static_cast<std::int32_t>( block_alignment );
        return true;
    }

    void copy_rgb32_rows( const std::uint8_t *scanline_zero,
        long pitch,
        std::int32_t width,
        std::int32_t height,
        std::vector<std::uint8_t> &rgba )
    {
        if( scanline_zero == nullptr || width <= 0 || height <= 0 || std::abs( pitch ) < width * 4 )
        {
            throw std::runtime_error( "Media Foundation returned an invalid RGB32 frame" );
        }

        rgba.resize( static_cast<std::size_t>( width ) * static_cast<std::size_t>( height ) * 4u );

        for( std::int32_t y = 0; y < height; ++y )
        {
            const std::uint8_t *source = scanline_zero + static_cast<std::ptrdiff_t>( y ) * pitch;
            std::uint8_t *destination =
                rgba.data() + static_cast<std::size_t>( height - 1 - y ) * static_cast<std::size_t>( width ) * 4u;

            for( std::int32_t x = 0; x < width; ++x )
            {
                destination[ x * 4 + 0 ] = source[ x * 4 + 2 ];

                destination[ x * 4 + 1 ] = source[ x * 4 + 1 ];

                destination[ x * 4 + 2 ] = source[ x * 4 + 0 ];

                destination[ x * 4 + 3 ] = 255u;
            }
        }
    }

    void copy_video_sample( IMFSample *sample, const VideoFormat &format, std::vector<std::uint8_t> &rgba )
    {
        ComPtr<IMFMediaBuffer> buffer;
        const HRESULT first_buffer_result = sample->GetBufferByIndex( 0, &buffer );

        ComPtr<IMF2DBuffer> buffer_2d;

        if( SUCCEEDED( first_buffer_result ) && SUCCEEDED( buffer.As( &buffer_2d ) ) )
        {
            BYTE *scanline_zero = nullptr;
            LONG pitch = 0;
            require( buffer_2d->Lock2D( &scanline_zero, &pitch ), "Lock decoded video frame" );

            try
            {
                copy_rgb32_rows( scanline_zero, pitch, format.width, format.height, rgba );
            }
            catch( ... )
            {
                buffer_2d->Unlock2D();
                throw;
            }

            buffer_2d->Unlock2D();
            return;
        }

        ComPtr<IMFMediaBuffer> contiguous;
        require( sample->ConvertToContiguousBuffer( &contiguous ), "Flatten decoded video frame" );

        BYTE *bytes = nullptr;
        DWORD maximum_length = 0;
        DWORD current_length = 0;
        require( contiguous->Lock( &bytes, &maximum_length, &current_length ), "Lock decoded video bytes" );

        try
        {
            const long row_bytes = static_cast<long>( format.width * 4 );
            const long pitch = format.stride == 0 ? row_bytes : format.stride;
            const std::size_t required =
                static_cast<std::size_t>( std::abs( pitch ) ) * static_cast<std::size_t>( format.height );

            if( current_length < required )
            {
                throw std::runtime_error( "Decoded video frame is shorter than its declared stride" );
            }

            const std::uint8_t *scanline_zero = bytes;

            if( pitch > 0 )
            {
                scanline_zero += static_cast<std::size_t>( format.height - 1 ) * static_cast<std::size_t>( pitch );
                copy_rgb32_rows( scanline_zero, -pitch, format.width, format.height, rgba );
            }
            else
            {
                scanline_zero += static_cast<std::size_t>( format.height - 1 ) * static_cast<std::size_t>( -pitch );
                copy_rgb32_rows( scanline_zero, pitch, format.width, format.height, rgba );
            }
        }
        catch( ... )
        {
            contiguous->Unlock();
            throw;
        }

        contiguous->Unlock();
    }

    std::vector<std::uint8_t> copy_audio_sample( IMFSample *sample )
    {
        ComPtr<IMFMediaBuffer> buffer;
        require( sample->ConvertToContiguousBuffer( &buffer ), "Flatten decoded audio sample" );

        BYTE *bytes = nullptr;
        DWORD maximum_length = 0;
        DWORD current_length = 0;
        require( buffer->Lock( &bytes, &maximum_length, &current_length ), "Lock decoded audio sample" );
        std::vector<std::uint8_t> output( bytes, bytes + current_length );
        buffer->Unlock();
        return output;
    }
}

namespace piko::runtime::movie
{
    using media_foundation::AudioFormat;
    using media_foundation::ComApartment;
    using media_foundation::VideoFormat;
    using media_foundation::configure_audio_reader;
    using media_foundation::configure_video_reader;
    using media_foundation::copy_audio_sample;
    using media_foundation::copy_video_sample;
    using media_foundation::create_reader;

    MediaFoundationMoviePlayer::~MediaFoundationMoviePlayer()
    {
        close();
    }

    bool MediaFoundationMoviePlayer::open( const std::filesystem::path &movie_path )
    {
        close();
        failed_ = false;

        video_finished_ = true;
        {
            std::scoped_lock lock( mutex_ );
            last_error_.clear();
        }

        try
        {
            if( movie_path.empty() || !std::filesystem::is_regular_file( movie_path ) )
            {
                throw std::runtime_error( "Movie file does not exist: " + movie_path.string() );
            }

            media_foundation::require( MFStartup( MF_VERSION, MFSTARTUP_FULL ), "Start Media Foundation" );
            media_foundation_started_ = true;

            movie_path_ = movie_path;

            if( !read_video_format() )
            {
                return false;
            }

            if( !decode_audio() )
            {
                return false;
            }

            stop_requested_ = false;

            video_finished_ = false;

            video_thread_ = std::thread( &MediaFoundationMoviePlayer::video_worker, this );

            {
                std::unique_lock lock( mutex_ );
                condition_.wait( lock,
                    [ this ]
                    {
                        return !frames_.empty() || video_finished_;
                    } );
            }

            if( failed_ )
            {
                return false;
            }

            if( video_finished() )
            {
                set_error( "MP4 video stream produced no frames" );
                return false;
            }

            return true;
        }
        catch( const std::exception &error )
        {
            set_error( error.what() );
            video_finished_ = true;
            return false;
        }
    }

    void MediaFoundationMoviePlayer::close()
    {
        stop_requested_ = true;
        condition_.notify_all();

        if( video_thread_.joinable() )
        {
            video_thread_.join();
        }

        {
            std::scoped_lock lock( mutex_ );
            frames_.clear();
        }

        audio_ = {};
        movie_path_.clear();
        width_ = 0;

        height_ = 0;

        frames_per_second_ = 30.0;

        video_finished_ = true;

        if( media_foundation_started_ )
        {
            MFShutdown();
            media_foundation_started_ = false;
        }
    }

    bool MediaFoundationMoviePlayer::pop_frame( MovieFrame &frame )
    {
        std::scoped_lock lock( mutex_ );

        if( frames_.empty() )
        {
            return false;
        }

        frame = std::move( frames_.front() );
        frames_.pop_front();
        condition_.notify_one();
        return true;
    }

    bool MediaFoundationMoviePlayer::video_finished() const
    {
        std::scoped_lock lock( mutex_ );
        return video_finished_ && frames_.empty();
    }

    bool MediaFoundationMoviePlayer::failed() const
    {
        return failed_;
    }

    std::string MediaFoundationMoviePlayer::last_error() const
    {
        std::scoped_lock lock( mutex_ );
        return last_error_;
    }

    runtime::audio::Pcm16 MediaFoundationMoviePlayer::take_audio()
    {
        return std::move( audio_ );
    }

    std::int32_t MediaFoundationMoviePlayer::width() const
    {
        return width_;
    }

    std::int32_t MediaFoundationMoviePlayer::height() const
    {
        return height_;
    }

    double MediaFoundationMoviePlayer::frames_per_second() const
    {
        return frames_per_second_ > 0.0 ? frames_per_second_ : 30.0;
    }

    bool MediaFoundationMoviePlayer::read_video_format()
    {
        try
        {
            ComApartment apartment;

            if( !apartment.available() )
            {
                throw std::runtime_error( "Could not initialize COM for movie playback (" +
                                          media_foundation::hresult_string( apartment.result() ) + ")" );
            }

            media_foundation::ComPtr<IMFSourceReader> reader = create_reader( movie_path_, true );
            const VideoFormat format = configure_video_reader( reader.Get() );

            if( format.width <= 0 || format.height <= 0 )
            {
                throw std::runtime_error( "MP4 video stream has invalid dimensions" );
            }

            width_ = format.width;

            height_ = format.height;

            frames_per_second_ = format.frames_per_second;
            return true;
        }
        catch( const std::exception &error )
        {
            set_error( error.what() );
            return false;
        }
    }

    bool MediaFoundationMoviePlayer::decode_audio()
    {
        try
        {
            ComApartment apartment;

            if( !apartment.available() )
            {
                throw std::runtime_error( "Could not initialize COM for movie audio (" +
                                          media_foundation::hresult_string( apartment.result() ) + ")" );
            }

            media_foundation::ComPtr<IMFSourceReader> reader = create_reader( movie_path_, false );
            AudioFormat format{};

            if( !configure_audio_reader( reader.Get(), format ) )
            {
                audio_ = {};
                return true;
            }

            if( format.bits_per_sample != 16 || format.sample_rate <= 0 || format.channels <= 0 )
            {
                throw std::runtime_error( "Media Foundation did not provide 16-bit PCM movie audio" );
            }

            std::vector<std::uint8_t> bytes;

            for( ;; )
            {
                DWORD actual_stream = 0;
                DWORD flags = 0;
                LONGLONG timestamp = 0;
                media_foundation::ComPtr<IMFSample> sample;
                media_foundation::require( reader->ReadSample( MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                                               0,
                                               &actual_stream,
                                               &flags,
                                               &timestamp,
                                               &sample ),
                    "Decode MP4 audio" );

                if( ( flags & MF_SOURCE_READERF_ENDOFSTREAM ) != 0u )
                {
                    break;
                }

                if( !sample )
                {
                    continue;
                }

                std::vector<std::uint8_t> sample_bytes = copy_audio_sample( sample.Get() );
                bytes.insert( bytes.end(), sample_bytes.begin(), sample_bytes.end() );
            }

            const std::size_t alignment =
                static_cast<std::size_t>( std::max<std::int32_t>( 2, format.block_alignment ) );

            bytes.resize( bytes.size() - bytes.size() % alignment );

            audio_.sample_rate = format.sample_rate;

            audio_.channels = format.channels;
            audio_.interleaved.resize( bytes.size() / sizeof( std::int16_t ) );

            if( !bytes.empty() )
            {
                std::memcpy( audio_.interleaved.data(), bytes.data(), bytes.size() );
            }

            return true;
        }
        catch( const std::exception &error )
        {
            set_error( error.what() );
            return false;
        }
    }

    void MediaFoundationMoviePlayer::video_worker()
    {
        try
        {
            ComApartment apartment;

            if( !apartment.available() )
            {
                throw std::runtime_error( "Could not initialize COM for movie video (" +
                                          media_foundation::hresult_string( apartment.result() ) + ")" );
            }

            media_foundation::ComPtr<IMFSourceReader> reader = create_reader( movie_path_, true );
            const VideoFormat format = configure_video_reader( reader.Get() );
            std::uint64_t frame_index = 0;

            while( !stop_requested_ )
            {
                DWORD actual_stream = 0;
                DWORD flags = 0;
                LONGLONG timestamp = 0;
                media_foundation::ComPtr<IMFSample> sample;
                media_foundation::require( reader->ReadSample( MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                               0,
                                               &actual_stream,
                                               &flags,
                                               &timestamp,
                                               &sample ),
                    "Decode MP4 video" );

                if( ( flags & MF_SOURCE_READERF_ENDOFSTREAM ) != 0u )
                {
                    break;
                }

                if( !sample )
                {
                    continue;
                }

                MovieFrame frame{};
                frame.index = frame_index++;

                frame.presentation_seconds = static_cast<double>( timestamp ) / 10000000.0;
                copy_video_sample( sample.Get(), format, frame.rgba );

                std::unique_lock lock( mutex_ );
                condition_.wait( lock,
                    [ this ]
                    {
                        return stop_requested_ || frames_.size() < max_queued_frames;
                    } );
                if( stop_requested_ )
                {
                    break;
                }

                frames_.push_back( std::move( frame ) );
                lock.unlock();
                condition_.notify_one();
            }
        }
        catch( const std::exception &error )
        {
            if( !stop_requested_ )
            {
                set_error( error.what() );
            }
        }

        video_finished_ = true;
        condition_.notify_all();
    }

    void MediaFoundationMoviePlayer::set_error( std::string message )
    {
        {
            std::scoped_lock lock( mutex_ );
            last_error_ = std::move( message );
        }

        failed_ = true;
    }
}

#else

namespace piko::runtime::movie
{
    MediaFoundationMoviePlayer::~MediaFoundationMoviePlayer()
    {
        close();
    }

    bool MediaFoundationMoviePlayer::open( const std::filesystem::path & )
    {
        set_error( "Native MP4 playback is currently available on Windows only" );
        return false;
    }

    void MediaFoundationMoviePlayer::close()
    {
        audio_ = {};
        movie_path_.clear();
        video_finished_ = true;
    }

    bool MediaFoundationMoviePlayer::pop_frame( MovieFrame & )
    {
        return false;
    }

    bool MediaFoundationMoviePlayer::video_finished() const
    {
        return true;
    }

    bool MediaFoundationMoviePlayer::failed() const
    {
        return failed_;
    }

    std::string MediaFoundationMoviePlayer::last_error() const
    {
        std::scoped_lock lock( mutex_ );
        return last_error_;
    }

    runtime::audio::Pcm16 MediaFoundationMoviePlayer::take_audio()
    {
        return {};
    }

    std::int32_t MediaFoundationMoviePlayer::width() const
    {
        return 0;
    }

    std::int32_t MediaFoundationMoviePlayer::height() const
    {
        return 0;
    }

    double MediaFoundationMoviePlayer::frames_per_second() const
    {
        return 30.0;
    }

    bool MediaFoundationMoviePlayer::read_video_format()
    {
        return false;
    }

    bool MediaFoundationMoviePlayer::decode_audio()
    {
        return false;
    }

    void MediaFoundationMoviePlayer::video_worker()
    {
        video_finished_ = true;
    }

    void MediaFoundationMoviePlayer::set_error( std::string message )
    {
        std::scoped_lock lock( mutex_ );
        last_error_ = std::move( message );

        failed_ = true;
    }
}

#endif
