// Controls shared by the preset menu and host checks. No changes on load.
#pragma once
#include <cstring>
namespace conker_controls {
struct Setting{const char* key;int classic,dual,touch,old3ds;};
constexpr Setting settings[]={
    {"free_camera",0,1,1,1},{"aim_swap_sticks",0,1,1,1},
    {"touch_stick",1,0,1,1},{"cstick_up",1,1,1,1},{"cstick_down",1,1,1,1},
    {"camera_invert_turning",0,0,0,0},{"look_stick_invert",2,2,2,2},
    {"face_camera",0,0,0,1}
};
inline int value(const Setting& s,int preset){return preset==1?s.classic:preset==2?s.dual:preset==3?s.touch:s.old3ds;}
// An empty source disables a face action; nullptr keeps the default binding.
inline const char* old3ds_button_source(const char* key){
    if(!std::strcmp(key,"button_a") || !std::strcmp(key,"button_b") || !std::strcmp(key,"button_x") || !std::strcmp(key,"button_y"))return "";
    if(!std::strcmp(key,"button_dright"))return "button_a";
    if(!std::strcmp(key,"button_ddown"))return "button_b";
    if(!std::strcmp(key,"button_dup"))return "button_x";
    if(!std::strcmp(key,"button_dleft"))return "button_y";
    return nullptr;
}
}
