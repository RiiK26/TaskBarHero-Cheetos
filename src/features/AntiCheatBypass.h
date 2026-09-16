#pragma once
#include "../core/ProcessMemory.h"

#include <cstdint>
#include <string>

class AntiCheatBypass
{
public:
  explicit AntiCheatBypass(ProcessMemory& mem) :
      m_mem(mem)
  {
  }

  bool IsActive() const { return m_active; }

  void Toggle() { m_active = !m_active; }
  void Enable() { m_active = true; }
  void Disable() { m_active = false; }

  // Apply the bypass by patching detector methods
  std::string Apply()
  {
    if (!m_active)
      return "";

    // Find GameAssembly.dll
    auto modInfo = m_mem.FindModule(L"GameAssembly.dll");
    if (!modInfo)
      return "[AntiCheat] GameAssembly.dll not found. Is the game running?";

    uintptr_t gaBase       = modInfo->base;
    m_gameAssemblyBase     = gaBase;

    int         patchCount = 0;
    std::string details;

    // Patch each detector's key method(s) with RET (0xC3)
    // This prevents the detector from executing its detection logic.
    struct PatchTarget
    {
      const char* name;
      uintptr_t   rva;
    };

    static const PatchTarget targets[] = {
      // Direct Obscured Types Bypass (Bypass op_Implicit completely Causing Crash or atleast broken UI)
      //{"ObscuredInt.op_Implicit",               0x731C80}, // @RVA[ObscuredInt.op_Implicit] [UNCHANGED]
      //{"ObscuredFloat.op_Implicit",             0x72FAF0}, // @RVA[ObscuredFloat.op_Implicit] [UNCHANGED]
      //{"ObscuredDouble.op_Implicit",            0x72ED00}, // @RVA[ObscuredDouble.op_Implicit] [UNCHANGED]
      //{"ObscuredLong.op_Implicit",              0x732630}, // @RVA[ObscuredLong.op_Implicit] [UNCHANGED]

      // Detector Core Methods (Override of ACTkDetectorBase StartDetection/Update)
      {"InjectionDetector.DetectorCore",        0x741510}, // @RVA[InjectionDetector.DetectorCore] [CHANGED]
      {"SpeedHackDetector.DetectorCore",        0x7473E0}, // @RVA[SpeedHackDetector.DetectorCore] [CHANGED]
      {"TimeCheatingDetector.DetectorCore",     0x748D70}, // @RVA[TimeCheatingDetector.DetectorCore] [CHANGED]
      {"ObscuredCheatingDetector.DetectorCore", 0x741900}, // @RVA[ObscuredCheatingDetector.DetectorCore] [CHANGED]
      {"WallHackDetector.DetectorCore",         0x74F870}, // @RVA[WallHackDetector.DetectorCore] [CHANGED]

      // Additional Unity Lifecycle Methods used by detectors
      {"SpeedHackDetector.Update",              0x746F00}, // @RVA[SpeedHackDetector.Update] [CHANGED]
      {"TimeCheatingDetector.Update",           0x748940}, // @RVA[TimeCheatingDetector.Update] [CHANGED]
      {"WallHackDetector.Update",               0x74F3C0}, // @RVA[WallHackDetector.Update] [CHANGED]
      {"WallHackDetector.FixedUpdate",          0x74E770}, // @RVA[WallHackDetector.FixedUpdate] [CHANGED]
    };

    for (const auto& t : targets) {
      uintptr_t addr = gaBase + t.rva;

      // Check if it's a direct bypass for Obscured types
      bool isDirectBypass = (std::string(t.name).find("op_Implicit") != std::string::npos);

      // Write instructions
      uint8_t retInstr[32];
      size_t  instrSize = 3;

      if (isDirectBypass) {
        if (std::string(t.name).find("ObscuredDouble") != std::string::npos) {
          // struct ObscuredDouble { int hash (0x0), long hiddenValue (0x8), long currentCryptoKey (0x10) }
          // passed by pointer in RCX (MS ABI for >8 byte structs)
          // mov rax, qword ptr [rcx + 8]   (48 8B 41 08)
          // xor rax, qword ptr [rcx + 10]  (48 33 41 10)
          // movq xmm0, rax                 (66 48 0F 6E C0)
          // ret                            (C3)
          // Completely bypasses hash checks and returns decrypted double in xmm0
          uint8_t bypassInstr[14] = {0x48, 0x8B, 0x41, 0x08, 0x48, 0x33, 0x41,
                                     0x10, 0x66, 0x48, 0x0F, 0x6E, 0xC0, 0xC3};
          memcpy(retInstr, bypassInstr, 14);
          instrSize = 14;
        }
        else if (std::string(t.name).find("ObscuredFloat") != std::string::npos) {
          // struct ObscuredFloat { int hash (0x0), int hiddenValue (0x4), int currentCryptoKey (0x8) }
          // passed by pointer in RCX
          // mov eax, dword ptr [rcx + 4] (8B 41 04)
          // xor eax, dword ptr [rcx + 8] (33 41 08)
          // movd xmm0, eax               (66 0F 6E C0)
          // ret (C3)
          // Completely bypasses hash checks and returns decrypted float in xmm0
          uint8_t bypassInstr[11] = {0x8B, 0x41, 0x04, 0x33, 0x41, 0x08, 0x66, 0x0F, 0x6E, 0xC0, 0xC3};
          memcpy(retInstr, bypassInstr, 11);
          instrSize = 11;
        }
        else if (std::string(t.name).find("ObscuredLong") != std::string::npos) {
          // struct ObscuredLong { int hash (0x0), long hiddenValue (0x8), long currentCryptoKey (0x10) }
          // passed by pointer in RCX
          // mov rax, qword ptr [rcx + 8]   (48 8B 41 08)
          // xor rax, qword ptr [rcx + 10]  (48 33 41 10)
          // ret (C3)
          // Completely bypasses hash checks and returns decrypted long in rax
          uint8_t bypassInstr[9] = {0x48, 0x8B, 0x41, 0x08, 0x48, 0x33, 0x41, 0x10, 0xC3};
          memcpy(retInstr, bypassInstr, 9);
          instrSize = 9;
        }
        else {
          // struct ObscuredInt { int hash (0x0), int hiddenValue (0x4), int currentCryptoKey (0x8), int fakeValue (0xC) }
          // passed by pointer in RCX

          // mov eax, dword ptr [rcx + 4] (8B 41 04)
          // xor eax, dword ptr [rcx + 8] (33 41 08)
          // ret (C3)
          // Completely bypasses hash checks and returns decrypted int in eax
          uint8_t bypassInstr[7] = {0x8B, 0x41, 0x04, 0x33, 0x41, 0x08, 0xC3};
          memcpy(retInstr, bypassInstr, 7);
          instrSize = 7;
        }
      }
      else {
        // XOR EAX, EAX; RET instruction (31 C0 C3)
        uint8_t nullRet[3] = {0x31, 0xC0, 0xC3};
        memcpy(retInstr, nullRet, 3);
      }

      DWORD oldProtect = 0;

      // Need to change memory protection first since code pages are typically RX
      if (VirtualProtectEx(m_mem.Handle(), (LPVOID) addr, instrSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        if (m_mem.WriteBytes(addr, retInstr, instrSize)) {
          patchCount++;
          details += "  Patched " + std::string(t.name) + " @ 0x" + ToHex(addr) + "\n";
        }
        // Restore original protection
        VirtualProtectEx(m_mem.Handle(), (LPVOID) addr, instrSize, oldProtect, &oldProtect);
      }
    }

    if (patchCount > 0)
      return "[AntiCheat] Bypassed " + std::to_string(patchCount) + " detector(s).\n" + details;
    else
      return "[AntiCheat] No detectors patched (may need RVA update).";
  }

private:
  static std::string ToHex(uintptr_t v)
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "%llX", (unsigned long long) v);
    return buf;
  }

  ProcessMemory& m_mem;
  bool           m_active           = true;
  uintptr_t      m_gameAssemblyBase = 0;
  uint8_t        m_origBytes[256]   = {};  // saved original bytes for restore
};
