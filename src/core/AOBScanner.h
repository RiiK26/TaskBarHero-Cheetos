#pragma once

#include "ProcessMemory.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <memory>

// Unaligned-safe 64-bit load from a scan buffer.
inline uint64_t LoadU64(const uint8_t* p)
{
  uint64_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

// Unaligned-safe 32-bit load from a scan buffer.
inline uint32_t LoadU32(const uint8_t* p)
{
  uint32_t v;
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
//
// Every scan in this class runs on the same engine (ScanStrided):
//  - memory is read in 1 MB chunks (no giant per-region buffers, and an
//    unreadable page only loses one chunk instead of a whole region),
//  - chunks are distributed across worker threads,
//  - the match test is an inlined predicate, so one pass can look for many
//    targets at once.
class AOBScanner
{
public:
  explicit AOBScanner(const ProcessMemory& mem) :
      m_mem(mem)
  {
  }

  // ---------------------------------------------------------------------------
  // Byte-pattern scan (any alignment). requireWritable mirrors "+W" in CE's
  // protection flags string.
  // ---------------------------------------------------------------------------
  std::vector<uintptr_t>
  Scan(const std::vector<PatternByte>& pattern, bool requireWritable = true, bool privateOnly = false) const
  {
    if (pattern.empty())
      return {};

    // Anchor on the first concrete byte for a cheap early reject.
    size_t anchor = 0;
    while (anchor < pattern.size() && pattern[anchor].wildcard)
      anchor++;
    if (anchor == pattern.size())
      anchor = 0;  // all wildcards: anchor check below is skipped
    const bool    allWild   = pattern[anchor].wildcard;
    const uint8_t anchorVal = pattern[anchor].value;

    return ScanStrided(
      [&pattern, anchor, anchorVal, allWild](const uint8_t* p) {
        if (!allWild && p[anchor] != anchorVal)
          return false;
        for (size_t j = 0; j < pattern.size(); j++) {
          if (!pattern[j].wildcard && p[j] != pattern[j].value)
            return false;
        }
        return true;
      },
      pattern.size(), 1, requireWritable, privateOnly
    );
  }

  std::vector<uintptr_t> Scan(const std::string& aob, bool requireWritable = true, bool privateOnly = false) const
  {
    return Scan(ParsePattern(aob), requireWritable, privateOnly);
  }

  // ---------------------------------------------------------------------------
  // Finds every 8-byte aligned slot holding a pointer to ANY of `targets`,
  // in a single pass. Returns target -> referencing addresses (ascending).
  // ---------------------------------------------------------------------------
  std::unordered_map<uintptr_t, std::vector<uintptr_t>>
  FindPointerRefs(std::vector<uintptr_t> targets, bool requireWritable = true, bool privateOnly = false) const
  {
    std::unordered_map<uintptr_t, std::vector<uintptr_t>> refs;

    targets.erase(std::remove(targets.begin(), targets.end(), (uintptr_t) 0), targets.end());
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    if (targets.empty())
      return refs;

    const uint64_t lo   = targets.front();
    const uint64_t hi   = targets.back();
    auto           hits = ScanAligned(
      [&targets, lo, hi](const uint8_t* p) {
        uint64_t v = LoadU64(p);
        if (v < lo || v > hi)
          return false;
        return std::binary_search(targets.begin(), targets.end(), (uintptr_t) v);
      },
      8, requireWritable, privateOnly
    );

    // Hits are sorted, so each per-target list stays in ascending order.
    for (uintptr_t hit : hits) {
      auto v = m_mem.ReadPointer(hit);
      if (v && std::binary_search(targets.begin(), targets.end(), *v))
        refs[*v].push_back(hit);
    }
    return refs;
  }

  // Scans memory for an ObscuredLong with a specific decrypted value
  std::vector<uintptr_t> ScanObscuredLong(int64_t targetValue) const
  {
    // Window starts 8 bytes before the hidden value (realHash lives there):
    //   [p+0] realHash | [p+8] hidden | [p+16] key | [p+24] fakeValue
    auto hits = ScanAligned(
      [targetValue](const uint8_t* p) {
        int64_t hidden    = (int64_t) LoadU64(p + 8);
        int64_t key       = (int64_t) LoadU64(p + 16);
        int64_t decrypted = key ^ hidden;
        if (std::abs(decrypted - targetValue) > 5000)
          return false;

        int32_t realHash  = (int32_t) LoadU32(p);
        int64_t fakeValue = (int64_t) LoadU64(p + 24);
        int32_t calcHash  = (int32_t) (targetValue ^ (targetValue >> 32));
        printf(
          "[DEBUG] Found ObscuredLong! target: %lld, found: %lld, fakeValue: %lld, realHash: "
          "%08X, calcHash: %08X\n",
          (long long) targetValue, (long long) decrypted, (long long) fakeValue, realHash, calcHash
        );
        return true;
      },
      32, true
    );

    for (auto& h : hits)
      h += 8;  // report the address of hiddenValue (same as before)
    return hits;
  }

  // Scans memory for an ObscuredDouble with a specific decrypted value
  std::vector<uintptr_t> ScanObscuredDouble(double targetValue) const
  {
    return ScanAligned(
      [targetValue](const uint8_t* p) {
        uint64_t decryptedBits = LoadU64(p + 8) ^ LoadU64(p);
        double   decryptedDouble;
        std::memcpy(&decryptedDouble, &decryptedBits, sizeof(double));
        return std::abs(decryptedDouble - targetValue) < 2.0;
      },
      24, true
    );
  }

  // ---------------------------------------------------------------------------
  // Parallel scan over 8-byte aligned slots.
  //
  // IL2CPP heap objects (and every pointer field inside them) are 8-byte
  // aligned, so visiting only aligned slots is lossless for object/pointer
  // searches while being far cheaper than a byte-by-byte walk.
  // ---------------------------------------------------------------------------
  template<typename Pred>
  std::vector<uintptr_t>
  ScanAligned(Pred pred, size_t windowSize, bool requireWritable = true, bool privateOnly = false) const
  {
    return ScanStrided(pred, windowSize, 8, requireWritable, privateOnly);
  }

  // ---------------------------------------------------------------------------
  // Core scan engine.
  //
  //  - `pred(const uint8_t* p)` is called for every slot (every `stride` bytes
  //    from a region start) that has at least `windowSize` valid bytes at `p`;
  //    return true to record a hit. It runs concurrently on several threads,
  //    so it must be read-only.
  //  - `stride` must divide the page size (1, 2, 4 or 8).
  //
  // Returned hits are sorted ascending (same order as a sequential walk).
  // ---------------------------------------------------------------------------
  template<typename Pred>
  std::vector<uintptr_t>
  ScanStrided(Pred pred, size_t windowSize, size_t stride, bool requireWritable = true, bool privateOnly = false) const
  {
    constexpr size_t kChunkSize = 1024 * 1024;
    constexpr size_t kMaxRegion = 256ull * 1024 * 1024;  // skip absurd regions

    std::vector<uintptr_t> hits;
    if (windowSize == 0 || stride == 0)
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
      uintptr_t regionBase = (uintptr_t) mbi.BaseAddress;  // page aligned -> slots stay aligned
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
        for (size_t off = 0; off < limit; off += stride) {
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
