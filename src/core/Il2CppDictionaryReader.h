#pragma once
#include "Il2CppOffsets.h"
#include "ProcessMemory.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

/* -----------------------------------------------------------------------
 * IL2CPP Dictionary<TKey, TValue> reader for specific struct layouts.
 * Uses centralized Il2CppDictOffsets and Il2CppArrayOffsets from Il2CppOffsets.h.
 * ----------------------------------------------------------------------- */
class Il2CppDictionaryReader
{
private:
  const ProcessMemory& m_mem;

public:
  explicit Il2CppDictionaryReader(const ProcessMemory& mem) :
      m_mem(mem)
  {
  }

  // Returns the total number of valid entries in the dictionary.
  std::optional<int32_t> GetCount(uintptr_t dictPtr) const
  {
    return m_mem.ReadInt32(dictPtr + Il2CppDictOffsets::Count);
  }

  // Reads the outer dictionary: Dictionary<int, Dictionary<...>>
  // Returns a vector of pairs: <RuneKey, InnerDictionaryPtr>
  std::vector<std::pair<int32_t, uintptr_t>> ReadOuterEntries(uintptr_t dictPtr) const
  {
    std::vector<std::pair<int32_t, uintptr_t>> result;

    auto countOpt = GetCount(dictPtr);
    if (!countOpt || *countOpt <= 0 || *countOpt > 10000)
      return result;
    int32_t count        = *countOpt;

    auto entriesArrayPtr = m_mem.ReadPointer(dictPtr + Il2CppDictOffsets::Entries);
    if (!entriesArrayPtr || *entriesArrayPtr == 0)
      return result;

    // In an IL2CPP Array, elements start at offset ArrayData (0x20)
    uintptr_t firstElementAddr = *entriesArrayPtr + Il2CppArrayOffsets::Data;
    size_t    entrySize        = Il2CppDictOffsets::EntrySizeRef;  // Size of Entry<int, object*> (24 bytes)

    // The actual array size might be bigger than count (due to capacity).
    // We can just read until we find `count` valid items or reach an upper bound.
    auto arrayLen = m_mem.ReadInt32(*entriesArrayPtr + Il2CppArrayOffsets::Length);
    if (!arrayLen || *arrayLen <= 0)
      return result;

    int32_t itemsToRead = std::min(*arrayLen, 2000);  // sanity cap

    for (int32_t i = 0; i < itemsToRead; i++) {
      uintptr_t entryAddr = firstElementAddr + (i * entrySize);

      auto hashCode       = m_mem.ReadInt32(entryAddr + Il2CppDictOffsets::EntryHashCode);
      if (!hashCode || *hashCode < 0)
        continue;  // Invalid or empty entry

      auto key = m_mem.ReadInt32(entryAddr + Il2CppDictOffsets::EntryKeyOffset);
      auto val = m_mem.ReadPointer(entryAddr + Il2CppDictOffsets::EntryValOffsetRef);

      if (key && val && *val != 0) {
        result.push_back({*key, *val});
        if (result.size() >= static_cast<size_t>(count)) {
          break;  // found all items
        }
      }
    }

    return result;
  }
};
