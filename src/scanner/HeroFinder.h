#pragma once
#include "../core/AOBScanner.h"
#include "../core/Il2CppOffsets.h"
#include "../core/Il2CppStatDictionary.h"
#include "../core/ProcessMemory.h"
#include "../core/StatType.h"

#include <map>
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

  std::vector<HeroResult> FindAll(bool useStatsDictB = true)
  {
    std::vector<HeroResult> results;

    // ---- Phase 1: locate HeroInfoData instances by embedded ID ----
    std::map<int, uintptr_t> heroInfoAddrs;

    for (const auto& [heroId, heroName] : HeroMap) {
      std::string idPattern = IdToPatternString((uint32_t) heroId);
      auto        hits      = m_scanner.Scan(idPattern, true);

      for (auto matchAddr : hits) {
        uintptr_t heroInfoBase = matchAddr - 0x28;

        auto strPtr            = m_mem.ReadPointer(heroInfoBase + Il2CppOffsets::HeroInfoData_HeroNameKey);
        if (!strPtr || *strPtr == 0)
          continue;

        auto strLen = m_mem.ReadInt32(*strPtr + Il2CppStringOffsets::Length);
        if (!strLen || *strLen <= 8 || *strLen >= 30)
          continue;

        std::wstring s = m_mem.ReadUtf16(*strPtr + Il2CppStringOffsets::Chars, (size_t) std::min<int32_t>(*strLen, 20));
        std::wstring expected = L"HeroName_" + std::to_wstring(heroId);

        if (s.find(expected) != std::wstring::npos) {
          heroInfoAddrs[heroId] = heroInfoBase;
          break;
        }
      }
    }

    // ---- Phase 2: find the vh instance referencing each HeroInfoData ----
    for (const auto& [heroId, hiAddr] : heroInfoAddrs) {
      std::string ptrPattern = PointerToPatternString(hiAddr);
      auto        hits       = m_scanner.Scan(ptrPattern, true);

      for (auto refAddr : hits) {
        uintptr_t vhBase = refAddr - Il2CppOffsets::Vh_HeroInfoDataRef;

        // ---- Phase 3: read the ze* stat container ----
        auto zePtr = m_mem.ReadPointer(vhBase + Il2CppOffsets::Vo_StatContainer);
        if (!zePtr || *zePtr == 0)
          continue;

        // ---- Phase 4: read one of the two stat dictionaries ----
        int32_t dictFieldOffset = useStatsDictB ? Il2CppOffsets::Ze_StatsDictB : Il2CppOffsets::Ze_StatsDictA;

        auto dictPtr            = m_mem.ReadPointer(*zePtr + dictFieldOffset);
        if (!dictPtr || *dictPtr == 0)
          continue;

        HeroResult hr;
        hr.id               = heroId;
        hr.name             = HeroMap.at(heroId);
        hr.heroInfoDataAddr = hiAddr;
        hr.vhInstanceAddr   = vhBase;
        hr.zeInstanceAddr   = *zePtr;

        hr.statsDictAddr    = *dictPtr;
        hr.stats            = m_statDict.ReadAll(*dictPtr);

        if (hr.stats.empty())
          continue;

        results.push_back(hr);
        break;  // first valid vh instance wins
      }
    }

    return results;
  }

private:
  ProcessMemory&       m_mem;
  AOBScanner           m_scanner;
  Il2CppStatDictionary m_statDict;
};
