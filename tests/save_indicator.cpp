#include "save_indicator.h"
#include <cassert>
#include <cstdio>
int main(){
 recomp3ds::SaveIndicator s;assert(!s.visible(0));
 s.signal(true);s.signal(false);assert(s.visible(100));assert(s.visible(1499));assert(!s.visible(1500));
 s.signal(true);assert(s.visible(2000));assert(s.visible(9000));s.signal(false);assert(!s.visible(9001));
 s.signal(true);s.signal(false);assert(s.visible(10000));
 s.signal(true);s.signal(false);assert(s.visible(11000));assert(s.visible(12399));assert(!s.visible(12400));
 puts("PASS short saves, long writes, minimum display time, repeated saves and hidden idle icon");
}
