#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <SDL3/SDL_dialog.h>

struct SDL_Window;

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*IOSFileCallback)(void* userdata, const char* path, const char* error);

void PartyBoard_iOS_ShowFileSelect(IOSFileCallback callback, void* userdata, SDL_Window* window,
                             const SDL_DialogFileFilter* filters, int nfilters,
                             const char* default_location, bool allow_many);

// Reopens a disc path saved by an earlier run: resolves the picker's bookmark (files outside the app's sandbox need
// its security scope, held until exit) or moves a path inside the app container onto this install's container.
// Writes the path to open to out (NUL-terminated) and returns true, or returns false if nothing readable was found.
bool PartyBoard_iOS_RestoreFileAccess(const char* path, char* out, size_t out_size);

#ifdef __cplusplus
}
#endif
