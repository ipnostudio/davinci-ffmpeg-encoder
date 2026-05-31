#pragma once

#include "wrapper/plugin_api.h"
#include <vector>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
}

namespace IOPlugin {

struct FFmpegAACContext {
    AVCodecContext* codecCtx  = nullptr;
    AVFrame*        frame     = nullptr;
    AVPacket*       pkt       = nullptr;
    int             frameSize = 0;
    int64_t         pts       = 0;
};

class AACEncoder final : public IPluginCodecRef {
public:
    static const uint8_t UUID[16];

    static StatusCode RegisterCodec(HostListRef* p_pList);
    static StatusCode GetEncoderSettings(HostPropertyCollectionRef* p_pValues,
                                         HostListRef* p_pSettingsList);

    AACEncoder();
    ~AACEncoder();

protected:
    StatusCode DoInit(HostPropertyCollectionRef* p_pProps) override;
    StatusCode DoOpen(HostBufferRef* p_pBuff)             override;
    StatusCode DoProcess(HostBufferRef* p_pBuff)          override;
    void       DoFlush()                                  override;

private:
    void SendEncodedPackets();
    void AddToRingBuffer(const float** planar, int samples);
    bool RingBufferFull() const;
    void GetRingBufferFrame(float** out);
    void PadRingBuffer();
    void ResetRingBuffer();

    int32_t  m_bitRate     = 320;
    uint32_t m_bitDepth    = 16;
    uint32_t m_sampleRate  = 48000;
    uint32_t m_numChannels = 2;

    std::unique_ptr<FFmpegAACContext> m_ctx;

    // Ring buffer (planar float, per channel)
    std::vector<std::vector<float>> m_ringBuf;
    size_t m_ringFill  = 0;
    size_t m_frameSize = 0;
    size_t m_channels  = 0;
};

} // namespace IOPlugin
