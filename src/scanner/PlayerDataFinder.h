#pragma once
#include "../core/AOBScanner.h"
#include "../core/Il2CppListReader.h"
#include "../core/ProcessMemory.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

struct PlayerDataResult
{
  uintptr_t          playerSaveDataAddr = 0;
  uintptr_t          heroListAddr       = 0;
  uintptr_t          currencyListAddr   = 0;
  uintptr_t          runeListAddr       = 0;
  std::map<int, int> runeMaxLevels;
};

struct RuneSaveInfo
{
  uintptr_t addr    = 0;
  int32_t   runeKey = 0;
  int32_t   level   = 0;
};

class PlayerDataFinder
{
public:
  explicit PlayerDataFinder(ProcessMemory& mem) :
      m_mem(mem),
      m_scanner(mem),
      m_listReader(mem)
  {
  }

  // Find PlayerSaveData by locating a known HeroSaveData and tracing back.
  std::optional<PlayerDataResult> Find();

  void ClearCache() { m_hasCache = false; }

  std::vector<RuneSaveInfo> ReadRunes(uintptr_t runeListAddr);

private:
  ProcessMemory&   m_mem;
  AOBScanner       m_scanner;
  Il2CppListReader m_listReader;

  bool             m_hasCache = false;
  PlayerDataResult m_cachedResult;
};
