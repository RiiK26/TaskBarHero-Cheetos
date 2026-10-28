#pragma once
#include "../core/ProcessMemory.h"
#include "../core/AOBScanner.h"
#include "../core/Signatures.h"

#include <cstdint>
#include <string>
#include <vector>

/* =========================================================================
 * AntiCheatBypass: Multi-Tiered Anti-Cheat & Abuse Guard Neutralizer
 *
 * Target: TaskBarHero (Unity 6000.0.72f1, x86_64 IL2CPP)
 * Anti-Cheat Systems Neutralized:
 *  1. CodeStage Anti-Cheat Toolkit (ACTk) v6.8.0:
 *     - Master Event Dispatcher (ACTkDetectorBase<T>.bbkt)
 *     - InjectionDetector (DLL and injection scanning)
 *     - SpeedHackDetector (Engine delta vs hardware timer clock checks)
 *     - TimeCheatingDetector (Online NTP/HTTP network time vs local UTC checks)
 *     - ObscuredCheatingDetector (In-memory ObscuredTypes honeypot violation handler)
 *     - WallHackDetector (Rigidbody, character controller, wireframe shader, raycasts)
 *  2. In-Game Abuse Guards:
 *     - StageBoxRequestAbuseGuard (Sliding-window box opening rate-limiter)
 *
 * Resolution Architecture:
 *  - AOB Signatures: Scans GameAssembly.dll dynamically using wildcard byte patterns.
 *  - Fallback RVAs: Utilized if a signature fails or during initial offline testing.
 *
 * Technical Note on ObscuredTypes (ObscuredInt, ObscuredFloat, ObscuredDouble, ObscuredLong):
 *  - DO NOT patch op_Implicit entry points directly! In Microsoft x64 calling convention,
 *    structs > 8 bytes are passed by pointer in RCX, and scalar modifications corrupt
 *    compiler-merged ICF stubs, causing segmentation faults or missing UI elements.
 *  - Instead, ObscuredTypes are safely manipulated atomically via ObscuredTypes.h
 *    (updating hash, hiddenValue, currentCryptoKey, and fakeValue in a single write).
 *
 * Patching Strategy:
 *  - Patches target method preambles with `31 C0 C3` (xor eax, eax; ret).
 *  - This safely returns false / 0 for boolean query methods (such as hug)
 *    and cleanly exits void methods without corrupting stack registers.
 *  - All targets support non-destructive rollback via Restore().
 * ========================================================================= */

class AntiCheatBypass
{
public:
  enum class TargetTier
  {
    MasterDispatch,    // ACTkDetectorBase master event dispatcher (Slot 11)
    DetectorCore,      // Individual detector initialization routines (Slot 18 / bbla)
    PollingWatchdog,   // Per-frame Update / FixedUpdate polling loops
    ViolationHandler,  // ObscuredTypes cheat event callbacks
    CustomGuard        // Application-level rate limiters and abuse guards
  };

  struct PatchTarget
  {
    std::string          name;
    TargetTier           tier;
    std::string          signature;
    uintptr_t            rva = 0;
    std::vector<uint8_t> patchBytes;
    std::vector<uint8_t> originalBytes;
    bool                 isApplied       = false;
    uintptr_t            resolvedAddress = 0;
    bool                 resolvedViaSig  = false;
  };

  explicit AntiCheatBypass(ProcessMemory& mem) :
      m_mem(mem)
  {
    InitializeTargets();
  }

  bool IsActive() const { return m_active; }
  void Toggle() { m_active = !m_active; }
  void Enable() { m_active = true; }
  void Disable() { m_active = false; }

  // Reset tracking state (used when game process restarts or detaches)
  void Reset()
  {
    m_gameAssemblyBase = 0;
    for (auto& t : m_targets) {
      t.isApplied       = false;
      t.resolvedAddress = 0;
      t.resolvedViaSig  = false;
      t.originalBytes.clear();
    }
  }

  // Apply all patches to the game process
  std::string Apply(bool force = false)
  {
    if (!m_active)
      return "[AntiCheat] Bypass is currently disabled in settings.";

    auto modInfo = m_mem.FindModule(L"GameAssembly.dll");
    if (!modInfo)
      return "[AntiCheat] GameAssembly.dll not found. Is the game running?";

    // Detect new process or ASLR shift; reset stale target flags
    if (m_gameAssemblyBase != modInfo->base || force) {
      m_gameAssemblyBase = modInfo->base;
      for (auto& t : m_targets) {
        t.isApplied       = false;
        t.resolvedAddress = 0;
        t.resolvedViaSig  = false;
        t.originalBytes.clear();
      }
    }

    AOBScanner  scanner(m_mem);
    int         patchCount   = 0;
    int         skippedCount = 0;
    std::string details;

    for (auto& t : m_targets) {
      uintptr_t addr    = 0;
      bool      fromSig = false;

      // 1. Dynamic  Signature Scan
      if (!t.signature.empty()) {
        addr = scanner.ScanModulePattern(L"GameAssembly.dll", t.signature);
        if (addr != 0) {
          fromSig = true;
        }
      }

      // 2. Fallback RVA
      if (addr == 0 && t.rva != 0) {
        addr = m_gameAssemblyBase + t.rva;
      }

      if (addr == 0) {
        details += "  [Warning] Failed to resolve target: " + t.name + "\n";
        continue;
      }

      t.resolvedAddress = addr;
      t.resolvedViaSig  = fromSig;

      // If already patched, skip redundant write
      if (t.isApplied && !force) {
        skippedCount++;
        continue;
      }

      size_t patchSize  = t.patchBytes.size();
      DWORD  oldProtect = 0;

      if (VirtualProtectEx(m_mem.Handle(), (LPVOID) addr, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        // Backup original bytes before applying patch
        std::vector<uint8_t> backup(patchSize);
        if (m_mem.ReadBytes(addr, backup.data(), patchSize)) {
          t.originalBytes = backup;
        }

        // Write patch instruction
        if (m_mem.WriteBytes(addr, t.patchBytes.data(), patchSize)) {
          t.isApplied = true;
          patchCount++;
          details += "  [" + std::string(TierToString(t.tier)) + "] Patched " + t.name + " @ 0x" + ToHex(addr)
                   + (fromSig ? " (AOB)" : " (RVA)") + "\n";
        }

        // Restore original memory protection
        VirtualProtectEx(m_mem.Handle(), (LPVOID) addr, patchSize, oldProtect, &oldProtect);
      }
    }

    if (patchCount > 0) {
      return "[AntiCheat] Successfully neutralized " + std::to_string(patchCount) + " detector/guard target(s).\n"
           + details;
    }
    else if (skippedCount > 0) {
      return "[AntiCheat] All " + std::to_string(skippedCount) + " target(s) are already active and patched.";
    }
    else {
      return "[AntiCheat] Failed to patch detectors. Verify GameAssembly.dll memory protections.\n" + details;
    }
  }

  // Restore original instructions across all patched targets
  std::string Restore()
  {
    if (m_gameAssemblyBase == 0) {
      auto modInfo = m_mem.FindModule(L"GameAssembly.dll");
      if (modInfo)
        m_gameAssemblyBase = modInfo->base;
      else
        return "[AntiCheat] Cannot restore: GameAssembly.dll not found.";
    }

    int         restoredCount = 0;
    std::string details;

    for (auto& t : m_targets) {
      if (!t.isApplied || t.originalBytes.empty())
        continue;

      uintptr_t addr       = t.resolvedAddress != 0 ? t.resolvedAddress : (m_gameAssemblyBase + t.rva);
      size_t    patchSize  = t.originalBytes.size();
      DWORD     oldProtect = 0;

      if (VirtualProtectEx(m_mem.Handle(), (LPVOID) addr, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        if (m_mem.WriteBytes(addr, t.originalBytes.data(), patchSize)) {
          t.isApplied = false;
          t.originalBytes.clear();
          restoredCount++;
          details += "  Restored " + t.name + " @ 0x" + ToHex(addr) + "\n";
        }
        VirtualProtectEx(m_mem.Handle(), (LPVOID) addr, patchSize, oldProtect, &oldProtect);
      }
    }

    if (restoredCount > 0) {
      return "[AntiCheat] Restored " + std::to_string(restoredCount) + " detector(s) to original game code.\n"
           + details;
    }
    else {
      return "[AntiCheat] No active patches needed restoration.";
    }
  }

  // Inspection helpers
  bool IsAnyPatched() const
  {
    for (const auto& t : m_targets) {
      if (t.isApplied)
        return true;
    }
    return false;
  }

  int GetPatchedCount() const
  {
    int count = 0;
    for (const auto& t : m_targets) {
      if (t.isApplied)
        count++;
    }
    return count;
  }

  int GetTotalTargets() const { return static_cast<int>(m_targets.size()); }

  const std::vector<PatchTarget>& GetTargets() const { return m_targets; }

private:
  void InitializeTargets()
  {
    m_targets.clear();

    // Standard x86_64 NULL RET stub: `xor eax, eax; ret` (31 C0 C3)
    const std::vector<uint8_t> nullRet = {0x31, 0xC0, 0xC3};

    // ---------------------------------------------------------------------
    // Tier 1: ACTk Master Event Dispatcher
    // ---------------------------------------------------------------------
    m_targets.push_back(
      {Signatures::ACTkDetectorBase_OnCheatingDetected.name, TargetTier::MasterDispatch,
       Signatures::ACTkDetectorBase_OnCheatingDetected.aob, Signatures::ACTkDetectorBase_OnCheatingDetected.rva,
       nullRet}
    );

    // ---------------------------------------------------------------------
    // Tier 2: Detector Core Routines
    // ---------------------------------------------------------------------
    m_targets.push_back(
      {Signatures::InjectionDetector_DetectorCore.name, TargetTier::DetectorCore,
       Signatures::InjectionDetector_DetectorCore.aob, Signatures::InjectionDetector_DetectorCore.rva, nullRet}
    );
    m_targets.push_back(
      {Signatures::SpeedHackDetector_DetectorCore.name, TargetTier::DetectorCore,
       Signatures::SpeedHackDetector_DetectorCore.aob, Signatures::SpeedHackDetector_DetectorCore.rva, nullRet}
    );
    m_targets.push_back(
      {Signatures::TimeCheatingDetector_DetectorCore.name, TargetTier::DetectorCore,
       Signatures::TimeCheatingDetector_DetectorCore.aob, Signatures::TimeCheatingDetector_DetectorCore.rva, nullRet}
    );
    m_targets.push_back(
      {Signatures::ObscuredCheatingDetector_DetectorCore.name, TargetTier::DetectorCore,
       Signatures::ObscuredCheatingDetector_DetectorCore.aob, Signatures::ObscuredCheatingDetector_DetectorCore.rva,
       nullRet}
    );
    m_targets.push_back(
      {Signatures::WallHackDetector_DetectorCore.name, TargetTier::DetectorCore,
       Signatures::WallHackDetector_DetectorCore.aob, Signatures::WallHackDetector_DetectorCore.rva, nullRet}
    );

    // ---------------------------------------------------------------------
    // Tier 3: Frame-by-Frame Polling Watchdogs
    // ---------------------------------------------------------------------
    m_targets.push_back(
      {Signatures::SpeedHackDetector_Update.name, TargetTier::PollingWatchdog, Signatures::SpeedHackDetector_Update.aob,
       Signatures::SpeedHackDetector_Update.rva, nullRet}
    );
    m_targets.push_back(
      {Signatures::TimeCheatingDetector_Update.name, TargetTier::PollingWatchdog,
       Signatures::TimeCheatingDetector_Update.aob, Signatures::TimeCheatingDetector_Update.rva, nullRet}
    );
    m_targets.push_back(
      {Signatures::TimeCheatingDetector_CheckOnlineTime.name, TargetTier::PollingWatchdog,
       Signatures::TimeCheatingDetector_CheckOnlineTime.aob, Signatures::TimeCheatingDetector_CheckOnlineTime.rva,
       nullRet}
    );
    m_targets.push_back(
      {Signatures::WallHackDetector_Update.name, TargetTier::PollingWatchdog, Signatures::WallHackDetector_Update.aob,
       Signatures::WallHackDetector_Update.rva, nullRet}
    );
    m_targets.push_back(
      {Signatures::WallHackDetector_FixedUpdate.name, TargetTier::PollingWatchdog,
       Signatures::WallHackDetector_FixedUpdate.aob, Signatures::WallHackDetector_FixedUpdate.rva, nullRet}
    );

    // ---------------------------------------------------------------------
    // Tier 4: Obscured Types Direct Violation Receivers
    // ---------------------------------------------------------------------
    m_targets.push_back(
      {Signatures::ObscuredCheatingDetector_OnCheatingType.name, TargetTier::ViolationHandler,
       Signatures::ObscuredCheatingDetector_OnCheatingType.aob, Signatures::ObscuredCheatingDetector_OnCheatingType.rva,
       nullRet}
    );
    m_targets.push_back(
      {Signatures::ObscuredCheatingDetector_OnCheatingValue.name, TargetTier::ViolationHandler,
       Signatures::ObscuredCheatingDetector_OnCheatingValue.aob,
       Signatures::ObscuredCheatingDetector_OnCheatingValue.rva, nullRet}
    );

    // ---------------------------------------------------------------------
    // Tier 5: Custom In-Game Anti-Abuse Guards
    // ---------------------------------------------------------------------
    m_targets.push_back(
      {Signatures::StageBoxRequestAbuseGuard_IsBlocked.name, TargetTier::CustomGuard,
       Signatures::StageBoxRequestAbuseGuard_IsBlocked.aob, Signatures::StageBoxRequestAbuseGuard_IsBlocked.rva,
       nullRet}
    );
    m_targets.push_back(
      {Signatures::StageBoxRequestAbuseGuard_RecordRequest.name, TargetTier::CustomGuard,
       Signatures::StageBoxRequestAbuseGuard_RecordRequest.aob, Signatures::StageBoxRequestAbuseGuard_RecordRequest.rva,
       nullRet}
    );
  }

  static std::string ToHex(uintptr_t v)
  {
    char buf[32];
    snprintf(buf, sizeof(buf), "%llX", (unsigned long long) v);
    return buf;
  }

  static const char* TierToString(TargetTier tier)
  {
    switch (tier) {
    case TargetTier::MasterDispatch :
      return "Master";
    case TargetTier::DetectorCore :
      return "Core";
    case TargetTier::PollingWatchdog :
      return "Watchdog";
    case TargetTier::ViolationHandler :
      return "Handler";
    case TargetTier::CustomGuard :
      return "Guard";
    default :
      return "Other";
    }
  }

  ProcessMemory&           m_mem;
  bool                     m_active           = true;
  uintptr_t                m_gameAssemblyBase = 0;
  std::vector<PatchTarget> m_targets;
};
