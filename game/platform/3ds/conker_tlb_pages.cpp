// Copyright (c) 2026 Raihwy3. MIT.
#include "conker_code_pages.h"
#include "conker.hpp"
#include "recomp.h"
#include <string>
#include <stdexcept>
static std::string selected_rom;
void conker_set_rom_path(const char* path){selected_rom=path;}
void conker::map_tlb_code_pages(uint8_t* rdram){
    if(!conker_rom::code_pages(selected_rom.c_str(),rdram+RECOMP_RDRAM_OFFSET(0x15000000),0x1FA130,
                              rdram+RECOMP_RDRAM_OFFSET(0x16000000),0x4D58))
        throw std::runtime_error("Unable to read code lookup pages from the selected ROM");
}
// Native mod loading is disabled; the streamed ROM retains its real layout.
std::vector<uint8_t> conker::decompress_rom(std::span<const uint8_t> rom){return {rom.begin(),rom.end()};}
