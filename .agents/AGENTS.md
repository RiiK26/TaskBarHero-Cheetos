# Project: TaskBarHero-Cheetos

## Project Overview
C++ memory reading and offset management utility for IL2Cpp game structures. It is an internal tool interacting
with the game's memory using the Win32 API.

## Tech Stack
- C++ (Win32 API)
- CMake
- vcpkg for package management
- ImGui + OpenGL3/GLFW for UI menu

## Code Conventions
- Features are placed in `src/features/` (e.g., `AntiCheatBypass`, `ExpMultiplier`, `GodMode`).
- Core memory management and offset definitions are in `src/core/`.
- Use the `ProcessMemory` utilities (from `src/core/ProcessMemory.h`) rather than raw Win32 API calls where applicable.
- Handle IL2CPP collections using `Il2CppListReader.h` and `Il2CppDictionaryReader.h`.
- Deal with in-game obscured values using `ObscuredTypes.h`.

## Core Offsets & Reverse Engineering Boundaries
- Offsets and structure layouts MUST be validated against `resources/dump/dump.cs`. (ignored)
- Struct definitions in `src/core/Il2CppOffsets.h` map directly to IL2Cpp TypeDefs from `dump.cs`.
- Keep offset comments updated with original field tags and source references.
- Verify game versions before writing new features or modifying offsets.

## Patterns
- Look at `src/features/ExpMultiplier.cpp` as an example of a typical feature implementation.
- Look at `src/core/Il2CppOffsets.h` for how IL2CPP structures are mapped.
