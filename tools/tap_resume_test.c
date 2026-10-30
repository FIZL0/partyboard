// The "tap screen to resume controller" flag (src/port/tap_resume.c), as the iOS observers and SDL event watch drive it.
#include <port/controller_watch.h>

#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(!port_tap_resume_wanted());
    port_tap_resume_focus_lost("screenshot", 0); // no controller: nothing to resume
    assert(!port_tap_resume_wanted());
    port_tap_resume_focus_lost("screenshot", 1);
    port_tap_resume_focus_lost("screenshot", 1); // iOS sends it twice: set, not toggled or counted
    assert(port_tap_resume_wanted());
    port_tap_resume_clear(); // one touch
    assert(!port_tap_resume_wanted());
    port_tap_resume_focus_lost("resign/active", 2);
    assert(port_tap_resume_wanted());
    port_tap_resume_clear(); // pad press
    assert(!port_tap_resume_wanted());
    puts("tap_resume ok");
    return 0;
}
