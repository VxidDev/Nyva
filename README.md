# Nyva

Nyva is a lightweight, modern video editor being built from scratch using C++ and Qt.

The goal of Nyva is to explore how professional video editing software works under the hood - including timeline systems, media decoding, and real-time rendering.

---

## Project Status

Nyva is currently in early development.

Current features:
- Basic Qt application window
- File picker (video import)
- Simple UI prototype

Planned features:
- Video preview playback
- Timeline editing system
- Multi-track support
- Effects and transitions
- FFmpeg-based decoding/encoding
- GPU-accelerated rendering

---

## Tech Stack

- C++
- Qt 6 (Widgets)
- CMake
- FFmpeg (planned)
- OpenGL / Vulkan (planned for rendering)

---

## Build Instructions

### Requirements
- CMake 3.16+
- Qt 6 (Core, Gui, Widgets)
- C++17 compatible compiler

### Build

```bash
cmake -S . -B build
cmake --build build
````

### Run

```bash
./build/nyva
```

---

## Project Structure

```text
Nyva/
├── src/            # Application source code
├── include/        # Headers
├── CMakeLists.txt
└── README.md
```

---

## Status Disclaimer

This project is experimental and under active development.
Expect frequent changes and incomplete features.

---

## 📜 License

GPL v3.0