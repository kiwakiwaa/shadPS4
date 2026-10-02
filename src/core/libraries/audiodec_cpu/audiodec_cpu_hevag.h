// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include "core/libraries/audiodec_cpu/audiodec_cpu_backend.h"

namespace Libraries::AudiodecCpu::Hevag {

constexpr u32 ContextSize = 0x1B0;
constexpr u32 MaxChannels = 8;
constexpr u32 SamplesPerBlock = 28;
constexpr u32 BlockSize = 16;

enum class SampleFormat : u32 {
    S16 = 1,
    Float32 = 2,
};

// Stream header integers retain their big endian representation.
struct VagHeader {
    // Reference: https://rewiki.miraheze.org/wiki/PlayStation_VAG_Audio
    std::array<char, 4> magic{};
    u32 version_be{};
    u32 reserved_08{};
    u32 encoded_size_be{};
    u32 sample_rate_be{};
    std::array<u8, 10> reserved_14{};
    u8 channel_count{};
    u8 reserved_1f{};
    std::array<char, 16> stream_name{};
};
static_assert(sizeof(VagHeader) == 0x30);
static_assert(offsetof(VagHeader, channel_count) == 0x1E);
static_assert(offsetof(VagHeader, stream_name) == 0x20);

struct CodecParam {
    u32 this_size{0x40};
    VagHeader header{};
    SampleFormat sample_format{SampleFormat::S16};
    u8 planar_output{};
    std::array<u8, 7> reserved{};
};
static_assert(sizeof(CodecParam) == 0x40);
static_assert(offsetof(CodecParam, sample_format) == 0x34);
static_assert(offsetof(CodecParam, planar_output) == 0x38);

// Decode returns metadata in host byte order, including the stream's raw channel count.
struct BsiInfo {
    u32 this_size{0x30};
    std::array<char, 4> magic{};
    u32 version{};
    u32 encoded_size{};
    u32 sample_rate{};
    u8 channel_count{};
    std::array<u8, 3> reserved{};
    std::array<char, 16> stream_name{};
    std::array<u8, MaxChannels> channel_loop_flags{};
};
static_assert(sizeof(BsiInfo) == 0x30);
static_assert(offsetof(BsiInfo, channel_loop_flags) == 0x28);

int PS4_SYSV_ABI QueryMemSize(const OrbisAudiodecCpuQueryCtrl* ctrl,
                              OrbisAudiodecCpuMemoryDescriptor* resource);
int PS4_SYSV_ABI InitDecoder(const OrbisAudiodecCpuInitCtrl* ctrl,
                             OrbisAudiodecCpuMemoryDescriptor* resource);
int PS4_SYSV_ABI Decode(const OrbisAudiodecCpuDecodeCtrl* ctrl,
                        OrbisAudiodecCpuMemoryDescriptor* resource);
int PS4_SYSV_ABI ClearContext(OrbisAudiodecCpuMemoryDescriptor* resource);

void RegisterLib(Core::Loader::SymbolsResolver* sym);

inline constexpr CodecOps Ops{QueryMemSize, InitDecoder, Decode, ClearContext};

} // namespace Libraries::AudiodecCpu::Hevag
