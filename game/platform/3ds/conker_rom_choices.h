// Copyright (c) 2026 Raihwy3. MIT.
#pragma once
#include <cstdint>
namespace conker_rom_choices {
struct Choice {const char* label;const char* filename;uint64_t hash;};
inline constexpr Choice choices[]={
    {"Original","CBFD.z64",0x23FBBA2DBCF2FD8EULL},
    {"Uncensored","CBFDUncensored.z64",0xAC445026C8F77A94ULL},
    {"French","CBFDfr.z64",0xF78F2024E0B8F2A3ULL},
    {"Spanish","CBFDes.z64",0x2B1C2905F62F5AE6ULL},
    {"Brazilian Portuguese","CBFDptbr.z64",0x140A66314ED14EE4ULL}
};
inline constexpr int count=sizeof(choices)/sizeof(choices[0]);
inline int wrap(int selected,int delta){return (selected+delta+3)%3;}
inline int touch_row(int y){return y>=64 && y<160?(y-64)/32:-1;}
inline int rom_index(bool translations,int row){return translations?row+2:row;}
}
