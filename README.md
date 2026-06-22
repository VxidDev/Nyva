# Nyva

Nyva is a lightweight, modern video editor being built from scratch using C++ and Qt.

The goal of Nyva is to explore how professional video editing software works under the hood — including timeline systems, media decoding, and real-time rendering.

---

## Project Status

Nyva is currently in early development, but already includes a working experimental playback pipeline.

Current features:
- Qt-based multi-page UI (start screen + editor view)
- File picker (video import)
- FFmpeg-based demuxing (libavformat)
- Hardware-independent video decoding (libavcodec)
- Multi-threaded architecture (demux, video decode, audio decode)
- Frame queue-based playback system
- Basic synchronized video rendering (PTS-based timing)
- Experimental audio decoding pipeline (in progress)

---

## Planned features

- Full timeline editing system (non-linear editing)
- Multi-track video/audio support
- Clip trimming and splitting
- Effects and transitions system
- GPU-accelerated rendering (OpenGL / Vulkan)
- Audio playback synchronization (real-time output)
- Scrubbing and frame seeking
- Export pipeline (FFmpeg encoding)

---

## Tech Stack

- C++
- Qt 6 (Widgets)
- FFmpeg (libavformat, libavcodec, libswresample)
- CMake
- OpenGL / Vulkan (planned for rendering)

---

## Build Instructions

### Requirements
- CMake 3.16+
- Qt 6 (Core, Gui, Widgets)
- FFmpeg development libraries
- C++17 compatible compiler

### Build

```bash
cmake -S . -B build
cmake --build build