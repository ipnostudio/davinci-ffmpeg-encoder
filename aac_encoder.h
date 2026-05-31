#pragma once

#include "wrapper/plugin_api.h"
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

namespace IOPlugin {

class AACEncoder : public IPluginCodecRef {
public:
    static constexpr uint8_t UUID[16] = {
        0xfa, 0x3c, 0x11, 0xe8, 0x9a, 0x5b, 0x4b, 0xd1,
        0xb2, 0x7f, 0xc0, 0x2d, 0x18, 0xaa, 0x62, 0x10
    };

    static StatusCode RegisterCodec(HostListRef* p_pList);
    static StatusCode GetEncoderSettings(HostPropertyCollectionRef* p_pValues,
                                         HostListRef* p_pSettingsList);

protected:
    StatusCode DoInit(HostPropertyCollectionRef* p_pProps) override;
    StatusCode DoOpen(HostBufferRef* p_pBuff) override;
    StatusCode DoProcess(HostBufferRef* p_pBuff) override;
    void DoFlush() override;

private:
    StatusCode DrainReadyPackets();
    StatusCode DrainEncoder();
    StatusCode SendOutputPacket(AVPacket* pkt);
    StatusCode ConvertAndEncode(const uint8_t* pcmData, int numSamples, int numChannels);

    int sampleRate_  = 48000;
    int bitRate_     = 320000;
    int channels_    = 2;

    AVCodecContext* ctx_    = nullptr;
    SwrContext*     swrCtx_ = nullptr;
    AVFrame*        frame_  = nullptr;
    AVPacket*       pkt_    = nullptr;

    std::vector<float> accumBuffer_;
    int64_t ptsAccum_ = 0;
    int     frameSize_ = 1024;
};

} // namespace IOPlugin
