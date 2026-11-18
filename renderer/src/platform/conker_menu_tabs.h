#pragma once
namespace conker_menu {
constexpr int count=5,cell_width=8,pixel_width=64;
constexpr const char* labels[count]={"Graphics","Controls","Audio","Achievements","Misc"};
constexpr int tab_x0[count]={0,62,124,172,272};
constexpr int tab_w[count]={62,62,48,100,48};
constexpr int wrap(int tab){return (tab%count+count)%count;}
constexpr int at_x(int x){
    for(int i=0;i<count;++i)if(x<tab_x0[i]+tab_w[i])return i;
    return count-1;
}
}
