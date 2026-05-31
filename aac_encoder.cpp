#include "aac_encoder.h"
#include "plugin.h"
#include <cstring>
#include <vector>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

namespace IOPlugin {

constexpr uint8_t AACEncoder::UUID[16];

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

    // Containers: mp4 + mov (null-separated)
    const char containers[] = "mp4\0mov\0mkv";
    codecInfo.SetProperty(pIOPropContainerList, propTypeString, containers, sizeof(containers));

    return p_pList->Append(&codecInfo) ? errNone : errFail;
}

StatusCode AACEncoder::GetEncoderSettings(HostPropertyCollectionRef* p_pValues,
                                           HostListRef* p_pSettingsList) {
    UISettingsController settings(p_pValues);

    int32_t curSampleRate = 48000;
    int32_t curBitRate    = 320000;
    int32_t curChannels   = 2;

    settings.Load("aac_sr",  curSampleRate);
    settings.Load("aac_br",  curBitRate);
    settings.Load("aac_ch",  curChannels);

    // Sample Rate
    {
        HostUIConfigEntryRef item("aac_sr");
        item.MakeRadioBox("Sample Rate",
            {"48000 Hz (Standard)", "44100 Hz (CD)"},
            {48000, 44100},
            curSampleRate);
        item.SetTriggersUpdate(true);
        p_pSettingsList->Append(&item);
    }

    // Separator
    {
        HostUIConfigEntryRef sep("sep1");
        sep.MakeSeparator();
        p_pSettingsList->Append(&sep);
    }

    // Bit Rate
    {
        HostUIConfigEntryRef item("aac_br");
        item.MakeRadioBox("Audio Bitrate (CBR)",
            {"320 kb/s (Recommended)", "256 kb/s", "192 kb/s", "128 kb/s"},
            {320000, 256000, 192000, 128000},
            curBitRate);
        item.SetTriggersUpdate(true);
        p_pSettingsList->Append(&item);
    }

    // Separator
    {
        HostUIConfigEntryRef sep("sep2");
        sep.MakeSeparator();
        p_pSettingsList->Append(&sep);
    }

    // Channels
    {
        HostUIConfigEntryRef item("aac_ch");
        item.MakeRadioBox("Channels",
            {"Stereo (2ch)", "5.1 Surround (6ch)"},
            {2, 6},
            curChannels);
        item.SetTriggersUpdate(true);
        p_pSettingsList->Append(&item);
    }

    return errNone;
}

StatusCode AACEncoder::DoInit(HostPropertyCollectionRef*) {
    return errNone;
}

StatusCode AACEncoder::DoOpen(HostBufferRef* p_pBuff) {
    // Read settings
    UISettingsController settings(p_pBuff);
    settings.Load("aac_sr", sampleRate_);
    settings.Load("aac_br", bitRate_);
    settings.Load("aac_ch", channels_);

    // Find AAC encoder
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

    // Channel layout
    if (channels_ == 6) {
        AVChannelLayout layout = AV_CHANNEL_LAYOUT_5POINT1;
        av_channel_layout_copy(&ctx_->ch_layout, &layout);
    } else {
        AVChannelLayout layout = AV_CHANNEL_LAYOUT_STEREO;
        av_channel_layout_copy(&ctx_->ch_layout, &layout);
    }

    if (avcodec_open2(ctx_, codec, nullptr) < 0) {
        avcodec_free_context(&ctx_);
        return errFail;
    }

    frameSize_ = ctx_->frame_size > 0 ? ctx_->frame_size : 1024;

    // AVFrame
    frame_ = av_frame_alloc();
    if (!frame_) return errAlloc;
    frame_->nb_samples  = frameSize_;
    frame_->format      = AV_SAMPLE_FMT_FLTP;
    frame_->sample_rate = sampleRate_;
    av_channel_layout_copy(&frame_->ch_layout, &ctx_->ch_layout);
    if (av_frame_get_buffer(frame_, 0) < 0) return errAlloc;

    // SwrContext: int16 interleaved → float planar
    AVChannelLayout srcLayout = (channels_ == 6)
        ? (AVChannelLayout)AV_CHANNEL_LAYOUT_5POINT1
        : (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;

    AVChannelLayout tmpSrc;
    av_channel_layout_copy(&tmpSrc, &srcLayout);

    swr_alloc_set_opts2(&swrCtx_,
        &ctx_->ch_layout, AV_SAMPLE_FMT_FLTP, sampleRate_,
        &tmpSrc,          AV_SAMPLE_FMT_S16,  sampleRate_,
        0, nullptr);

    if (!swrCtx_ || swr_init(swrCtx_) < 0) return errFail;

    pkt_ = av_packet_alloc();
    if (!pkt_) return errAlloc;

    // Send magic cookie (ADTS extradata) to Resolve
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

StatusCode AACEncoder::DoProcess(HostBufferRef* p_pBuff) {
    if (!p_pBuff || !p_pBuff->IsValid())
        return DrainEncoder();

    char* buf = nullptr;
    size_t bufSize = 0;
    if (!p_pBuff->LockBuffer(&buf, &bufSize))
        return errFail;

    // Resolve sends int16 interleaved
    const int numSamples = (int)(bufSize / (sizeof(int16_t) * channels_));
    StatusCode err = ConvertAndEncode(reinterpret_cast<const uint8_t*>(buf), numSamples, channels_);
    p_pBuff->UnlockBuffer();
    return err;
}

void AACEncoder::DoFlush() {
    DrainEncoder();
}

StatusCode AACEncoder::ConvertAndEncode(const uint8_t* pcmData, int numSamples, int numChannels) {
    // Convert int16 interleaved → float planar via swr
    std::vector<std::vector<float>> planars(numChannels, std::vector<float>(numSamples));
    std::vector<uint8_t*> planarPtrs(numChannels);
    for (int i = 0; i < numChannels; ++i)
        planarPtrs[i] = reinterpret_cast<uint8_t*>(planars[i].data());

    const uint8_t* inPtr[1] = { pcmData };
    int converted = swr_convert(swrCtx_, planarPtrs.data(), numSamples, inPtr, numSamples);
    if (converted < 0) return errFail;

    // Accumulate and encode in frameSize_ chunks
    int done = 0;
    while (done < converted) {
        int inAccum  = (int)(accumBuffer_.size() / numChannels);
        int canFill  = frameSize_ - inAccum;
        int toCopy   = std::min(converted - done, canFill);

        for (int s = done; s < done + toCopy; ++s)
            for (int c = 0; c < numChannels; ++c)
                accumBuffer_.push_back(planars[c][s]);
        done += toCopy;

        if ((int)(accumBuffer_.size() / numChannels) >= frameSize_) {
            if (av_frame_make_writable(frame_) < 0) return errFail;

            for (int c = 0; c < numChannels; ++c) {
                float* dst = reinterpret_cast<float*>(frame_->data[c]);
                for (int s = 0; s < frameSize_; ++s)
                    dst[s] = accumBuffer_[(size_t)(s * numChannels + c)];
            }
            frame_->pts = ptsAccum_;
            ptsAccum_ += frameSize_;
            accumBuffer_.erase(accumBuffer_.begin(),
                               accumBuffer_.begin() + frameSize_ * numChannels);

            if (avcodec_send_frame(ctx_, frame_) < 0) return errFail;
            DrainReadyPackets();
        }
    }
    return errMoreData;
}

StatusCode AACEncoder::DrainReadyPackets() {
    while (true) {
        int ret = avcodec_receive_packet(ctx_, pkt_);
        if (ret == AVERROR(EAGAIN)) return errMoreData;
        if (ret == AVERROR_EOF)   { av_packet_unref(pkt_); return errNone; }
        if (ret < 0)              { av_packet_unref(pkt_); return errFail; }
        SendOutputPacket(pkt_);
        av_packet_unref(pkt_);
    }
}

StatusCode AACEncoder::DrainEncoder() {
    // Pad and send remaining samples
    if (!accumBuffer_.empty()) {
        int inAccum = (int)(accumBuffer_.size() / channels_);
        if (av_frame_make_writable(frame_) >= 0) {
            for (int c = 0; c < channels_; ++c) {
                float* dst = reinterpret_cast<float*>(frame_->data[c]);
                for (int s = 0; s < frameSize_; ++s)
                    dst[s] = (s < inAccum) ? accumBuffer_[(size_t)(s * channels_ + c)] : 0.0f;
            }
            frame_->pts = ptsAccum_;
            ptsAccum_ += frameSize_;
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

StatusCode AACEncoder::SendOutputPacket(AVPacket* pkt) {
    HostBufferRef outBuf(false);
    if (!outBuf.IsValid() || !outBuf.Resize(pkt->size)) return errAlloc;

    char* outPtr = nullptr;
    size_t outSize = 0;
    if (!outBuf.LockBuffer(&outPtr, &outSize)) return errAlloc;
    memcpy(outPtr, pkt->data, pkt->size);

    outBuf.SetProperty(pIOPropPTS, propTypeInt64, &pkt->pts, 1);
    outBuf.SetProperty(pIOPropDTS, propTypeInt64, &pkt->dts, 1);
    uint8_t isKey = 1;
    outBuf.SetProperty(pIOPropIsKeyFrame, propTypeUInt8, &isKey, 1);

    m_pCallback->SendOutput(&outBuf);
    return errNone;
}

} // namespace IOPlugin
