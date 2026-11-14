#include <cassert>
#include <cstring>
#include "../game/platform/3ds/conker_rom_choices.h"
int main(){using namespace conker_rom_choices;
    static_assert(count==7);
    static_assert(translation_count==5);
    assert(wrap(0,-1,3)==2 && wrap(2,1,3)==0);
    assert(touch_row(63)==-1 && touch_row(64)==0 && touch_row(95)==0);
    assert(touch_row(96)==1 && touch_row(128)==2 && touch_row(159)==2 && touch_row(160)==-1);
    assert(rom_index(false,0)==0 && rom_index(false,1)==1);
    assert(rom_index(true,0,0)==2); // French
    assert(rom_index(true,1,0)==3); // Spanish
    assert(rom_index(true,0,1)==4); // Portuguese
    assert(rom_index(true,1,1)==5); // Russian
    assert(rom_index(true,0,2)==6); // German
    assert(!strcmp(choices[2].filename,"CBFDfr.z64"));
    assert(!strcmp(choices[3].filename,"CBFDes.z64"));
    assert(!strcmp(choices[4].filename,"CBFDptbr.z64"));
    assert(!strcmp(choices[5].filename,"CBFDru.z64"));
    assert(!strcmp(choices[6].filename,"CBFDde.z64"));
}
