#pragma once

#include <cassert>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace ccs
{

/// Configurable bit layout — adapt to different detector geometries.
/// All fields must fit within 64 bits total.
/// This is the header representation
struct LayoutConfig {
  uint8_t rowBits = 9;          // max row index (512 → 9 bits)
  uint8_t colBits = 10;         // max col index (1024 → 10 bits)
  uint8_t heightBits = rowBits; // max height (stored as h-1)
  uint8_t widthBits = colBits;  // max width  (stored as w-1)
  uint8_t sensorIdBits = 15;    // max sensorId (ITS2: ~27000 chips so 15 is enough, ALICE3 ~78000 so 17)

  constexpr uint8_t totalBits() const noexcept
  {
    return rowBits + colBits + heightBits + widthBits + sensorIdBits;
  }

  constexpr bool fitsIn64() const noexcept { return totalBits() <= 64; }
};
static_assert(LayoutConfig{}.fitsIn64(), "layout exceeds 64 bits");

/// In-memory cluster representation.
struct Cluster {
  uint32_t sensorId = 0;
  uint16_t row = 0;
  uint16_t col = 0;
  uint16_t height = 0;          // actual height (≥1)
  uint16_t width = 0;           // actual width  (≥1)
  std::vector<uint8_t> pattern; // bit-packed bitmap, ceil(h*w/8) bytes, MSB-first (O2 convention)

  /// Number of bytes needed for the pattern bitmap.
  size_t patternBytes() const
  {
    return (size_t(height) * width + 7) / 8;
  }

  /// Test pixel at (r,c) within the bounding box. MSB-first bit ordering.
  bool bit(uint16_t r, uint16_t c) const
  {
    size_t bitIndex = (size_t(r) * width) + c;
    return (pattern[bitIndex >> 3] >> (7 - (bitIndex % 8))) & 1u;
  }

  /// Set pixel at (r,c) within the bounding box. MSB-first bit ordering.
  void setBit(uint16_t r, uint16_t c)
  {
    size_t bitIndex = (size_t(r) * width) + c;
    pattern[bitIndex >> 3] |= uint8_t(1) << (7 - (bitIndex % 8));
  }

  void print() const;
};

// ---- Bit-packing helpers ----

/// Pack cluster header into a uint64_t according to layout.
/// Layout (LSB to MSB): row | col | height-1 | width-1 | sensorId
inline uint64_t packHeader(const Cluster& c, const LayoutConfig& cfg = {})
{
  assert(cfg.fitsIn64());
  uint64_t v = 0;
  unsigned shift = 0;
  v |= uint64_t(c.row) << shift;
  shift += cfg.rowBits;
  v |= uint64_t(c.col) << shift;
  shift += cfg.colBits;
  v |= uint64_t(c.height - 1) << shift;
  shift += cfg.heightBits;
  v |= uint64_t(c.width - 1) << shift;
  shift += cfg.widthBits;
  v |= uint64_t(c.sensorId) << shift;
  return v;
}

/// Unpack cluster header from a uint64_t.
inline void unpackHeader(uint64_t v, Cluster& c, const LayoutConfig& cfg = {})
{
  unsigned shift = 0;
  c.row = (v >> shift) & ((1u << cfg.rowBits) - 1);
  shift += cfg.rowBits;
  c.col = (v >> shift) & ((1u << cfg.colBits) - 1);
  shift += cfg.colBits;
  c.height = ((v >> shift) & ((1u << cfg.heightBits) - 1)) + 1;
  shift += cfg.heightBits;
  c.width = ((v >> shift) & ((1u << cfg.widthBits) - 1)) + 1;
  shift += cfg.widthBits;
  c.sensorId = (v >> shift) & ((uint64_t(1) << cfg.sensorIdBits) - 1);
}

// ---- Raw binary I/O (the format produced by O2 exporter) ----
// Streamable format — no header, just back-to-back records until EOF:
//   per cluster: [u32 sensorId][u16 row][u16 col][u16 height][u16 width][bitmap bytes?]
// Single-pixel clusters (height==1 && width==1) have NO pattern bytes on disk.
// Fixed per-record header: 4 + 2 + 2 + 2 + 2 = 12 bytes.

inline constexpr size_t kRawRecordHeaderSize = 12; // sensorId(4) + row(2) + col(2) + height(2) + width(2)

inline std::vector<Cluster> readRawClusters(std::span<const uint8_t> data)
{
  const uint8_t* p = data.data();
  const uint8_t* end = data.data() + data.size();

  std::vector<Cluster> out;
  while (p + kRawRecordHeaderSize <= end) {
    Cluster c;
    std::memcpy(&c.sensorId, p, 4);
    p += 4;
    std::memcpy(&c.row, p, 2);
    p += 2;
    std::memcpy(&c.col, p, 2);
    p += 2;
    std::memcpy(&c.height, p, 2);
    p += 2;
    std::memcpy(&c.width, p, 2);
    p += 2;

    if (c.height == 1 && c.width == 1) {
      // Single pixel: no pattern on disk, synthesize a 1-byte pattern with the bit set
      c.pattern = {0x80}; // MSB-first: bit 0 set
    } else {
      size_t nb = c.patternBytes();
      if (p + nb > end)
        break;
      c.pattern.assign(p, p + nb);
      p += nb;
    }
    out.push_back(std::move(c));
  }
  return out;
}

} // namespace ccs
