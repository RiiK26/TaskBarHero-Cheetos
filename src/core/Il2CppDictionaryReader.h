#pragma once
#include "../core/ProcessMemory.h"

#include <cstdint>
#include <optional>
#include <vector>

/* -----------------------------------------------------------------------
 * IL2CPP Dictionary<TKey, TValue> reader for specific struct layouts.
 *
 * Dictionary<TKey, TValue> memory layout:
 *   dictPtr + 0x10 -> int[] buckets
 *   dictPtr + 0x18 -> Entry[] entries
 *   dictPtr + 0x20 -> int count
 *
 * Entry<int, object*> memory layout:
 *   0x00: int hashCode
 *   0x04: int next
 *   0x08: int key (TKey)
 *   0x10: uintptr_t value (TValue)
 *   Size: 0x18
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
  std::optional<int32_t> GetCount(uintptr_t dictPtr) const { return m_mem.ReadInt32(dictPtr + 0x20); }

  // Reads the outer dictionary: Dictionary<int, Dictionary<...>>
  // Returns a vector of pairs: <RuneKey, InnerDictionaryPtr>
  std::vector<std::pair<int32_t, uintptr_t>> ReadOuterEntries(uintptr_t dictPtr) const
  {
    std::vector<std::pair<int32_t, uintptr_t>> result;

    auto countOpt = GetCount(dictPtr);
    if (!countOpt || *countOpt <= 0 || *countOpt > 10000)
      return result;
    int32_t count        = *countOpt;

    auto entriesArrayPtr = m_mem.ReadPointer(dictPtr + 0x18);
    if (!entriesArrayPtr || *entriesArrayPtr == 0)
      return result;

    // In an IL2CPP Array, elements start at offset 0x20
    uintptr_t firstElementAddr = *entriesArrayPtr + 0x20;
    size_t    entrySize        = 0x18;  // Size of Entry<int, object*>

    // The actual array size might be bigger than count (due to capacity).
    // We can just read until we find `count` valid items or reach an upper bound.
    auto arrayLen = m_mem.ReadInt32(*entriesArrayPtr + 0x18);
    if (!arrayLen || *arrayLen <= 0)
      return result;

    int32_t itemsToRead = std::min(*arrayLen, 2000);  // sanity cap

    for (int32_t i = 0; i < itemsToRead; i++) {
      uintptr_t entryAddr = firstElementAddr + (i * entrySize);

      auto hashCode       = m_mem.ReadInt32(entryAddr + 0x00);
      if (!hashCode || *hashCode < 0)
        continue;  // Invalid or empty entry

      auto key = m_mem.ReadInt32(entryAddr + 0x08);
      auto val = m_mem.ReadPointer(entryAddr + 0x10);

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
