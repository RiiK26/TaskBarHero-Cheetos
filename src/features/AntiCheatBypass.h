#pragma once
#include "../core/ProcessMemory.h"

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
    uintptr_t            rva;
    std::vector<uint8_t> patchBytes;
    std::vector<uint8_t> originalBytes;
    bool                 isApplied = false;
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
      t.isApplied = false;
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
        t.isApplied = false;
        t.originalBytes.clear();
      }
    }

    int         patchCount   = 0;
    int         skippedCount = 0;
    std::string details;

    for (auto& t : m_targets) {
      uintptr_t addr = m_gameAssemblyBase + t.rva;

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
          details += "  [" + std::string(TierToString(t.tier)) + "] Patched " + t.name + " @ 0x" + ToHex(addr) + "\n";
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
      return "[AntiCheat] Failed to patch detectors. Verify GameAssembly.dll memory protections.";
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

      uintptr_t addr       = m_gameAssemblyBase + t.rva;
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
    // - Safely returns false (0) for bool query methods
    // - Safely returns void for void methods
    // - Preserves caller stack and register constraints
    const std::vector<uint8_t> nullRet = {0x31, 0xC0, 0xC3};

    // ---------------------------------------------------------------------
    // Tier 1: ACTk Master Event Dispatcher
    // ACTkDetectorBase<object>.bbkt (Slot 11) is the core method that raises
    // detectionEvent. If neutralized, NO detector can trigger a cheat response.
    // ---------------------------------------------------------------------
    m_targets.push_back(
      {"ACTkDetectorBase.OnCheatingDetected", TargetTier::MasterDispatch,
       0x1A1C480,  // @RVA[ACTkDetectorBase<object>.bbkt]
       nullRet}
    );

    // ---------------------------------------------------------------------
    // Tier 2: Detector Core Routines (StartDetection / DetectorCore / bbla)
    // Prevents individual detectors from spawning background threads or coroutines.
    // ---------------------------------------------------------------------
    m_targets.push_back(
      {"InjectionDetector.DetectorCore", TargetTier::DetectorCore,
       0x763230,  // @RVA[InjectionDetector.bbla]
       nullRet}
    );
    m_targets.push_back(
      {"SpeedHackDetector.DetectorCore", TargetTier::DetectorCore,
       0x7651D0,  // @RVA[SpeedHackDetector.bbla]
       nullRet}
    );
    m_targets.push_back(
      {"TimeCheatingDetector.DetectorCore", TargetTier::DetectorCore,
       0x767160,  // @RVA[TimeCheatingDetector.bbla]
       nullRet}
    );
    m_targets.push_back(
      {"ObscuredCheatingDetector.DetectorCore", TargetTier::DetectorCore,
       0x763620,  // @RVA[ObscuredCheatingDetector.bbla]
       nullRet}
    );
    m_targets.push_back(
      {"WallHackDetector.DetectorCore", TargetTier::DetectorCore,
       0x76C7C0,  // @RVA[WallHackDetector.bbla]
       nullRet}
    );

    // ---------------------------------------------------------------------
    // Tier 3: Frame-by-Frame Polling Watchdogs (Update / FixedUpdate)
    // Neutralizes per-frame delta checks in case detectors were already initialized.
    // ---------------------------------------------------------------------
    m_targets.push_back(
      {"SpeedHackDetector.Update", TargetTier::PollingWatchdog,
       0x764D50,  // @RVA[SpeedHackDetector.Update]
       nullRet}
    );
    m_targets.push_back(
      {"TimeCheatingDetector.Update", TargetTier::PollingWatchdog,
       0x766B00,  // @RVA[TimeCheatingDetector.Update]
       nullRet}
    );
    m_targets.push_back(
      {"WallHackDetector.Update", TargetTier::PollingWatchdog,
       0x76C310,  // @RVA[WallHackDetector.Update]
       nullRet}
    );
    m_targets.push_back(
      {"WallHackDetector.FixedUpdate", TargetTier::PollingWatchdog,
       0x76B860,  // @RVA[WallHackDetector.FixedUpdate]
       nullRet}
    );

    // ---------------------------------------------------------------------
    // Tier 4: Obscured Types Direct Violation Receivers
    // Directly silences callbacks fired when honeypots or hash mismatches occur.
    // ---------------------------------------------------------------------
    m_targets.push_back(
      {"ObscuredCheatingDetector.OnCheatingType", TargetTier::ViolationHandler,
       0x7640F0,  // @RVA[ObscuredCheatingDetector.bblw]
       nullRet}
    );
    m_targets.push_back(
      {"ObscuredCheatingDetector.OnCheatingValue", TargetTier::ViolationHandler,
       0x7641D0,  // @RVA[ObscuredCheatingDetector.bblx]
       nullRet}
    );

    // ---------------------------------------------------------------------
    // Tier 5: Custom In-Game Anti-Abuse Guards
    // Guards against client-side multi-minute penalty lockouts during batch box opening.
    // ---------------------------------------------------------------------
    m_targets.push_back(
      {"StageBoxRequestAbuseGuard.IsBlocked", TargetTier::CustomGuard,
       0x8BE380,  // @RVA[StageBoxRequestAbuseGuard.hug] -> forces false
       nullRet}
    );
    m_targets.push_back(
      {"StageBoxRequestAbuseGuard.RecordRequest", TargetTier::CustomGuard,
       0x8BE480,  // @RVA[StageBoxRequestAbuseGuard.huh] -> suppresses request accumulation
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
