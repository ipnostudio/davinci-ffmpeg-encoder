// ===========================================================================
//  AAC Audio Encoder Plugin for DaVinci Resolve Studio
//  aac_encoder.cpp
//
//  Implements AACEncoder using libavcodec's built-in "aac" encoder.
//  Supports:
//    • Sample rate: 44100 Hz or 48000 Hz (user-selectable)
//    • Bitrate    : 320 kb/s CBR (fixed default; UI allows 128/192/256/320)
//    • Channels   : Stereo (2) or 5.1 (6)
//    • Containers : MP4, MOV, MKV
// ===========================================================================

#include "aac_encoder.h"
#include <cstring>
#include <vector>
#include <algorithm>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}

// Suppress the stupid av_err2str macro that uses VLAs (non-standard in C++)
#undef av_err2str
static inline std::string av_err_to_string(int errnum) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_make_error_string(buf, AV_ERROR_MAX_STRING_SIZE, errnum);
    return std::string(buf);
}
#define av_err2str(e) av_err_to_string(e).c_str()

// ---------------------------------------------------------------------------
//  DaVinci Resolve IOPlugin property IDs for AUDIO
//  These are defined in wrapper/plugin_api.h — listed here for reference.
//
//  pIOPropUUID              UUID of this codec
//  pIOPropName              Display name
//  pIOPropGroup             Group name in codec picker
//  pIOPropFourCC            FourCC
//  pIOPropMediaType         mediaAudio
//  pIOPropCodecDirection    dirEncode
//  pIOPropSampleRate        Audio sample rate (int)
//  pIOPropAudioChannelCount Audio channel count (int)
//  pIOPropBitDepth          Bits per sample (16 or 32)
//  pIOPropContainerList     Null-separated container list string
//  pIOPropPTS               Presentation time stamp
//  pIOPropMagicCookie       ADTS/ADIF extradata
//  pIOPropMagicCookieType   FourCC for cookie type (esds / 'mp4a')
// ---------------------------------------------------------------------------

namespace IOPlugin {

// ---------------------------------------------------------------------------
//  UUID definition (must match declaration in header)
// ---------------------------------------------------------------------------
constexpr uint8_t AACEncoder::UUID[16];

// ---------------------------------------------------------------------------
//  Constructor / Destructor
// ---------------------------------------------------------------------------
AACEncoder::AACEncoder()  = default;

AACEncoder::~AACEncoder() {
    if (ctx_)    { avcodec_free_context(&ctx_); }
    if (swrCtx_) { swr_free(&swrCtx_);          }
    if (frame_)  { av_frame_free(&frame_);       }
    if (pkt_)    { av_packet_free(&pkt_);        }
}

// ---------------------------------------------------------------------------
//  RegisterCodec
//  Called once during plugin startup to tell DaVinci Resolve about this codec.
// ---------------------------------------------------------------------------
StatusCode AACEncoder::RegisterCodec(HostListRef* p_pList) {
    HostPropertyCollectionRef codecInfo;
    if (!codecInfo.IsValid()) return errAlloc;

    // --- UUID ----------------------------------------------------------
    codecInfo.SetProperty(pIOPropUUID, propTypeUInt8, UUID, 16);

    // --- Names ---------------------------------------------------------
    codecInfo.SetProperty(pIOPropGroup, propTypeString,
                          CODEC_GROUP, static_cast<int>(strlen(CODEC_GROUP)));
    codecInfo.SetProperty(pIOPropName,  propTypeString,
                          CODEC_NAME,  static_cast<int>(strlen(CODEC_NAME)));

    // --- FourCC --------------------------------------------------------
    const uint32_t fourCC = FOURCC;
    codecInfo.SetProperty(pIOPropFourCC, propTypeUInt32, &fourCC, 1);

    // --- Media type: AUDIO ---------------------------------------------
    constexpr uint32_t mediaTypeAudio = mediaAudio;
    codecInfo.SetProperty(pIOPropMediaType, propTypeUInt32, &mediaTypeAudio, 1);

    // --- Direction: encode ---------------------------------------------
    constexpr uint32_t dir = dirEncode;
    codecInfo.SetProperty(pIOPropCodecDirection, propTypeUInt32, &dir, 1);

    // --- Bit depth (16-bit PCM input from DR) --------------------------
    constexpr uint32_t bitDepth = 16;
    codecInfo.SetProperty(pIOPropBitDepth,       propTypeUInt32, &bitDepth, 1);
    codecInfo.SetProperty(pIOPropBitsPerSample,  propTypeUInt32, &bitDepth, 1);

    // --- Thread-safe ---------------------------------------------------
    constexpr uint8_t threadSafe = 1;
    codecInfo.SetProperty(pIOPropThreadSafe, propTypeUInt8, &threadSafe, 1);

    // --- Supported sample rates (44100 and 48000) ----------------------
    constexpr uint32_t sampleRates[] = { 44100, 48000 };
    codecInfo.SetProperty(pIOPropSupportedSampleRates, propTypeUInt32,
                          sampleRates, 2);

    // --- Supported channel counts (stereo=2, 5.1=6) --------------------
    constexpr uint32_t channels[] = { 2, 6 };
    codecInfo.SetProperty(pIOPropSupportedChannelCounts, propTypeUInt32,
                          channels, 2);

    // --- Container list: mp4, mov, mkv ---------------------------------
    const std::string containerList = std::string("mp4") + '\0' +
                                      std::string("mov") + '\0' +
                                      std::string("mkv");
    codecInfo.SetProperty(pIOPropContainerList, propTypeString,
                          containerList.c_str(),
                          static_cast<int>(containerList.size()));

    if (!p_pList->Append(&codecInfo)) return errFail;
    return errNone;
}

// ---------------------------------------------------------------------------
//  GetEncoderSettings
//  Builds the settings panel shown in Resolve's Render page when this codec
//  is selected.  We expose:
//    1. Sample Rate  — Radio box: 48000 Hz (default) | 44100 Hz
//    2. Bit Rate     — Radio box: 320 kb/s | 256 kb/s | 192 kb/s | 128 kb/s
//    3. Channels     — Radio box: Stereo | 5.1
// ---------------------------------------------------------------------------
StatusCode AACEncoder::GetEncoderSettings(HostPropertyCollectionRef* p_pValues,
                                          HostListRef*               p_pSettingsList) {
    // Read current values (may be defaults on first open)
    int32_t curSampleRate = 48000;
    int32_t curBitRate    = 320000;
    int32_t curChannels   = 2;

    if (p_pValues) {
        p_pValues->GetINT32(KEY_SAMPLE_RATE, curSampleRate);
        p_pValues->GetINT32(KEY_BIT_RATE,    curBitRate);
        p_pValues->GetINT32(KEY_CHANNELS,    curChannels);
    }

    // ---- 1. Sample Rate -----------------------------------------------
    {
        HostUIConfigEntryRef item(KEY_SAMPLE_RATE);
        const std::vector<std::string> labels  = { "48000 Hz (Standard)", "44100 Hz (CD)" };
        const std::vector<int>         values  = { 48000, 44100 };
        item.MakeRadioBox("Sample Rate", labels, values, curSampleRate);
        item.SetTriggersUpdate(true);
        if (!item.IsSuccess() || !p_pSettingsList->Append(&item)) {
            g_Log(logLevelError, "AAC Plugin :: Failed to render Sample Rate UI");
            return errFail;
        }
    }

    // ---- Separator ----------------------------------------------------
    {
        HostUIConfigEntryRef sep("aac_sep1");
        sep.MakeSeparator();
        if (!sep.IsSuccess() || !p_pSettingsList->Append(&sep)) return errFail;
    }

    // ---- 2. Bit Rate --------------------------------------------------
    {
        HostUIConfigEntryRef item(KEY_BIT_RATE);
        const std::vector<std::string> labels = {
            "320 kb/s  (Recommended — Platforms)",
            "256 kb/s",
            "192 kb/s",
            "128 kb/s"
        };
        const std::vector<int> values = { 320000, 256000, 192000, 128000 };
        item.MakeRadioBox("Audio Bitrate (CBR)", labels, values, curBitRate);
        item.SetTriggersUpdate(true);
        if (!item.IsSuccess() || !p_pSettingsList->Append(&item)) {
            g_Log(logLevelError, "AAC Plugin :: Failed to render Bit Rate UI");
            return errFail;
        }
    }

    // ---- Separator ----------------------------------------------------
    {
        HostUIConfigEntryRef sep("aac_sep2");
        sep.MakeSeparator();
        if (!sep.IsSuccess() || !p_pSettingsList->Append(&sep)) return errFail;
    }

    // ---- 3. Channels --------------------------------------------------
    {
        HostUIConfigEntryRef item(KEY_CHANNELS);
        const std::vector<std::string> labels = { "Stereo (2ch)", "5.1 Surround (6ch)" };
        const std::vector<int>         values = { 2, 6 };
        item.MakeRadioBox("Channels", labels, values, curChannels);
        item.SetTriggersUpdate(true);
        if (!item.IsSuccess() || !p_pSettingsList->Append(&item)) {
            g_Log(logLevelError, "AAC Plugin :: Failed to render Channels UI");
            return errFail;
        }
    }

    return errNone;
}

// ---------------------------------------------------------------------------
//  DoInit  — called right after object construction
// ---------------------------------------------------------------------------
StatusCode AACEncoder::DoInit(HostPropertyCollectionRef* /*p_pProps*/) {
    return errNone;
}

// ---------------------------------------------------------------------------
//  DoOpen  — called when a render job starts; configure the codec here
// ---------------------------------------------------------------------------
StatusCode AACEncoder::DoOpen(HostBufferRef* p_pBuff) {
    // Load user settings from the buffer
    {
        int32_t v = 0;
        if (p_pBuff->GetINT32(KEY_SAMPLE_RATE, v)) settings_.sampleRate = v;
        if (p_pBuff->GetINT32(KEY_BIT_RATE,    v)) settings_.bitRate    = v;
        if (p_pBuff->GetINT32(KEY_CHANNELS,    v)) settings_.channels   = v;
    }

    g_Log(logLevelInfo,
          "AAC Plugin :: Opening encoder — SR=%d Hz, Bitrate=%d bps, Ch=%d",
          settings_.sampleRate, settings_.bitRate, settings_.channels);

    // We do NOT support multi-pass for audio
    constexpr uint8_t isMultiPass = 0;
    p_pBuff->SetProperty(pIOPropMultiPass, propTypeUInt8, &isMultiPass, 1);

    // Open FFmpeg AAC codec context
    const StatusCode err = OpenCodecContext(settings_);
    if (err != errNone) return err;

    // Allocate packet
    pkt_ = av_packet_alloc();
    if (!pkt_) return errAlloc;

    // Pass the ADTS/ADIF extradata (Magic Cookie) back to Resolve
    // so it can embed it in the container (esds box for MP4)
    if (ctx_->extradata && ctx_->extradata_size > 0) {
        p_pBuff->SetProperty(pIOPropMagicCookie, propTypeUInt8,
                             ctx_->extradata, ctx_->extradata_size);
    }
    const uint32_t cookieType = 'mp4a';
    p_pBuff->SetProperty(pIOPropMagicCookieType, propTypeUInt32, &cookieType, 1);

    return errNone;
}

// ---------------------------------------------------------------------------
//  OpenCodecContext  — initialise libavcodec AAC encoder
// ---------------------------------------------------------------------------
StatusCode AACEncoder::OpenCodecContext(const AACSettings& s) {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        g_Log(logLevelError, "AAC Plugin :: libavcodec AAC encoder not found");
        return errNoCodec;
    }

    ctx_ = avcodec_alloc_context3(codec);
    if (!ctx_) {
        g_Log(logLevelError, "AAC Plugin :: Failed to allocate codec context");
        return errAlloc;
    }

    // ---- Codec parameters ----------------------------------------
    ctx_->codec_id        = AV_CODEC_ID_AAC;
    ctx_->codec_type      = AVMEDIA_TYPE_AUDIO;
    ctx_->sample_rate     = s.sampleRate;
    ctx_->bit_rate        = s.bitRate;
    ctx_->sample_fmt      = AV_SAMPLE_FMT_FLTP;   // libavcodec aac expects float planar
    ctx_->flags          |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // Channel layout (FFmpeg 6+ API)
    if (s.channels == 6) {
        AVChannelLayout layout = AV_CHANNEL_LAYOUT_5POINT1;
        av_channel_layout_copy(&ctx_->ch_layout, &layout);
    } else {
        AVChannelLayout layout = AV_CHANNEL_LAYOUT_STEREO;
        av_channel_layout_copy(&ctx_->ch_layout, &layout);
    }

    // ---- Force CBR via -b:a (bit_rate already set; ensure no VBR) ---
    // The built-in aac encoder defaults to ABR/CBR when bit_rate is set.
    // Optionally force with profile for compatibility:
    ctx_->profile = AV_PROFILE_AAC_LOW;  // AAC-LC — universally supported

    // ---- Open --------------------------------------------------------
    const int ret = avcodec_open2(ctx_, codec, nullptr);
    if (ret < 0) {
        g_Log(logLevelError, "AAC Plugin :: avcodec_open2 failed: %s", av_err2str(ret));
        avcodec_free_context(&ctx_);
        ctx_ = nullptr;
        return errFail;
    }

    frameSize_ = ctx_->frame_size;   // typically 1024 for AAC-LC
    if (frameSize_ <= 0) frameSize_ = 1024;

    // ---- Allocate reusable AVFrame ----------------------------------
    frame_ = av_frame_alloc();
    if (!frame_) return errAlloc;

    frame_->nb_samples     = frameSize_;
    frame_->format         = AV_SAMPLE_FMT_FLTP;
    frame_->sample_rate    = s.sampleRate;
    av_channel_layout_copy(&frame_->ch_layout, &ctx_->ch_layout);

    if (av_frame_get_buffer(frame_, 0) < 0) {
        g_Log(logLevelError, "AAC Plugin :: Failed to allocate frame buffer");
        return errAlloc;
    }

    // ---- SwrContext: DR sends int16_t interleaved → float planar ----
    AVChannelLayout srcLayout;
    if (s.channels == 6) {
        AVChannelLayout tmp = AV_CHANNEL_LAYOUT_5POINT1;
        av_channel_layout_copy(&srcLayout, &tmp);
    } else {
        AVChannelLayout tmp = AV_CHANNEL_LAYOUT_STEREO;
        av_channel_layout_copy(&srcLayout, &tmp);
    }

    int swrRet = swr_alloc_set_opts2(
        &swrCtx_,
        &ctx_->ch_layout,    // out layout
        AV_SAMPLE_FMT_FLTP,  // out format
        s.sampleRate,        // out sample rate
        &srcLayout,          // in layout
        AV_SAMPLE_FMT_S16,   // in format (16-bit signed interleaved from DR)
        s.sampleRate,        // in sample rate (same — no resampling needed)
        0, nullptr
    );
    if (swrRet < 0 || !swrCtx_) {
        g_Log(logLevelError, "AAC Plugin :: swr_alloc_set_opts2 failed");
        return errFail;
    }

    swrRet = swr_init(swrCtx_);
    if (swrRet < 0) {
        g_Log(logLevelError, "AAC Plugin :: swr_init failed: %s", av_err2str(swrRet));
        return errFail;
    }

    // Pre-size the accumulation buffer
    accumBuffer_.reserve(static_cast<size_t>(frameSize_) * s.channels * 4);

    g_Log(logLevelInfo,
          "AAC Plugin :: Codec ready — frame_size=%d, SR=%d, ch=%d, br=%d bps",
          frameSize_, s.sampleRate, s.channels, s.bitRate);

    return errNone;
}

// ---------------------------------------------------------------------------
//  DoProcess  — called for each audio buffer delivered by DaVinci Resolve
//  p_pBuff == nullptr means flush (end of stream)
// ---------------------------------------------------------------------------
StatusCode AACEncoder::DoProcess(HostBufferRef* p_pBuff) {
    if (p_pBuff == nullptr || !p_pBuff->IsValid()) {
        return DrainEncoder();
    }

    char*  pBuf   = nullptr;
    size_t bufSize = 0;

    if (!p_pBuff->LockBuffer(&pBuf, &bufSize)) {
        g_Log(logLevelError, "AAC Plugin :: Failed to lock audio buffer");
        return errFail;
    }

    if (!pBuf || bufSize == 0) {
        p_pBuff->UnlockBuffer();
        return errUnsupported;
    }

    // PTS from Resolve
    int64_t pts = 0;
    p_pBuff->GetINT64(pIOPropPTS, pts);

    // Resolve delivers int16_t interleaved PCM
    const int16_t* pcm = reinterpret_cast<const int16_t*>(pBuf);
    const int numSamples = static_cast<int>(bufSize / (sizeof(int16_t) * settings_.channels));

    const StatusCode err = ConvertAndEncode(pcm, numSamples, pts);

    p_pBuff->UnlockBuffer();
    return err;
}

// ---------------------------------------------------------------------------
//  ConvertAndEncode
//  Converts int16 interleaved → float planar, accumulates into frameSize_-
//  aligned chunks, and sends each chunk to the encoder.
// ---------------------------------------------------------------------------
StatusCode AACEncoder::ConvertAndEncode(const int16_t* pcmData,
                                        int            numSamples,
                                        int64_t        pts) {
    // We need float planar intermediate. Convert the whole input block first.
    // Temporary float buffers (per channel, planar)
    const int ch = settings_.channels;
    std::vector<std::vector<float>> planars(ch, std::vector<float>(numSamples));
    std::vector<uint8_t*>           planarPtrs(ch);
    for (int i = 0; i < ch; ++i)
        planarPtrs[i] = reinterpret_cast<uint8_t*>(planars[i].data());

    const uint8_t* inData[1] = { reinterpret_cast<const uint8_t*>(pcmData) };

    const int converted = swr_convert(
        swrCtx_,
        planarPtrs.data(), numSamples,
        inData,            numSamples
    );

    if (converted < 0) {
        g_Log(logLevelError, "AAC Plugin :: swr_convert failed: %s", av_err2str(converted));
        return errFail;
    }

    // Interleave into our accumulation buffer (just for bookkeeping)
    // Actually we process frame by frame directly:

    int processed = 0;
    while (processed < converted) {
        const int avail   = converted - processed;
        const int canFill = frameSize_ - static_cast<int>(accumBuffer_.size() / ch);
        const int toCopy  = std::min(avail, canFill);

        // Append to accumBuffer_ in interleaved-float form
        for (int s = processed; s < processed + toCopy; ++s) {
            for (int c = 0; c < ch; ++c) {
                accumBuffer_.push_back(planars[c][s]);
            }
        }
        processed += toCopy;

        // Once we have a full frame, encode it
        const int accumulated = static_cast<int>(accumBuffer_.size()) / ch;
        if (accumulated >= frameSize_) {
            // Fill AVFrame
            if (av_frame_make_writable(frame_) < 0) return errFail;

            for (int c = 0; c < ch; ++c) {
                float* dst = reinterpret_cast<float*>(frame_->data[c]);
                for (int s = 0; s < frameSize_; ++s) {
                    dst[s] = accumBuffer_[static_cast<size_t>(s * ch + c)];
                }
            }

            frame_->pts = ptsAccum_;
            ptsAccum_ += frameSize_;

            // Remove consumed samples
            accumBuffer_.erase(accumBuffer_.begin(),
                               accumBuffer_.begin() + frameSize_ * ch);

            // Send to encoder
            const int ret = avcodec_send_frame(ctx_, frame_);
            if (ret < 0 && ret != AVERROR(EAGAIN)) {
                g_Log(logLevelError, "AAC Plugin :: avcodec_send_frame: %s", av_err2str(ret));
                return errFail;
            }

            // Drain ready packets
            StatusCode drainErr = DrainReadyPackets();
            if (drainErr != errNone && drainErr != errMoreData)
                return drainErr;
        }
    }

    (void)pts;  // pts from DR is used implicitly through ptsAccum_
    return errMoreData;
}

// ---------------------------------------------------------------------------
//  DrainReadyPackets  — pull all currently available output packets
// ---------------------------------------------------------------------------
StatusCode AACEncoder::DrainReadyPackets() {
    while (true) {
        const int ret = avcodec_receive_packet(ctx_, pkt_);
        if (ret == AVERROR(EAGAIN))  return errMoreData;
        if (ret == AVERROR_EOF)     { av_packet_unref(pkt_); return errNone; }
        if (ret < 0) {
            g_Log(logLevelError, "AAC Plugin :: avcodec_receive_packet: %s", av_err2str(ret));
            av_packet_unref(pkt_);
            return errFail;
        }
        const StatusCode err = SendOutputPacket(pkt_);
        av_packet_unref(pkt_);
        if (err != errNone) return err;
    }
}

// ---------------------------------------------------------------------------
//  DrainEncoder  — flush at end of stream
// ---------------------------------------------------------------------------
StatusCode AACEncoder::DrainEncoder() {
    // Flush any partial frame with silence padding
    if (!accumBuffer_.empty()) {
        const int ch  = settings_.channels;
        const int got = static_cast<int>(accumBuffer_.size()) / ch;

        if (av_frame_make_writable(frame_) < 0) return errFail;

        for (int c = 0; c < ch; ++c) {
            float* dst = reinterpret_cast<float*>(frame_->data[c]);
            for (int s = 0; s < frameSize_; ++s) {
                dst[s] = (s < got) ? accumBuffer_[static_cast<size_t>(s * ch + c)] : 0.0f;
            }
        }
        frame_->pts = ptsAccum_;
        ptsAccum_ += frameSize_;
        accumBuffer_.clear();

        avcodec_send_frame(ctx_, frame_);
        DrainReadyPackets();
    }

    // Signal EOF to encoder
    avcodec_send_frame(ctx_, nullptr);

    // Collect all remaining packets
    while (true) {
        const int ret = avcodec_receive_packet(ctx_, pkt_);
        if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN)) { av_packet_unref(pkt_); break; }
        if (ret < 0) { av_packet_unref(pkt_); break; }
        SendOutputPacket(pkt_);
        av_packet_unref(pkt_);
    }

    return errNone;
}

// ---------------------------------------------------------------------------
//  DoFlush — Resolve calls this at end-of-stream
// ---------------------------------------------------------------------------
void AACEncoder::DoFlush() {
    DrainEncoder();
}

// ---------------------------------------------------------------------------
//  SendOutputPacket — wraps encoded data in a Resolve HostBufferRef and
//  sends it upstream via the callback
// ---------------------------------------------------------------------------
StatusCode AACEncoder::SendOutputPacket(AVPacket* pkt) {
    HostBufferRef outBuf(false);
    if (!outBuf.IsValid() || !outBuf.Resize(pkt->size)) {
        g_Log(logLevelError, "AAC Plugin :: Failed to allocate output buffer");
        return errAlloc;
    }

    char*  outPtr  = nullptr;
    size_t outSize = 0;
    if (!outBuf.LockBuffer(&outPtr, &outSize)) {
        g_Log(logLevelError, "AAC Plugin :: Failed to lock output buffer");
        return errAlloc;
    }

    memcpy(outPtr, pkt->data, pkt->size);

    outBuf.SetProperty(pIOPropPTS,        propTypeInt64, &pkt->pts, 1);
    outBuf.SetProperty(pIOPropDTS,        propTypeInt64, &pkt->dts, 1);

    constexpr uint8_t isKey = 1;          // all audio frames are key frames
    outBuf.SetProperty(pIOPropIsKeyFrame, propTypeUInt8, &isKey, 1);

    m_pCallback->SendOutput(&outBuf);
    return errNone;
}

} // namespace IOPlugin
