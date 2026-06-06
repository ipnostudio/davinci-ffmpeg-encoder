#pragma once
#include <map>
#include <string>
#include <vector>

extern "C" {
#include <libavutil/avutil.h>
}

namespace IOPlugin {

enum HardwareAcceleration { None, Vaapi, Nvenc };
enum QualityMode { CRF = 1, CQP = 2, VBR = 4, CBR = 8 };

struct EncoderFormat {
    const char* codecName{};
    int bitDepth{8};
    uint32_t colorModel{};
    uint8_t hSubsampling{};
    uint8_t vSubsampling{};
    AVPixelFormat pixelFormat{};
    AVPixelFormat srcPixelFormat{AV_PIX_FMT_NONE};
};

struct EncoderInfo {
    uint8_t UUID[16]{};
    const char* codecGroup{};
    uint32_t fourCC{};
    const char* encoder{};
    HardwareAcceleration hwAcceleration{};
    int32_t qualityModes{};
    uint8_t qp[3]{};
    std::map<int, std::string> presets{};
    int defaultPreset{};
    const char* customParamsKey{};
    std::vector<EncoderFormat> formats{};

    // Flags de UI — activa solo las opciones que soporta el encoder
    bool supportsProfile{false};  // Baseline / Main / High
    bool supportsLevel{false};    // 4.1 → 5.2
    bool supportsGOP{false};      // Keyframe Interval
};

}
