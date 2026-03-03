#pragma once

#include "Cluster.h"
#include "Compressor.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ccs
{

/// Layout variants for serialization:
///   SoA_Packed:   [packed_header0..N] [pattern0..N]         — headers bit-packed into uint64_t
///   SoA_Unpacked: [sensorId0..N][row0..N][col0..N][h0..N][w0..N] [pattern0..N] — each field separate
///   AoS_Packed:   [packed_header0][pattern0] [packed_header1][pattern1] ...
///   AoS_Unpacked: [sensorId0][row0][col0][h0][w0][pattern0] [sensorId1]... — raw fields interleaved
enum class Layout {
  SoA_Packed,
  SoA_Unpacked,
  AoS_Packed,
  AoS_Unpacked,
};

inline const char* layoutName(Layout l)
{
  switch (l) {
    case Layout::SoA_Packed:   return "SoA_P";
    case Layout::SoA_Unpacked: return "SoA_U";
    case Layout::AoS_Packed:   return "AoS_P";
    case Layout::AoS_Unpacked: return "AoS_U";
  }
  return "?";
}

inline constexpr Layout allLayouts[] = {
  Layout::SoA_Packed,
  Layout::SoA_Unpacked,
  Layout::AoS_Packed,
  Layout::AoS_Unpacked,
};

/// A compressed chunk of clusters.
struct CompressedChunk {
  uint32_t nClusters = 0;
  Layout layout = Layout::SoA_Packed;

  // SoA: two separate blobs
  std::vector<uint8_t> compressedHeaders;
  std::vector<uint8_t> compressedPatterns;
  size_t originalHeaderBytes = 0;
  size_t originalPatternBytes = 0;

  // AoS: single interleaved blob
  std::vector<uint8_t> compressedData;
  size_t originalDataBytes = 0;

  size_t totalCompressedBytes() const
  {
    if (layout == Layout::SoA_Packed || layout == Layout::SoA_Unpacked)
      return compressedHeaders.size() + compressedPatterns.size();
    return compressedData.size();
  }
  size_t totalOriginalBytes() const
  {
    if (layout == Layout::SoA_Packed || layout == Layout::SoA_Unpacked)
      return originalHeaderBytes + originalPatternBytes;
    return originalDataBytes;
  }
};

/// SoA: headers and patterns compressed separately.
CompressedChunk encodeSoA(std::span<const Cluster> clusters,
                          ICompressor& comp, int level,
                          const LayoutConfig& cfg = {});
std::vector<Cluster> decodeSoA(const CompressedChunk& chunk,
                               ICompressor& comp,
                               const LayoutConfig& cfg = {});

/// AoS: each cluster serialized as [header][pattern] interleaved, single blob.
CompressedChunk encodeAoS(std::span<const Cluster> clusters,
                          ICompressor& comp, int level,
                          const LayoutConfig& cfg = {});
std::vector<Cluster> decodeAoS(const CompressedChunk& chunk,
                               ICompressor& comp,
                               const LayoutConfig& cfg = {});

// ---- Serialization without compression (for benchmarks) ----

/// Serialize clusters into a flat buffer using the given layout (no compression).
std::vector<uint8_t> serialize(std::span<const Cluster> clusters, Layout layout,
                               const LayoutConfig& cfg = {});

/// Deserialize clusters from a flat buffer produced by serialize().
std::vector<Cluster> deserialize(std::span<const uint8_t> data, uint32_t nClusters,
                                 Layout layout, const LayoutConfig& cfg = {});

} // namespace ccs
