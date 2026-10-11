#include "Modules/PikoModule.hpp"

namespace piko::modules
{
    PikoModule::~PikoModule() = default;

    void PikoModule::enter( App &app )
    {
        (void)app;
    }

    void PikoModule::leave( App &app )
    {
        (void)app;
    }

    void PikoModule::handle_event( App &app, const SDL_Event &event )
    {
        (void)app;
        (void)event;
    }

    void PikoModule::update( App &app, double delta_seconds )
    {
        (void)app;
        (void)delta_seconds;
    }

    void PikoModule::render( App &app, rendering::Renderer &renderer )
    {
        (void)app;
        (void)renderer;
    }
}
