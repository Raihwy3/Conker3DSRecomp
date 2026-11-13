#ifndef CONKER_AUDIO_HLE_H
#define CONKER_AUDIO_HLE_H

#include <cstdint>
#include "librecomp/rsp.hpp"
#include "audio_hle_diff.h"

namespace conker {

RspExitReason audio_hle_run(uint8_t* rdram, uint32_t ucode_addr);
bool task_has_overlay(const uint8_t* rdram, const OSTask& task);

void audio_hle_set_reference(RspUcodeFunc* reference);
void audio_hle_set_diff_verbose(int mismatches);

const recomp3ds::AudioHleStats& audio_hle_stats();
const uint8_t* audio_hle_dmem();
void audio_hle_reset_dmem();

} // namespace conker

#endif
