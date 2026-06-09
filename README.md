FFmpeg Encoder Plugin for DaVinci Resolve Studio
Windows Edition — Advanced AAC Audio + CBR Video
A fork of ffmpeg_encoder_plugin by Edvin Nilsson, extended with a corrected native AAC encoder, advanced CBR video controls, and expanded codec support up to 8K.

What's New in This Fork
AAC Audio — Native Encoder (FFmpeg aac)
The AAC encoder has been fully reworked. It now uses FFmpeg's built-in aac encoder with correct AV_PROFILE_AAC_LOW support across multiple FFmpeg versions, proper ASC and esds cookie generation for accurate stream identification, and no third-party license restrictions.

libfdk_aac was removed due to its licensing restrictions. The native FFmpeg encoder now handles all AAC encoding with full compatibility.

ParameterOptionsEncoderFFmpeg native aacBitrate128 / 192 / 256 / 320 kb/sSample Rate48000 Hz / 44100 HzProfileAAC-LC (AV_PROFILE_AAC_LOW)
Advanced Video Controls — H.264, H.265, AV1
The video encoder has been significantly expanded with per-encoder UI flags and broader format support.
ControlOptionsQuality ModeCRF / CQP / VBR / CBREncoder ProfileH.264: Auto / Baseline / Main / High — H.265: Main / Main10 / and othersEncoder LevelAuto / 4.1 / 4.2 / 5.0 / 5.1 / 5.2 / up to 6.1 (8K)Keyframe Interval1–300 frames (default: 30)B-frames (CBR)2 consecutive (platform standard)GOPClosed GOP in CBR mode — GOP control also available for AV1 encodersCABACEnabled in CBR mode
Profile, Level, and GOP controls are shown or hidden per encoder via a UI flags system — only the options relevant to the selected encoder are displayed.

Why CBR?
YouTube, Instagram, TikTok and most streaming platforms require or strongly recommend CBR for uploads. Variable rate modes often trigger aggressive re-encoding on ingest, which can significantly degrade the final quality. Using CBR gives you a predictable, platform-friendly output straight from Resolve.

Metadata Monitoring Script
A companion script is included for automatic metadata correction. It monitors encoded files and patches container metadata when needed, useful for long-form or batch export workflows.

Requirements

DaVinci Resolve Studio (paid version — the free version does not support IO plugins)
Windows 10 or 11 (Win64 build)
NVIDIA GPU required for NVENC encoders


Installation

Download ffmpeg_encoder_plugin_windows.zip from Releases
Extract the zip file
Copy the folder ffmpeg_encoder_plugin.dvcp.bundle into:

C:\ProgramData\Blackmagic Design\DaVinci Resolve\Support\IOPlugins\

Restart DaVinci Resolve Studio
Open the Deliver page, select your format — the new encoders will appear automatically in the codec list


Usage in DaVinci Resolve
Video — CBR (recommended for platform delivery)

Go to the Deliver page and set Format to MP4
Under the Video tab, set Codec to H.264 and Encoder to x264 8-bit 4:2:0 (FFmpeg)
Set Quality Control to Constant Bit Rate (CBR)
Set your target Bit Rate, Profile (High), and Level (4.1 for 1080p / 5.1 for 4K)
Set Keyframe Interval to 30 frames

Audio — AAC

Under the Audio tab, set Codec to AAC (FFmpeg)
Select your target Bit Rate (128 / 192 / 256 / 320 kb/s)


Credits & Licenses

Original plugin by Edvin Nilsson — GPL-3.0
Video & audio encoding: FFmpeg — LGPL / GPL
This fork is released under GPL-3.0

DaVinci Resolve is a trademark of Blackmagic Design Pty. Ltd.
