#include "ClusterIO.h"

#include <cstring>
#include <stdexcept>

namespace ccs
{

// Per-record raw field sizes (unpacked)
static constexpr size_t kFieldBytes = sizeof(uint32_t) + 4 * sizeof(uint16_t); // sensorId(4) + row(2) + col(2) + h(2) + w(2) = 12

// ===================== SoA =====================

CompressedChunk encodeSoA(std::span<const Cluster> clusters,
                          ICompressor& comp, int level,
                          const LayoutConfig& cfg)
{
  const uint32_t n = static_cast<uint32_t>(clusters.size());

  std::vector<uint8_t> rawHeaders(n * sizeof(uint64_t));
  for (uint32_t i = 0; i < n; ++i) {
    uint64_t h = packHeader(clusters[i], cfg);
    std::memcpy(rawHeaders.data() + i * sizeof(uint64_t), &h, sizeof(uint64_t));
  }

  size_t totalPatternBytes = 0;
  for (auto& c : clusters)
    totalPatternBytes += c.pattern.size();

  std::vector<uint8_t> rawPatterns(totalPatternBytes);
  size_t off = 0;
  for (auto& c : clusters) {
    std::memcpy(rawPatterns.data() + off, c.pattern.data(), c.pattern.size());
    off += c.pattern.size();
  }

  CompressedChunk chunk;
  chunk.nClusters = n;
  chunk.layout = Layout::SoA_Packed;
  chunk.originalHeaderBytes = rawHeaders.size();
  chunk.originalPatternBytes = rawPatterns.size();
  chunk.compressedHeaders = comp.compress(rawHeaders, level);
  chunk.compressedPatterns = comp.compress(rawPatterns, level);
  return chunk;
}

std::vector<Cluster> decodeSoA(const CompressedChunk& chunk,
                               ICompressor& comp,
                               const LayoutConfig& cfg)
{
  auto rawHeaders = comp.decompress(chunk.compressedHeaders, chunk.originalHeaderBytes);
  auto rawPatterns = comp.decompress(chunk.compressedPatterns, chunk.originalPatternBytes);

  std::vector<Cluster> out(chunk.nClusters);
  for (uint32_t i = 0; i < chunk.nClusters; ++i) {
    uint64_t h;
    std::memcpy(&h, rawHeaders.data() + i * sizeof(uint64_t), sizeof(uint64_t));
    unpackHeader(h, out[i], cfg);
  }

  size_t off = 0;
  for (auto& c : out) {
    size_t nb = c.patternBytes();
    if (off + nb > rawPatterns.size())
      throw std::runtime_error("pattern data truncated");
    c.pattern.assign(rawPatterns.data() + off, rawPatterns.data() + off + nb);
    off += nb;
  }

  return out;
}

// ===================== AoS =====================

CompressedChunk encodeAoS(std::span<const Cluster> clusters,
                          ICompressor& comp, int level,
                          const LayoutConfig& cfg)
{
  const uint32_t n = static_cast<uint32_t>(clusters.size());

  size_t totalBytes = 0;
  for (auto& c : clusters)
    totalBytes += sizeof(uint64_t) + c.pattern.size();

  std::vector<uint8_t> raw(totalBytes);
  size_t off = 0;
  for (auto& c : clusters) {
    uint64_t h = packHeader(c, cfg);
    std::memcpy(raw.data() + off, &h, sizeof(uint64_t));
    off += sizeof(uint64_t);
    std::memcpy(raw.data() + off, c.pattern.data(), c.pattern.size());
    off += c.pattern.size();
  }

  CompressedChunk chunk;
  chunk.nClusters = n;
  chunk.layout = Layout::AoS_Packed;
  chunk.originalDataBytes = totalBytes;
  chunk.compressedData = comp.compress(raw, level);
  return chunk;
}

std::vector<Cluster> decodeAoS(const CompressedChunk& chunk,
                               ICompressor& comp,
                               const LayoutConfig& cfg)
{
  auto raw = comp.decompress(chunk.compressedData, chunk.originalDataBytes);

  std::vector<Cluster> out(chunk.nClusters);
  size_t off = 0;
  for (uint32_t i = 0; i < chunk.nClusters; ++i) {
    uint64_t h;
    std::memcpy(&h, raw.data() + off, sizeof(uint64_t));
    off += sizeof(uint64_t);
    unpackHeader(h, out[i], cfg);
    size_t nb = out[i].patternBytes();
    if (off + nb > raw.size())
      throw std::runtime_error("AoS data truncated");
    out[i].pattern.assign(raw.data() + off, raw.data() + off + nb);
    off += nb;
  }

  return out;
}

// ===================== Serialize (no compression) =====================

// Helper: write a value into buf at offset, advance offset
template <typename T>
static void writeVal(std::vector<uint8_t>& buf, size_t& off, T val)
{
  std::memcpy(buf.data() + off, &val, sizeof(T));
  off += sizeof(T);
}

template <typename T>
static T readVal(const uint8_t* p, size_t& off)
{
  T val;
  std::memcpy(&val, p + off, sizeof(T));
  off += sizeof(T);
  return val;
}

std::vector<uint8_t> serialize(std::span<const Cluster> clusters, Layout layout,
                               const LayoutConfig& cfg)
{
  const uint32_t n = static_cast<uint32_t>(clusters.size());

  size_t totalPatternBytes = 0;
  for (auto& c : clusters)
    totalPatternBytes += c.pattern.size();

  switch (layout) {

    case Layout::SoA_Packed: {
      std::vector<uint8_t> buf(n * sizeof(uint64_t) + totalPatternBytes);
      for (uint32_t i = 0; i < n; ++i) {
        uint64_t h = packHeader(clusters[i], cfg);
        std::memcpy(buf.data() + i * sizeof(uint64_t), &h, sizeof(uint64_t));
      }
      size_t off = n * sizeof(uint64_t);
      for (auto& c : clusters) {
        std::memcpy(buf.data() + off, c.pattern.data(), c.pattern.size());
        off += c.pattern.size();
      }
      return buf;
    }

    case Layout::SoA_Unpacked: {
      // [sensorId0..N][row0..N][col0..N][height0..N][width0..N][patterns]
      size_t headerBytes = n * (sizeof(uint32_t) + 4 * sizeof(uint16_t));
      std::vector<uint8_t> buf(headerBytes + totalPatternBytes);
      size_t offSensor = 0;
      size_t offRow    = n * sizeof(uint32_t);
      size_t offCol    = offRow + n * sizeof(uint16_t);
      size_t offH      = offCol + n * sizeof(uint16_t);
      size_t offW      = offH + n * sizeof(uint16_t);
      for (uint32_t i = 0; i < n; ++i) {
        writeVal(buf, offSensor, clusters[i].sensorId);
        writeVal(buf, offRow, clusters[i].row);
        writeVal(buf, offCol, clusters[i].col);
        writeVal(buf, offH, clusters[i].height);
        writeVal(buf, offW, clusters[i].width);
      }
      size_t offPat = headerBytes;
      for (auto& c : clusters) {
        std::memcpy(buf.data() + offPat, c.pattern.data(), c.pattern.size());
        offPat += c.pattern.size();
      }
      return buf;
    }

    case Layout::AoS_Packed: {
      size_t totalBytes = n * sizeof(uint64_t) + totalPatternBytes;
      std::vector<uint8_t> buf(totalBytes);
      size_t off = 0;
      for (auto& c : clusters) {
        uint64_t h = packHeader(c, cfg);
        writeVal(buf, off, h);
        std::memcpy(buf.data() + off, c.pattern.data(), c.pattern.size());
        off += c.pattern.size();
      }
      return buf;
    }

    case Layout::AoS_Unpacked: {
      // [sensorId][row][col][height][width][pattern] per cluster
      size_t totalBytes = n * kFieldBytes + totalPatternBytes;
      std::vector<uint8_t> buf(totalBytes);
      size_t off = 0;
      for (auto& c : clusters) {
        writeVal(buf, off, c.sensorId);
        writeVal(buf, off, c.row);
        writeVal(buf, off, c.col);
        writeVal(buf, off, c.height);
        writeVal(buf, off, c.width);
        std::memcpy(buf.data() + off, c.pattern.data(), c.pattern.size());
        off += c.pattern.size();
      }
      return buf;
    }
  }
  return {};
}

std::vector<Cluster> deserialize(std::span<const uint8_t> data, uint32_t nClusters,
                                 Layout layout, const LayoutConfig& cfg)
{
  std::vector<Cluster> out(nClusters);
  const uint8_t* p = data.data();

  switch (layout) {

    case Layout::SoA_Packed: {
      for (uint32_t i = 0; i < nClusters; ++i) {
        uint64_t h;
        std::memcpy(&h, p + i * sizeof(uint64_t), sizeof(uint64_t));
        unpackHeader(h, out[i], cfg);
      }
      size_t off = nClusters * sizeof(uint64_t);
      for (auto& c : out) {
        size_t nb = c.patternBytes();
        c.pattern.assign(p + off, p + off + nb);
        off += nb;
      }
      break;
    }

    case Layout::SoA_Unpacked: {
      size_t offSensor = 0;
      size_t offRow    = nClusters * sizeof(uint32_t);
      size_t offCol    = offRow + nClusters * sizeof(uint16_t);
      size_t offH      = offCol + nClusters * sizeof(uint16_t);
      size_t offW      = offH + nClusters * sizeof(uint16_t);
      for (uint32_t i = 0; i < nClusters; ++i) {
        out[i].sensorId = readVal<uint32_t>(p, offSensor);
        out[i].row      = readVal<uint16_t>(p, offRow);
        out[i].col      = readVal<uint16_t>(p, offCol);
        out[i].height   = readVal<uint16_t>(p, offH);
        out[i].width    = readVal<uint16_t>(p, offW);
      }
      size_t offPat = nClusters * kFieldBytes;
      for (auto& c : out) {
        size_t nb = c.patternBytes();
        c.pattern.assign(p + offPat, p + offPat + nb);
        offPat += nb;
      }
      break;
    }

    case Layout::AoS_Packed: {
      size_t off = 0;
      for (uint32_t i = 0; i < nClusters; ++i) {
        auto h = readVal<uint64_t>(p, off);
        unpackHeader(h, out[i], cfg);
        size_t nb = out[i].patternBytes();
        out[i].pattern.assign(p + off, p + off + nb);
        off += nb;
      }
      break;
    }

    case Layout::AoS_Unpacked: {
      size_t off = 0;
      for (uint32_t i = 0; i < nClusters; ++i) {
        out[i].sensorId = readVal<uint32_t>(p, off);
        out[i].row      = readVal<uint16_t>(p, off);
        out[i].col      = readVal<uint16_t>(p, off);
        out[i].height   = readVal<uint16_t>(p, off);
        out[i].width    = readVal<uint16_t>(p, off);
        size_t nb = out[i].patternBytes();
        out[i].pattern.assign(p + off, p + off + nb);
        off += nb;
      }
      break;
    }
  }

  return out;
}

} // namespace ccs
