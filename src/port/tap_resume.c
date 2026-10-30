// "Tap screen to resume controller" (include/port/controller_watch.h). After a screenshot iOS gives event focus to the
// screenshot preview, and GameController reports every button released and the sticks centred until a touch gives it
// back; the app can't take it back itself, so the overlay (src/port/ui/overlay.cpp) asks for that tap. Draw only: the
// flag never reaches the sim or the inputs, so netplay is unaffected.

#include <port/controller_watch.h>

#include <stdatomic.h>
#include <stdbool.h>

static atomic_bool s_wanted;

void port_tap_resume_focus_lost(const char *why, int controllers)
{
    (void)why;
    if (controllers > 0) {
        atomic_store(&s_wanted, true);
    }
}

void port_tap_resume_clear(void)
{
    atomic_store(&s_wanted, false);
}

int port_tap_resume_wanted(void)
{
    return atomic_load(&s_wanted);
}
