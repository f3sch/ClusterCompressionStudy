#include "Compressor.h"

#include <brotli/decode.h>
#include <brotli/encode.h>
#include <lz4.h>
#include <snappy.h>
#include <stdexcept>
#include <zdict.h>
#include <zlib.h>
#include <zstd.h>

namespace ccs
{

// ===================== Base defaults =====================

std::vector<uint8_t> ICompressor::compressWithDict(std::span<const uint8_t> src, int level, const Dict& /*dict*/)
{
  return compress(src, level); // fallback
}

std::vector<uint8_t> ICompressor::decompressWithDict(std::span<const uint8_t> src, size_t originalSize, const Dict& /*dict*/)
{
  return decompress(src, originalSize); // fallback
}

Dict ICompressor::trainDict(const std::vector<std::span<const uint8_t>>& /*samples*/, size_t /*dictSize*/)
{
  return {}; // not supported
}

// ===================== ZSTD =====================
struct ZstdCompressor : ICompressor {
  std::vector<uint8_t> compress(std::span<const uint8_t> src, int level) override
  {
    size_t bound = ZSTD_compressBound(src.size());
    std::vector<uint8_t> dst(bound);
    size_t sz = ZSTD_compress(dst.data(), dst.size(), src.data(), src.size(), level);
    if (ZSTD_isError(sz))
      throw std::runtime_error(std::string("ZSTD compress: ") + ZSTD_getErrorName(sz));
    dst.resize(sz);
    return dst;
  }

  std::vector<uint8_t> decompress(std::span<const uint8_t> src, size_t originalSize) override
  {
    std::vector<uint8_t> dst(originalSize);
    size_t sz = ZSTD_decompress(dst.data(), dst.size(), src.data(), src.size());
    if (ZSTD_isError(sz))
      throw std::runtime_error(std::string("ZSTD decompress: ") + ZSTD_getErrorName(sz));
    dst.resize(sz);
    return dst;
  }

  std::vector<uint8_t> compressWithDict(std::span<const uint8_t> src, int level, const Dict& dict) override
  {
    ZSTD_CDict* cdict = ZSTD_createCDict(dict.data(), dict.size(), level);
    if (!cdict)
      throw std::runtime_error("ZSTD_createCDict failed");

    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    size_t bound = ZSTD_compressBound(src.size());
    std::vector<uint8_t> dst(bound);
    size_t sz = ZSTD_compress_usingCDict(cctx, dst.data(), dst.size(), src.data(), src.size(), cdict);
    ZSTD_freeCCtx(cctx);
    ZSTD_freeCDict(cdict);

    if (ZSTD_isError(sz))
      throw std::runtime_error(std::string("ZSTD compress_usingCDict: ") + ZSTD_getErrorName(sz));
    dst.resize(sz);
    return dst;
  }

  std::vector<uint8_t> decompressWithDict(std::span<const uint8_t> src, size_t originalSize, const Dict& dict) override
  {
    ZSTD_DDict* ddict = ZSTD_createDDict(dict.data(), dict.size());
    if (!ddict)
      throw std::runtime_error("ZSTD_createDDict failed");

    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    std::vector<uint8_t> dst(originalSize);
    size_t sz = ZSTD_decompress_usingDDict(dctx, dst.data(), dst.size(), src.data(), src.size(), ddict);
    ZSTD_freeDCtx(dctx);
    ZSTD_freeDDict(ddict);

    if (ZSTD_isError(sz))
      throw std::runtime_error(std::string("ZSTD decompress_usingDDict: ") + ZSTD_getErrorName(sz));
    dst.resize(sz);
    return dst;
  }

  Dict trainDict(const std::vector<std::span<const uint8_t>>& samples, size_t dictSize) override
  {
    // Flatten samples + build sizes array
    std::vector<uint8_t> combined;
    std::vector<size_t> sizes;
    sizes.reserve(samples.size());
    for (auto& s : samples) {
      combined.insert(combined.end(), s.begin(), s.end());
      sizes.push_back(s.size());
    }

    Dict dict(dictSize);
    size_t result = ZDICT_trainFromBuffer(dict.data(), dict.size(),
                                          combined.data(), sizes.data(),
                                          static_cast<unsigned>(sizes.size()));
    if (ZSTD_isError(result))
      throw std::runtime_error(std::string("ZDICT_trainFromBuffer: ") + ZSTD_getErrorName(result));
    dict.resize(result);
    return dict;
  }

  bool supportsDict() const override { return true; }
  std::string name() const override { return "zstd"; }
  std::vector<int> defaultLevels() const override { return {1, 3, 5, 9, 15, 19}; }
};

// ===================== LZ4 =====================
struct LZ4Compressor : ICompressor {
  std::vector<uint8_t> compress(std::span<const uint8_t> src, int level) override
  {
    (void)level;
    int bound = LZ4_compressBound(static_cast<int>(src.size()));
    std::vector<uint8_t> dst(bound);
    int sz = LZ4_compress_default(
      reinterpret_cast<const char*>(src.data()),
      reinterpret_cast<char*>(dst.data()),
      static_cast<int>(src.size()),
      bound);
    if (sz <= 0)
      throw std::runtime_error("LZ4 compress failed");
    dst.resize(sz);
    return dst;
  }

  std::vector<uint8_t> decompress(std::span<const uint8_t> src, size_t originalSize) override
  {
    std::vector<uint8_t> dst(originalSize);
    int sz = LZ4_decompress_safe(
      reinterpret_cast<const char*>(src.data()),
      reinterpret_cast<char*>(dst.data()),
      static_cast<int>(src.size()),
      static_cast<int>(originalSize));
    if (sz < 0)
      throw std::runtime_error("LZ4 decompress failed");
    dst.resize(sz);
    return dst;
  }

  std::string name() const override { return "lz4"; }
  std::vector<int> defaultLevels() const override { return {0}; }
};

// ===================== ZLIB =====================
struct ZlibCompressor : ICompressor {
  std::vector<uint8_t> compress(std::span<const uint8_t> src, int level) override
  {
    uLongf bound = compressBound(static_cast<uLong>(src.size()));
    std::vector<uint8_t> dst(bound);
    int rc = compress2(dst.data(), &bound, src.data(), static_cast<uLong>(src.size()), level);
    if (rc != Z_OK)
      throw std::runtime_error("zlib compress failed: " + std::to_string(rc));
    dst.resize(bound);
    return dst;
  }

  std::vector<uint8_t> decompress(std::span<const uint8_t> src, size_t originalSize) override
  {
    std::vector<uint8_t> dst(originalSize);
    uLongf dstLen = static_cast<uLongf>(originalSize);
    int rc = uncompress(dst.data(), &dstLen, src.data(), static_cast<uLong>(src.size()));
    if (rc != Z_OK)
      throw std::runtime_error("zlib decompress failed: " + std::to_string(rc));
    dst.resize(dstLen);
    return dst;
  }

  std::string name() const override { return "zlib"; }
  std::vector<int> defaultLevels() const override { return {1, 6, 9}; }
};

// ===================== Brotli =====================
struct BrotliCompressor : ICompressor {
  std::vector<uint8_t> compress(std::span<const uint8_t> src, int level) override
  {
    size_t outSize = BrotliEncoderMaxCompressedSize(src.size());
    std::vector<uint8_t> dst(outSize);
    BROTLI_BOOL ok = BrotliEncoderCompress(
      level, BROTLI_DEFAULT_WINDOW, BROTLI_DEFAULT_MODE,
      src.size(), src.data(), &outSize, dst.data());
    if (!ok)
      throw std::runtime_error("Brotli compress failed");
    dst.resize(outSize);
    return dst;
  }

  std::vector<uint8_t> decompress(std::span<const uint8_t> src, size_t originalSize) override
  {
    std::vector<uint8_t> dst(originalSize);
    size_t outSize = originalSize;
    BrotliDecoderResult res = BrotliDecoderDecompress(
      src.size(), src.data(), &outSize, dst.data());
    if (res != BROTLI_DECODER_RESULT_SUCCESS)
      throw std::runtime_error("Brotli decompress failed");
    dst.resize(outSize);
    return dst;
  }

  std::string name() const override { return "brotli"; }
  // Brotli levels: 0-11, where 0 is fastest
  std::vector<int> defaultLevels() const override { return {0, 1, 4, 6, 9, 11}; }
};

// ===================== Snappy =====================
struct SnappyCompressor : ICompressor {
  std::vector<uint8_t> compress(std::span<const uint8_t> src, int /*level*/) override
  {
    size_t maxLen = snappy::MaxCompressedLength(src.size());
    std::vector<uint8_t> dst(maxLen);
    size_t outLen = 0;
    snappy::RawCompress(reinterpret_cast<const char*>(src.data()), src.size(),
                        reinterpret_cast<char*>(dst.data()), &outLen);
    dst.resize(outLen);
    return dst;
  }

  std::vector<uint8_t> decompress(std::span<const uint8_t> src, size_t originalSize) override
  {
    std::vector<uint8_t> dst(originalSize);
    if (!snappy::RawUncompress(reinterpret_cast<const char*>(src.data()), src.size(),
                               reinterpret_cast<char*>(dst.data())))
      throw std::runtime_error("Snappy decompress failed");
    return dst;
  }

  std::string name() const override { return "snappy"; }
  std::vector<int> defaultLevels() const override { return {0}; } // no levels
};

// ===================== Factory =====================
std::unique_ptr<ICompressor> makeZstdCompressor() { return std::make_unique<ZstdCompressor>(); }
std::unique_ptr<ICompressor> makeLZ4Compressor() { return std::make_unique<LZ4Compressor>(); }
std::unique_ptr<ICompressor> makeZlibCompressor() { return std::make_unique<ZlibCompressor>(); }
std::unique_ptr<ICompressor> makeBrotliCompressor() { return std::make_unique<BrotliCompressor>(); }
std::unique_ptr<ICompressor> makeSnappyCompressor() { return std::make_unique<SnappyCompressor>(); }

} // namespace ccs
