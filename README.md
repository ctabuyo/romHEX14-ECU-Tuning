<p align="center">
  <img src="resources/icon.png" width="140" height="140" alt="romHEX14">
</p>

<h1 align="center">romHEX 14 Community Edition</h1>

<p align="center">
  <strong>Professional ECU Calibration Hex Editor</strong><br>
  Open-source, cross-platform desktop application for automotive engineers
</p>

<p align="center">
  <a href="./README.md">English</a> | <a href="./README_zh.md">中文</a>
</p>

<p align="center">
  <img src="https://img.shields.io/github/v/release/ctabuyo/romHEX14-community?include_prereleases&label=version&color=blue" alt="Version">
  <img src="https://img.shields.io/badge/Windows-0078D6?logo=windows&logoColor=white" alt="Windows">
  <img src="https://img.shields.io/badge/macOS-000000?logo=apple&logoColor=white" alt="macOS">
  <img src="https://img.shields.io/badge/Linux-FCC624?logo=linux&logoColor=black" alt="Linux">
  <img src="https://img.shields.io/badge/Qt_6-41cd52?logo=qt&logoColor=white" alt="Qt 6">
  <img src="https://img.shields.io/badge/license-GPL--3.0-blue" alt="GPLv3">
</p>

<p align="center">
  <i>Professional ECU tuning software with A2L/HEX/OLS support, AI-powered tools, and cross-platform workflows.</i>
</p>

<p align="center">
  <a href="https://github.com/ctabuyo/romHEX14-community/releases"><strong>Download Latest Release</strong></a>  ·  
  <a href="https://gitee.com/liuyanpeng14/romHEX14-community">Gitee Mirror (中国镜像)</a>
</p>

---

## Features

### A2L, HEX & OLS Engine

- Full **ASAP2 (A2L) parser** with characteristic, axis, and measurement support
- HEX/BIN/S19 file loading with automatic format detection
- **OLS project import** with ROM extraction and map definitions
- **KP map pack import** with offset auto-detection
- Native CBOR project format with BLAKE3 integrity checksums

### AI Assistant — Bring Your Own Key

- AI-powered ECU calibration assistant (Claude, GPT-4o, Qwen, DeepSeek, Gemini, Groq, Ollama, LM Studio)
- Intelligent map identification and explanation
- Natural language queries about your calibration data
- Works with local models — no data leaves your machine

### Map Editor

- Interactive **2D/3D map visualization** with perceptual heat gradient
- Inline editing with real-time waveform preview
- 3D surface simulation view
- Map pack import/export for sharing calibrations
- Patch creation and management (`.rxpatch`)

### Project Management

- Multi-file projects with linked ROMs
- Project registry for quick access
- Auto-save with crash recovery
- Full undo/redo history

### Multi-Language

- English, Chinese (简体中文), Spanish (Español), Thai (ไทย)
- Adaptive CJK toolbar icons

---

## Supported ECUs

| Manufacturer | ECU Families |
|---|---|
| **Bosch** | MED17, MG1, MD1, EDC17, EDC16, EDC15, ME7, ME9, MED9, MSV, MSD |
| **Continental** | SIMOS 12/16/18/19/22, SID, SCG, SCM |
| **Delphi** | DCM3.x, DCM6.x, DCU-10x |
| **Denso** | Multiple generations |
| **Magneti Marelli** | MJD, 7GV/8GMK |
| **Valeo** | VD46 |

---

## System Requirements

| | Minimum | Recommended |
|---|---|---|
| **OS** | Windows 10 / macOS 12 / Ubuntu 22.04 | Windows 11 / macOS 14 / Latest LTS |
| **RAM** | 4 GB | 8 GB |
| **Disk** | 500 MB | 1 GB |
| **Display** | 1280 × 720 | 1920 × 1080 |

---

## Building from Source

### Prerequisites

| Dependency | Minimum Version |
|---|---|
| CMake | 3.16 |
| Qt | 6.5 (Core, Gui, Widgets, Concurrent, Network, LinguistTools) |
| C++ compiler | C++17 required |
| zlib | Any |
| Qt Advanced Docking System | 5.0.0 — fetched automatically by CMake |

### Workspace dependency

romHEX uses [Qt Advanced Docking System](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System)
for tabbed, split, floating, and re-dockable Hex and map-editor windows. CMake
fetches the exact ADS 5.0.0 release commit on the first configure; no submodule
checkout is required.

### Quick Build

```bash
git clone https://github.com/ctabuyo/romHEX14-community.git
cd romHEX14-community
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/qt6
cmake --build . --parallel
```

<details>
<summary><strong>macOS</strong></summary>

```bash
cmake .. -DCMAKE_PREFIX_PATH=~/Qt/6.8.3/macos
make -j8
open rx14.app
```
</details>

<details>
<summary><strong>Windows (MinGW)</strong></summary>

```bash
cmake -G "MinGW Makefiles" .. -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64
mingw32-make -j8
```
</details>

<details>
<summary><strong>Linux (Ubuntu/Debian)</strong></summary>

```bash
sudo apt install qt6-base-dev qt6-tools-dev qt6-l10n-tools libgl-dev zlib1g-dev
cmake ..
make -j$(nproc)
./rx14
```
</details>

---

## Project Structure

```
romHEX14-community/
├── src/                 Core application source (C++17 / Qt6)
│   ├── io/ols/         OLS format parser (import)
│   └── stubs/          Community build type stubs
├── data/                Vehicle & ECU database (JSON)
├── resources/           Icons, stylesheets, Qt resources
├── translations/        Qt Linguist translation files
├── third_party/blake3/  BLAKE3 hash library (portable C)
├── build/_deps/         CMake-fetched build dependencies (generated)
├── docs/                Documentation source
└── CMakeLists.txt       Build configuration
```

---

## Repository Mirrors

| Platform | URL | Notes |
|---|---|---|
| **GitHub** | [github.com/ctabuyo/romHEX14-community](https://github.com/ctabuyo/romHEX14-community) | Primary — PRs and Issues welcome |
| **Gitee** | [gitee.com/ctabuyo/romHEX14-community](https://gitee.com/liuyanpeng14/romHEX14-community) | 中国镜像 — Issues also monitored |

China users (中国用户): Gitee 镜像自动同步，建议使用 Gitee 获取更快下载速度。

---

## Contributing

We welcome contributions! See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on:
- Pull request workflow
- Code style (C++17, Qt6 conventions)
- Issue reporting (GitHub or Gitee)

---

## License

```
romHEX 14 Community Edition
Copyright (C) 2026 Cristian Tabuyo <contact@romhex14.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
```

Built with the [Qt Framework](https://www.qt.io/) (LGPL). See [LICENSE](LICENSE) for full text.
