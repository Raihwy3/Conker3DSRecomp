#include "conker_code_pages.h"
#include <cassert>
int main(int argc,char** argv){
    assert(argc==3);std::vector<uint8_t> game(0x1FA130),debugger(0x4D58);
    assert(conker_rom::code_pages(argv[1],game.data(),game.size(),debugger.data(),debugger.size()));
    FILE* f=fopen(argv[2],"rb");assert(f);
    for(auto pair:{std::pair<uint32_t,std::vector<uint8_t>*>(0x2D4B0,&game),{0x255880,&debugger}}){
        std::vector<uint8_t> reference(pair.second->size());assert(conker_rom::read(f,pair.first,reference.data(),reference.size()));
        for(size_t i=0;i<reference.size();i++)assert(reference[i]==(*pair.second)[i^3]);
    }
    fclose(f);assert(!conker_rom::code_pages("/no-such-rom",game.data(),game.size(),debugger.data(),debugger.size()));
    puts("PASS: both ROM lookup sections match the independently unpacked original byte for byte.");
}
