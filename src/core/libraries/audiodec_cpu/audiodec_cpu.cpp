// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/libraries/audiodec_cpu/audiodec_cpu.h"
#include "core/libraries/audiodec_cpu/audiodec_cpu_error.h"
#include "core/libraries/libs.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <iterator>
#include <string>
#include <string_view>

namespace Libraries::AudiodecCpu {

namespace {

constexpr s32 CodecIdBase = 0x1000;
constexpr s32 FirstCodecId = ORBIS_AUDIODECCPU_CODEC_M4AAC2;
constexpr s32 LastCodecId = ORBIS_AUDIODECCPU_CODEC_FLAC2;
constexpr u32 CoreOpsTableSize = 0xB;

using QueryMemSizeFunc = int (*)(const OrbisAudiodecCpuQueryParam*, OrbisAudiodecCpuMemoryDescriptor*);
using InitDecoderFunc = int (*)(const OrbisAudiodecCpuInitParam*, OrbisAudiodecCpuMemoryDescriptor*);
using DecodeFunc = int (*)(OrbisAudiodecCpuDecodeParam*, OrbisAudiodecCpuMemoryDescriptor*);
using ClearContextFunc = int (*)(OrbisAudiodecCpuMemoryDescriptor*);

struct AudiodecCpuCoreOps {
    QueryMemSizeFunc QueryMemSize;
    InitDecoderFunc InitDecoder;
    DecodeFunc Decode;
    ClearContextFunc ClearContext;
};
static_assert(sizeof(AudiodecCpuCoreOps) == 0x20);
static_assert(offsetof(AudiodecCpuCoreOps, QueryMemSize) == 0x00);
static_assert(offsetof(AudiodecCpuCoreOps, InitDecoder) == 0x08);
static_assert(offsetof(AudiodecCpuCoreOps, Decode) == 0x10);
static_assert(offsetof(AudiodecCpuCoreOps, ClearContext) == 0x18);

int UnsupportedQueryMemSize(const OrbisAudiodecCpuQueryParam*, OrbisAudiodecCpuMemoryDescriptor*) {
    return ORBIS_AUDIODECCPU_ERROR_UNSUPPORTED_CODEC;
}

int UnsupportedInitDecoder(const OrbisAudiodecCpuInitParam*, OrbisAudiodecCpuMemoryDescriptor*) {
    return ORBIS_AUDIODECCPU_ERROR_UNSUPPORTED_CODEC;
}

int UnsupportedDecode(OrbisAudiodecCpuDecodeParam*, OrbisAudiodecCpuMemoryDescriptor*) {
    return ORBIS_AUDIODECCPU_ERROR_UNSUPPORTED_CODEC;
}

int UnsupportedClearContext(OrbisAudiodecCpuMemoryDescriptor*) {
    return ORBIS_AUDIODECCPU_ERROR_UNSUPPORTED_CODEC;
}

constexpr AudiodecCpuCoreOps UnsupportedCodecOps = {
    .QueryMemSize = UnsupportedQueryMemSize,
    .InitDecoder = UnsupportedInitDecoder,
    .Decode = UnsupportedDecode,
    .ClearContext = UnsupportedClearContext,
};

constexpr AudiodecCpuCoreOps audiodeccpuinternal_core_ops_m4aac2 = UnsupportedCodecOps;
constexpr AudiodecCpuCoreOps audiodeccpuinternal_core_ops_dts = UnsupportedCodecOps;
constexpr AudiodecCpuCoreOps audiodeccpuinternal_core_ops_lpcm_bd2 = UnsupportedCodecOps;
constexpr AudiodecCpuCoreOps audiodeccpuinternal_core_ops_lpcm_dvd2 = UnsupportedCodecOps;
constexpr AudiodecCpuCoreOps audiodeccpuinternal_core_ops_dts_hd_ma = UnsupportedCodecOps;
constexpr AudiodecCpuCoreOps audiodeccpuinternal_core_ops_dts_hd_lbr = UnsupportedCodecOps;
constexpr AudiodecCpuCoreOps audiodeccpuinternal_core_ops_ddp = UnsupportedCodecOps;
constexpr AudiodecCpuCoreOps audiodeccpuinternal_core_ops_hevag2 = UnsupportedCodecOps;
constexpr AudiodecCpuCoreOps audiodeccpuinternal_core_ops_alac2 = UnsupportedCodecOps;
constexpr AudiodecCpuCoreOps audiodeccpuinternal_core_ops_flac2 = UnsupportedCodecOps;

constexpr std::array<const AudiodecCpuCoreOps*, CoreOpsTableSize>
audiodeccpuinternal_core_ops_by_codec_id_minus_0x1000 = {
    nullptr,
    &audiodeccpuinternal_core_ops_m4aac2,
    &audiodeccpuinternal_core_ops_dts,
    &audiodeccpuinternal_core_ops_lpcm_bd2,
    &audiodeccpuinternal_core_ops_lpcm_dvd2,
    &audiodeccpuinternal_core_ops_dts_hd_ma,
    &audiodeccpuinternal_core_ops_dts_hd_lbr,
    &audiodeccpuinternal_core_ops_ddp,
    &audiodeccpuinternal_core_ops_hevag2,
    &audiodeccpuinternal_core_ops_alac2,
    &audiodeccpuinternal_core_ops_flac2,
};

static_assert(audiodeccpuinternal_core_ops_by_codec_id_minus_0x1000.size() == 0xB);
static_assert(ORBIS_AUDIODECCPU_CODEC_HEVAG - CodecIdBase == 0x8);

const AudiodecCpuCoreOps* GetCoreOps(const s32 codec_id) {
    if (codec_id < FirstCodecId || codec_id > LastCodecId) {
        return nullptr;
    }
    return audiodeccpuinternal_core_ops_by_codec_id_minus_0x1000[codec_id - CodecIdBase];
}

} // namespace

static int DispatchQueryMemSize(const OrbisAudiodecCpuQueryParam* query,
                                OrbisAudiodecCpuMemoryDescriptor* memory,
                                const s32 codec_id) {
    if (!query) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_CONFIG_DESCRIPTOR;
    }
    if (!query->config) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_CONFIG_POINTER;
    }
    if (!memory) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_CONTEXT_DESCRIPTOR;
    }
    if (memory->this_size != ORBIS_AUDIODECCPU_DESCRIPTOR_SIZE) {
        return ORBIS_AUDIODECCPU_ERROR_INVALID_DESCRIPTOR_SIZE;
    }
    const AudiodecCpuCoreOps* ops = GetCoreOps(codec_id);
    if (!ops || !ops->QueryMemSize) {
        return ORBIS_AUDIODECCPU_ERROR_UNSUPPORTED_CODEC;
    }
    return ops->QueryMemSize(query, memory);
}

static int DispatchInitDecoder(const OrbisAudiodecCpuInitParam* init,
                               OrbisAudiodecCpuMemoryDescriptor* context,
                               const s32 codec_id) {
    if (!init) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_CONFIG_DESCRIPTOR;
    }
    if (!init->config) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_CONFIG_POINTER;
    }
    if (!init->work) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_INIT_WORK_POINTER;
    }
    if (!context) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_CONTEXT_DESCRIPTOR;
    }
    if (!context->ptr) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_CONTEXT_POINTER;
    }
    if (context->this_size != ORBIS_AUDIODECCPU_DESCRIPTOR_SIZE) {
        return ORBIS_AUDIODECCPU_ERROR_INVALID_DESCRIPTOR_SIZE;
    }
    const AudiodecCpuCoreOps* ops = GetCoreOps(codec_id);
    if (!ops || !ops->InitDecoder) {
        return ORBIS_AUDIODECCPU_ERROR_UNSUPPORTED_CODEC;
    }
    return ops->InitDecoder(init, context);
}

static int DispatchDecode(OrbisAudiodecCpuDecodeParam* decode,
                          OrbisAudiodecCpuMemoryDescriptor* context,
                          const s32 codec_id) {
    if (!decode) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_CONFIG_DESCRIPTOR;
    }
    if (!decode->control) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_CONFIG_POINTER;
    }
    if (!decode->decode_param) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_INIT_WORK_POINTER;
    }
    if (!decode->input) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_INPUT_DESCRIPTOR;
    }
    if (!decode->input->ptr) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_INPUT_POINTER;
    }
    if (!decode->output) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_OUTPUT_DESCRIPTOR;
    }
    if (!decode->output->ptr) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_OUTPUT_POINTER;
    }
    if (decode->input->size == 0) {
        return ORBIS_AUDIODECCPU_ERROR_ZERO_INPUT_SIZE;
    }
    if (decode->output->size == 0) {
        return ORBIS_AUDIODECCPU_ERROR_ZERO_OUTPUT_SIZE;
    }
    if (decode->input->this_size != ORBIS_AUDIODECCPU_DESCRIPTOR_SIZE) {
        return ORBIS_AUDIODECCPU_ERROR_INVALID_INPUT_DESCRIPTOR_SIZE;
    }
    if (decode->output->this_size != ORBIS_AUDIODECCPU_DESCRIPTOR_SIZE) {
        return ORBIS_AUDIODECCPU_ERROR_INVALID_OUTPUT_DESCRIPTOR_SIZE;
    }
    if (!context) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_CONTEXT_DESCRIPTOR;
    }
    if (!context->ptr) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_CONTEXT_POINTER;
    }
    if (context->this_size != ORBIS_AUDIODECCPU_DESCRIPTOR_SIZE) {
        return ORBIS_AUDIODECCPU_ERROR_INVALID_DESCRIPTOR_SIZE;
    }
    const AudiodecCpuCoreOps* ops = GetCoreOps(codec_id);
    if (!ops || !ops->Decode) {
        return ORBIS_AUDIODECCPU_ERROR_UNSUPPORTED_CODEC;
    }
    return ops->Decode(decode, context);
}

static int DispatchClearContext(OrbisAudiodecCpuMemoryDescriptor* context, s32 codec_id) {
    if (!context) {
        return ORBIS_AUDIODECCPU_ERROR_INVALID_CONTEXT_DESCRIPTOR;
    }
    if (!context->ptr) {
        return ORBIS_AUDIODECCPU_ERROR_NULL_CONTEXT_POINTER;
    }
    if (context->this_size != ORBIS_AUDIODECCPU_DESCRIPTOR_SIZE) {
        return ORBIS_AUDIODECCPU_ERROR_INVALID_DESCRIPTOR_SIZE;
    }
    const AudiodecCpuCoreOps* ops = GetCoreOps(codec_id);
    if (!ops || !ops->ClearContext) {
        return ORBIS_AUDIODECCPU_ERROR_UNSUPPORTED_CODEC;
    }
    return ops->ClearContext(context);
}

int PS4_SYSV_ABI sceAudiodecCpuQueryMemSize(const OrbisAudiodecCpuQueryParam* query,
                                            OrbisAudiodecCpuMemoryDescriptor* memory,
                                            s32 codec_id) {
    return DispatchQueryMemSize(query, memory, codec_id);
}

int PS4_SYSV_ABI sceAudiodecCpuInitDecoder(const OrbisAudiodecCpuInitParam* init,
                                           OrbisAudiodecCpuMemoryDescriptor* context,
                                           s32 codec_id) {
    const int result = DispatchInitDecoder(init, context, codec_id);
    LOG_INFO(Lib_AudiodecCpu, "sceAudiodecCpuInitDecoder codec_id={:#x} result={:#x} ctx=({}, {})",
             codec_id, result, fmt::ptr(context ? context->ptr : nullptr),
             context ? context->size : 0);
    return result;
}

int PS4_SYSV_ABI sceAudiodecCpuDecode(OrbisAudiodecCpuDecodeParam* decode,
                                      OrbisAudiodecCpuMemoryDescriptor* context,
                                      const s32 codec_id) {
    return DispatchDecode(decode, context, codec_id);
}

int PS4_SYSV_ABI sceAudiodecCpuClearContext(OrbisAudiodecCpuMemoryDescriptor* context,
                                            const s32 codec_id) {
    return DispatchClearContext(context, codec_id);
}

int PS4_SYSV_ABI sceAudiodecCpuInternalQueryMemSize(const OrbisAudiodecCpuQueryParam* query,
                                                    OrbisAudiodecCpuMemoryDescriptor* memory,
                                                    const s32 codec_id) {
    return DispatchQueryMemSize(query, memory, codec_id);
}

int PS4_SYSV_ABI sceAudiodecCpuInternalInitDecoder(const OrbisAudiodecCpuInitParam* init,
                                                   OrbisAudiodecCpuMemoryDescriptor* context,
                                                   s32 codec_id) {
    const int result = DispatchInitDecoder(init, context, codec_id);
    LOG_INFO(Lib_AudiodecCpu, "sceAudiodecCpuInternalInitDecoder codec_id={:#x} result={:#x} ctx=({}, {})",
            codec_id, result, fmt::ptr(context ? context->ptr : nullptr), context ? context->size : 0);
    return result;
}

int PS4_SYSV_ABI sceAudiodecCpuInternalDecode(OrbisAudiodecCpuDecodeParam* decode,
                                              OrbisAudiodecCpuMemoryDescriptor* context,
                                              const s32 codec_id) {
    return DispatchDecode(decode, context, codec_id);
}

int PS4_SYSV_ABI sceAudiodecCpuInternalClearContext(OrbisAudiodecCpuMemoryDescriptor* context,
                                                    const s32 codec_id) {
    return DispatchClearContext(context, codec_id);
}

void RegisterLib(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("ktD2w3D4G2U", "libSceAudiodecCpu", 1, "libSceAudiodecCpu", sceAudiodecCpuQueryMemSize);
    LIB_FUNCTION("hdFsxo3MFu8", "libSceAudiodecCpu", 1, "libSceAudiodecCpu", sceAudiodecCpuInitDecoder);
    LIB_FUNCTION("lSVTiWV5wLc", "libSceAudiodecCpu", 1, "libSceAudiodecCpu", sceAudiodecCpuDecode);
    LIB_FUNCTION("hAS5WH6hxrE", "libSceAudiodecCpu", 1, "libSceAudiodecCpu", sceAudiodecCpuClearContext);

    LIB_FUNCTION("R8v5kdZ55mY", "libSceAudiodecCpu", 1, "libSceAudiodecCpu", sceAudiodecCpuInternalQueryMemSize);
    LIB_FUNCTION("KkhdeVCyo6Y", "libSceAudiodecCpu", 1, "libSceAudiodecCpu", sceAudiodecCpuInternalInitDecoder);
    LIB_FUNCTION("-0jDlM2hG5k", "libSceAudiodecCpu", 1, "libSceAudiodecCpu", sceAudiodecCpuInternalDecode);
    LIB_FUNCTION("CnY1NGmdi7I", "libSceAudiodecCpu", 1, "libSceAudiodecCpu", sceAudiodecCpuInternalClearContext);
}

} // namespace Libraries::AudiodecCpu
