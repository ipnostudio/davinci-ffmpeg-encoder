#include "aac_encoder.h"
#include "plugin.h"
#include <cstring>
#include <algorithm>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/opt.h>
}

namespace IOPlugin {

constexpr uint8_t AACEncoder::UUID[16];

static const char* kKeySampleRate = "aac_sr";
static const char* kKeyBitRate    = "aac_br";
static const char* kKeyChannels   = "aac_ch";

// ---------------------------------------------------------------------------
StatusCode AACEncoder::RegisterCodec(HostListRef* p_pList) {
    HostPropertyCollectionRef codecInfo;

    codecInfo.SetProperty(pIOPropUUID, propTypeUInt8, UUID, 16);

    const char* group = "AAC Audio (FFmpeg)";
    codecInfo.SetProperty(pIOPropGroup, propTypeString, group, (int)strlen(group));

    const char* name = "AAC 320kb/s (FFmpeg)";
    codecInfo.SetProperty(pIOPropName, propTypeString, name, (int)strlen(name));

    uint32_t fourCC = 'mp4a';
    codecInfo.SetProperty(pIOPropFourCC, propTypeUInt32, &fourCC, 1);

    uint32_t mediaType = mediaAudio;
    codecInfo.SetProperty(pIOPropMediaType, propTypeUInt32, &mediaType, 1);

    uint32_t dir = dirEncode;
    codecInfo.SetProperty(pIOPropCodecDirection, propTypeUInt32, &dir, 1);

    uint32_t bitDepth = 16;
    codecInfo.SetProperty(pIOPropBitDepth, propTypeUInt32, &bitDepth, 1);

    uint8_t threadSafe = 1;
    codecInfo.SetProperty(pIOPropThreadSafe, propTypeUInt8, &threadSafe, 1);

    const char containers[] = "mp4\0mov\0mkv";
    codecInfo.SetProperty(pIOPropContainerList, propTypeString,
                          containers, sizeof(containers));

    return p_pList->Append(&codecInfo) ? errNone : errFail;
}

// ---------------------------------------------------------------------------
StatusCode AACEncoder::GetEncoderSettings(HostPropertyCollectionRef* p_pValues,
                                           HostListRef* p_pSettingsList) {
    int32_t curSR = 48000;
    int32_t curBR = 320000;
    int32_t curCH = 2;

    if (p_pValues) {
        p_pValues->GetINT32(kKeySampleRate, curSR);
        p_pValues->GetINT32(kKeyBitRate,    curBR);
        p_pValues->GetINT32(kKeyChannels,   curCH);
    }

    {
        HostUIConfigEntryRef item(kKeySampleRate);
        item.MakeRadioBox("Sample Rate",
            {"48000 Hz (Standard)", "44100 Hz (CD)"},
            {48000, 44100}, curSR);
        item.SetTriggersUpdate(true);
        if (!p_pSettingsList->Append(&item)) return errFail;
    }
    {
        HostUIConfigEntryRef sep("aac_sep1");
        sep.MakeSeparator();
        p_pSettingsList->Append(&sep);
    }
    {
        HostUIConfigEntryRef item(kKeyBitRate);
        item.MakeRadioBox("Audio Bitrate (CBR)",
            {"320 kb/s (Recommended)", "256 kb/s", "192 kb/s", "128 kb/s"},
            {320000, 256000, 192000, 128000}, curBR);
        item.SetTriggersUpdate(true);
        if (!p_pSettingsList->Append(&item)) return errFail;
    }
    {
        HostUIConfigEntryRef sep("aac_sep2");
        sep.MakeSeparator();
        p_pSettingsList->Append(&sep);
    }
    {
        HostUIConfigEntryRef item(kKeyChannels);
        item.MakeRadioBox("Channels",
            {"Stereo (2ch)", "5.1 Surround (6ch)"},
            {2, 6}, curCH);
        item.SetTriggersUpdate(true);
        if (!p_pSettingsList->Append(&item)) return errFail;
    }

    return errNone;
}

// ---------------------------------------------------------------------------
StatusCode AACEncoder::DoInit(HostPropertyCollectionRef*) {
    return errNone;
}

// ---------------------------------------------------------------------------
StatusCode AACEncoder::DoOpen(HostBufferRef* p_pBuff) {
    p_pBuff->GetINT32(kKeySampleRate, sampleRate_);
    p_pBuff->GetINT32(kKeyBitRate,    bitRate_);
    p_pBuff->GetINT32(kKeyChannels,   channels_);

    if (sampleRate_ == 0) sampleRate_ = 48000;
    if (bitRate_    == 0) bitRate_    = 320000;
    if (channels_   == 0) channels_   = 2;

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) return errNoCodec;

    ctx_ = avcodec_alloc_context3(codec);
    if (!ctx_) return errAlloc;

    ctx_->codec_id    = AV_CODEC_ID_AAC;
    ctx_->codec_type  = AVMEDIA_TYPE_AUDIO;
    ctx_->sample_rate = sampleRate_;
    ctx_->bit_rate    = bitRate_;
    ctx_->sample_fmt  = AV_SAMPLE_FMT_FLTP;
    ctx_->profile     = AV_PROFILE_AAC_LOW;
    ctx_->flags      |= AV_CODEC_FLAG_GLOBAL_HEADER;

    {
        AVChannelLayout tmp = (channels_ == 6)
            ? (AVChannelLayout)AV_CHANNEL_LAYOUT_5POINT1
            : (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
        av_channel_layout_copy(&ctx_->ch_layout, &tmp);
    }

    if (avcodec_open2(ctx_, codec, nullptr) < 0) {
        avcodec_free_context(&ctx_);
        return errFail;
    }

    frameSize_ = ctx_->frame_size > 0 ? ctx_->frame_size : 1024;

    frame_ = av_frame_alloc();
    if (!frame_) return errAlloc;
    frame_->nb_samples  = frameSize_;
    frame_->format      = AV_SAMPLE_FMT_FLTP;
    frame_->sample_rate = sampleRate_;
    av_channel_layout_copy(&frame_->ch_layout, &ctx_->ch_layout);
    if (av_frame_get_buffer(frame_, 0) < 0) return errAlloc;

    {
        AVChannelLayout srcLayout = (channels_ == 6)
            ? (AVChannelLayout)AV_CHANNEL_LAYOUT_5POINT1
            : (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
        swr_alloc_set_opts2(&swrCtx_,
            &ctx_->ch_layout, AV_SAMPLE_FMT_FLTP, sampleRate_,
            &srcLayout,       AV_SAMPLE_FMT_S16,  sampleRate_,
            0, nullptr);
    }
    if (!swrCtx_ || swr_init(swrCtx_) < 0) return errFail;

    pkt_ = av_packet_alloc();
    if (!pkt_) return errAlloc;

    if (ctx_->extradata && ctx_->extradata_size > 0) {
        p_pBuff->SetProperty(pIOPropMagicCookie, propTypeUInt8,
                             ctx_->extradata, ctx_->extradata_size);
        uint32_t cookieType = 'mp4a';
        p_pBuff->SetProperty(pIOPropMagicCookieType, propTypeUInt32, &cookieType, 1);
    }

    uint8_t noMultiPass = 0;
    p_pBuff->SetProperty(pIOPropMultiPass, propTypeUInt8, &noMultiPass, 1);

    return errNone;
}

// ---------------------------------------------------------------------------
StatusCode AACEncoder::DoProcess(HostBufferRef* p_pBuff) {
    if (!p_pBuff || !p_pBuff->IsValid())
        return DrainEncoder();

    char*  buf     = nullptr;
    size_t bufSize = 0;
    if (!p_pBuff->LockBuffer(&buf, &bufSize)) return errFail;

    const int numSamples = (int)(bufSize / (sizeof(int16_t) * channels_));
    StatusCode err = ConvertAndEncode(reinterpret_cast<const uint8_t*>(buf), numSamples);
    p_pBuff->UnlockBuffer();
    return err;
}

// ---------------------------------------------------------------------------
void AACEncoder::DoFlush() {
    DrainEncoder();
}

// ---------------------------------------------------------------------------
StatusCode AACEncoder::ConvertAndEncode(const uint8_t* pcmData, int numSamples) {
    std::vector<std::vector<float>> planars(channels_, std::vector<float>(numSamples));
    std::vector<uint8_t*> planarPtrs(channels_);
    for (int i = 0; i < channels_; ++i)
        planarPtrs[i] = reinterpret_cast<uint8_t*>(planars[i].data());

    const uint8_t* inPtr[1] = { pcmData };
    int converted = swr_convert(swrCtx_, planarPtrs.data(), numSamples, inPtr, numSamples);
    if (converted < 0) return errFail;

    int done = 0;
    while (done < converted) {
        int inAccum = (int)(accumBuffer_.size() / channels_);
        int canFill = frameSize_ - inAccum;
        int toCopy  = std::min(converted - done, canFill);

        for (int s = done; s < done + toCopy; ++s)
            for (int c = 0; c < channels_; ++c)
                accumBuffer_.push_back(planars[c][s]);
        done += toCopy;

        if ((int)(accumBuffer_.size() / channels_) >= frameSize_) {
            if (av_frame_make_writable(frame_) < 0) return errFail;

            for (int c = 0; c < channels_; ++c) {
                float* dst = reinterpret_cast<float*>(frame_->data[c]);
                for (int s = 0; s < frameSize_; ++s)
                    dst[s] = accumBuffer_[(size_t)(s * channels_ + c)];
            }
            frame_->pts = ptsAccum_;
            ptsAccum_  += frameSize_;
            accumBuffer_.erase(accumBuffer_.begin(),
                               accumBuffer_.begin() + frameSize_ * channels_);

            if (avcodec_send_frame(ctx_, frame_) < 0) return errFail;
            DrainReadyPackets();
        }
    }
    return errMoreData;
}

// ---------------------------------------------------------------------------
StatusCode AACEncoder::DrainReadyPackets() {
    while (true) {
        int ret = avcodec_receive_packet(ctx_, pkt_);
        if (ret == AVERROR(EAGAIN)) { av_packet_unref(pkt_); return errMoreData; }
        if (ret == AVERROR_EOF)     { av_packet_unref(pkt_); return errNone; }
        if (ret < 0)                { av_packet_unref(pkt_); return errFail; }
        SendOutputPacket(pkt_);
        av_packet_unref(pkt_);
    }
}

// ---------------------------------------------------------------------------
StatusCode AACEncoder::DrainEncoder() {
    if (!accumBuffer_.empty()) {
        int inAccum = (int)(accumBuffer_.size() / channels_);
        if (av_frame_make_writable(frame_) >= 0) {
            for (int c = 0; c < channels_; ++c) {
                float* dst = reinterpret_cast<float*>(frame_->data[c]);
                for (int s = 0; s < frameSize_; ++s)
                    dst[s] = (s < inAccum)
                             ? accumBuffer_[(size_t)(s * channels_ + c)]
                             : 0.0f;
            }
            frame_->pts = ptsAccum_;
            ptsAccum_  += frameSize_;
            accumBuffer_.clear();
            avcodec_send_frame(ctx_, frame_);
            DrainReadyPackets();
        }
    }
    avcodec_send_frame(ctx_, nullptr);
    while (true) {
        int ret = avcodec_receive_packet(ctx_, pkt_);
        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) { av_packet_unref(pkt_); break; }
        if (ret < 0) { av_packet_unref(pkt_); break; }
        SendOutputPacket(pkt_);
        av_packet_unref(pkt_);
    }
    return errNone;
}

// ---------------------------------------------------------------------------
StatusCode AACEncoder::SendOutputPacket(AVPacket* pkt) {
    HostBufferRef outBuf(false);
    if (!outBuf.IsValid() || !outBuf.Resize(pkt->size)) return errAlloc;

    char*  outPtr  = nullptr;
    size_t outSize = 0;
    if (!outBuf.LockBuffer(&outPtr, &outSize)) return errAlloc;
    memcpy(outPtr, pkt->data, pkt->size);

    outBuf.SetProperty(pIOPropPTS,        propTypeInt64, &pkt->pts, 1);
    outBuf.SetProperty(pIOPropDTS,        propTypeInt64, &pkt->dts, 1);
    uint8_t isKey = 1;
    outBuf.SetProperty(pIOPropIsKeyFrame, propTypeUInt8, &isKey, 1);

    m_pCallback->SendOutput(&outBuf);
    return errNone;
}

} // namespace IOPlugin
