#pragma once

#include <SDL3/SDL.h>

namespace piko
{
    class App;
}

namespace piko::rendering
{
    class Renderer;
}

namespace piko::modules
{
    class PikoModule
    {
    public:
        virtual ~PikoModule();

        virtual void enter( App &app );
        virtual void leave( App &app );

        virtual void handle_event( App &app, const SDL_Event &event );
        virtual void update( App &app, double delta_seconds );
        virtual void render( App &app, rendering::Renderer &renderer );
    };
}
