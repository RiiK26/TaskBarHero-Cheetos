#pragma once
#include "../core/AOBScanner.h"
#include "../core/Il2CppOffsets.h"
#include "../core/Il2CppStatDictionary.h"
#include "../core/ProcessMemory.h"
#include "../core/StatType.h"

#include <atomic>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct HeroResult
{
  int                                                          id = 0;
  std::string                                                  name;
  uintptr_t                                                    heroInfoDataAddr = 0;
  uintptr_t                                                    vhInstanceAddr   = 0;
  uintptr_t                                                    zeInstanceAddr   = 0;
  uintptr_t                                                    statsDictAddr    = 0;
  std::unordered_map<StatType, Il2CppStatDictionary::StatData> stats;
};

class HeroFinder
{
public:
  inline static const std::map<int, std::string> HeroMap = {
    {101, "Knight"  },
    {201, "Ranger"  },
    {301, "Sorcerer"},
    {401, "Priest"  },
    {501, "Hunter"  },
    {601, "Slayer"  },
  };

  explicit HeroFinder(ProcessMemory& mem) :
      m_mem(mem),
      m_scanner(mem),
      m_statDict(mem)
  {
  }

  // Locates every hero's stat dictionary.
  //
  // After a successful scan the hero object addresses are cached. Later calls
  // re-validate the cached pointer chain (a handful of small reads) and only
  // fall back to a full memory scan when validation fails or forceRescan is set.
  std::vector<HeroResult> FindAll(bool useStatsDictB = true, bool forceRescan = false)
  {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!forceRescan && !m_cache.empty() && m_cacheDictB == useStatsDictB) {
      auto refreshed = RefreshCache(useStatsDictB);
      if (!refreshed.empty()) {
        m_lastFromCache = true;
        m_cache         = refreshed;
        return refreshed;
      }
    }

    m_lastFromCache = false;
    m_cache.clear();

    // The IL2CPP GC heap lives in MEM_PRIVATE pages -> scanning only those is much
    // cheaper. Fall back to every writable region if that finds nothing.
    auto results = ScanHeroes(useStatsDictB, /*privateOnly=*/true);
    if (results.empty())
      results = ScanHeroes(useStatsDictB, /*privateOnly=*/false);

    if (!results.empty()) {
      m_cache      = results;
      m_cacheDictB = useStatsDictB;
    }
    return results;
  }

  // Drop cached hero addresses (call on attach/detach).
  void ClearCache()
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache.clear();
    m_lastFromCache = false;
  }

  // True if the last FindAll() was served from the validated cache (no scan).
  bool WasLastFromCache() const { return m_lastFromCache; }

private:
  // The ID pattern ("00x8 <id:4> 00x4") starts 8 bytes before HeroInfoData.HeroKey.
  static constexpr int32_t kIdPatternOffset = Il2CppOffsets::HeroInfoData_HeroKey - 8;  // 0x28

  std::vector<HeroResult> ScanHeroes(bool useStatsDictB, bool privateOnly)
  {
    // ---- Phase 1: locate HeroInfoData instances by embedded ID ----
    // One pass over memory matches ALL hero IDs at once (previously one full
    // scan per hero ID).
    std::vector<uint32_t> heroIds;
    for (const auto& [heroId, heroName] : HeroMap)
      heroIds.push_back((uint32_t) heroId);

    auto idHits = m_scanner.ScanAligned(
      [&heroIds](const uint8_t* p) {
        if (LoadU64(p) != 0)
          return false;
        uint64_t q = LoadU64(p + 8);
        if ((q >> 32) != 0)
          return false;
        uint32_t id = (uint32_t) q;
        if (id == 0)
          return false;
        for (uint32_t h : heroIds)
          if (id == h)
            return true;
        return false;
      },
      16, true, privateOnly
    );

    // Hits are address-sorted, so "first valid hit per ID wins" matches the old behavior.
    std::map<int, uintptr_t> heroInfoAddrs;
    for (uintptr_t matchAddr : idHits) {
      if (heroInfoAddrs.size() == HeroMap.size())
        break;

      auto id = m_mem.ReadInt32(matchAddr + 8);
      if (!id || HeroMap.count(*id) == 0 || heroInfoAddrs.count(*id) != 0)
        continue;

      uintptr_t heroInfoBase = matchAddr - kIdPatternOffset;
      if (IsHeroInfoData(heroInfoBase, *id))
        heroInfoAddrs[*id] = heroInfoBase;
    }

    if (heroInfoAddrs.empty())
      return {};

    // ---- Phase 2: find the vh instance referencing each HeroInfoData ----
    // Again a single pass that looks for pointers to ANY of the found HeroInfoData.
    std::map<uintptr_t, int> addrToId;
    uint64_t                 lo = UINT64_MAX, hi = 0;
    for (const auto& [heroId, hiAddr] : heroInfoAddrs) {
      addrToId[hiAddr] = heroId;
      lo               = std::min<uint64_t>(lo, hiAddr);
      hi               = std::max<uint64_t>(hi, hiAddr);
    }
    std::vector<uint64_t> targets;
    for (const auto& [addr, heroId] : addrToId)
      targets.push_back(addr);

    auto refHits = m_scanner.ScanAligned(
      [&targets, lo, hi](const uint8_t* p) {
        uint64_t v = LoadU64(p);
        if (v < lo || v > hi)
          return false;
        for (uint64_t t : targets)
          if (v == t)
            return true;
        return false;
      },
      8, true, privateOnly
    );

    std::map<int, HeroResult> found;
    for (uintptr_t refAddr : refHits) {
      if (found.size() == heroInfoAddrs.size())
        break;

      auto ptr = m_mem.ReadPointer(refAddr);
      if (!ptr)
        continue;
      auto it = addrToId.find(*ptr);
      if (it == addrToId.end() || found.count(it->second) != 0)
        continue;

      // ---- Phase 3/4: ze* stat container -> stat dictionary ----
      auto hr = ResolveFromVh(it->second, *ptr, refAddr - Il2CppOffsets::Vh_HeroInfoDataRef, useStatsDictB);
      if (hr)
        found.emplace(it->second, std::move(*hr));  // first valid vh instance wins
    }

    std::vector<HeroResult> results;
    for (auto& [heroId, hr] : found)
      results.push_back(std::move(hr));
    return results;
  }

  // Confirms a candidate HeroInfoData by checking its HeroNameKey string.
  bool IsHeroInfoData(uintptr_t heroInfoBase, int heroId) const
  {
    auto strPtr = m_mem.ReadPointer(heroInfoBase + Il2CppOffsets::HeroInfoData_HeroNameKey);
    if (!strPtr || *strPtr == 0)
      return false;

    auto strLen = m_mem.ReadInt32(*strPtr + Il2CppStringOffsets::Length);
    if (!strLen || *strLen <= 8 || *strLen >= 30)
      return false;

    std::wstring s = m_mem.ReadUtf16(*strPtr + Il2CppStringOffsets::Chars, (size_t) std::min<int32_t>(*strLen, 20));
    std::wstring expected = L"HeroName_" + std::to_wstring(heroId);
    return s.find(expected) != std::wstring::npos;
  }

  // Follows vh -> ze* stat container -> stat dictionary and reads all stats.
  std::optional<HeroResult> ResolveFromVh(int heroId, uintptr_t hiAddr, uintptr_t vhBase, bool useStatsDictB) const
  {
    auto zePtr = m_mem.ReadPointer(vhBase + Il2CppOffsets::Vo_StatContainer);
    if (!zePtr || *zePtr == 0)
      return std::nullopt;

    int32_t dictFieldOffset = useStatsDictB ? Il2CppOffsets::Ze_StatsDictB : Il2CppOffsets::Ze_StatsDictA;

    auto dictPtr            = m_mem.ReadPointer(*zePtr + dictFieldOffset);
    if (!dictPtr || *dictPtr == 0)
      return std::nullopt;

    HeroResult hr;
    hr.id               = heroId;
    hr.name             = HeroMap.at(heroId);
    hr.heroInfoDataAddr = hiAddr;
    hr.vhInstanceAddr   = vhBase;
    hr.zeInstanceAddr   = *zePtr;
    hr.statsDictAddr    = *dictPtr;
    hr.stats            = m_statDict.ReadAll(*dictPtr);

    if (hr.stats.empty())
      return std::nullopt;
    return hr;
  }

  // Re-validates every cached hero; returns empty if ANY of them went stale.
  std::vector<HeroResult> RefreshCache(bool useStatsDictB) const
  {
    std::vector<HeroResult> out;
    out.reserve(m_cache.size());
    for (const auto& cached : m_cache) {
      // HeroInfoData must still carry the same ID...
      auto key = m_mem.ReadInt32(cached.heroInfoDataAddr + Il2CppOffsets::HeroInfoData_HeroKey);
      if (!key || *key != cached.id)
        return {};

      // ...and the vh instance must still reference it.
      auto ref = m_mem.ReadPointer(cached.vhInstanceAddr + Il2CppOffsets::Vh_HeroInfoDataRef);
      if (!ref || *ref != cached.heroInfoDataAddr)
        return {};

      // Re-read ze/dict every time: the dictionary's entries array can be reallocated.
      auto hr = ResolveFromVh(cached.id, cached.heroInfoDataAddr, cached.vhInstanceAddr, useStatsDictB);
      if (!hr)
        return {};
      out.push_back(std::move(*hr));
    }
    return out;
  }

  ProcessMemory&       m_mem;
  AOBScanner           m_scanner;
  Il2CppStatDictionary m_statDict;

  std::mutex              m_mutex;
  std::vector<HeroResult> m_cache;
  bool                    m_cacheDictB = true;
  std::atomic<bool>       m_lastFromCache{false};
};
