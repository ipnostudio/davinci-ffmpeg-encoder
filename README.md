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
