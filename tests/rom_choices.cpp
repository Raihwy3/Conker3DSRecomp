#include <cassert>
#include <cstring>
#include "../game/platform/3ds/conker_rom_choices.h"
int main(){using namespace conker_rom_choices;
    static_assert(count==5);
    assert(wrap(0,-1)==2 && wrap(2,1)==0);
    assert(touch_row(63)==-1 && touch_row(64)==0 && touch_row(95)==0);
    assert(touch_row(96)==1 && touch_row(128)==2 && touch_row(159)==2 && touch_row(160)==-1);
    assert(rom_index(false,0)==0 && rom_index(false,1)==1);
    for(int i=0;i<3;++i)assert(rom_index(true,i)==i+2);
    assert(!strcmp(choices[2].filename,"CBFDfr.z64"));
    assert(!strcmp(choices[3].filename,"CBFDes.z64"));
    assert(!strcmp(choices[4].filename,"CBFDptbr.z64"));
}
