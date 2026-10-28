#pragma once
#include "Il2CppOffsets.h"
#include "ProcessMemory.h"
#include "StatType.h"

#include <cstdint>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <vector>

/* -----------------------------------------------------------------------
 * Reads a real IL2CPP Dictionary<TKey, TValue> where TKey is a 4-byte
 * enum/int and TValue is a 4-byte float -- e.g. Dictionary<StatType, float>
 * as found in class `ze` (bgac / bgad fields).
 *
 * Uses centralized Il2CppDictOffsets and Il2CppArrayOffsets from Il2CppOffsets.h.
 * ----------------------------------------------------------------------- */
class Il2CppStatDictionary
{
public:
  explicit Il2CppStatDictionary(const ProcessMemory& mem) :
      m_mem(mem)
  {
  }

  struct StatData
  {
    uintptr_t address;
    float     value;
  };

  // Reads every {StatType, StatData} pair out of the dictionary at dictPtr.
  std::unordered_map<StatType, StatData> ReadAll(uintptr_t dictPtr) const
  {
    std::unordered_map<StatType, StatData> result;

    auto entriesPtr = m_mem.ReadPointer(dictPtr + Il2CppDictOffsets::Entries);
    if (!entriesPtr || *entriesPtr == 0)
      return result;

    auto length = m_mem.ReadInt32(*entriesPtr + Il2CppArrayOffsets::Length);
    if (!length || *length <= 0 || *length > 4096)
      return result;  // sanity cap

    // Pull the whole entries array in one ReadProcessMemory (max 64 KB)
    // instead of two remote reads per entry.
    const uintptr_t      dataAddr = *entriesPtr + Il2CppArrayOffsets::Data;
    std::vector<uint8_t> buf((size_t) *length * Il2CppDictOffsets::EntrySize);
    if (!m_mem.ReadBytes(dataAddr, buf.data(), buf.size()))
      return result;

    result.reserve((size_t) *length);
    for (int32_t i = 0; i < *length; i++) {
      const uint8_t* entry = buf.data() + (size_t) i * Il2CppDictOffsets::EntrySize;

      int32_t key;
      std::memcpy(&key, entry + Il2CppDictOffsets::EntryKeyOffset, sizeof(key));

      // hashCode == -1 (or key == 0/NONE with no valid hash) marks a
      // free/unused slot in .NET's Dictionary implementation -- skip.
      if (key <= 0)
        continue;

      float value;
      std::memcpy(&value, entry + Il2CppDictOffsets::EntryValOffset, sizeof(value));

      uintptr_t valAddr = dataAddr + (uintptr_t) i * Il2CppDictOffsets::EntrySize + Il2CppDictOffsets::EntryValOffset;
      result[(StatType) key] = {valAddr, value};
    }
    return result;
  }

  // Convenience: look up a single stat without materializing the whole map.
  std::optional<StatData> ReadOne(uintptr_t dictPtr, StatType stat) const
  {
    auto all = ReadAll(dictPtr);
    auto it  = all.find(stat);
    if (it == all.end())
      return std::nullopt;
    return it->second;
  }

private:
  const ProcessMemory& m_mem;
};
