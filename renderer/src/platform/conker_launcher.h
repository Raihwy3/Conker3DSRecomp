// Copyright (c) 2026 Raihwy3. MIT.
#pragma once
#define CONKER_LAUNCHER_VERSION "0.2.0"
#ifdef __cplusplus
extern "C" {
#endif
// Startup runs before render/audio workers. All strings are borrowed for one draw.
struct ConkerLauncherView {
    const char *stage, *message, *detail, *actions;
    const char *labels[3], *notes[3];
    int available[3], selected, menu;
    unsigned done, total;
};
// Returns zero when ROM-derived artwork is unavailable; keep the text fallback.
int conker_launcher_draw(const struct ConkerLauncherView *view);
void conker_launcher_finish(void);
#ifdef __cplusplus
}
#endif
