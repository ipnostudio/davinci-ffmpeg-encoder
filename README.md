# FFmpeg Encoder Plugin for DaVinci Resolve Studio
### Windows Edition — Advanced AAC Audio + CBR Video

A fork of [ffmpeg_encoder_plugin](https://github.com/EdvinNilsson/ffmpeg_encoder_plugin) by [Edvin Nilsson](https://github.com/EdvinNilsson), extended with a corrected native AAC encoder, advanced CBR video controls, and expanded codec support up to 8K.

---

## What's New in This Fork

### AAC Audio — Native Encoder (FFmpeg `aac`)

The AAC encoder has been fully reworked. It now uses FFmpeg's built-in `aac` encoder with correct `AV_PROFILE_AAC_LOW` support across multiple FFmpeg versions, proper ASC and `esds` cookie generation for accurate stream identification, and no third-party license restrictions.

> `libfdk_aac` was removed due to its licensing restrictions. The native FFmpeg encoder now handles all AAC encoding with full compatibility.

| Parameter | Options |
|-----------|---------|
| Encoder | FFmpeg native `aac` |
| Bitrate | 128 / 192 / 256 / 320 kb/s |
| Sample Rate | 48000 Hz / 44100 Hz |
| Profile | AAC-LC (`AV_PROFILE_AAC_LOW`) |

### Advanced Video Controls — H.264, H.265, AV1

The video encoder has been significantly expanded with per-encoder UI flags and broader format support.

| Control | Options |
|---------|---------|
| Quality Mode | CRF / CQP / VBR / **CBR** |
| Encoder Profile | H.264: Auto / Baseline / Main / High — H.265: Main / Main10 / and others |
| Encoder Level | Auto / 4.1 / 4.2 / 5.0 / 5.1 / 5.2 / up to **6.1** (8K) |
| Keyframe Interval | 1–300 frames (default: 30) |
| B-frames (CBR) | 2 consecutive (platform standard) |
| GOP | Closed GOP in CBR mode — GOP control also available for **AV1** encoders |
| CABAC | Enabled in CBR mode |

Profile, Level, and GOP controls are shown or hidden per encoder via a UI flags system — only the options relevant to the selected encoder are displayed.

> **Why CBR?**
> YouTube, Instagram, TikTok and most streaming platforms require or strongly recommend CBR for uploads. Variable rate modes often trigger aggressive re-encoding on ingest, which can significantly degrade the final quality. Using CBR gives you a predictable, platform-friendly output straight from Resolve.

### Metadata Monitoring Script

A companion script is included for automatic metadata correction. It monitors encoded files and patches container metadata when needed, useful for long-form or batch export workflows.

---

## Requirements

- **DaVinci Resolve Studio** (paid version — the free version does not support IO plugins)
- **Windows 10 or 11** (Win64 build)
- **NVIDIA GPU** required for NVENC encoders

---

## Installation

1. Download `ffmpeg_encoder_plugin_windows.zip` from [Releases](../../releases)
2. Extract the zip file
3. Copy the folder `ffmpeg_encoder_plugin.dvcp.bundle` into:

```
C:\ProgramData\Blackmagic Design\DaVinci Resolve\Support\IOPlugins\
```

4. Restart DaVinci Resolve Studio
5. Open the **Deliver** page, select your format — the new encoders will appear automatically in the codec list

---

## Usage in DaVinci Resolve

### Video — CBR (recommended for platform delivery)

1. Go to the **Deliver** page and set Format to `MP4`
2. Under the **Video** tab, set Codec to `H.264` and Encoder to `x264 8-bit 4:2:0 (FFmpeg)`
3. Set Quality Control to `Constant Bit Rate (CBR)`
4. Set your target Bit Rate, Profile (`High`), and Level (`4.1` for 1080p / `5.1` for 4K)
5. Set Keyframe Interval to `30` frames

### Audio — AAC

1. Under the **Audio** tab, set Codec to `AAC (FFmpeg)`
2. Select your target Bit Rate (128 / 192 / 256 / 320 kb/s)

---

## Credits & Licenses

- Original plugin by [Edvin Nilsson](https://github.com/EdvinNilsson/ffmpeg_encoder_plugin) — [GPL-3.0](https://www.gnu.org/licenses/gpl-3.0.html)
- Video & audio encoding: [FFmpeg](https://ffmpeg.org/) — LGPL / GPL
- This fork is released under **GPL-3.0**
