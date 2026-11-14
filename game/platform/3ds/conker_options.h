#pragma once
#include <atomic>
#include <cstring>
namespace conker3ds {
struct Option { const char* key; std::atomic<int> value; };
inline Option options[]={
 {"camera_turn_speed",100},{"camera_invert_turning",0},{"camera_field_of_view_degrees",50},
 {"look_stick_response",0},{"look_stick_invert",2},{"look_gyro_response",0},{"look_gyro_invert",0},
 {"gyro_sensitivity",100},{"gyro_enabled",0},{"aim_reticle",5},{"aim_reticle_color",1},{"aim_reticle_opacity",40},{"aim_swap_sticks",0},{"aim_lock_on",0},
 {"cutscene_4x3",0},{"model_highest",1},{"misc_skip_intro",0}
};
inline int get(const char* key,int fallback=0){for(auto& o:options)if(!strcmp(o.key,key))return o.value.load();return fallback;}
inline void set(const char* key,int v){for(auto& o:options)if(!strcmp(o.key,key)){o.value.store(v);return;}}
}
