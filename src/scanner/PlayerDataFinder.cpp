#include "PlayerDataFinder.h"
#include "../core/Il2CppDictionaryReader.h"
#include "../core/Il2CppOffsets.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <memory>

std::optional<PlayerDataResult> PlayerDataFinder::Find()
{
  if (m_hasCache) {
    // Validate that cached pointer is still valid by checking vtable AND hero list
    auto vtable = m_mem.ReadPointer(m_cachedResult.playerSaveDataAddr);
    if (vtable && *vtable > 0x10000) {
      // Double-check: can we still read the hero list?
      auto heroList = m_mem.ReadPointer(m_cachedResult.playerSaveDataAddr + PlayerSaveDataOffsets::HeroSaveDatas);
      if (heroList && *heroList != 0) {
        auto heroSize = m_mem.ReadInt32(*heroList + Il2CppListOffsets::Size);
        if (heroSize && *heroSize >= 1 && *heroSize <= 100) {
          return m_cachedResult;
        }
      }
    }
    m_hasCache = false;
  }

  // Step 1: Find a HeroSaveData by scanning for known hero keys
  // Try multiple hero IDs for better hit rate: Knight(101), Ranger(201), Sorcerer(301)
  struct HeroPattern
  {
    int32_t     heroKey;
    const char* name;
    std::string pattern;
  };
  std::vector<HeroPattern> heroPatterns = {
    {101, "Knight",   "65 00 00 00 ?? ?? ?? ?? 01 00 00 00 00 00 00 00"},
    {201, "Ranger",   "C9 00 00 00 ?? ?? ?? ?? 01 00 00 00 00 00 00 00"},
    {301, "Sorcerer", "2D 01 00 00 ?? ?? ?? ?? 01 00 00 00 00 00 00 00"},
  };

  struct HeroCandidate
  {
    uintptr_t   addr;
    double      exp;
    const char* name;
  };
  std::vector<HeroCandidate> heroCandidates;

  for (const auto& hp : heroPatterns) {
    printf("  [Debug] Searching for %s HeroSaveData pattern...\n", hp.name);
    auto hits = m_scanner.Scan(hp.pattern, true);
    printf("  [Debug] Found %zu potential %s hits.\n", hits.size(), hp.name);

    for (auto addr : hits) {
      uintptr_t objBase = addr - HeroSaveDataOffsets::HeroKey;

      auto level        = m_mem.ReadInt32(objBase + HeroSaveDataOffsets::HeroLevel);
      if (!level || *level < 1 || *level > 9999999)
        continue;

      auto unlocked = m_mem.ReadBool(objBase + HeroSaveDataOffsets::IsUnLock);
      if (!unlocked || !*unlocked)
        continue;

      auto exp = m_mem.ReadDouble(objBase + HeroSaveDataOffsets::HeroExp);
      if (!exp || *exp < 0.0)
        continue;

      auto vtable = m_mem.ReadPointer(objBase);
      if (!vtable || *vtable < 0x100000)
        continue;

      heroCandidates.push_back({objBase, *exp, hp.name});
    }
  }

  // Sort candidates by highest EXP first
  std::sort(heroCandidates.begin(), heroCandidates.end(), [](const HeroCandidate& a, const HeroCandidate& b) {
    return a.exp > b.exp;
  });

  printf(
    "  [Debug] Found %zu valid heroes across all patterns. Starting trace from the highest EXP...\n",
    heroCandidates.size()
  );

  // Step 2: Scan upward to find the PlayerSaveData, starting with the most active hero
  for (const auto& hc : heroCandidates) {
    printf("  [Debug] Tracing upward for %s @ 0x%llX (EXP: %.1f)\n", hc.name, (unsigned long long) hc.addr, hc.exp);

    std::string ptrPattern = PointerToPatternString(hc.addr);
    auto        ptrHits    = m_scanner.Scan(ptrPattern, true);

    if (!ptrHits.empty()) {
      printf("  [Debug]   -> Found %zu pointers to %s.\n", ptrHits.size(), hc.name);
      for (auto ptrAddr : ptrHits) {
        // The hero could be at any index in the HeroSaveDatas array
        for (int idx = 0; idx < 100; idx++) {
          uintptr_t arrayBase = ptrAddr - Il2CppArrayOffsets::Data - (idx * sizeof(uintptr_t));
          if (arrayBase == 0)
            continue;

          auto arrLen = m_mem.ReadInt32(arrayBase + Il2CppArrayOffsets::Length);
          if (!arrLen || *arrLen < 1 || *arrLen > 1000)
            continue;

          std::string arrPtrPattern = PointerToPatternString(arrayBase);
          auto        arrPtrHits    = m_scanner.Scan(arrPtrPattern, true);

          for (auto listCandAddr : arrPtrHits) {
            uintptr_t listBase = listCandAddr - Il2CppListOffsets::Items;

            auto listSize      = m_mem.ReadInt32(listBase + Il2CppListOffsets::Size);
            if (!listSize || *listSize < 1 || *listSize > 1000)
              continue;

            std::string listPtrPattern = PointerToPatternString(listBase);
            auto        listPtrHits    = m_scanner.Scan(listPtrPattern, true);

            for (auto psdCandAddr : listPtrHits) {
              uintptr_t psdBase = psdCandAddr - PlayerSaveDataOffsets::HeroSaveDatas;

              auto currencyList = m_mem.ReadPointer(psdBase + PlayerSaveDataOffsets::CurrencySaveDatas);
              auto heroList     = m_mem.ReadPointer(psdBase + PlayerSaveDataOffsets::HeroSaveDatas);
              auto runeList     = m_mem.ReadPointer(psdBase + PlayerSaveDataOffsets::RuneSaveData);

              if (!currencyList || *currencyList == 0)
                continue;
              if (!runeList)
                continue;

              auto currSize = m_mem.ReadInt32(*currencyList + Il2CppListOffsets::Size);
              if (!currSize || *currSize < 0 || *currSize > 1000)
                continue;

              int actualRuneSize = 0;
              if (*runeList != 0) {
                auto runeSize = m_mem.ReadInt32(*runeList + Il2CppListOffsets::Size);
                if (!runeSize || *runeSize < 0 || *runeSize > 1000)
                  continue;
                actualRuneSize = *runeSize;
              }

              PlayerDataResult result;
              result.playerSaveDataAddr = psdBase;
              result.heroListAddr       = listBase;
              result.currencyListAddr   = *currencyList;
              result.runeListAddr       = *runeList;

              printf("  [Debug] SUCCESS! Found active PlayerSaveData @ 0x%llX\n", (unsigned long long) psdBase);

              auto regions         = m_mem.EnumerateRegions(true);
              bool foundDict       = false;

              size_t maxRegionSize = 0;
              for (const auto& reg : regions) {
                if (reg.RegionSize > maxRegionSize && reg.RegionSize <= (256ull * 1024 * 1024))
                  maxRegionSize = reg.RegionSize;
              }

              std::unique_ptr<uint8_t[]> buffer;
              if (maxRegionSize > 0) {
                buffer.reset(new uint8_t[maxRegionSize]);
              }

              for (const auto& reg : regions) {
                if (foundDict)
                  break;
                if (!buffer || reg.RegionSize == 0 || reg.RegionSize > maxRegionSize)
                  continue;

                if (!m_mem.ReadBytes((uintptr_t) reg.BaseAddress, buffer.get(), reg.RegionSize))
                  continue;

                for (size_t i = Il2CppDictOffsets::Count; reg.RegionSize >= 0x30 && i < reg.RegionSize - 0x30; i += 4) {
                  int32_t val = *reinterpret_cast<int32_t*>(&buffer[i]);
                  // Total runes history during update: 197 -> 241 -> 248
                  // make it between range even knowing the exact nodes
                  if (val >= 225 && val <= 260) {
                    size_t dictOffset = i - Il2CppDictOffsets::Count;

                    int32_t freeCount = *reinterpret_cast<int32_t*>(&buffer[dictOffset + Il2CppDictOffsets::FreeCount]);
                    if (freeCount < 0 || freeCount > 1000)
                      continue;

                    uintptr_t bucketsPtr =
                      *reinterpret_cast<uintptr_t*>(&buffer[dictOffset + Il2CppDictOffsets::Buckets]);
                    uintptr_t entriesPtr =
                      *reinterpret_cast<uintptr_t*>(&buffer[dictOffset + Il2CppDictOffsets::Entries]);

                    if (bucketsPtr < 0x100000 || bucketsPtr > 0x7FFFFFFFFFFF)
                      continue;
                    if (entriesPtr < 0x100000 || entriesPtr > 0x7FFFFFFFFFFF)
                      continue;

                    auto isValidPtr = [&](uintptr_t ptr) {
                      auto it = std::lower_bound(
                        regions.begin(), regions.end(), ptr, [](const MEMORY_BASIC_INFORMATION& mbi, uintptr_t val) {
                          return (uintptr_t) mbi.BaseAddress + mbi.RegionSize <= val;
                        }
                      );
                      return it != regions.end() && ptr >= (uintptr_t) it->BaseAddress;
                    };

                    if (!isValidPtr(bucketsPtr) || !isValidPtr(entriesPtr))
                      continue;

                    uintptr_t dictAddr = (uintptr_t) reg.BaseAddress + dictOffset;
                    if (entriesPtr == 0)
                      continue;

                    auto entriesLen = m_mem.ReadInt32(entriesPtr + Il2CppArrayOffsets::Length);
                    if (!entriesLen || *entriesLen < val || *entriesLen > 10000)
                      continue;

                    Il2CppDictionaryReader dictReader(m_mem);
                    auto                   outerEntries = dictReader.ReadOuterEntries(dictAddr);

                    if (outerEntries.size() == static_cast<size_t>(val)) {
                      bool isValid         = true;
                      int  validInnerCount = 0;
                      for (size_t j = 0; j < 5; j++) {
                        if (outerEntries[j].first <= 0) {
                          isValid = false;
                          break;
                        }
                        auto innerDictPtr = outerEntries[j].second;
                        auto innerCount   = dictReader.GetCount(innerDictPtr);
                        if (!innerCount || *innerCount <= 0 || *innerCount > 20) {
                          isValid = false;
                          break;
                        }
                        validInnerCount++;
                      }

                      if (isValid && validInnerCount == 5) {
                        for (const auto& pair : outerEntries) {
                          int       runeKey      = pair.first;
                          uintptr_t innerDictPtr = pair.second;
                          auto      maxLevelOpt  = dictReader.GetCount(innerDictPtr);
                          if (maxLevelOpt) {
                            result.runeMaxLevels[runeKey] = *maxLevelOpt;
                          }
                        }
                        foundDict = true;
                        break;
                      }
                    }
                  }
                }
              }

              m_cachedResult = result;
              m_hasCache     = true;
              return result;
            }
          }
        }
      }
    }
    else {
      printf("  [Debug]   -> No pointers found to %s.\n", hc.name);
    }
  }

  printf("  [Debug] Checked all valid heroes, but found no PlayerSaveData chain.\n");
  return std::nullopt;
}

std::vector<RuneSaveInfo> PlayerDataFinder::ReadRunes(uintptr_t runeListAddr)
{
  std::vector<RuneSaveInfo> result;
  auto                      elements = m_listReader.ReadElementPointers(runeListAddr);

  for (auto elemAddr : elements) {
    RuneSaveInfo ri;
    ri.addr  = elemAddr;

    auto key = m_mem.ReadInt32(elemAddr + RuneSaveDataOffsets::RuneKey);
    if (!key)
      continue;
    ri.runeKey = *key;

    auto level = m_mem.ReadInt32(elemAddr + RuneSaveDataOffsets::Level);
    if (level)
      ri.level = *level;

    result.push_back(ri);
  }
  return result;
}
