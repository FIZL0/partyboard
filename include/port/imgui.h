#ifndef PORT_IMGUI_H_
#define PORT_IMGUI_H_

#include <aurora/aurora.h>

#ifdef __cplusplus
extern "C"
{
#endif

void imgui_main(const AuroraInfo* info);
void frame_limiter();
/* N120: with on (--turbo), a frame is shown at most every 1/30 s of real time, the others are recorded but not drawn
 * (aurora_set_present_skip); off shows every frame, as does a PB_ICON_SHOT run (its captures are of every frame).
 * Call before the frame is begun. */
void frame_present_throttle(bool on);
const char* imgui_get_image_path_from_popup();

#ifdef __cplusplus
}
#endif

#endif
