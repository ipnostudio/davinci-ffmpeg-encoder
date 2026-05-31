#pragma once

// ===========================================================================
//  AAC Audio Encoder Plugin for DaVinci Resolve Studio
//  Based on the FFmpeg Encoder Plugin by Edvin Nilsson (GPL-3.0)
//  https://github.com/EdvinNilsson/ffmpeg_encoder_plugin
//
//  Adds AAC audio encoding via libavcodec (aac encoder) with:
//    - Sample Rate: 44100 Hz or 48000 Hz
//    - Bitrate: Constant 320 kb/s (CBR)
//    - Channels: Stereo (2ch) / 5.1 (6ch)
//
//  Compatible with MP4, MOV and MKV containers in DaVinci Resolve Studio.
// ===========================================================================

#include <cstdint>
#include <string>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include "wrapper/plugin_api.h"

namespace IOPlugin {

// ---------------------------------------------------------------------------
//  AAC audio settings stored per-session
// ---------------------------------------------------------------------------
struct AACSettings {
    int sampleRate   = 48000;   // 44100 or 48000
    int bitRate      = 320000;  // 320 kb/s fixed CBR (can be overridden via UI)
    int channels     = 2;       // 2 = stereo, 6 = 5.1
};

// ---------------------------------------------------------------------------
//  AACEncoder — IPluginCodecRef implementation for audio AAC
// ---------------------------------------------------------------------------
class AACEncoder final : public IPluginCodecRef {
public:
    // Plugin registry UUID  (must be unique across the whole plugin)
    // Generated with: python3 -c "import uuid; print([hex(b) for b in uuid.uuid4().bytes])"
    static constexpr uint8_t UUID[16] = {
        0xfa, 0x3c, 0x11, 0xe8, 0x9a, 0x5b, 0x4b, 0xd1,
        0xb2, 0x7f, 0xc0, 0x2d, 0x18, 0xaa, 0x62, 0x10
    };

    // Human-readable names shown in DaVinci Resolve Render UI
    static constexpr const char* CODEC_GROUP = "AAC Audio (FFmpeg)";
    static constexpr const char* CODEC_NAME  = "AAC 320kb/s (FFmpeg)";

    // Four-CC for AAC inside MP4/MOV
    static constexpr uint32_t FOURCC = 'mp4a';

    AACEncoder();
    ~AACEncoder() override;

    // Called once at plugin discovery — registers this codec in DR's list
    static StatusCode RegisterCodec(HostListRef* p_pList);

    // Called when DR opens the settings panel for this codec
    static StatusCode GetEncoderSettings(HostPropertyCollectionRef* p_pValues,
                                         HostListRef*               p_pSettingsList);

protected:
    // IPluginCodecRef interface
    StatusCode DoInit(HostPropertyCollectionRef* p_pProps) override;
    StatusCode DoOpen(HostBufferRef* p_pBuff)             override;
    StatusCode DoProcess(HostBufferRef* p_pBuff)          override;
    void       DoFlush()                                  override;

private:
    // Helpers
    StatusCode OpenCodecContext(const AACSettings& s);
    StatusCode ConvertAndEncode(const int16_t* pcmData, int numSamples, int64_t pts);
    StatusCode DrainReadyPackets();
    StatusCode DrainEncoder();
    StatusCode SendOutputPacket(AVPacket* pkt);

    // Settings loaded from DaVinci Resolve properties
    AACSettings     settings_;

    // FFmpeg objects
    AVCodecContext* ctx_       = nullptr;
    SwrContext*     swrCtx_    = nullptr;
    AVFrame*        frame_     = nullptr;
    AVPacket*       pkt_       = nullptr;

    // Accumulation buffer for frame-size-aligned encoding
    // libavcodec's aac encoder uses fixed frame sizes (typically 1024 samples)
    std::vector<float> accumBuffer_;
    int64_t            ptsAccum_ = 0;
    int                frameSize_ = 0;   // filled after avcodec_open2

    // Property key names used to persist UI settings
    static constexpr const char* KEY_SAMPLE_RATE = "aac_ffmpeg_sample_rate";
    static constexpr const char* KEY_BIT_RATE    = "aac_ffmpeg_bit_rate";
    static constexpr const char* KEY_CHANNELS    = "aac_ffmpeg_channels";
};

} // namespace IOPlugin
