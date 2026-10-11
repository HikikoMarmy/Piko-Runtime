#pragma once

#include "Managers/AudioManager.hpp"
#include "Managers/TextureManager.hpp"
#include "Modules/MainMenu/TitleBackgroundRenderer.hpp"
#include "Modules/PikoModule.hpp"
#include "Rendering/RenderTypes.hpp"

#include <cstdint>
#include <array>
#include <optional>
#include <string>

namespace piko::rendering
{
    struct LogicalViewport;
}

namespace piko::modules
{
    class CollectionModule final : public PikoModule
    {
    public:
        struct RewardIcon
        {
            std::string asset{};
            rendering::Rect source{};
        };

        [[nodiscard]] static const char *reward_name( std::int32_t index );
        [[nodiscard]] static std::optional<RewardIcon> reward_icon( std::int32_t index );

        void enter( App &app ) override;
        void leave( App &app ) override;
        void update( App &app, double delta_seconds ) override;
        void render( App &app, rendering::Renderer &renderer ) override;

    private:
        enum class Screen
        {
            Board,
            Figure
        };

        enum class TextureSlot : std::int32_t
        {
            CharactersA,
            CharactersB,
            BoardLeft,
            BoardRight,
            DetailBackground
        };

        enum class FigureLayout : std::int32_t
        {
            Portrait,
            Landscape,
            ExpandedText
        };

        enum class PortableBackground : std::int32_t
        {
            Blue,
            Green,
            Red,
            Yellow
        };

        enum class BoardSide : std::int32_t
        {
            A,
            B
        };

        enum class Direction : std::int32_t
        {
            Up,
            Down,
            Left,
            Right
        };

        struct SpritePiece
        {
            TextureSlot texture_slot = TextureSlot::CharactersA;
            float source_x = 0.0f;
            float source_y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            float destination_x = 0.0f;
            float destination_y = 0.0f;
        };

        struct DetailPlacement
        {
            FigureLayout layout = FigureLayout::Portrait;
            float character_small_x = 0.0f;
            float character_small_y = 0.0f;
            float character_large_x = 0.0f;
            float character_large_y = 0.0f;
            float text_large_x = 0.0f;
            float text_large_y = 0.0f;
            float text_bottom_x = 0.0f;
            float text_bottom_y = 0.0f;
        };

        [[nodiscard]] static rendering::Rect detail_name_bounds( const DetailPlacement &placement );

        static constexpr float detail_name_height = 24.0f;
        static constexpr float detail_name_maximum_width = 260.0f;
        static constexpr float detail_name_margin = 12.0f;

        void initialize_audio( App &app );
        void play_menu_sound( App &app, const char *name );
        void play_character_voice( App &app );
        void play_board_bgm( App &app );
        void play_detail_bgm( App &app );
        void move_board_selection( App &app, Direction direction );
        void switch_board_side( App &app );
        void open_figure( App &app );
        void close_figure( App &app );
        void load_fixed_textures( App &app );
        void unload_fixed_textures( App &app );
        void load_detail_textures( App &app );
        void unload_detail_textures( App &app );
        void load_portable_figures( App &app );
        void unload_portable_figures( App &app );
        void render_board( App &app, rendering::Renderer &renderer );
        void render_original_board( App &app, rendering::Renderer &renderer );
        void render_portable_board( App &app, rendering::Renderer &renderer );
        void render_figure( App &app, rendering::Renderer &renderer );
        void render_original_figure( App &app, rendering::Renderer &renderer );
        void render_portable_figure( App &app, rendering::Renderer &renderer );
        void render_original_figure_background( rendering::Renderer &renderer );
        void draw_piece( App &app,
            rendering::Renderer &renderer,
            const rendering::LogicalViewport &layout,
            const SpritePiece &piece,
            float offset_x = 0.0f ) const;

        [[nodiscard]] bool has_original_board( App &app ) const;
        [[nodiscard]] bool has_original_figure( App &app ) const;
        [[nodiscard]] bool board_node_visible( App &app, std::int32_t node ) const;
        [[nodiscard]] BoardSide board_side_for_node( std::int32_t node ) const;
        [[nodiscard]] std::int32_t portable_page_slot( std::int32_t item ) const;

        // Collection indices follow CHR_01 through CHR_28
        static constexpr std::array<const char *, 28> names_{
            "@character.panpeus",
            "@character.flying_o",
            "@character.mojo_king_bee",
            "@character.ben_k",
            "@character.ben_k",
            "@character.sambone_trio",
            "@character.gregorio",
            "@character.kirah",
            "@character.kirah",
            "@character.zowie",
            "@character.zowie",
            "@character.ming_ming",
            "@character.gitaroo_man",
            "@character.u1",
            "@character.gitaroo_man",
            "@character.puma",
            "@character.puma_ac30",
            "@character.miranda",
            "@character.pico",
            "@character.kazuya",
            "@character.woofer_jet",
            "@character.woofer_jumbo",
            "@character.woofer_mazing",
            "@character.yung_noen_yasuda",
            "@character.angel",
            "@character.gitaroo",
            "@character.puma",
            "@character.puma"
        };

        static constexpr std::array<const char *, 5> original_texture_paths_{
            "CCOLLE/TEXTURES/CCOLLE/CHARA_A.png",
            "CCOLLE/TEXTURES/CCOLLE/CHARA_B.png",
            "CCOLLE/TEXTURES/CCOLLE/BAG_LEFT.png",
            "CCOLLE/TEXTURES/CCOLLE/BAG_RIGH.png",
            "CCOLLE/TEXTURES/CCOLLE/BG_DTL.png"
        };

        static constexpr std::array<std::int32_t, 28> requirement_stages_{
            10,
            8,
            2,
            1,
            6,
            6,
            0,
            5,
            2,
            5,
            0,
            4,
            3,
            0,
            7,
            1,
            2,
            4,
            3,
            9,
            10,
            4,
            10,
            6,
            8,
            7,
            9,
            8
        };

        static constexpr std::array<PortableBackground, 28> background_colors_{
            PortableBackground::Blue,
            PortableBackground::Green,
            PortableBackground::Red,
            PortableBackground::Yellow,
            PortableBackground::Blue,
            PortableBackground::Green,
            PortableBackground::Red,
            PortableBackground::Yellow,
            PortableBackground::Blue,
            PortableBackground::Green,
            PortableBackground::Red,
            PortableBackground::Yellow,
            PortableBackground::Blue,
            PortableBackground::Green,
            PortableBackground::Red,
            PortableBackground::Yellow,
            PortableBackground::Blue,
            PortableBackground::Green,
            PortableBackground::Red,
            PortableBackground::Yellow,
            PortableBackground::Blue,
            PortableBackground::Green,
            PortableBackground::Red,
            PortableBackground::Yellow,
            PortableBackground::Blue,
            PortableBackground::Green,
            PortableBackground::Red,
            PortableBackground::Yellow
        };

        static constexpr std::array<std::int32_t, 28> voice_cues_{
            13,
            15,
            20,
            19,
            14,
            24,
            0,
            22,
            23,
            21,
            0,
            18,
            16,
            0,
            17,
            1,
            2,
            4,
            3,
            8,
            11,
            5,
            10,
            9,
            7,
            6,
            12,
            25
        };

        static constexpr std::array<const char *, 28> ps2_voice_cues_{
            "CHRL01",
            "CHRL02",
            "CHRL03",
            "CHRL04",
            "CHRL05",
            "CHRL06",
            "CHRL07",
            "CHRL08",
            "CHRL09",
            "CHRL11",
            "CHRL10",
            "CHRL12",
            "CHRL13",
            "CHRL14",
            "CHRL15",
            "CHRL16",
            "CHRL17",
            "CHRL18",
            "CHRL19",
            "CHRL20",
            "CHRL21",
            "CHRL23",
            "CHRL22",
            "CHRL24",
            "CHRL25",
            nullptr,
            nullptr,
            nullptr
        };

        static constexpr std::array<SpritePiece, 28> board_locked_pieces_{
            SpritePiece{ TextureSlot::CharactersB, 371.0f, 300.0f, 79.0f, 114.0f, 794.0f, 206.0f },
            SpritePiece{ TextureSlot::CharactersB, 367.0f, 115.0f, 145.0f, 94.0f, 719.0f, 308.0f },
            SpritePiece{ TextureSlot::CharactersB, 182.0f, 147.0f, 66.0f, 120.0f, 1086.0f, 183.0f },
            SpritePiece{ TextureSlot::CharactersB, 382.0f, 209.0f, 130.0f, 91.0f, 1105.0f, 99.0f },
            SpritePiece{ TextureSlot::CharactersB, 0.0f, 0.0f, 145.0f, 149.0f, 694.0f, 96.0f },
            SpritePiece{ TextureSlot::CharactersB, 357.0f, 0.0f, 155.0f, 115.0f, 1055.0f, 286.0f },
            SpritePiece{ TextureSlot::CharactersB, 0.0f, 149.0f, 57.0f, 124.0f, 1039.0f, 96.0f },
            SpritePiece{ TextureSlot::CharactersB, 57.0f, 149.0f, 74.0f, 121.0f, 888.0f, 265.0f },
            SpritePiece{ TextureSlot::CharactersB, 131.0f, 149.0f, 51.0f, 120.0f, 981.0f, 131.0f },
            SpritePiece{ TextureSlot::CharactersB, 145.0f, 0.0f, 132.0f, 147.0f, 851.0f, 93.0f },
            SpritePiece{ TextureSlot::CharactersB, 277.0f, 0.0f, 76.0f, 141.0f, 972.0f, 259.0f },
            SpritePiece{ TextureSlot::CharactersB, 248.0f, 147.0f, 65.0f, 88.0f, 1166.0f, 192.0f },
            SpritePiece{ TextureSlot::CharactersA, 273.0f, 0.0f, 102.0f, 151.0f, 197.0f, 255.0f },
            SpritePiece{ TextureSlot::CharactersA, 0.0f, 120.0f, 65.0f, 111.0f, 307.0f, 195.0f },
            SpritePiece{ TextureSlot::CharactersA, 375.0f, 0.0f, 137.0f, 146.0f, 171.0f, 107.0f },
            SpritePiece{ TextureSlot::CharactersA, 203.0f, 91.0f, 70.0f, 64.0f, 529.0f, 122.0f },
            SpritePiece{ TextureSlot::CharactersA, 189.0f, 155.0f, 68.0f, 82.0f, 301.0f, 306.0f },
            SpritePiece{ TextureSlot::CharactersA, 257.0f, 155.0f, 68.0f, 84.0f, 385.0f, 194.0f },
            SpritePiece{ TextureSlot::CharactersA, 65.0f, 120.0f, 59.0f, 113.0f, 459.0f, 192.0f },
            SpritePiece{ TextureSlot::CharactersA, 124.0f, 120.0f, 58.0f, 127.0f, 528.0f, 184.0f },
            SpritePiece{ TextureSlot::BoardLeft, 333.0f, 174.0f, 115.0f, 94.0f, 61.0f, 232.0f },
            SpritePiece{ TextureSlot::CharactersA, 247.0f, 239.0f, 147.0f, 69.0f, 50.0f, 329.0f },
            SpritePiece{ TextureSlot::CharactersA, 394.0f, 146.0f, 118.0f, 134.0f, 51.0f, 101.0f },
            SpritePiece{ TextureSlot::CharactersA, 0.0f, 0.0f, 190.0f, 120.0f, 395.0f, 277.0f },
            SpritePiece{ TextureSlot::CharactersB, 302.0f, 353.0f, 69.0f, 82.0f, 681.0f, 246.0f },
            SpritePiece{ TextureSlot::CharactersA, 192.0f, 0.0f, 81.0f, 91.0f, 304.0f, 102.0f },
            SpritePiece{ TextureSlot::BoardLeft, 453.0f, 232.0f, 59.0f, 96.0f, 390.0f, 93.0f },
            SpritePiece{ TextureSlot::CharactersA, 325.0f, 151.0f, 69.0f, 88.0f, 460.0f, 99.0f }
        };

        static constexpr std::array<SpritePiece, 28> board_unlocked_pieces_{
            SpritePiece{ TextureSlot::CharactersB, 248.0f, 235.0f, 64.0f, 102.0f, 802.0f, 212.0f },
            SpritePiece{ TextureSlot::BoardLeft, 323.0f, 268.0f, 130.0f, 81.0f, 727.0f, 314.0f },
            SpritePiece{ TextureSlot::CharactersB, 313.0f, 141.0f, 54.0f, 105.0f, 1092.0f, 190.0f },
            SpritePiece{ TextureSlot::CharactersB, 248.0f, 435.0f, 118.0f, 77.0f, 1112.0f, 106.0f },
            SpritePiece{ TextureSlot::CharactersB, 0.0f, 377.0f, 130.0f, 135.0f, 701.0f, 103.0f },
            SpritePiece{ TextureSlot::CharactersB, 0.0f, 278.0f, 139.0f, 99.0f, 1063.0f, 294.0f },
            SpritePiece{ TextureSlot::CharactersB, 199.0f, 268.0f, 44.0f, 111.0f, 1046.0f, 102.0f },
            SpritePiece{ TextureSlot::CharactersB, 139.0f, 272.0f, 60.0f, 107.0f, 895.0f, 272.0f },
            SpritePiece{ TextureSlot::CharactersB, 312.0f, 246.0f, 37.0f, 105.0f, 988.0f, 138.0f },
            SpritePiece{ TextureSlot::CharactersB, 130.0f, 379.0f, 118.0f, 133.0f, 858.0f, 100.0f },
            SpritePiece{ TextureSlot::CharactersB, 450.0f, 300.0f, 62.0f, 127.0f, 978.0f, 265.0f },
            SpritePiece{ TextureSlot::CharactersB, 367.0f, 438.0f, 51.0f, 74.0f, 1172.0f, 199.0f },
            SpritePiece{ TextureSlot::CharactersA, 103.0f, 372.0f, 91.0f, 140.0f, 203.0f, 260.0f },
            SpritePiece{ TextureSlot::CharactersA, 343.0f, 308.0f, 51.0f, 95.0f, 314.0f, 203.0f },
            SpritePiece{ TextureSlot::CharactersA, 194.0f, 380.0f, 123.0f, 132.0f, 178.0f, 114.0f },
            SpritePiece{ TextureSlot::CharactersA, 163.0f, 268.0f, 56.0f, 50.0f, 536.0f, 129.0f },
            SpritePiece{ TextureSlot::CharactersA, 110.0f, 252.0f, 53.0f, 66.0f, 309.0f, 314.0f },
            SpritePiece{ TextureSlot::CharactersA, 236.0f, 309.0f, 52.0f, 71.0f, 393.0f, 199.0f },
            SpritePiece{ TextureSlot::CharactersA, 394.0f, 305.0f, 45.0f, 98.0f, 466.0f, 199.0f },
            SpritePiece{ TextureSlot::CharactersA, 439.0f, 289.0f, 46.0f, 114.0f, 534.0f, 190.0f },
            SpritePiece{ TextureSlot::CharactersA, 0.0f, 314.0f, 101.0f, 78.0f, 67.0f, 240.0f },
            SpritePiece{ TextureSlot::CharactersA, 101.0f, 318.0f, 135.0f, 54.0f, 56.0f, 337.0f },
            SpritePiece{ TextureSlot::CharactersA, 0.0f, 392.0f, 103.0f, 120.0f, 58.0f, 107.0f },
            SpritePiece{ TextureSlot::CharactersA, 317.0f, 403.0f, 180.0f, 109.0f, 400.0f, 282.0f },
            SpritePiece{ TextureSlot::CharactersB, 248.0f, 367.0f, 54.0f, 68.0f, 688.0f, 253.0f },
            SpritePiece{ TextureSlot::CharactersA, 44.0f, 237.0f, 66.0f, 77.0f, 311.0f, 109.0f },
            SpritePiece{ TextureSlot::CharactersA, 0.0f, 233.0f, 44.0f, 81.0f, 397.0f, 100.0f },
            SpritePiece{ TextureSlot::CharactersA, 288.0f, 308.0f, 55.0f, 72.0f, 467.0f, 107.0f }
        };

        static constexpr std::array<SpritePiece, 6> board_frame_pieces_{
            SpritePiece{ TextureSlot::BoardLeft, 127.0f, 0.0f, 133.0f, 48.0f, 741.0f, 19.0f },
            SpritePiece{ TextureSlot::BoardLeft, 0.0f, 0.0f, 127.0f, 48.0f, 414.0f, 19.0f },
            SpritePiece{ TextureSlot::BoardLeft, 0.0f, 83.0f, 306.0f, 429.0f, 17.0f, -1.0f },
            SpritePiece{ TextureSlot::BoardRight, 0.0f, 83.0f, 307.0f, 429.0f, 319.0f, -1.0f },
            SpritePiece{ TextureSlot::BoardLeft, 0.0f, 83.0f, 306.0f, 429.0f, 657.0f, -1.0f },
            SpritePiece{ TextureSlot::BoardRight, 0.0f, 83.0f, 307.0f, 429.0f, 959.0f, -1.0f }
        };

        static constexpr std::array<SpritePiece, 1> board_title_pieces_{
            SpritePiece{ TextureSlot::BoardLeft, 278.0f, 0.0f, 234.0f, 57.0f, 24.0f, 15.0f }
        };

        static constexpr std::array<SpritePiece, 2> board_side_switch_pieces_{
            SpritePiece{ TextureSlot::BoardLeft, 402.0f, 57.0f, 51.0f, 51.0f, 670.0f, 14.0f },
            SpritePiece{ TextureSlot::BoardLeft, 351.0f, 57.0f, 51.0f, 51.0f, 560.0f, 14.0f }
        };

        static constexpr std::array<std::array<std::int32_t, 4>, 30> board_neighbors_{
            std::array<std::int32_t, 4>{ 4, 1, 24, 9 },
            std::array<std::int32_t, 4>{ 24, -1, -1, 7 },
            std::array<std::int32_t, 4>{ 3, 5, 8, 11 },
            std::array<std::int32_t, 4>{ -1, 11, 6, -1 },
            std::array<std::int32_t, 4>{ 28, 24, -1, 9 },
            std::array<std::int32_t, 4>{ 2, -1, 10, -1 },
            std::array<std::int32_t, 4>{ -1, 5, 8, 3 },
            std::array<std::int32_t, 4>{ 9, -1, 1, 10 },
            std::array<std::int32_t, 4>{ -1, 10, 9, 6 },
            std::array<std::int32_t, 4>{ -1, 8, 4, 8 },
            std::array<std::int32_t, 4>{ 8, -1, 7, 5 },
            std::array<std::int32_t, 4>{ 3, 5, 2, -1 },
            std::array<std::int32_t, 4>{ 14, -1, 20, 16 },
            std::array<std::int32_t, 4>{ 25, 16, 12, 17 },
            std::array<std::int32_t, 4>{ -1, 12, 22, 25 },
            std::array<std::int32_t, 4>{ 29, 19, 27, -1 },
            std::array<std::int32_t, 4>{ 13, -1, 12, 23 },
            std::array<std::int32_t, 4>{ 26, 23, 13, 18 },
            std::array<std::int32_t, 4>{ 27, 23, 17, 19 },
            std::array<std::int32_t, 4>{ 15, 23, 18, -1 },
            std::array<std::int32_t, 4>{ 22, 21, -1, 12 },
            std::array<std::int32_t, 4>{ 20, -1, -1, 12 },
            std::array<std::int32_t, 4>{ -1, 20, -1, 14 },
            std::array<std::int32_t, 4>{ 18, -1, 16, -1 },
            std::array<std::int32_t, 4>{ 4, 1, -1, 0 },
            std::array<std::int32_t, 4>{ -1, 13, 14, 26 },
            std::array<std::int32_t, 4>{ -1, 17, 25, 27 },
            std::array<std::int32_t, 4>{ -1, 18, 26, 15 },
            std::array<std::int32_t, 4>{ -1, 4, -1, -1 },
            std::array<std::int32_t, 4>{ -1, 15, -1, -1 }
        };

        static constexpr std::array<DetailPlacement, 28> detail_placements_{
            DetailPlacement{ FigureLayout::Landscape, 13.0f, 74.0f, 10.0f, 47.0f, 343.0f, 120.0f, 25.0f, 337.0f },
            DetailPlacement{ FigureLayout::Landscape, 20.0f, 114.0f, 10.0f, 48.0f, 334.0f, 72.0f, 305.0f, 381.0f },
            DetailPlacement{ FigureLayout::Portrait, 77.0f, 38.0f, 10.0f, 48.0f, 327.0f, 85.0f, 262.0f, 381.0f },
            DetailPlacement{ FigureLayout::Landscape, 19.0f, 112.0f, 10.0f, 48.0f, 335.0f, 127.0f, 257.0f, 79.0f },
            DetailPlacement{ FigureLayout::Landscape, 14.0f, 43.0f, 10.0f, 47.0f, 374.0f, 81.0f, 185.0f, 373.0f },
            DetailPlacement{ FigureLayout::Landscape, 22.0f, 78.0f, 10.0f, 48.0f, 339.0f, 72.0f, 18.0f, 316.0f },
            DetailPlacement{ FigureLayout::Portrait, 92.0f, 24.0f, 10.0f, 47.0f, 247.0f, 73.0f, 195.0f, 360.0f },
            DetailPlacement{ FigureLayout::Portrait, 99.0f, 24.0f, 10.0f, 48.0f, 314.0f, 126.0f, 251.0f, 78.0f },
            DetailPlacement{ FigureLayout::Portrait, 89.0f, 18.0f, 10.0f, 48.0f, 331.0f, 135.0f, 262.0f, 83.0f },
            DetailPlacement{ FigureLayout::Portrait, 35.0f, 13.0f, 10.0f, 47.0f, 347.0f, 88.0f, 145.0f, 347.0f },
            DetailPlacement{ FigureLayout::Portrait, 74.0f, 17.0f, 10.0f, 47.0f, 331.0f, 135.0f, 266.0f, 85.0f },
            DetailPlacement{ FigureLayout::Portrait, 83.0f, 35.0f, 10.0f, 48.0f, 327.0f, 83.0f, 222.0f, 362.0f },
            DetailPlacement{ FigureLayout::Portrait, 51.0f, 10.0f, 10.0f, 48.0f, 316.0f, 138.0f, 242.0f, 88.0f },
            DetailPlacement{ FigureLayout::Portrait, 31.0f, 18.0f, 10.0f, 47.0f, 331.0f, 135.0f, 316.0f, 84.0f },
            DetailPlacement{ FigureLayout::Portrait, 25.0f, 14.0f, 10.0f, 48.0f, 331.0f, 88.0f, 193.0f, 381.0f },
            DetailPlacement{ FigureLayout::Portrait, 21.0f, 132.0f, 10.0f, 65.0f, 328.0f, 109.0f, 188.0f, 67.0f },
            DetailPlacement{ FigureLayout::Portrait, 26.0f, 49.0f, 10.0f, 47.0f, 330.0f, 85.0f, 160.0f, 365.0f },
            DetailPlacement{ FigureLayout::Portrait, 26.0f, 55.0f, 10.0f, 48.0f, 303.0f, 130.0f, 319.0f, 80.0f },
            DetailPlacement{ FigureLayout::Portrait, 84.0f, 21.0f, 10.0f, 47.0f, 330.0f, 131.0f, 260.0f, 85.0f },
            DetailPlacement{ FigureLayout::Portrait, 55.0f, 21.0f, 10.0f, 47.0f, 296.0f, 134.0f, 280.0f, 75.0f },
            DetailPlacement{ FigureLayout::Landscape, 19.0f, 71.0f, 10.0f, 47.0f, 381.0f, 102.0f, 30.0f, 334.0f },
            DetailPlacement{ FigureLayout::Landscape, 13.0f, 86.0f, 10.0f, 47.0f, 364.0f, 91.0f, 74.0f, 299.0f },
            DetailPlacement{ FigureLayout::Portrait, 34.0f, 28.0f, 14.0f, 62.0f, 329.0f, 88.0f, 202.0f, 386.0f },
            DetailPlacement{ FigureLayout::Landscape, 7.0f, 85.0f, 10.0f, 61.0f, 346.0f, 69.0f, 46.0f, 326.0f },
            DetailPlacement{ FigureLayout::Portrait, 30.0f, 45.0f, 10.0f, 47.0f, 331.0f, 135.0f, 269.0f, 0.0f },
            DetailPlacement{ FigureLayout::ExpandedText, 92.0f, 113.0f, 44.0f, 55.0f, 134.0f, 24.0f, 355.0f, 284.0f },
            DetailPlacement{ FigureLayout::ExpandedText, 97.0f, 64.0f, 474.0f, 84.0f, 118.0f, 41.0f, 15.0f, 342.0f },
            DetailPlacement{ FigureLayout::ExpandedText, 98.0f, 62.0f, 474.0f, 84.0f, 0.0f, 0.0f, 15.0f, 342.0f }
        };

        static constexpr std::array<SpritePiece, 32> detail_background_pieces_{
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 1.0f, 90.0f, 63.0f, 127.0f, 118.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 1.0f, 90.0f, 63.0f, 462.0f, 11.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 1.0f, 90.0f, 63.0f, 737.0f, 276.0f },
            SpritePiece{ TextureSlot::DetailBackground, 93.0f, 1.0f, 147.0f, 63.0f, 805.0f, 51.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 66.0f, 426.0f, 61.0f, -41.0f, 187.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 66.0f, 426.0f, 61.0f, 294.0f, 80.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 66.0f, 426.0f, 61.0f, 779.0f, 342.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 66.0f, 426.0f, 61.0f, 1239.0f, 187.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 129.0f, 148.0f, 50.0f, 98.0f, 270.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 129.0f, 148.0f, 50.0f, 1014.0f, 104.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 129.0f, 148.0f, 50.0f, 433.0f, 163.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 129.0f, 148.0f, 50.0f, 918.0f, 425.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 181.0f, 214.0f, 54.0f, 74.0f, 338.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 181.0f, 214.0f, 54.0f, 409.0f, 231.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 181.0f, 214.0f, 54.0f, 1046.0f, 250.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 237.0f, 284.0f, 60.0f, 20.0f, 403.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 237.0f, 284.0f, 60.0f, 365.0f, 296.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 299.0f, 414.0f, 63.0f, 300.0f, 366.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 299.0f, 414.0f, 63.0f, 695.0f, -19.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 364.0f, 487.0f, 63.0f, -78.0f, 22.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 364.0f, 487.0f, 63.0f, 659.0f, 177.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 364.0f, 487.0f, 63.0f, 1202.0f, 22.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 1.0f, 90.0f, 63.0f, 1407.0f, 118.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 1.0f, 90.0f, 63.0f, 1742.0f, 11.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 66.0f, 426.0f, 61.0f, 1574.0f, 80.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 129.0f, 148.0f, 50.0f, 1378.0f, 270.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 129.0f, 148.0f, 50.0f, 1713.0f, 163.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 181.0f, 214.0f, 54.0f, 1354.0f, 338.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 181.0f, 214.0f, 54.0f, 1689.0f, 231.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 237.0f, 284.0f, 60.0f, 1300.0f, 403.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 237.0f, 284.0f, 60.0f, 1645.0f, 296.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 299.0f, 414.0f, 63.0f, 1580.0f, 366.0f }
        };

        static constexpr std::array<SpritePiece, 2> detail_foreground_pieces_{
            SpritePiece{ TextureSlot::DetailBackground, 251.0f, 429.0f, 232.0f, 54.0f, 385.0f, 17.0f },
            SpritePiece{ TextureSlot::DetailBackground, 1.0f, 479.0f, 90.0f, 26.0f, 27.0f, 412.0f }
        };

        static constexpr std::array<std::int32_t, 15>
            side_a_items_{
            12,
            13,
            14,
            15,
            16,
            17,
            18,
            19,
            20,
            21,
            22,
            23,
            25,
            26,
            27
        };

        static constexpr std::array<std::int32_t, 13> side_b_items_{
            0,
            1,
            2,
            3,
            4,
            5,
            6,
            7,
            8,
            9,
            10,
            11,
            24
        };

        std::array<managers::TextureHandle, 28> portable_figure_textures_{};
        std::array<managers::TextureHandle, 4> portable_background_textures_{};
        std::array<managers::TextureHandle, 5> original_fixed_textures_{};
        TitleBackgroundRenderer detail_background_renderer_{};
        managers::TextureHandle portable_words_texture_{};
        managers::TextureHandle detail_character_texture_{};
        managers::TextureHandle detail_text_texture_{};
        managers::TextureHandle detail_name_texture_{};
        managers::AudioChannelHandle bgm_channel_{};
        managers::AudioChannelHandle sfx_channel_{};
        managers::AudioChannelHandle voice_channel_{};
        Screen screen_ = Screen::Board;
        std::int32_t selected_node_ = 29;
        std::int32_t selected_item_ = 15;
        BoardSide board_side_ = BoardSide::A;
        float board_offset_ = 0.0f;
        float board_target_offset_ = 0.0f;
        double elapsed_seconds_ = 0.0;
        double figure_enter_seconds_ = 0.0;
        bool board_transitioning_ = false;
        bool leaving_ = false;
    };
}
