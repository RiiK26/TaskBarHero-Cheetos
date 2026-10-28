<div align="center">

# TaskBarHero - Cheetos

[![Release Status](https://github.com/RiiK26/TaskBarHero-Cheetos/actions/workflows/release.yml/badge.svg)](https://github.com/RiiK26/TaskBarHero-Cheetos/actions)
[![Cheetos Version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2FRiiK26%2FTaskBarHero-Cheetos%2Fmain%2Fconfig.json&query=%24%5B%27cheetos-version%27%5D&label=Version&color=blue)](#)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20-green)](#)
[![Game Version](https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fraw.githubusercontent.com%2FRiiK26%2FTaskBarHero-Cheetos%2Fmain%2Fconfig.json&query=%24%5B%27game-version%27%5D&label=Game%20Version&color=orange)](#)

An internal memory manipulation, feature modification, and offset management utility for **TaskBarHero**.

</div>

---

## Overview

**TaskBarHero - Cheetos** directly interacts with the game's memory using the Win32 API and IL2CPP metadata to provide a comprehensive suite of enhancements. Everything is controlled from a single lightweight, blazing-fast GUI built with **Dear ImGui**, **GLFW**, and **OpenGL3**.

The project uses a self-contained vendor dependency model (no package manager required) and an AOB signature engine with centralized struct offset management.

<details>
<summary>Preview image</summary>

![Preview image](./resources/images/preview.png)

</details>

---

## Features

| Feature | Description | Status | Type |
|---|---|:---:|---|
| **Multi-Tier Anti-Cheat Bypass** | Neutralizes CodeStage ACTk detectors, watchdogs, and abuse guards via 15 dynamic AOB signatures. | ✅ | Automatic |
| **God Mode** | Real-time injection of Max HP, Attack Speed, Crit Chance, Crit Damage, Armor, Movement Speed, and Cast Speed across all heroes. | ✅ | Repeatable |
| **Rune Unlocker** | Safely discovers, unlocks, and upgrades all runes to maximum levels without corrupting dictionary structures. | ✅ | Interactive |
| **Speedhack** | High-precision engine delta time scaler injected via MinHook DLL, strictly caller-scoped to `UnityPlayer.dll`. | ✅ | Toggle |
| **EXP Multiplier** | Real-time hero EXP rate modification using decrypted ACTk primitives. | ✅ | Interactive |

### Architecture & Technical Capabilities
- **AOB Signatures:** Centralized C++ registry of 22 unique function signatures in `Signatures.h` with masked RIP-relative displacements and branch offsets.
- **Centralized Struct Offsets:** Single source of truth for all IL2CPP container headers (`List`, `Array`, `Dictionary`, `String`) and game class fields in `Il2CppOffsets.h`.
- **Zero Package Manager Dependencies:** Completely self-contained within `src/vendor/` (`MinHook`, `Dear ImGui`, `GLFW`). No `vcpkg` required.
- **Unified 1-Command Update Pipeline:** Automatically scans Steam build updates, checks signatures, validates struct offsets, and compiles binaries via `./scripts/update`.
- **Wine / Proton Native Compatibility:** Isolated runtime configuration (`imgui.ini` is strictly stored alongside binaries in `build/`).

---

## Getting Started

### Option A: Download Pre-compiled Release (Recommended)
1. Navigate to the **[Releases](../../releases)** page.
2. Download the latest `TBH-Cheetos-Release.zip`.
3. Extract the ZIP file to a folder on your computer.

### Option B: Build from Source
<details>
<summary>Click here for build instructions</summary>

#### Prerequisites
- [CMake](https://cmake.org/) (v3.20 or higher)
- **Windows:** Visual Studio 2022 (MSVC) or MinGW-w64
- **Linux:** `x86_64-w64-mingw32-g++` (for cross-compiling to Windows `.exe`)

#### 1. Clone the repository
```bash
git clone https://github.com/RiiK26/TaskBarHero-Cheetos.git
cd TaskBarHero-Cheetos
```

#### 2. Linux (Cross-compiling via MinGW)
```bash
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-toolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

#### 3. Windows (MSVC or Native MinGW)
```powershell
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
</details>

---

## Usage

Run the tool while **TaskBarHero** is actively running.

### Using Pre-compiled Release
- **Windows:** Run `.\launch.bat` *(Run as Administrator)*
- **Linux / Steam Deck:** Run `./launch_linux.sh` *(Requires `protontricks`)*

### Using Source Build
- **Windows:** `.\scripts\launch.bat`
- **Linux / Steam Deck:** `./scripts/launch_linux.sh`

---

## Automated Game Update Pipeline

When *TaskBarHero* receives an update on Steam, update the offsets and signatures with a single command:

```bash
# Routine update: checks Steam build ID, scans 22 AOB signatures, and rebuilds
./scripts/update

# Dry run status check (no modifications)
./scripts/update --check

# Force update and synchronize all struct offsets against dump.cs
./scripts/update --force --sync-fields
```

For advanced update runbooks, see the [Update Workflow Documentation](.agents/workflows/update.md).

---

## Support & Contributing

Suggestions and contributions are always welcome! If you encounter any issues or have ideas for enhancements, please open an issue:

- **[Report a Bug](../../issues/new?template=bug-report.yml)**
- **[Request a Feature](../../issues/new?template=feature-request.yml)**
- **[Ask a Question](../../discussions)**

## License
This project licensed under [MIT License](LICENSE).
