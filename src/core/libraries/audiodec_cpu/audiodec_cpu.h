// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

#include <cstddef>

namespace Core::Loader {
class SymbolsResolver;
}

namespace Libraries::AudiodecCpu {

constexpr s32 ORBIS_AUDIODECCPU_CODEC_M4AAC2 = 0x1001;
constexpr s32 ORBIS_AUDIODECCPU_CODEC_DTS = 0x1002;
constexpr s32 ORBIS_AUDIODECCPU_CODEC_LPCM_BD2 = 0x1003;
constexpr s32 ORBIS_AUDIODECCPU_CODEC_LPCM_DVD2 = 0x1004;
constexpr s32 ORBIS_AUDIODECCPU_CODEC_DTS_HD_MA = 0x1005;
constexpr s32 ORBIS_AUDIODECCPU_CODEC_DTS_HD_LBR = 0x1006;
constexpr s32 ORBIS_AUDIODECCPU_CODEC_DDP = 0x1007;
constexpr s32 ORBIS_AUDIODECCPU_CODEC_HEVAG = 0x1008;
constexpr s32 ORBIS_AUDIODECCPU_CODEC_ALAC2 = 0x1009;
constexpr s32 ORBIS_AUDIODECCPU_CODEC_FLAC2 = 0x100A;

constexpr u32 ORBIS_AUDIODECCPU_DESCRIPTOR_SIZE = 0x18;
constexpr u32 ORBIS_AUDIODECCPU_HEVAG_CONTEXT_SIZE = 0x1B0;

struct OrbisAudiodecCpuMemoryDescriptor {
    u32 this_size;
    u32 reserved;
    void* ptr;
    u32 size;
    u32 reserved2;
};
static_assert(sizeof(OrbisAudiodecCpuMemoryDescriptor) == ORBIS_AUDIODECCPU_DESCRIPTOR_SIZE);
static_assert(offsetof(OrbisAudiodecCpuMemoryDescriptor, ptr) == 0x08);
static_assert(offsetof(OrbisAudiodecCpuMemoryDescriptor, size) == 0x10);

struct OrbisAudiodecCpuQueryParam {
    void* config;
};

struct OrbisAudiodecCpuInitParam {
    void* config;
    void* work;
};

struct OrbisAudiodecCpuDecodeParam {
    void* control;
    void* decode_param;
    OrbisAudiodecCpuMemoryDescriptor* input;
    OrbisAudiodecCpuMemoryDescriptor* output;
};
static_assert(offsetof(OrbisAudiodecCpuDecodeParam, input) == 0x10);
static_assert(offsetof(OrbisAudiodecCpuDecodeParam, output) == 0x18);

int PS4_SYSV_ABI sceAudiodecCpuQueryMemSize(const OrbisAudiodecCpuQueryParam* query,
                                            OrbisAudiodecCpuMemoryDescriptor* memory, s32 codec_id);
int PS4_SYSV_ABI sceAudiodecCpuInitDecoder(const OrbisAudiodecCpuInitParam* init,
                                           OrbisAudiodecCpuMemoryDescriptor* context, s32 codec_id);
int PS4_SYSV_ABI sceAudiodecCpuDecode(OrbisAudiodecCpuDecodeParam* decode,
                                      OrbisAudiodecCpuMemoryDescriptor* context, s32 codec_id);
int PS4_SYSV_ABI sceAudiodecCpuClearContext(OrbisAudiodecCpuMemoryDescriptor* context,
                                            s32 codec_id);

int PS4_SYSV_ABI sceAudiodecCpuInternalQueryMemSize(const OrbisAudiodecCpuQueryParam* query,
                                                    OrbisAudiodecCpuMemoryDescriptor* memory,
                                                    s32 codec_id);
int PS4_SYSV_ABI sceAudiodecCpuInternalInitDecoder(const OrbisAudiodecCpuInitParam* init,
                                                   OrbisAudiodecCpuMemoryDescriptor* context,
                                                   s32 codec_id);
int PS4_SYSV_ABI sceAudiodecCpuInternalDecode(OrbisAudiodecCpuDecodeParam* decode,
                                              OrbisAudiodecCpuMemoryDescriptor* context,
                                              s32 codec_id);
int PS4_SYSV_ABI sceAudiodecCpuInternalClearContext(OrbisAudiodecCpuMemoryDescriptor* context,
                                                    s32 codec_id);

void RegisterLib(Core::Loader::SymbolsResolver* sym);

} // namespace Libraries::AudiodecCpu
