#include "aac_encoder.h"
#include "plugin.h"
#include <cstring>
#include <algorithm>

extern "C" {
#include <libavutil/opt.h>
#include <libavutil/mem.h>
}

namespace IOPlugin {

const uint8_t AACEncoder::UUID[16] = {
    0xfa, 0x3c, 0x11, 0xe8, 0x9a, 0x5b, 0x4b, 0xd1,
    0xb2, 0x7f, 0xc0, 0x2d, 0x18, 0xaa, 0x62, 0x10
};

AACEncoder::AACEncoder()  = default;

AACEncoder::~AACEncoder() {
    if (m_ctx) {
        if (m_ctx->frame)    av_frame_free(&m_ctx->frame);
        if (m_ctx->pkt)      av_packet_free(&m_ctx->pkt);
        if (m_ctx->codecCtx) avcodec_free_context(&m_ctx->codecCtx);
    }
}

// ---------------------------------------------------------------------------
StatusCode AACEncoder::RegisterCodec(HostListRef* p_pList) {
    HostPropertyCollectionRef codecInfo;
    if (!codecInfo.IsValid()) return errAlloc;

    codecInfo.SetProperty(pIOPropUUID, propTypeUInt8, UUID, 16);

    const char* name = "AAC 320kb/s (FFmpeg)";
    codecInfo.SetProperty(pIOPropName, propTypeString, name, (int)strlen(name));

    uint32_t fourCC = 'aac ';
    codecInfo.SetProperty(pIOPropFourCC, propTypeUInt32, &fourCC, 1);

    uint32_t mediaType = mediaAudio;
    codecInfo.SetProperty(pIOPropMediaType, propTypeUInt32, &mediaType, 1);

    uint32_t dir = dirEncode;
    codecInfo.SetProperty(pIOPropCodecDirection, propTypeUInt32, &dir, 1);

    uint32_t bitDepths[] = { 16, 24 };
    codecInfo.SetProperty(pIOPropBitDepth, propTypeUInt32, bitDepths, 2);

    uint32_t sampleRates[] = { 44100, 48000 };
    codecInfo.SetProperty(pIOPropSamplingRate, propTypeUInt32, sampleRates, 2);

    const char containers[] = "mp4\0mov\0mkv";
    codecInfo.SetProperty(pIOPropContainerList, propTypeString,
                          containers, sizeof(containers));

    return p_pList->Append(&codecInfo) ? errNone : errFail;
}

// ---------------------------------------------------------------------------
StatusCode AACEncoder::GetEncoderSettings(HostPropertyCollectionRef* p_pValues,
                                           HostListRef* p_pSettingsList) {
    int32_t curBR = 320;
    if (p_pValues) p_pValues->GetINT32("aac_br", curBR);

    HostUIConfigEntryRef item("aac_br");
    item.MakeSlider("Bit Rate", "kbps", curBR, 128, 512, 64, 320);
    item.SetTriggersUpdate(true);
    if (!item.IsSuccess() || !p_pSettingsList->Append(&item)) return errFail;

    return errNone;
}

// ---------------------------------------------------------------------------
// DoInit — solo guardamos lo que Resolve nos da, SIN inicializar FFmpeg
// ---------------------------------------------------------------------------
StatusCode AACEncoder::DoInit(HostPropertyCollectionRef* p_pProps) {
    uint32_t sr = 0, ch = 0, bd = 0;
    p_pProps->GetUINT32(pIOPropSamplingRate, sr);
    p_pProps->GetUINT32(pIOPropNumChannels,  ch);
    p_pProps->GetUINT32(pIOPropBitDepth,     bd);

    if (sr > 0) m_sampleRate  = sr;
    if (ch > 0) m_numChannels = ch;
    if (bd > 0) m_bitDepth    = bd;

    g_Log(logLevelWarn, "AAC Plugin :: DoInit — SR=%u CH=%u BD=%u",
          m_sampleRate, m_numChannels, m_bitDepth);
    return errNone;
}

// ---------------------------------------------------------------------------
// DoOpen — aquí Resolve nos da los parámetros REALES del proyecto
// ---------------------------------------------------------------------------
StatusCode AACEncoder::DoOpen(HostBufferRef* p_pBuff) {
    p_pBuff->GetINT32("aac_br", m_bitRate);
    if (m_bitRate <= 0) m_bitRate = 320;

    uint32_t sr = 0, ch = 0, bd = 0;
    p_pBuff->GetUINT32(pIOPropSamplingRate, sr);
    p_pBuff->GetUINT32(pIOPropNumChannels,  ch);
    p_pBuff->GetUINT32(pIOPropBitDepth,     bd);

    if (sr > 0) m_sampleRate  = sr;
    if (ch > 0) m_numChannels = ch;
    if (bd > 0) m_bitDepth    = bd;

    if (m_sampleRate  == 0) m_sampleRate  = 48000;
    if (m_numChannels == 0) m_numChannels = 2;
    if (m_bitDepth    == 0) m_bitDepth    = 16;

    g_Log(logLevelWarn, "AAC Plugin :: DoOpen — SR=%u CH=%u BD=%u BR=%d",
          m_sampleRate, m_numChannels, m_bitDepth, m_bitRate);

    uint64_t br = (uint64_t)m_bitRate * 1000;
    p_pBuff->SetProperty(pIOPropBitRate, propTypeUInt32, &br, 1);

    // Inicializar FFmpeg aquí, con los parámetros correctos
    return InitFFmpeg();
}

// ---------------------------------------------------------------------------
// InitFFmpeg — separado para poder llamarlo desde DoOpen con certeza
// ---------------------------------------------------------------------------
StatusCode AACEncoder::InitFFmpeg() {
    // Limpiar instancia previa si existe
    if (m_ctx) {
        if (m_ctx->frame)    av_frame_free(&m_ctx->frame);
        if (m_ctx->pkt)      av_packet_free(&m_ctx->pkt);
        if (m_ctx->codecCtx) avcodec_free_context(&m_ctx->codecCtx);
        m_ctx.reset();
    }

   const AVCodec* codec = avcodec_find_encoder_by_name("libfdk_aac");
    if (!codec) {
        g_Log(logLevelWarn, "AAC Plugin :: libfdk_aac not found, using native AAC");
        codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    }
    if (!codec) {
        g_Log(logLevelError, "AAC Plugin :: no AAC encoder found");
        return errFail;
    }

    m_ctx.reset(new FFmpegAACContext());
    m_ctx->codecCtx = avcodec_alloc_context3(codec);
    if (!m_ctx->codecCtx) return errAlloc;

    m_ctx->codecCtx->bit_rate    = (int64_t)m_bitRate * 1000;
    m_ctx->codecCtx->sample_fmt  = AV_SAMPLE_FMT_FLTP;
    m_ctx->codecCtx->sample_rate = (int)m_sampleRate;    
    m_ctx->codecCtx->strict_std_compliance = FF_COMPLIANCE_NORMAL;
    m_ctx->codecCtx->profile = FF_PROFILE_AAC_LOW;    
    
    av_channel_layout_default(&m_ctx->codecCtx->ch_layout, (int)m_numChannels);

    g_Log(logLevelWarn, "AAC Plugin :: InitFFmpeg — SR=%d CH=%d BR=%lld profile=%d",
          m_ctx->codecCtx->sample_rate,
          m_ctx->codecCtx->ch_layout.nb_channels,
          m_ctx->codecCtx->bit_rate,
          m_ctx->codecCtx->profile);
    
    if (avcodec_open2(m_ctx->codecCtx, codec, nullptr) < 0) {
    g_Log(logLevelError, "AAC Plugin :: avcodec_open2 failed");
    avcodec_free_context(&m_ctx->codecCtx);
    return errFail;
    } 

    if (m_ctx->codecCtx->extradata && 
        m_ctx->codecCtx->extradata_size > 0){
    g_Log(logLevelWarn,
        "AAC sending magic cookie size=%d",
        m_ctx->codecCtx->extradata_size);        
    SetProperty(
        pIOPropMagicCookie,
        propTypeUInt8,
        m_ctx->codecCtx->extradata,
        m_ctx->codecCtx->extradata_size);
    }

    g_Log(logLevelWarn,
    "AAC profile=%d extradata_size=%d",
    m_ctx->codecCtx->profile,
    m_ctx->codecCtx->extradata_size);

    if (m_ctx->codecCtx->extradata){
        for (int i = 0; i < m_ctx->codecCtx->extradata_size; i++)
        {
            g_Log(logLevelWarn,
            "AAC extradata[%d] = 0x%02X",
            i,
            m_ctx->codecCtx->extradata[i]);
        }
    }
    
    m_ctx->frameSize = m_ctx->codecCtx->frame_size;
    m_ctx->pts       = 0;

    m_ctx->frame = av_frame_alloc();
    m_ctx->frame->nb_samples = m_ctx->frameSize;
    m_ctx->frame->format     = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_copy(&m_ctx->frame->ch_layout, &m_ctx->codecCtx->ch_layout);
    av_frame_get_buffer(m_ctx->frame, 0);

    m_ctx->pkt = av_packet_alloc();

    // Ring buffer
    m_channels  = m_numChannels;
    m_frameSize = (size_t)m_ctx->frameSize;
    m_ringBuf.assign(m_channels, std::vector<float>(m_frameSize, 0.0f));
    m_ringFill = 0;

    g_Log(logLevelWarn, "AAC Plugin :: FFmpeg ready — frameSize=%d SR=%d CH=%zu",
          m_ctx->frameSize, m_ctx->codecCtx->sample_rate, m_channels);

    return errNone;
}

// ---------------------------------------------------------------------------
StatusCode AACEncoder::DoProcess(HostBufferRef* p_pBuff) {
    if (!m_ctx || !m_ctx->codecCtx) {
        g_Log(logLevelError, "AAC Plugin :: DoProcess called but FFmpeg not initialized");
        return errFail;
    }

    if (p_pBuff == nullptr) {
        // Flush
        if (m_ringFill > 0) {
            PadRingBuffer();
            AVFrame* tmp = av_frame_alloc();
            tmp->format     = AV_SAMPLE_FMT_FLTP;
            tmp->nb_samples = (int)m_frameSize;
            av_channel_layout_copy(&tmp->ch_layout, &m_ctx->codecCtx->ch_layout);
            av_frame_get_buffer(tmp, 0);
            for (size_t ch = 0; ch < m_channels; ++ch)
                memcpy(tmp->extended_data[ch], m_ringBuf[ch].data(), m_frameSize * sizeof(float));
            tmp->pts = m_ctx->pts;
            m_ctx->pts += (int)m_frameSize;
            avcodec_send_frame(m_ctx->codecCtx, tmp);
            av_frame_free(&tmp);
            SendEncodedPackets();
            ResetRingBuffer();
        }
        avcodec_send_frame(m_ctx->codecCtx, nullptr);
        SendEncodedPackets();
        return errNone;
    }

    char*  buf     = nullptr;
    size_t bufSize = 0;
    if (!p_pBuff->LockBuffer(&buf, &bufSize) || bufSize == 0)
        return errNone;

    // Leer sample rate real del buffer por si Resolve lo actualiza tarde
    uint32_t sr = 0;
    if (p_pBuff->GetUINT32(pIOPropSamplingRate, sr) && sr > 0 && sr != m_sampleRate) {
        g_Log(logLevelWarn, "AAC Plugin :: SR changed in DoProcess %u→%u, reinit", m_sampleRate, sr);
        m_sampleRate = sr;
        p_pBuff->UnlockBuffer();
        InitFFmpeg();
        if (!p_pBuff->LockBuffer(&buf, &bufSize) || bufSize == 0)
            return errNone;
    }

    int bytesPerSample = (m_bitDepth == 24) ? 3 : 2;
    int totalSamples   = (int)(bufSize / ((size_t)m_channels * bytesPerSample));

    std::vector<std::vector<float>> planar(m_channels, std::vector<float>(totalSamples, 0.0f));

    if (m_bitDepth == 24) {
        unsigned char* src = (unsigned char*)buf;
        for (int i = 0; i < totalSamples; ++i)
            for (size_t ch = 0; ch < m_channels; ++ch) {
                int idx = (i * (int)m_channels + (int)ch) * 3;
                int32_t s = ((int32_t)src[idx+2] << 24) | ((int32_t)src[idx+1] << 16) | ((int32_t)src[idx] << 8);
                s >>= 8;
                planar[ch][i] = s / 8388608.0f;
            }
    } else {
        int16_t* src = (int16_t*)buf;
        for (int i = 0; i < totalSamples; ++i)
            for (size_t ch = 0; ch < m_channels; ++ch)
                planar[ch][i] = src[i * (int)m_channels + (int)ch] / 32768.0f;
    }

    p_pBuff->UnlockBuffer();

    int done = 0;
    while (done < totalSamples) {
        int canFill = (int)(m_frameSize - m_ringFill);
        int toCopy  = std::min(totalSamples - done, canFill);

        std::vector<const float*> ptrs(m_channels);
        for (size_t ch = 0; ch < m_channels; ++ch)
            ptrs[ch] = &planar[ch][done];
        AddToRingBuffer(ptrs.data(), toCopy);
        done += toCopy;

        if (RingBufferFull()) {
            AVFrame* tmp = av_frame_alloc();
            tmp->format     = AV_SAMPLE_FMT_FLTP;
            tmp->nb_samples = (int)m_frameSize;
            av_channel_layout_copy(&tmp->ch_layout, &m_ctx->codecCtx->ch_layout);
            av_frame_get_buffer(tmp, 0);
            for (size_t ch = 0; ch < m_channels; ++ch)
                memcpy(tmp->extended_data[ch], m_ringBuf[ch].data(), m_frameSize * sizeof(float));
            tmp->pts = m_ctx->pts;
            m_ctx->pts += (int)m_frameSize;
            avcodec_send_frame(m_ctx->codecCtx, tmp);
            av_frame_free(&tmp);
            SendEncodedPackets();
            ResetRingBuffer();
        }
    }

    return errNone;
}

// ---------------------------------------------------------------------------
void AACEncoder::DoFlush() {
    if (m_ctx && m_ctx->codecCtx)
        avcodec_flush_buffers(m_ctx->codecCtx);
}

// ---------------------------------------------------------------------------
void AACEncoder::SendEncodedPackets() {
    while (true) {
        int ret = avcodec_receive_packet(m_ctx->codecCtx, m_ctx->pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) break;

        HostBufferRef outBuf(false);
        outBuf.Resize(m_ctx->pkt->size);
        char*  outData = nullptr;
        size_t outSize = 0;
        if (outBuf.LockBuffer(&outData, &outSize) && outSize >= (size_t)m_ctx->pkt->size) {
            memcpy(outData, m_ctx->pkt->data, m_ctx->pkt->size);
            outBuf.UnlockBuffer();
          
            outBuf.SetProperty(pIOPropSamplingRate, propTypeUInt32, &m_sampleRate, 1);
            outBuf.SetProperty(pIOPropNumChannels,  propTypeUInt32, &m_numChannels,1);

            uint8_t isKey = 0;
            outBuf.SetProperty(pIOPropIsKeyFrame, propTypeUInt8, &isKey, 1);

            int64_t pts = m_ctx->pkt->pts;
            int64_t dur = m_ctx->pkt->duration;
            outBuf.SetProperty(pIOPropPTS,      propTypeInt64, &pts, 1);
            outBuf.SetProperty(pIOPropDuration, propTypeInt64, &dur, 1);

            IPluginCodecRef::DoProcess(&outBuf);
        }
        av_packet_unref(m_ctx->pkt);
    }
}

// ---------------------------------------------------------------------------
void AACEncoder::AddToRingBuffer(const float** planar, int samples) {
    for (int i = 0; i < samples; ++i) {
        if (m_ringFill >= m_frameSize) break;
        for (size_t ch = 0; ch < m_channels; ++ch)
            m_ringBuf[ch][m_ringFill] = planar[ch][i];
        m_ringFill++;
    }
}

bool AACEncoder::RingBufferFull() const {
    return m_ringFill >= m_frameSize;
}

void AACEncoder::PadRingBuffer() {
    for (size_t ch = 0; ch < m_channels; ++ch)
        for (size_t i = m_ringFill; i < m_frameSize; ++i)
            m_ringBuf[ch][i] = 0.0f;
}

void AACEncoder::ResetRingBuffer() {
    m_ringFill = 0;
    for (auto& ch : m_ringBuf)
        std::fill(ch.begin(), ch.end(), 0.0f);
}

} // namespace IOPlugin
