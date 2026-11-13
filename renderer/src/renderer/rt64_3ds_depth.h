#ifndef RT64_3DS_DEPTH_H
#define RT64_3DS_DEPTH_H
#include "rt64_3ds_record.h"
namespace rt64_3ds {
// Rectangles have no interpolated Z. The RDP can nevertheless depth-test
// them against world geometry when G_ZS_PRIM supplies their screen depth.
inline bool depth_source_available(const DrawRecord& d) {
    const unsigned cycle = (d.othermode_h >> 20) & 3;
    return cycle < 2 && (d.kind == DrawRecord::Tris || (d.othermode_l & 4));
}
inline bool depth_compare_enabled(const DrawRecord& d) { return depth_source_available(d) && (d.othermode_l & 0x10); }
inline bool depth_write_enabled(const DrawRecord& d) { return depth_source_available(d) && (d.othermode_l & 0x20); }
}
#endif
