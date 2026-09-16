#include "RuneUnlocker.h"
#include <map>
#include <string>

const std::vector<RuneSaveInfo>& RuneUnlocker::ScanRunes(PlayerDataFinder& finder, const std::map<int, int>& maxLevels)
{
  if (m_runeListAddr != 0) {
    m_runes         = finder.ReadRunes(m_runeListAddr);
    m_runeMaxLevels = maxLevels;
  }
  return m_runes;
}

std::string RuneUnlocker::UpgradeRune(int32_t runeKey, int32_t addAmount)
{
  for (auto& ri : m_runes) {
    if (ri.runeKey == runeKey) {
      int maxLevel = 3;  // default safe fallback if not found
      if (m_runeMaxLevels.find(runeKey) != m_runeMaxLevels.end()) {
        maxLevel = m_runeMaxLevels[runeKey];
      }

      int32_t newLevel = ri.level + addAmount;
      if (newLevel > maxLevel)
        newLevel = maxLevel;

      if (ri.level >= maxLevel) {
        return "[Runes] Rune " + std::to_string(runeKey) + " is already at Max Level (" + std::to_string(maxLevel)
             + ").";
      }

      uintptr_t levelAddr = ri.addr + RuneSaveDataOffsets::Level;
      if (m_mem.WriteInt32(levelAddr, newLevel)) {
        ri.level = newLevel;  // Update cache
        return "[Runes] Upgraded rune " + std::to_string(runeKey) + " to level " + std::to_string(newLevel);
      }
      return "[Runes] Failed to write memory for rune " + std::to_string(runeKey);
    }
  }
  return "[Runes] Rune " + std::to_string(runeKey) + " not found.";
}

std::string RuneUnlocker::UnlockAllLocked()
{
  if (m_runeListAddr == 0 || m_runes.empty())
    return "[Runes] No runes scanned or list not found.";

  int successCount = 0;
  for (auto& ri : m_runes) {
    if (ri.level == 0) {
      uintptr_t levelAddr = ri.addr + RuneSaveDataOffsets::Level;
      if (m_mem.WriteInt32(levelAddr, 1)) {
        ri.level = 1;  // Update cache
        successCount++;
      }
    }
  }

  return "[Runes] Unlocked " + std::to_string(successCount) + " previously locked runes.";
}

std::string RuneUnlocker::UpgradeAllUnlocked(int32_t addAmount)
{
  if (m_runeListAddr == 0 || m_runes.empty())
    return "[Runes] No runes scanned or list not found.";

  int successCount = 0;
  int skippedMax   = 0;
  for (auto& ri : m_runes) {
    if (ri.level > 0) {
      int maxLevel = 5;  // default safe fallback if not found
      if (m_runeMaxLevels.find(ri.runeKey) != m_runeMaxLevels.end()) {
        maxLevel = m_runeMaxLevels[ri.runeKey];
      }

      if (ri.level >= maxLevel) {
        skippedMax++;
        continue;
      }

      int32_t newLevel = ri.level + addAmount;
      if (newLevel > maxLevel)
        newLevel = maxLevel;

      uintptr_t levelAddr = ri.addr + RuneSaveDataOffsets::Level;
      if (m_mem.WriteInt32(levelAddr, newLevel)) {
        ri.level = newLevel;  // Update cache
        successCount++;
      }
    }
  }

  return "[Runes] Upgraded " + std::to_string(successCount) + " unlocked runes. Skipped " + std::to_string(skippedMax)
       + " maxed runes.";
}

std::string RuneUnlocker::UpgradeAllToMax()
{
  if (m_runeListAddr == 0 || m_runes.empty())
    return "[Runes] No runes scanned or list not found.";

  int successCount = 0;
  int skippedMax   = 0;
  for (auto& ri : m_runes) {
    if (ri.level > 0) {
      int maxLevel = 3;  // default safe fallback
      if (m_runeMaxLevels.find(ri.runeKey) != m_runeMaxLevels.end()) {
        maxLevel = m_runeMaxLevels[ri.runeKey];
      }

      if (ri.level >= maxLevel) {
        skippedMax++;
        continue;
      }

      uintptr_t levelAddr = ri.addr + RuneSaveDataOffsets::Level;
      if (m_mem.WriteInt32(levelAddr, maxLevel)) {
        ri.level = maxLevel;  // Update cache
        successCount++;
      }
    }
  }

  return "[Runes] Blasted " + std::to_string(successCount) + " runes to Max Level! Skipped "
       + std::to_string(skippedMax) + " maxed. NOTE: To trigger Steam Cloud Sync, fully CLOSE the game AND this app.";
}

int RuneUnlocker::GetMaxLevelForRune(int runeKey) const
{
  if (m_runeMaxLevels.find(runeKey) != m_runeMaxLevels.end()) {
    return m_runeMaxLevels.at(runeKey);
  }
  return 3;  // Fallback
}
