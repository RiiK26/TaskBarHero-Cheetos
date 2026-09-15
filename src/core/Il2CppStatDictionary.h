#pragma once
#include "ProcessMemory.h"
#include "StatType.h"

#include <cstdint>
#include <optional>
#include <unordered_map>

/* -----------------------------------------------------------------------
 * Reads a real IL2CPP Dictionary<TKey, TValue> where TKey is a 4-byte
 * enum/int and TValue is a 4-byte float -- e.g. Dictionary<StatType, float>
 * as found in class `ze` (bgac / bgad fields).
 *
 * Standard IL2CPP layout:
 *   dictPtr + 0x18        -> _entries array pointer
 *   entriesPtr + 0x18     -> array Length (standard IL2CPP array header)
 *   entriesPtr + 0x20     -> start of entry data
 *   each entry is 16 bytes: { int32 hashCode; int32 next; int32 key; float value; }
 *     key   at entry offset +8
 *     value at entry offset +12
 * ----------------------------------------------------------------------- */
class Il2CppStatDictionary
{
public:
  static constexpr int32_t EntriesOffset     = 0x18;
  static constexpr int32_t ArrayLengthOffset = 0x18;
  static constexpr int32_t ArrayDataOffset   = 0x20;
  static constexpr int32_t EntrySize         = 16;
  static constexpr int32_t EntryKeyOffset    = 8;
  static constexpr int32_t EntryValueOffset  = 12;

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

    auto entriesPtr = m_mem.ReadPointer(dictPtr + EntriesOffset);
    if (!entriesPtr || *entriesPtr == 0)
      return result;

    auto length = m_mem.ReadInt32(*entriesPtr + ArrayLengthOffset);
    if (!length || *length <= 0 || *length > 4096)
      return result;  // sanity cap

    for (int32_t i = 0; i < *length; i++) {
      uintptr_t entryAddr = *entriesPtr + ArrayDataOffset + (i * EntrySize);

      auto key            = m_mem.ReadInt32(entryAddr + EntryKeyOffset);
      if (!key)
        continue;

      // hashCode == -1 (or key == 0/NONE with no valid hash) marks a
      // free/unused slot in .NET's Dictionary implementation -- skip.
      if (*key <= 0)
        continue;

      uintptr_t valAddr = entryAddr + EntryValueOffset;
      auto      value   = m_mem.ReadFloat(valAddr);
      if (!value)
        continue;

      result[(StatType) *key] = {valAddr, *value};
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
