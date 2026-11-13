#ifndef CONKER_3DS_RECOMPINPUT_SHIM_H
#define CONKER_3DS_RECOMPINPUT_SHIM_H

namespace recompinput {
    inline void get_mouse_deltas(float* x, float* y) {
        if (x) *x = 0.0f;
        if (y) *y = 0.0f;
    }
}

#endif
