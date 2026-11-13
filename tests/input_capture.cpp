#include "input_capture_gate.h"
#include "face_camera_axes.h"
#include <cmath>
#include "conker_control_presets.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main(){
    recomp3ds::InputCaptureGate gate;assert(!gate.blocked());
    gate.set_open(true);assert(gate.blocked());gate.poll(true);assert(gate.blocked());
    // Closing while A, a stick or the stylus is still held must not leak.
    gate.set_open(false);assert(gate.blocked());
    for(int i=0;i<100;++i){gate.poll(false);assert(gate.blocked());}
    gate.poll(true);assert(!gate.blocked());
    // Reopening while waiting for release must take ownership immediately.
    gate.set_open(true);gate.set_open(false);gate.set_open(true);gate.poll(true);assert(gate.blocked());
    gate.set_open(false);gate.poll(true);assert(!gate.blocked());
    for(const auto& s:conker_controls::settings){
        int a=conker_controls::value(s,1),b=conker_controls::value(s,2),c=conker_controls::value(s,3);
        if(!strcmp(s.key,"free_camera") || !strcmp(s.key,"aim_swap_sticks"))assert(a==0&&b==1&&c==1);
        if(!strcmp(s.key,"touch_stick"))assert(a==1&&b==0&&c==1);
    }
    for(unsigned mask=0;mask<16;++mask){
        float x,y;recomp3ds::face_camera_axes(mask&1,mask&2,mask&4,mask&8,&x,&y);
        assert(x*x+y*y<=1.00001f);
        int dx=int(bool(mask&2))-int(bool(mask&1)),dy=int(bool(mask&4))-int(bool(mask&8));
        assert((x>0)==(dx>0) && (x<0)==(dx<0) && (y>0)==(dy>0) && (y<0)==(dy<0));
    }
    const char* destinations[]={"button_dright","button_ddown","button_dup","button_dleft"};
    const char* sources[]={"button_a","button_b","button_x","button_y"};
    for(int i=0;i<4;++i)assert(!strcmp(conker_controls::old3ds_button_source(destinations[i]),sources[i]));
    for(const char* key:sources)assert(!*conker_controls::old3ds_button_source(key));
    assert(conker_controls::old3ds_button_source("button_l")==nullptr);
    for(const auto& setting:conker_controls::settings)if(!strcmp(setting.key,"face_camera")){
        assert(conker_controls::value(setting,4)==1);
        for(int preset=1;preset<=3;++preset)assert(conker_controls::value(setting,preset)==0);
    }
    puts("PASS: menu capture, presets, all face-camera directions and Old 3DS action remapping");
}
