#pragma once

#include "ProcessMemory.h"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>
#include <memory>

// Pattern element: value + whether it's a wildcard ("??" in CE's AOB syntax)
struct PatternByte
{
  uint8_t value    = 0;
  bool    wildcard = false;
};

// Parses a CE-style AOB string, e.g. "00 00 ?? ?? 8C 01" -> vector<PatternByte>
inline std::vector<PatternByte> ParsePattern(const std::string& aob)
{
  std::vector<PatternByte> pattern;
  std::istringstream       iss(aob);
  std::string              tok;
  while (iss >> tok) {
    PatternByte pb;
    if (tok == "??" || tok == "?") {
      pb.wildcard = true;
    }
    else {
      pb.value = (uint8_t) std::stoul(tok, nullptr, 16);
    }
    pattern.push_back(pb);
  }
  return pattern;
}

// Equivalent to CE's AOBScan(pattern, "+W-C"): scans committed/readable
// (optionally writable) regions of the target process for a byte pattern.
class AOBScanner
{
public:
  explicit AOBScanner(const ProcessMemory& mem) :
      m_mem(mem)
  {
  }

  // requireWritable mirrors "+W" in CE's protection flags string.
  std::vector<uintptr_t> Scan(const std::vector<PatternByte>& pattern, bool requireWritable = true) const
  {
    std::vector<uintptr_t> hits;
    if (pattern.empty())
      return hits;

    auto regions         = m_mem.EnumerateRegions(requireWritable);

    size_t maxRegionSize = 0;
    for (const auto& mbi : regions) {
      if (mbi.RegionSize > maxRegionSize && mbi.RegionSize <= (256ull * 1024 * 1024))
        maxRegionSize = mbi.RegionSize;
    }
    if (maxRegionSize == 0)
      return hits;

    std::unique_ptr<uint8_t[]> buf(new uint8_t[maxRegionSize]);

    for (const auto& mbi : regions) {
      size_t regionSize = mbi.RegionSize;
      if (regionSize == 0 || regionSize > maxRegionSize)
        continue;  // skip absurd regions

      SIZE_T bytesRead = 0;
      if (!ReadProcessMemory(m_mem.Handle(), mbi.BaseAddress, buf.get(), regionSize, &bytesRead) || bytesRead == 0) {
        continue;
      }

      if (bytesRead < pattern.size())
        continue;

      for (size_t i = 0; i + pattern.size() <= bytesRead; i++) {
        bool match = true;
        for (size_t j = 0; j < pattern.size(); j++) {
          if (!pattern[j].wildcard && buf[i + j] != pattern[j].value) {
            match = false;
            break;
          }
        }
        if (match) {
          hits.push_back((uintptr_t) mbi.BaseAddress + i);
        }
      }
    }
    return hits;
  }

  // Scans memory for an ObscuredLong with a specific decrypted value
  std::vector<uintptr_t> ScanObscuredLong(int64_t targetValue) const
  {
    std::vector<uintptr_t> hits;
    auto                   regions = m_mem.EnumerateRegions(true);

    size_t maxRegionSize           = 0;
    for (const auto& mbi : regions) {
      if (mbi.RegionSize > maxRegionSize && mbi.RegionSize <= (256ull * 1024 * 1024))
        maxRegionSize = mbi.RegionSize;
    }
    if (maxRegionSize == 0)
      return hits;

    std::unique_ptr<uint8_t[]> buf(new uint8_t[maxRegionSize]);

    for (const auto& mbi : regions) {
      size_t regionSize = mbi.RegionSize;
      if (regionSize == 0 || regionSize > maxRegionSize)
        continue;

      SIZE_T bytesRead = 0;
      if (!ReadProcessMemory(m_mem.Handle(), mbi.BaseAddress, buf.get(), regionSize, &bytesRead) || bytesRead == 0) {
        continue;
      }

      if (bytesRead < 24)
        continue;

      for (size_t i = 8; i + 24 <= bytesRead; i += 8) {
        int64_t hidden;
        int64_t key;
        std::memcpy(&hidden, &buf[i], sizeof(int64_t));
        std::memcpy(&key, &buf[i + 8], sizeof(int64_t));

        int64_t decrypted = key ^ hidden;
        if (std::abs(decrypted - targetValue) <= 5000) {
          int32_t realHash;
          int64_t fakeValue;
          std::memcpy(&realHash, &buf[i - 8], sizeof(int32_t));
          std::memcpy(&fakeValue, &buf[i + 16], sizeof(int64_t));
          int32_t calcHash = (int32_t) (targetValue ^ (targetValue >> 32));
          printf(
            "[DEBUG] Found ObscuredLong! target: %lld, found: %lld, fakeValue: %lld, realHash: "
            "%08X, calcHash: %08X\n",
            (long long) targetValue, (long long) decrypted, (long long) fakeValue, realHash, calcHash
          );

          hits.push_back((uintptr_t) mbi.BaseAddress + i);
        }
      }
    }
    return hits;
  }

  // Scans memory for an ObscuredDouble with a specific decrypted value
  std::vector<uintptr_t> ScanObscuredDouble(double targetValue) const
  {
    std::vector<uintptr_t> hits;
    auto                   regions = m_mem.EnumerateRegions(true);

    size_t maxRegionSize           = 0;
    for (const auto& mbi : regions) {
      if (mbi.RegionSize > maxRegionSize && mbi.RegionSize <= (256ull * 1024 * 1024))
        maxRegionSize = mbi.RegionSize;
    }
    if (maxRegionSize == 0)
      return hits;

    std::unique_ptr<uint8_t[]> buf(new uint8_t[maxRegionSize]);

    for (const auto& mbi : regions) {
      size_t regionSize = mbi.RegionSize;
      if (regionSize == 0 || regionSize > maxRegionSize)
        continue;

      SIZE_T bytesRead = 0;
      if (!ReadProcessMemory(m_mem.Handle(), mbi.BaseAddress, buf.get(), regionSize, &bytesRead) || bytesRead == 0) {
        continue;
      }

      if (bytesRead < 24)
        continue;

      for (size_t i = 0; i + 24 <= bytesRead; i += 8) {
        int64_t hidden;
        int64_t key;
        std::memcpy(&hidden, &buf[i], sizeof(int64_t));
        std::memcpy(&key, &buf[i + 8], sizeof(int64_t));

        int64_t decryptedBits = key ^ hidden;
        double  decryptedDouble;
        std::memcpy(&decryptedDouble, &decryptedBits, sizeof(double));

        if (std::abs(decryptedDouble - targetValue) < 2.0) {
          hits.push_back((uintptr_t) mbi.BaseAddress + i);
        }
      }
    }
    return hits;
  }

  std::vector<uintptr_t> Scan(const std::string& aob, bool requireWritable = true) const
  {
    return Scan(ParsePattern(aob), requireWritable);
  }

private:
  const ProcessMemory& m_mem;
};

inline std::string PointerToPatternString(uintptr_t ptr)
{
  std::ostringstream oss;
  for (int i = 0; i < 8; i++) {
    uint8_t b = (uint8_t) ((ptr >> (i * 8)) & 0xFF);
    char    buf[4];
    snprintf(buf, sizeof(buf), "%02X ", b);
    oss << buf;
  }
  return oss.str();
}

inline std::string IdToPatternString(uint32_t id)
{
  std::ostringstream oss;
  oss << "00 00 00 00 00 00 00 00 ";
  for (int i = 0; i < 4; i++) {
    uint8_t b = (uint8_t) ((id >> (i * 8)) & 0xFF);
    char    buf[4];
    snprintf(buf, sizeof(buf), "%02X ", b);
    oss << buf;
  }
  oss << "00 00 00 00";
  return oss.str();
}
