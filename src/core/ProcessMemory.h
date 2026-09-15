#pragma once
#include <windows.h>
#include <cstdint>
#include <optional>
#include <string>
#include <tlhelp32.h>
#include <vector>

class ProcessMemory
{
public:
  ~ProcessMemory() { Close(); }

  bool AttachByName(const std::wstring& exeName)
  {
    DWORD pid = FindPidByName(exeName);
    if (pid == 0)
      return false;
    return AttachByPid(pid);
  }

  bool AttachByPid(DWORD pid)
  {
    Close();
    m_pid    = pid;
    m_handle = OpenProcess(
      PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD,
      FALSE, pid
    );
    return m_handle != nullptr;
  }

  void Close()
  {
    if (m_handle) {
      CloseHandle(m_handle);
      m_handle = nullptr;
    }
    m_pid = 0;
  }

  bool IsAttached() const { return m_handle != nullptr; }

  // Returns list of committed, readable regions (analogous to CE's "+W-C" region walk).
  std::vector<MEMORY_BASIC_INFORMATION> EnumerateRegions(bool requireWritable = true) const
  {
    std::vector<MEMORY_BASIC_INFORMATION> regions;
    uintptr_t                             addr = 0;
    MEMORY_BASIC_INFORMATION              mbi{};
    while (VirtualQueryEx(m_handle, (LPCVOID) addr, &mbi, sizeof(mbi))) {
      bool committed = mbi.State == MEM_COMMIT;
      bool readable =
        (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) != 0;
      bool notGuard = (mbi.Protect & PAGE_GUARD) == 0;
      bool notCow   = (mbi.Protect & (PAGE_WRITECOPY | PAGE_EXECUTE_WRITECOPY)) == 0;
      bool writable = (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) != 0;
      if (committed && readable && notGuard && notCow && (!requireWritable || writable)) {
        regions.push_back(mbi);
      }
      uintptr_t next = (uintptr_t) mbi.BaseAddress + mbi.RegionSize;
      if (next <= addr)
        break;  // overflow guard
      addr = next;
    }
    return regions;
  }

  // ---------------------------------------------------------------------------
  // Raw byte read / write
  // ---------------------------------------------------------------------------
  bool ReadBytes(uintptr_t addr, void* out, size_t size) const
  {
    SIZE_T bytesRead = 0;
    return ReadProcessMemory(m_handle, (LPCVOID) addr, out, size, &bytesRead) && bytesRead == size;
  }

  bool WriteBytes(uintptr_t addr, const void* data, size_t size) const
  {
    SIZE_T bytesWritten = 0;
    return WriteProcessMemory(m_handle, (LPVOID) addr, data, size, &bytesWritten) && bytesWritten == size;
  }

  // ---------------------------------------------------------------------------
  // Pointer
  // ---------------------------------------------------------------------------
  std::optional<uintptr_t> ReadPointer(uintptr_t addr) const
  {
    uintptr_t v = 0;
    if (!ReadBytes(addr, &v, sizeof(v)))
      return std::nullopt;
    return v;
  }

  // ---------------------------------------------------------------------------
  // Int32
  // ---------------------------------------------------------------------------
  std::optional<int32_t> ReadInt32(uintptr_t addr) const
  {
    int32_t v = 0;
    if (!ReadBytes(addr, &v, sizeof(v)))
      return std::nullopt;
    return v;
  }

  bool WriteInt32(uintptr_t addr, int32_t value) const { return WriteBytes(addr, &value, sizeof(value)); }

  // ---------------------------------------------------------------------------
  // Int64 (for currency Quantity which is stored as C# long)
  // ---------------------------------------------------------------------------
  std::optional<int64_t> ReadInt64(uintptr_t addr) const
  {
    int64_t v = 0;
    if (!ReadBytes(addr, &v, sizeof(v)))
      return std::nullopt;
    return v;
  }

  bool WriteInt64(uintptr_t addr, int64_t value) const { return WriteBytes(addr, &value, sizeof(value)); }

  // ---------------------------------------------------------------------------
  // Float
  // ---------------------------------------------------------------------------
  std::optional<float> ReadFloat(uintptr_t addr) const
  {
    float v = 0;
    if (!ReadBytes(addr, &v, sizeof(v)))
      return std::nullopt;
    return v;
  }

  bool WriteFloat(uintptr_t addr, float value) const { return WriteBytes(addr, &value, sizeof(value)); }

  // ---------------------------------------------------------------------------
  // Double (for HeroExp which is stored as C# double)
  // ---------------------------------------------------------------------------
  std::optional<double> ReadDouble(uintptr_t addr) const
  {
    double v = 0;
    if (!ReadBytes(addr, &v, sizeof(v)))
      return std::nullopt;
    return v;
  }

  bool WriteDouble(uintptr_t addr, double value) const { return WriteBytes(addr, &value, sizeof(value)); }

  // ---------------------------------------------------------------------------
  // Bool
  // ---------------------------------------------------------------------------
  std::optional<bool> ReadBool(uintptr_t addr) const
  {
    uint8_t v = 0;
    if (!ReadBytes(addr, &v, sizeof(v)))
      return std::nullopt;
    return v != 0;
  }

  bool WriteBool(uintptr_t addr, bool value) const
  {
    uint8_t v = value ? 1 : 0;
    return WriteBytes(addr, &v, sizeof(v));
  }

  // ---------------------------------------------------------------------------
  // UInt16
  // ---------------------------------------------------------------------------
  std::optional<uint16_t> ReadUInt16(uintptr_t addr) const
  {
    uint16_t v = 0;
    if (!ReadBytes(addr, &v, sizeof(v)))
      return std::nullopt;
    return v;
  }

  // Best-effort UTF-16 string read (mirrors the Lua script's manual char loop).
  std::wstring ReadUtf16(uintptr_t addr, size_t maxChars) const
  {
    std::wstring result;
    for (size_t i = 0; i < maxChars; i++) {
      auto ch = ReadUInt16(addr + i * 2);
      if (!ch || *ch == 0)
        break;
      if (*ch < 32 || *ch > 126)
        continue;  // keep it printable-ascii-ish, same filter as the script
      result.push_back((wchar_t) *ch);
    }
    return result;
  }

  HANDLE Handle() const { return m_handle; }
  DWORD Pid() const { return m_pid; }

  // Base address of the main module (useful for RVA-relative pattern hits).
  std::optional<uintptr_t> MainModuleBase() const
  {
    MODULEENTRY32W me{};
    me.dwSize   = sizeof(me);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, m_pid);
    if (snap == INVALID_HANDLE_VALUE)
      return std::nullopt;
    std::optional<uintptr_t> base;
    if (Module32FirstW(snap, &me)) {
      base = (uintptr_t) me.modBaseAddr;
    }
    CloseHandle(snap);
    return base;
  }

  // Find a specific module's base address and size by name.
  struct ModuleInfo
  {
    uintptr_t base = 0;
    size_t    size = 0;
  };

  std::optional<ModuleInfo> FindModule(const std::wstring& moduleName) const
  {
    MODULEENTRY32W me{};
    me.dwSize   = sizeof(me);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, m_pid);
    if (snap == INVALID_HANDLE_VALUE)
      return std::nullopt;

    std::optional<ModuleInfo> result;
    if (Module32FirstW(snap, &me)) {
      do {
        if (_wcsicmp(me.szModule, moduleName.c_str()) == 0) {
          result = ModuleInfo{(uintptr_t) me.modBaseAddr, me.modBaseSize};
          break;
        }
      } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return result;
  }

private:
  static DWORD FindPidByName(const std::wstring& exeName)
  {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
      return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
      do {
        if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0) {
          pid = pe.th32ProcessID;
          break;
        }
      } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
  }

  HANDLE m_handle = nullptr;
  DWORD  m_pid    = 0;
};
