#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ccs
{

/// Opaque dictionary handle. Compressors that support dictionaries
/// return a non-empty vector from trainDict(); others return {}.
using Dict = std::vector<uint8_t>;

/// Uniform compression interface.
struct ICompressor {
  ICompressor() = default;
  ICompressor(const ICompressor&) = default;
  ICompressor(ICompressor&&) = delete;
  ICompressor& operator=(const ICompressor&) = default;
  ICompressor& operator=(ICompressor&&) = delete;
  virtual ~ICompressor() = default;

  virtual std::vector<uint8_t> compress(std::span<const uint8_t> src, int level) = 0;
  virtual std::vector<uint8_t> decompress(std::span<const uint8_t> src, size_t originalSize) = 0;

  /// Compress/decompress using a pre-trained dictionary.
  /// Default implementations fall back to dict-less versions.
  virtual std::vector<uint8_t> compressWithDict(std::span<const uint8_t> src, int level, const Dict& dict);
  virtual std::vector<uint8_t> decompressWithDict(std::span<const uint8_t> src, size_t originalSize, const Dict& dict);

  /// Train a dictionary from sample data chunks.
  /// Returns empty if this compressor doesn't support dictionaries.
  virtual Dict trainDict(const std::vector<std::span<const uint8_t>>& samples, size_t dictSize = 64 * 1024);

  virtual bool supportsDict() const { return false; }
  virtual std::string name() const = 0;
  /// Return default compression levels to benchmark (low, mid, high).
  virtual std::vector<int> defaultLevels() const = 0;
};

std::unique_ptr<ICompressor> makeZstdCompressor();
std::unique_ptr<ICompressor> makeLZ4Compressor();
std::unique_ptr<ICompressor> makeZlibCompressor();
std::unique_ptr<ICompressor> makeBrotliCompressor();
std::unique_ptr<ICompressor> makeSnappyCompressor();

/// Convenience: get all compressors.
inline std::vector<std::unique_ptr<ICompressor>> allCompressors()
{
  std::vector<std::unique_ptr<ICompressor>> v;
  v.push_back(makeZstdCompressor());
  v.push_back(makeLZ4Compressor());
  v.push_back(makeZlibCompressor());
  v.push_back(makeBrotliCompressor());
  v.push_back(makeSnappyCompressor());
  return v;
}

} // namespace ccs
