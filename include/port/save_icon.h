#ifndef PORT_SAVE_ICON_H
#define PORT_SAVE_ICON_H

/*
 * N129: the saving icon (src/port/ui/save_icon.cpp). A small save glyph in a top corner of the RmlUi overlay, inside
 * the safe area, standing in for the party board's saving screen: shown from begin to end, and for at least
 * kMinShown wall-clock time after begin so a quick save is still seen. The game never waits on it (its frames and
 * state are those of a save without the icon).
 */

#ifdef __cplusplus
extern "C" {
#endif

void port_save_icon_begin(void);
void port_save_icon_end(void);

#ifdef __cplusplus
}
#endif

#endif
