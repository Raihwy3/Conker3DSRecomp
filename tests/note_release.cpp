#include "conker_note_release.h"
#include <vector>
#include <cassert>
#include <cstdio>
using conker_audio::NoteReleases;
int main() {
 std::vector<uint8_t> rd(16*1024*1024);
 auto put=[&](uint32_t a,uint32_t v){std::memcpy(rd.data()+a-0x80000000u,&v,4);};
 const uint32_t voice=0x8007df84,sound=0x801ff5d4,envelope=0x80200000;
 NoteReleases notes;bool cached;
 put(voice+0x24,sound);put(sound,envelope);put(envelope+8,120000);
 assert(notes.capture(rd.data(),rd.size(),voice,sound));
 put(sound,0xaf2f49ef); // The invalid envelope recovered from the Luma dump.
 assert(notes.release(rd.data(),rd.size(),voice,cached)==120000 && cached);
 assert(notes.release(rd.data(),rd.size(),voice,cached)==0 && !cached);
 put(sound,envelope);put(envelope+8,6543);
 assert(notes.capture(rd.data(),rd.size(),voice,sound));
 put(envelope+8,99999); // Captured timing must survive changes to the bank.
 assert(notes.release(rd.data(),rd.size(),voice,cached)==6543 && cached);
 put(envelope+8,1234);assert(notes.capture(rd.data(),rd.size(),voice,sound));
 put(sound,0xaf2f49ef);assert(!notes.capture(rd.data(),rd.size(),voice,sound));
 assert(notes.release(rd.data(),rd.size(),voice,cached)==0 && !cached);
 put(sound,envelope);put(envelope+8,3210);
 assert(notes.release(rd.data(),rd.size(),voice,cached)==3210 && !cached);
 for(uint32_t p:{0u,0xffffffffu,0x81000000u,0x80fffffcu,0x80200001u}) {
  put(sound,p);assert(!notes.capture(rd.data(),rd.size(),voice,sound));
 }
 assert(notes.release(rd.data(),rd.size(),0xffffffffu,cached)==0 && !cached);
 puts("PASS exact dump envelope, normal timing, reused voices, missing snapshots and invalid/boundary addresses");
}
