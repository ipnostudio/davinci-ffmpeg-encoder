# FFmpeg Encoder Plugin for DaVinci Resolve Studio
### Windows Edition — with AAC Audio + CBR Video

A fork of [ffmpeg_encoder_plugin](https://github.com/EdvinNilsson/ffmpeg_encoder_plugin) 
by [Edvin Nilsson](https://github.com/EdvinNilsson), extended with high-quality AAC audio 
encoding and advanced video CBR controls for content delivery platforms.

---

## ✨ What's new in this fork

### 🔊 AAC Audio via libfdk_aac
| Parameter | Options |
|-----------|---------|
| Encoder | libfdk_aac (Fraunhofer — industry standard) |
| Bitrate | 128 / 192 / 256 / 320 kb/s (adjustable) |
| Sample Rate | 48000 Hz / 44100 Hz |
| Profile | AAC-LC (universal compatibility) |

### 🎬 Advanced Video Controls (H.264 x264 / NVENC)
| Control | Options |
|---------|---------|
| Quality Mode | CRF / CQP / VBR / **CBR** |
| Encoder Profile | Auto / Baseline / Main / High |
| Encoder Level | Auto / 4.1 / 4.2 / 5.0 / 5.1 / 5.2 |
| Keyframe Interval | 1–300 frames (default: 30) |
| B-frames (CBR) | 2 consecutive (platform standard) |
| GOP | Closed GOP in CBR mode |
| CABAC | Enabled in CBR mode |

> **Why CBR?**  
> YouTube, Instagram, TikTok and most streaming platforms require or strongly recommend 
> CBR (Constant Bit Rate) for uploads. Variable rate modes often trigger aggressive 
> re-encoding that degrades quality significantly.

---

## 📋 Requirements

- **DaVinci Resolve Studio** (paid version — free version does not support IO plugins)
- **Windows 10/11** (this build targets Win64)
- NVIDIA GPU required for NVENC encoders

---

## 🚀 Installation (Windows)

1. Download `ffmpeg_encoder_plugin_windows.zip` from [Releases](../../releases)
2. Extract the zip file
3. Copy the folder `ffmpeg_encoder_plugin.dvcp.bundle` to:
4. C:\ProgramData\Blackmagic Design\DaVinci Resolve\Support\IOPlugins\
5. 4. Restart DaVinci Resolve Studio
5. Go to **Deliver** page → select your format → the new encoders appear automatically

---

## 🎬 Usage in DaVinci Resolve

### Video CBR (recommended for platforms)
1. Deliver page → Format: `MP4`
2. Video tab → Codec: `H.264` → Encoder: `x264 8-bit 4:2:0 (FFmpeg)`
3. Quality Control: `Constant Bit Rate (CBR)`
4. Set Bit Rate, Profile (`High`), Level (`4.1` for 1080p)
5. Keyframe Interval: `30` frames

### Audio AAC 320kb/s
1. Audio tab → Codec: `AAC 320kb/s (FFmpeg)`
2. Bit Rate: `320` kbps

---

## 📄 Credits & Licenses

- Original plugin by [Edvin Nilsson](https://github.com/EdvinNilsson/ffmpeg_encoder_plugin) — GPL-3.0
- Audio encoding: [libfdk_aac](https://github.com/mstorsjo/fdk-aac) — Fraunhofer FDK AAC License
- Video encoding: [FFmpeg](https://ffmpeg.org/) — LGPL/GPL
- This fork is released under **GPL-3.0**

> ⚠️ libfdk_aac has its own license that restricts commercial distribution.  
> This plugin is free for personal and non-commercial use.

---

DaVinci Resolve is a trademark of Blackmagic Design Pty. Ltd.
