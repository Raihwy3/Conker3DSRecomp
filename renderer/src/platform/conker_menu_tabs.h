#pragma once
namespace conker_menu {
constexpr int count=5,cell_width=8,pixel_width=64;
constexpr const char* labels[count]={"Graphics","Controls","Audio","Interface","Misc"};
constexpr int wrap(int tab){return (tab%count+count)%count;}
constexpr int at_x(int x){return x<0?0:x>=320?count-1:x/pixel_width;}
}
