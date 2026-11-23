#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace conker_audio {
// A note can outlive the bank storage from which it was started. Retain the
// envelope value, not a pointer back into that storage, until note-off.
class NoteReleases {
    struct Entry { uint32_t voice=0; int32_t time=0; bool valid=false; };
    std::array<Entry,256> notes{};
    size_t next=0;
    static bool word(const uint8_t* rd, size_t size, uint64_t address, uint32_t& value) {
        if (address<0x80000000ull || (address&3) || address-0x80000000ull+4>size) return false;
        std::memcpy(&value,rd+size_t(address-0x80000000ull),4);
        return true;
    }
    static bool read_time(const uint8_t* rd,size_t size,uint32_t sound,int32_t& time) {
        uint32_t envelope,value;
        if (!word(rd,size,sound,envelope) || !word(rd,size,uint64_t(envelope)+8,value)) return false;
        std::memcpy(&time,&value,4);
        return true;
    }
public:
    bool capture(const uint8_t* rd,size_t size,uint32_t voice,uint32_t sound) {
        Entry* slot=nullptr;
        for(auto& note:notes)if(note.voice==voice){slot=&note;break;}
        if(!slot){slot=&notes[next];next=(next+1)%notes.size();}
        *slot={voice,0,false}; // Reusing a voice must invalidate its old value.
        slot->valid=read_time(rd,size,sound,slot->time);
        return slot->valid;
    }
    int32_t release(const uint8_t* rd,size_t size,uint32_t voice,bool& captured) {
        for(auto& note:notes)if(note.voice==voice && note.valid){
            captured=true;note.valid=false;return note.time;
        }
        captured=false;
        uint32_t sound;int32_t time=0;
        // Missing/invalid notes release immediately instead of following an
        // invalid bank pointer. Normal notes use their exact captured time.
        if(word(rd,size,uint64_t(voice)+0x24,sound))read_time(rd,size,sound,time);
        return time;
    }
};
}
