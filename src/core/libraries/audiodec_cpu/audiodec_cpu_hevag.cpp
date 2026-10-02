// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

#include <immintrin.h>

#include "core/libraries/audiodec_cpu/audiodec_cpu_error.h"
#include "core/libraries/audiodec_cpu/audiodec_cpu_hevag.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/libs.h"

namespace Libraries::AudiodecCpu::Hevag {
namespace {

struct DecoderState {
    std::array<char, 4> magic{};
    u32 version_be{};
    u32 encoded_size_be{};
    u32 sample_rate_be{};
    u8 channel_count{};
    std::array<u8, 3> reserved_11{};
    std::array<char, 16> stream_name{};
    SampleFormat sample_format{};
    u8 planar_output{};
    std::array<u8, 3> reserved_29{};
    u32 header_consumed{};
    std::array<std::array<float, 4>, MaxChannels> history{};
};
static_assert(sizeof(DecoderState) == 0xB0);
static_assert(offsetof(DecoderState, history) == 0x30);
static_assert(sizeof(DecoderState) + 7 <= ContextSize);

u8* StateAddress(const OrbisAudiodecCpuMemoryDescriptor& resource) {
    // LLE aligns the state within the work buffer, the buffer itself need not be aligned.
    const auto address = (reinterpret_cast<uintptr_t>(resource.data) + 7) & ~uintptr_t{7};
    return reinterpret_cast<u8*>(address);
}

DecoderState LoadState(const OrbisAudiodecCpuMemoryDescriptor& resource) {
    DecoderState state;
    std::memcpy(&state, StateAddress(resource), sizeof(state));
    return state;
}

void StoreState(const OrbisAudiodecCpuMemoryDescriptor& resource, const DecoderState& state) {
    std::memcpy(StateAddress(resource), &state, sizeof(state));
}

constexpr std::array<std::array<float, 4>, 128> PredictorCoefficients{{
    {{-0.0f, -0.0f, -0.0f, -0.0f}},
    {{0.9375f, -0.0f, -0.0f, -0.0f}},
    {{1.796875f, -0.8125f, -0.0f, -0.0f}},
    {{1.53125f, -0.859375f, -0.0f, -0.0f}},
    {{1.90625f, -0.9375f, -0.0f, -0.0f}},
    {{1.7982177734375f, -0.8616943359375f, -0.0f, -0.0f}},
    {{1.7708740234375f, -0.899169921875f, -0.0f, -0.0f}},
    {{1.69921875f, -0.918212890625f, -0.0f, -0.0f}},
    {{1.6031494140625f, -0.9375f, -0.0f, -0.0f}},
    {{1.46826171875f, -0.9375f, -0.0f, -0.0f}},
    {{1.31396484375f, -0.9375f, -0.0f, -0.0f}},
    {{1.1424560546875f, -0.9375f, -0.0f, -0.0f}},
    {{0.9560546875f, -0.9375f, -0.0f, -0.0f}},
    {{0.7569580078125f, -0.9375f, -0.0f, -0.0f}},
    {{0.5478515625f, -0.9375f, -0.0f, -0.0f}},
    {{0.3316650390625f, -0.9375f, -0.0f, -0.0f}},
    {{0.111083984375f, -0.9375f, -0.0f, -0.0f}},
    {{-0.111083984375f, -0.9375f, -0.0f, -0.0f}},
    {{-0.3316650390625f, -0.9375f, -0.0f, -0.0f}},
    {{-0.5478515625f, -0.9375f, -0.0f, -0.0f}},
    {{-0.7569580078125f, -0.9375f, -0.0f, -0.0f}},
    {{-0.9560546875f, -0.9375f, -0.0f, -0.0f}},
    {{-1.1424560546875f, -0.9375f, -0.0f, -0.0f}},
    {{-1.31396484375f, -0.9375f, -0.0f, -0.0f}},
    {{-1.46826171875f, -0.9375f, -0.0f, -0.0f}},
    {{-1.6031494140625f, -0.9375f, -0.0f, -0.0f}},
    {{-1.69921875f, -0.918212890625f, -0.0f, -0.0f}},
    {{-1.7708740234375f, -0.899169921875f, -0.0f, -0.0f}},
    {{-1.7982177734375f, -0.8616943359375f, -0.0f, -0.0f}},
    {{0.65625f, -1.125f, 0.40625f, -0.375f}},
    {{-0.78125f, -0.875f, -0.40625f, -0.28125f}},
    {{-1.28125f, -0.90625f, -0.4375f, -0.125f}},
    {{-0.0203857421875f, -0.332275390625f, -0.060302734375f, -0.0660400390625f}},
    {{-0.906982421875f, -0.2711181640625f, -0.280517578125f, 0.0517578125f}},
    {{-0.9766845703125f, -0.386474609375f, -0.343505859375f, 0.0352783203125f}},
    {{0.734619140625f, -0.579833984375f, 0.3233642578125f, -0.158447265625f}},
    {{0.463623046875f, -0.847900390625f, 0.4730224609375f, -0.1484375f}},
    {{-1.0054931640625f, -0.31689453125f, -0.2528076171875f, 0.0277099609375f}},
    {{1.1229248046875f, 0.241943359375f, -0.168701171875f, -0.28271484375f}},
    {{1.5894775390625f, -0.37158203125f, -0.462890625f, 0.1546630859375f}},
    {{1.6005859375f, -0.5477294921875f, -0.274658203125f, 0.2032470703125f}},
    {{-0.20361328125f, -0.45703125f, -0.7880859375f, 0.1025390625f}},
    {{0.9544677734375f, -0.5283203125f, 0.2576904296875f, -0.061767578125f}},
    {{1.1683349609375f, -0.1630859375f, -0.0924072265625f, 0.0594482421875f}},
    {{1.224609375f, -0.312744140625f, 0.03662109375f, 0.0242919921875f}},
    {{-0.5792236328125f, -0.503173828125f, -0.669677734375f, -0.1822509765625f}},
    {{-0.7197265625f, 0.290283203125f, -0.5843505859375f, -0.8480224609375f}},
    {{-0.1456298828125f, -1.1129150390625f, -0.1510009765625f, -0.380126953125f}},
    {{0.3397216796875f, -0.86767578125f, -0.1922607421875f, -0.1766357421875f}},
    {{-0.895263671875f, -0.251708984375f, -0.27001953125f, 0.054443359375f}},
    {{0.7479248046875f, -0.3145751953125f, -0.0384521484375f, -0.002197265625f}},
    {{1.1544189453125f, -0.226806640625f, 0.012451171875f, 0.031494140625f}},
    {{0.96142578125f, -0.5472412109375f, 0.259521484375f, -0.065673828125f}},
    {{-0.87548828125f, -0.2191162109375f, -0.2525634765625f, 0.058837890625f}},
    {{-0.898193359375f, -0.256591796875f, -0.2725830078125f, 0.0537109375f}},
    {{-1.119384765625f, -0.4283447265625f, -0.326416015625f, -0.0477294921875f}},
    {{-0.322021484375f, -0.3231201171875f, -0.2354736328125f, -0.1998291015625f}},
    {{0.2286376953125f, 1.1209716796875f, 0.22705078125f, -0.701416015625f}},
    {{1.124755859375f, 0.2269287109375f, -0.13720703125f, -0.2962646484375f}},
    {{1.61181640625f, -0.36767578125f, -0.5052490234375f, 0.167236328125f}},
    {{1.5181884765625f, -0.5849609375f, -0.03125f, 0.075927734375f}},
    {{-0.3238525390625f, -0.1396484375f, -0.388427734375f, -0.839599609375f}},
    {{1.1390380859375f, -0.1279296875f, -0.10107421875f, 0.0618896484375f}},
    {{0.200439453125f, -0.07568359375f, -0.115478515625f, -0.5162353515625f}},
    {{0.518310546875f, -0.9259033203125f, -0.0650634765625f, -0.2757568359375f}},
    {{-1.09716796875f, -0.4749755859375f, -0.3426513671875f, 0.00537109375f}},
    {{-0.312744140625f, -0.3338623046875f, -0.211181640625f, -0.2318115234375f}},
    {{0.388427734375f, -0.0589599609375f, -0.087158203125f, -0.1734619140625f}},
    {{0.9688720703125f, -0.46923828125f, 0.3443603515625f, -0.1243896484375f}},
    {{1.2291259765625f, -0.3184814453125f, 0.038330078125f, 0.0238037109375f}},
    {{1.025390625f, -0.4024658203125f, 0.1893310546875f, -0.0189208984375f}},
    {{-1.0411376953125f, -0.3387451171875f, -0.296875f, -0.041015625f}},
    {{1.1568603515625f, -0.229736328125f, 0.01318359375f, 0.03125f}},
    {{0.0091552734375f, -0.2735595703125f, -0.036376953125f, -0.8468017578125f}},
    {{-1.1160888671875f, -0.5078125f, -0.3616943359375f, 0.0006103515625f}},
    {{-0.887451171875f, -0.239013671875f, -0.26318359375f, 0.05615234375f}},
    {{-0.33447265625f, 0.4571533203125f, 0.724609375f, -0.1329345703125f}},
    {{1.0977783203125f, 0.23779296875f, -0.0833740234375f, -0.330078125f}},
    {{1.5992431640625f, -0.3460693359375f, -0.470458984375f, 0.1287841796875f}},
    {{1.1649169921875f, -0.2393798828125f, 0.015869140625f, 0.030517578125f}},
    {{0.6435546875f, -0.521240234375f, 0.38134765625f, -0.3853759765625f}},
    {{-0.939453125f, -0.4129638671875f, -0.3548583984375f, -0.0556640625f}},
    {{0.8922119140625f, 0.3079833984375f, 0.052978515625f, -0.3004150390625f}},
    {{1.2542724609375f, -0.3499755859375f, 0.0477294921875f, 0.02099609375f}},
    {{1.33544921875f, -0.4542236328125f, 0.0811767578125f, 0.0118408203125f}},
    {{0.0029296875f, -0.037841796875f, -0.154052734375f, 0.0390625f}},
    {{-0.991455078125f, -0.2943115234375f, -0.2821044921875f, -0.0330810546875f}},
    {{-1.0389404296875f, -0.3743896484375f, -0.2852783203125f, 0.0198974609375f}},
    {{0.039794921875f, -0.469482421875f, 0.0511474609375f, -0.1138916015625f}},
    {{1.0858154296875f, 0.267822265625f, -0.0660400390625f, -0.3515625f}},
    {{1.4737548828125f, -0.22900390625f, -0.2462158203125f, -0.0733642578125f}},
    {{1.0655517578125f, -0.4178466796875f, 0.204345703125f, -0.0206298828125f}},
    {{1.580810546875f, -0.4696044921875f, -0.3670654296875f, 0.237548828125f}},
    {{1.225341796875f, -0.313720703125f, 0.036865234375f, 0.024169921875f}},
    {{1.1456298828125f, -0.3365478515625f, 0.123046875f, 0.0050048828125f}},
    {{-0.576171875f, -0.611083984375f, -0.34814453125f, -0.1417236328125f}},
    {{0.9605712890625f, -0.528076171875f, 0.2606201171875f, -0.0611572265625f}},
    {{0.299072265625f, -1.0494384765625f, 0.1585693359375f, -0.33935546875f}},
    {{1.244140625f, -0.3372802734375f, 0.0439453125f, 0.0220947265625f}},
    {{1.3809814453125f, -0.5142822265625f, 0.1016845703125f, 0.0064697265625f}},
    {{1.239501953125f, -0.33154296875f, 0.0421142578125f, 0.0225830078125f}},
    {{1.176513671875f, -0.1729736328125f, -0.0899658203125f, 0.058837890625f}},
    {{0.470458984375f, -0.555908203125f, 0.3470458984375f, -0.4146728515625f}},
    {{0.8177490234375f, -0.6907958984375f, 0.2745361328125f, -0.131103515625f}},
    {{1.352783203125f, -0.47705078125f, 0.0888671875f, 0.009765625f}},
    {{-0.125244140625f, -1.197509765625f, -0.0982666015625f, -0.422607421875f}},
    {{1.26904296875f, -0.457275390625f, 0.1668701171875f, -0.01171875f}},
    {{1.2557373046875f, 0.12060546875f, -0.2337646484375f, -0.1754150390625f}},
    {{0.9708251953125f, 0.473388671875f, -0.09326171875f, -0.3983154296875f}},
    {{1.5489501953125f, -0.4119873046875f, -0.409423828125f, 0.2537841796875f}},
    {{0.8106689453125f, 0.386474609375f, 0.0281982421875f, -0.2550048828125f}},
    {{-0.28662109375f, -0.897705078125f, -0.2373046875f, -0.503173828125f}},
    {{1.134033203125f, -0.4930419921875f, 0.2301025390625f, -0.030029296875f}},
    {{0.5655517578125f, -0.7816162109375f, 0.21337890625f, -0.1976318359375f}},
    {{1.3729248046875f, -0.5035400390625f, 0.097900390625f, 0.0074462890625f}},
    {{1.1971435546875f, -0.27880859375f, 0.0267333984375f, 0.027099609375f}},
    {{1.1884765625f, -0.1875f, -0.086181640625f, 0.0577392578125f}},
    {{1.0302734375f, -0.41943359375f, 0.190673828125f, -0.021484375f}},
    {{1.1361083984375f, -0.1246337890625f, -0.1019287109375f, 0.0621337890625f}},
    {{0.207275390625f, -1.1016845703125f, 0.083984375f, -0.3707275390625f}},
    {{1.246826171875f, -0.3406982421875f, 0.044921875f, 0.0218505859375f}},
    {{1.024169921875f, 0.396484375f, -0.092529296875f, -0.3648681640625f}},
    {{0.8790283203125f, 0.40478515625f, 0.005615234375f, -0.319091796875f}},
    {{-0.0107421875f, -0.9532470703125f, -0.065673828125f, -0.5579833984375f}},
    {{0.7559814453125f, -0.6334228515625f, 0.3369140625f, -0.1519775390625f}},
    {{1.5045166015625f, -0.157470703125f, -0.40087890625f, 0.0308837890625f}},
    {{1.5947265625f, -0.4974365234375f, -0.3447265625f, 0.2291259765625f}},
    {{0.6510009765625f, 0.3660888671875f, 0.0946044921875f, -0.13818359375f}},
}};

struct Predictor {
    std::array<std::array<float, 4>, 4> residual{};
    std::array<std::array<float, 4>, 4> history{};
};

constexpr auto MakePredictors() {
    std::array<Predictor, PredictorCoefficients.size()> predictors{};
    // Work out four samples at a time, keeping the float rounding used by the LLE table.
    for (size_t p = 0; p < predictors.size(); ++p) {
        for (size_t term = 0; term < 8; ++term) {
            std::array<float, 4> history{};
            if (term >= 4) {
                history[term - 4] = 1.0f;
            }
            for (size_t sample = 0; sample < 4; ++sample) {
                float value = term == sample ? 1.0f : 0.0f;
                for (size_t h = 0; h < 4; ++h) {
                    value = value + PredictorCoefficients[p][h] * history[h];
                }
                auto& column =
                    term < 4 ? predictors[p].residual[term] : predictors[p].history[term - 4];
                column[sample] = value;
                history = {value, history[0], history[1], history[2]};
            }
        }
    }
    return predictors;
}

alignas(16) constexpr auto Predictors = MakePredictors();

__m128 Clip4(__m128 value) {
    return _mm_max_ps(_mm_set1_ps(-1.0f), _mm_min_ps(_mm_set1_ps(1.0f), value));
}
void StorePcm16(s16* output, __m128 value) {
    const auto positive = _mm_cmpgt_ps(value, _mm_setzero_ps());
    const auto scale = _mm_or_ps(_mm_and_ps(positive, _mm_set1_ps(32767.0f)),
                                 _mm_andnot_ps(positive, _mm_set1_ps(32768.0f)));
    const auto rounding = _mm_or_ps(_mm_and_ps(positive, _mm_set1_ps(0.5f)),
                                    _mm_andnot_ps(positive, _mm_set1_ps(-0.5f)));
    const auto integers = _mm_cvttps_epi32(_mm_add_ps(_mm_mul_ps(value, scale), rounding));
    const auto packed = _mm_packs_epi32(integers, integers);
    _mm_storel_epi64(reinterpret_cast<__m128i*>(output), packed);
}

void DecodeBlock(std::array<float, 4>& history, const u8* block, float* output) {
    const u32 predictor_index = (block[0] >> 4) | (block[1] & 0x70);
    const auto& predictor = Predictors[predictor_index];
    const auto scale = _mm_set1_ps(1.0f / float(1u << (7 + (block[0] & 0xF))));
    const auto r0 = _mm_mul_ps(_mm_loadu_ps(predictor.residual[0].data()), scale);
    const auto r1 = _mm_mul_ps(_mm_loadu_ps(predictor.residual[1].data()), scale);
    const auto r2 = _mm_mul_ps(_mm_loadu_ps(predictor.residual[2].data()), scale);
    const auto r3 = _mm_mul_ps(_mm_loadu_ps(predictor.residual[3].data()), scale);
    const auto h0 = _mm_loadu_ps(predictor.history[0].data());
    const auto h1 = _mm_loadu_ps(predictor.history[1].data());
    const auto h2 = _mm_loadu_ps(predictor.history[2].data());
    const auto h3 = _mm_loadu_ps(predictor.history[3].data());
    auto previous0 = _mm_set1_ps(history[0]);
    auto previous1 = _mm_set1_ps(history[1]);
    auto previous2 = _mm_set1_ps(history[2]);
    auto previous3 = _mm_set1_ps(history[3]);

    for (u32 sample = 0; sample < SamplesPerBlock; sample += 4) {
        const u8 a = block[2 + sample / 2];
        const u8 b = block[3 + sample / 2];
        const auto n0 = _mm_set1_ps(float(std::bit_cast<s8>(u8(a << 4))));
        const auto n1 = _mm_set1_ps(float(std::bit_cast<s8>(u8(a & 0xF0))));
        const auto n2 = _mm_set1_ps(float(std::bit_cast<s8>(u8(b << 4))));
        const auto n3 = _mm_set1_ps(float(std::bit_cast<s8>(u8(b & 0xF0))));
        // Keep the LLE's pairwise addition order and separate multiply/add operations.
        const auto residual = _mm_add_ps(_mm_add_ps(_mm_mul_ps(r0, n0), _mm_mul_ps(r1, n1)),
                                         _mm_add_ps(_mm_mul_ps(r2, n2), _mm_mul_ps(r3, n3)));
        auto prediction = _mm_add_ps(_mm_mul_ps(h0, previous0), _mm_mul_ps(h1, previous1));
        if (predictor_index >= 29) {
            prediction = _mm_add_ps(
                prediction, _mm_add_ps(_mm_mul_ps(h2, previous2), _mm_mul_ps(h3, previous3)));
        }
        const auto samples = _mm_add_ps(prediction, residual);
        _mm_storeu_ps(output + sample, samples);
        previous0 = _mm_shuffle_ps(samples, samples, _MM_SHUFFLE(3, 3, 3, 3));
        previous1 = _mm_shuffle_ps(samples, samples, _MM_SHUFFLE(2, 2, 2, 2));
        previous2 = _mm_shuffle_ps(samples, samples, _MM_SHUFFLE(1, 1, 1, 1));
        previous3 = _mm_shuffle_ps(samples, samples, _MM_SHUFFLE(0, 0, 0, 0));
    }
    // Prediction uses unclipped floats. Clipping belongs only to the output conversion.
    history = {output[27], output[26], output[25], output[24]};
}

int ValidateHeader(DecoderState& state, const VagHeader& header) {
    if (header.magic != std::array{'V', 'A', 'G', 'p'}) {
        return ORBIS_AUDIODEC_CPU_ERROR_HEVAG_NOT_FOUND_HEADER;
    }
    if (header.magic != state.magic) {
        return ORBIS_AUDIODEC_CPU_ERROR_HEVAG_INVALID_ID;
    }
    if (header.version_be != state.version_be) {
        return ORBIS_AUDIODEC_CPU_ERROR_HEVAG_INVALID_VERSION;
    }
    if (header.encoded_size_be != state.encoded_size_be) {
        return ORBIS_AUDIODEC_CPU_ERROR_HEVAG_INVALID_DATA_SIZE;
    }
    if (header.sample_rate_be != state.sample_rate_be) {
        return ORBIS_AUDIODEC_CPU_ERROR_HEVAG_INVALID_SAMPLING_FREQ;
    }
    // Both 0 and 1 denote mono.
    if (header.channel_count != state.channel_count) {
        if (std::max(header.channel_count, state.channel_count) > 1) {
            return ORBIS_AUDIODEC_CPU_ERROR_HEVAG_INVALID_CHANNELS;
        }
        state.channel_count = header.channel_count;
    }
    if (header.stream_name != state.stream_name) {
        return ORBIS_AUDIODEC_CPU_ERROR_HEVAG_INVALID_DATA_NAME;
    }
    state.header_consumed = 1;
    return ORBIS_OK;
}

void WriteOutput(const DecoderState& state,
                 const std::array<std::array<float, SamplesPerBlock>, MaxChannels>& samples,
                 u8* output, u32 channels) {
    const u32 stride = state.planar_output ? 1 : channels;
    for (u32 channel = 0; channel < channels; ++channel) {
        const u32 start = state.planar_output ? channel * SamplesPerBlock : channel;
        for (u32 sample = 0; sample < SamplesPerBlock; sample += 4) {
            const auto value = _mm_loadu_ps(samples[channel].data() + sample);
            if (state.sample_format == SampleFormat::Float32) {
                std::array<float, 4> converted;
                _mm_storeu_ps(converted.data(), Clip4(value));
                if (stride == 1) {
                    std::memcpy(output + (start + sample) * sizeof(float), converted.data(),
                                sizeof(converted));
                } else {
                    for (u32 lane = 0; lane < 4; ++lane) {
                        std::memcpy(output + (start + (sample + lane) * stride) * sizeof(float),
                                    &converted[lane], sizeof(float));
                    }
                }
            } else {
                std::array<s16, 4> converted;
                StorePcm16(converted.data(), value);
                if (stride == 1) {
                    std::memcpy(output + (start + sample) * sizeof(s16), converted.data(),
                                sizeof(converted));
                } else {
                    for (u32 lane = 0; lane < 4; ++lane) {
                        std::memcpy(output + (start + (sample + lane) * stride) * sizeof(s16),
                                    &converted[lane], sizeof(s16));
                    }
                }
            }
        }
    }
}

} // namespace

int PS4_SYSV_ABI QueryMemSize(const OrbisAudiodecCpuQueryCtrl*,
                              OrbisAudiodecCpuMemoryDescriptor* resource) {
    resource->data_size = ContextSize;
    return ORBIS_OK;
}

int PS4_SYSV_ABI InitDecoder(const OrbisAudiodecCpuInitCtrl* ctrl,
                             OrbisAudiodecCpuMemoryDescriptor* resource) {
    if (resource->data_size < ContextSize) {
        return ORBIS_AUDIODECCPU_ERROR_INVALID_WORK_MEM_SIZE;
    }
    u32 param_size;
    std::memcpy(&param_size, ctrl->param, sizeof(param_size));
    if (param_size != sizeof(CodecParam)) {
        return ORBIS_AUDIODECCPU_ERROR_INVALID_PARAM_SIZE;
    }
    CodecParam param;
    std::memcpy(&param, ctrl->param, sizeof(param));
    if (param.header.channel_count > MaxChannels) {
        return ORBIS_AUDIODEC_CPU_ERROR_HEVAG_INVALID_CHANNELS;
    }
    const u32 sample_rate = std::byteswap(param.header.sample_rate_be);
    if (sample_rate < 2000 || sample_rate > 192000) {
        return ORBIS_AUDIODEC_CPU_ERROR_HEVAG_INVALID_SAMPLING_FREQ;
    }
    if (param.sample_format != SampleFormat::S16 && param.sample_format != SampleFormat::Float32) {
        return ORBIS_AUDIODECCPU_ERROR_INVALID_WORD_LENGTH;
    }
    if (param.planar_output > 1) {
        return ORBIS_AUDIODEC_CPU_ERROR_HEVAG_INVALID_OUTPUT_FORMAT;
    }
    DecoderState state{};
    state.magic = param.header.magic;
    state.version_be = param.header.version_be;
    state.encoded_size_be = param.header.encoded_size_be;
    state.sample_rate_be = param.header.sample_rate_be;
    state.channel_count = param.header.channel_count;
    state.stream_name = param.header.stream_name;
    state.sample_format = param.sample_format;
    state.planar_output = param.planar_output;
    StoreState(*resource, state);
    return ORBIS_OK;
}

int PS4_SYSV_ABI Decode(const OrbisAudiodecCpuDecodeCtrl* ctrl,
                        OrbisAudiodecCpuMemoryDescriptor* resource) {
    if (resource->data_size < ContextSize) {
        return ORBIS_AUDIODECCPU_ERROR_INVALID_WORK_MEM_SIZE;
    }
    u32 bsi_info_size;
    std::memcpy(&bsi_info_size, ctrl->bsi_info, sizeof(bsi_info_size));
    if (bsi_info_size != sizeof(BsiInfo)) {
        return ORBIS_AUDIODECCPU_ERROR_INVALID_BSI_INFO_SIZE;
    }
    const u32 input_capacity = ctrl->au_info->data_size;
    const u32 output_capacity = ctrl->pcm_item->data_size;
    ctrl->au_info->data_size = 0;
    ctrl->pcm_item->data_size = 0;
    auto state = LoadState(*resource);
    const u32 channels = std::max<u32>(state.channel_count, 1);
    const auto* input = static_cast<const u8*>(ctrl->au_info->data);
    u32 header_bytes = 0;
    if (!state.header_consumed) {
        // LLE assumes header is complete but it doesn't hurt to check before reading fields.
        if (input_capacity < sizeof(VagHeader)) {
            return ORBIS_AUDIODECCPU_ERROR_INVALID_AU_SIZE;
        }
        VagHeader header;
        std::memcpy(&header, input, sizeof(header));
        const int result = ValidateHeader(state, header);
        // Channel normalisation can occur even when a later header check fails.
        StoreState(*resource, state);
        if (result != ORBIS_OK) {
            return result;
        }
        header_bytes = sizeof(VagHeader);
    }
    if (channels > MaxChannels || input_capacity - header_bytes < BlockSize * channels) {
        return ORBIS_AUDIODECCPU_ERROR_INVALID_AU_SIZE;
    }
    const u32 sample_size =
        state.sample_format == SampleFormat::Float32 ? sizeof(float) : sizeof(s16);
    const u32 output_bytes = SamplesPerBlock * sample_size * channels;
    if (output_capacity < output_bytes) {
        return ORBIS_AUDIODECCPU_ERROR_INVALID_PCM_SIZE;
    }
    std::array<std::array<float, SamplesPerBlock>, MaxChannels> samples;
    std::array<u8, MaxChannels> loop_flags{};
    for (u32 channel = 0; channel < channels; ++channel) {
        const auto* block = input + header_bytes + channel * BlockSize;
        DecodeBlock(state.history[channel], block, samples[channel].data());
        loop_flags[channel] = block[1] & 0xF;
    }
    // The LLE updates the channel histories even when the loop flags don't match.
    StoreState(*resource, state);
    for (u32 channel = 1; channel < channels; ++channel) {
        if (loop_flags[channel] != loop_flags[0]) {
            return ORBIS_AUDIODEC_CPU_ERROR_HEVAG_INVALID_LOOP_FLAG;
        }
    }
    BsiInfo bsi_info;
    bsi_info.magic = state.magic;
    bsi_info.version = std::byteswap(state.version_be);
    bsi_info.encoded_size = std::byteswap(state.encoded_size_be);
    bsi_info.sample_rate = std::byteswap(state.sample_rate_be);
    bsi_info.channel_count = state.channel_count;
    bsi_info.stream_name = state.stream_name;
    bsi_info.channel_loop_flags = loop_flags;
    std::memcpy(ctrl->bsi_info, &bsi_info, sizeof(bsi_info));
    WriteOutput(state, samples, static_cast<u8*>(ctrl->pcm_item->data), channels);
    ctrl->au_info->data_size = header_bytes + BlockSize * channels;
    ctrl->pcm_item->data_size = output_bytes;
    return ORBIS_OK;
}

int PS4_SYSV_ABI ClearContext(OrbisAudiodecCpuMemoryDescriptor* resource) {
    if (resource->data_size < ContextSize) {
        return ORBIS_AUDIODECCPU_ERROR_INVALID_WORK_MEM_SIZE;
    }
    auto state = LoadState(*resource);
    state.history = {};
    StoreState(*resource, state);
    return ORBIS_OK;
}

void RegisterLib(Core::Loader::SymbolsResolver* sym) {
    LIB_OBJ("lYA31T9O1KU", "libSceAudiodecCpuHevag", 1, "libSceAudiodecCpuHevag", &Ops);
}

} // namespace Libraries::AudiodecCpu::Hevag
