#pragma once

#include "ProcessMemory.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <memory>

// Unaligned-safe 64-bit load from a scan buffer.
inline uint64_t LoadU64(const uint8_t* p)
{
  uint64_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

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

  // ---------------------------------------------------------------------------
  // Fast parallel scan over 8-byte aligned slots.
  //
  // IL2CPP heap objects (and every pointer field inside them) are 8-byte
  // aligned, so visiting only aligned slots is lossless for object/pointer
  // searches while being far cheaper than a byte-by-byte AOB walk.
  //
  //  - Memory is read in fixed-size chunks (no giant per-region buffers, and an
  //    unreadable page only loses one chunk instead of a whole region).
  //  - Chunks are distributed across worker threads.
  //  - `pred(const uint8_t* p)` is called for every aligned slot that has at
  //    least `windowSize` valid bytes at `p`; return true to record a hit.
  //    It runs concurrently on several threads, so it must be read-only.
  //  - One pass can match many targets at once (put them all in `pred`).
  //
  // Returned hits are sorted ascending (same order as the sequential Scan()).
  // ---------------------------------------------------------------------------
  template<typename Pred>
  std::vector<uintptr_t>
  ScanAligned(Pred pred, size_t windowSize, bool requireWritable = true, bool privateOnly = false) const
  {
    constexpr size_t kAlign     = 8;
    constexpr size_t kChunkSize = 1024 * 1024;
    constexpr size_t kMaxRegion = 256ull * 1024 * 1024;  // same cap as Scan()

    std::vector<uintptr_t> hits;
    if (windowSize == 0)
      return hits;

    struct Chunk
    {
      uintptr_t base;
      size_t    scanLen;  // slots that "belong" to this chunk
      size_t    readLen;  // scanLen + overlap so the last window is complete
    };
    std::vector<Chunk> chunks;
    for (const auto& mbi : m_mem.EnumerateRegions(requireWritable, privateOnly)) {
      if (mbi.RegionSize == 0 || mbi.RegionSize > kMaxRegion)
        continue;
      uintptr_t regionBase = (uintptr_t) mbi.BaseAddress;  // page aligned -> slots stay 8-aligned
      uintptr_t regionEnd  = regionBase + mbi.RegionSize;
      for (uintptr_t base = regionBase; base < regionEnd; base += kChunkSize) {
        size_t scanLen = std::min<size_t>(kChunkSize, regionEnd - base);
        size_t readLen = std::min<size_t>(scanLen + windowSize, regionEnd - base);
        chunks.push_back({base, scanLen, readLen});
      }
    }
    if (chunks.empty())
      return hits;

    unsigned workerCount = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
    workerCount          = (unsigned) std::min<size_t>(workerCount, chunks.size());

    std::atomic<size_t> nextChunk{0};
    std::mutex          hitsMutex;

    auto worker = [&]() {
      std::unique_ptr<uint8_t[]> buf(new uint8_t[kChunkSize + windowSize]);
      std::vector<uintptr_t> localHits;

      for (size_t idx; (idx = nextChunk.fetch_add(1, std::memory_order_relaxed)) < chunks.size();) {
        const Chunk& c         = chunks[idx];
        SIZE_T       bytesRead = 0;
        if (
          !ReadProcessMemory(m_mem.Handle(), (LPCVOID) c.base, buf.get(), c.readLen, &bytesRead)
          || bytesRead < windowSize
        )
          continue;

        const size_t   limit = std::min<size_t>(c.scanLen, bytesRead - windowSize + 1);
        const uint8_t* p     = buf.get();
        for (size_t off = 0; off < limit; off += kAlign) {
          if (pred(p + off))
            localHits.push_back(c.base + off);
        }
      }

      if (!localHits.empty()) {
        std::lock_guard<std::mutex> lock(hitsMutex);
        hits.insert(hits.end(), localHits.begin(), localHits.end());
      }
    };

    std::vector<std::thread> threads;
    threads.reserve(workerCount - 1);
    for (unsigned t = 1; t < workerCount; t++)
      threads.emplace_back(worker);
    worker();  // calling thread does its share too
    for (auto& th : threads)
      th.join();

    std::sort(hits.begin(), hits.end());
    return hits;
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
