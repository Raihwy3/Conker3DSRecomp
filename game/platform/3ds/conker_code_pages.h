// Copyright (c) 2026 Raihwy3. MIT. Decode lookup pages from the selected ROM.
#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>
#include <algorithm>
#include "miniz/miniz.h"
namespace conker_rom {
inline uint32_t be32(const uint8_t* b){return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];}
inline bool read(FILE* f,uint32_t at,void* out,size_t n){return !fseek(f,at,SEEK_SET) && fread(out,1,n,f)==n;}
inline bool code_pages(const char* path,uint8_t* game,size_t game_size,uint8_t* debugger,size_t debugger_size){
    if(game_size!=0x1FA130 || debugger_size!=0x4D58)return false;
    FILE* f=fopen(path,"rb");if(!f)return false;
    bool ok=true,ended=false;std::vector<uint32_t> starts;
    for(uint32_t i=1;i<1024;i++){
        uint8_t word[4];if(!read(f,0x42450+i*4,word,4)){ok=false;break;}
        uint32_t value=be32(word);if(!value){ended=true;break;}
        value^=0x8039CCCA;
        if(value>0x144700 || (!starts.empty() && value<=starts.back())){ok=false;break;}
        starts.push_back(value);
    }
    // The final entry is an end offset and may equal the code block boundary.
    ok=ok && ended;size_t used=0;
    for(size_t i=1;ok && i<starts.size();i++){
        size_t length=starts[i]-starts[i-1];
        if(length<5 || length>65536){ok=false;break;}
        std::vector<uint8_t> block(length);
        if(!read(f,0x42450+starts[i-1],block.data(),length)){ok=false;break;}
        size_t n=be32(block.data());
        if(!n || n>game_size-used || tinfl_decompress_mem_to_mem(game+used,n,block.data()+4,length-4,0)!=n){ok=false;break;}
        used+=n;
    }
    ok=ok && used==game_size && read(f,0x19EA88,debugger,debugger_size);
    fclose(f);
    if(ok){
        // librecomp stores each N64 word in little-endian host order.
        for(size_t i=0;i<game_size;i+=4){std::swap(game[i],game[i+3]);std::swap(game[i+1],game[i+2]);}
        for(size_t i=0;i<debugger_size;i+=4){std::swap(debugger[i],debugger[i+3]);std::swap(debugger[i+1],debugger[i+2]);}
    }
    return ok;
}
}
