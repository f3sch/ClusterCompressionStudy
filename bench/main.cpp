#include "Cluster.h"
#include "ClusterIO.h"
#include "Compressor.h"
#include "Helpers.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <numeric>
#include <random>
#include <vector>

using namespace ccs;
using Clock = std::chrono::high_resolution_clock;

// ---- Helpers ----

static std::vector<uint8_t> readFile(const char* path)
{
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f)
    throw std::runtime_error(std::string("Cannot open ") + path);
  auto sz = f.tellg();
  f.seekg(0);
  std::vector<uint8_t> buf(sz);
  f.read(reinterpret_cast<char*>(buf.data()), sz);
  return buf;
}

/// Generate a single cluster via random walk of `nPixels` fired pixels.
/// The walk starts at (0,0) in a local grid and moves to one of 8 neighbours
/// each step, guaranteeing diagonal connectivity. We then compute the bounding
/// box, clamp the origin so it fits on the chip, and produce the bitmap.
static Cluster generateOneCluster(std::mt19937& rng, uint32_t sensorId, int nPixels, const LayoutConfig& cfg)
{
  const int maxRow = static_cast<int>(max_from_bits(cfg.rowBits));
  const int maxCol = static_cast<int>(max_from_bits(cfg.colBits));

  // 8-connected random walk in local coordinates
  static constexpr int dr[] = {-1, -1, -1, 0, 0, 1, 1, 1};
  static constexpr int dc[] = {-1, 0, 1, -1, 1, -1, 0, 1};
  std::uniform_int_distribution<int> dirDist(0, 7);

  std::vector<std::pair<int, int>> pixels;
  pixels.reserve(nPixels);
  pixels.push_back({0, 0});

  int r = 0, c = 0;
  for (int i = 1; i < nPixels; ++i) {
    int d = dirDist(rng);
    r += dr[d];
    c += dc[d];
    pixels.push_back({r, c});
  }

  // Compute bounding box in local coords
  int minR = 0, maxR = 0, minC = 0, maxC = 0;
  for (auto& [pr, pc] : pixels) {
    minR = std::min(minR, pr);
    maxR = std::max(maxR, pr);
    minC = std::min(minC, pc);
    maxC = std::max(maxC, pc);
  }
  int h = maxR - minR + 1;
  int w = maxC - minC + 1;

  // Pick a random origin such that the box fits on the chip
  std::uniform_int_distribution<int> originRowDist(0, std::max(0, maxRow - h + 1));
  std::uniform_int_distribution<int> originColDist(0, std::max(0, maxCol - w + 1));
  int originRow = originRowDist(rng);
  int originCol = originColDist(rng);

  // Build bit-packed pattern
  Cluster cl;
  cl.sensorId = sensorId;
  cl.row = static_cast<uint16_t>(originRow);
  cl.col = static_cast<uint16_t>(originCol);
  cl.height = static_cast<uint16_t>(h);
  cl.width = static_cast<uint16_t>(w);
  cl.pattern.resize(cl.patternBytes(), 0);

  for (auto& [pr, pc] : pixels) {
    cl.setBit(pr - minR, pc - minC);
  }
  return cl;
}

static std::vector<Cluster> generateSynthetic(size_t n, const LayoutConfig& cfg)
{
  std::random_device dev;
  std::mt19937 rng(dev());
  std::uniform_int_distribution<uint32_t> sensorDist(0, max_from_bits(cfg.sensorIdBits));
  // Cluster sizes: geometric distribution, most are 1-3 pixels
  std::geometric_distribution<int> sizeDist(0.2);

  std::vector<Cluster> out;
  out.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    int nPixels = std::clamp(sizeDist(rng) + 1, 1, (int)(max_from_bits(cfg.colBits) * max_from_bits(cfg.rowBits)));
    out.push_back(generateOneCluster(rng, sensorDist(rng), nPixels, cfg));
  }
  return out;
}

static double median(std::vector<double>& v)
{
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}

// ---- Main ----

/// Load timeframes: each file is one TF of streamed clusters.
/// Returns one vector<Cluster> per TF.
static std::vector<std::vector<Cluster>> loadTimeframes(int argc, char** argv)
{
  std::vector<std::vector<Cluster>> tfs;
  for (int i = 1; i < argc; ++i) {
    std::fprintf(stderr, "Loading TF from %s ...\n", argv[i]);
    auto data = readFile(argv[i]);
    tfs.push_back(readRawClusters(data));
    std::fprintf(stderr, "  -> %zu clusters\n", tfs.back().size());
  }
  return tfs;
}

/// Generate synthetic timeframes.
static std::vector<std::vector<Cluster>> generateSyntheticTFs(size_t nTFs, size_t clustersPerTF,
                                                              const LayoutConfig& cfg)
{
  std::random_device dev;
  std::mt19937 rng(dev());
  std::uniform_int_distribution<uint32_t> sensorDist(0, max_from_bits(cfg.sensorIdBits));
  std::geometric_distribution<int> sizeDist(0.2);
  int maxPixels = static_cast<int>(max_from_bits(cfg.colBits) * max_from_bits(cfg.rowBits));

  std::vector<std::vector<Cluster>> tfs;
  for (size_t tf = 0; tf < nTFs; ++tf) {
    std::vector<Cluster> clusters;
    clusters.reserve(clustersPerTF);
    for (size_t i = 0; i < clustersPerTF; ++i) {
      int nPixels = std::clamp(sizeDist(rng) + 1, 1, maxPixels);
      clusters.push_back(generateOneCluster(rng, sensorDist(rng), nPixels, cfg));
    }
    tfs.push_back(std::move(clusters));
  }
  return tfs;
}

// ---- Main ----

int main(int argc, char** argv)
{
  LayoutConfig cfg;

  // Load data: one file per timeframe, or generate synthetic
  std::vector<std::vector<Cluster>> timeframes;
  if (argc >= 2) {
    timeframes = loadTimeframes(argc, argv);
  } else {
    constexpr size_t N_TFS = 10;
    constexpr size_t CLUSTERS_PER_TF = 10'000'000;
    std::fprintf(stderr, "No input files given, generating %zu synthetic TFs of %zu clusters ...\n",
                 N_TFS, CLUSTERS_PER_TF);
    timeframes = generateSyntheticTFs(N_TFS, CLUSTERS_PER_TF, cfg);
  }

  // Stats
  size_t totalClusters = 0;
  size_t totalPatternBytes = 0;
  for (auto& tf : timeframes) {
    totalClusters += tf.size();
    for (auto& c : tf)
      totalPatternBytes += c.pattern.size();
  }
  size_t totalHeaderBytes = totalClusters * sizeof(uint64_t);
  size_t totalRawBytes = totalHeaderBytes + totalPatternBytes;

  std::printf("%zu timeframes, %zu clusters total\n", timeframes.size(), totalClusters);
  std::printf("Raw data: headers (%zu B) + patterns (%zu B) = %zu B (%.2f MB)\n",
              totalHeaderBytes, totalPatternBytes, totalRawBytes,
              totalRawBytes / (1024.0 * 1024.0));
  std::printf("\n");

  constexpr int ITERATIONS = 5;
  auto compressors = allCompressors();

  // Pre-serialize each TF for each layout (done once, outside timing loop)
  struct SerializedTFs {
    Layout layout;
    std::vector<std::vector<uint8_t>> buffers; // one per TF
    size_t totalBytes = 0;
  };

  std::vector<SerializedTFs> serializedByLayout;
  for (Layout layout : allLayouts) {
    SerializedTFs stf;
    stf.layout = layout;
    for (auto& tf : timeframes) {
      stf.buffers.push_back(serialize(tf, layout, cfg));
      stf.totalBytes += stf.buffers.back().size();
    }
    serializedByLayout.push_back(std::move(stf));
  }

  // Open CSV file for results
  std::FILE* csv = std::fopen("results.csv", "w");
  if (!csv) {
    std::fprintf(stderr, "WARNING: could not open results.csv for writing\n");
  } else {
    std::fprintf(csv, "Layout,Algo,Level,Dict,Compressed,Ratio,EncodeMBs,DecodeMBs\n");
  }

  // Pretty table header
  std::printf("%-6s %-8s %5s %5s %12s %10s %12s %12s\n",
              "Layout", "Algo", "Level", "Dict", "Compressed", "Ratio", "Encode MB/s", "Decode MB/s");
  std::printf("%-6s %-8s %5s %5s %12s %10s %12s %12s\n",
              "------", "------", "-----", "-----", "----------", "----------", "-----------", "-----------");

  for (auto& stf : serializedByLayout) {
    double rawMB = stf.totalBytes / (1024.0 * 1024.0);

    for (auto& comp : compressors) {
      // Train dictionary across TFs (each TF is a sample)
      Dict dict;
      if (comp->supportsDict() && stf.buffers.size() > 1) {
        std::vector<std::span<const uint8_t>> samples;
        size_t trainCount = std::min<size_t>(stf.buffers.size(), 1);
        for (size_t i = 0; i < trainCount; ++i)
          samples.emplace_back(stf.buffers[i]);
        dict = comp->trainDict(samples);
        std::fprintf(stderr, "  %s/%s: trained dict %zu bytes from %zu TFs\n",
                     layoutName(stf.layout), comp->name().c_str(),
                     dict.size(), trainCount);
      }

      for (int level : comp->defaultLevels()) {
        for (bool useDict : {false, true}) {
          if (useDict && (!comp->supportsDict() || stf.buffers.size() <= 1))
            continue;

          std::vector<double> encodeTimes, decodeTimes;
          size_t totalCompressed = 0;

          for (int iter = 0; iter < ITERATIONS; ++iter) {
            totalCompressed = 0;

            // Encode all TFs
            auto t0 = Clock::now();
            std::vector<std::vector<uint8_t>> compressedBufs(stf.buffers.size());
            for (size_t i = 0; i < stf.buffers.size(); ++i) {
              compressedBufs[i] = useDict
                                    ? comp->compressWithDict(stf.buffers[i], level, dict)
                                    : comp->compress(stf.buffers[i], level);
              totalCompressed += compressedBufs[i].size();
            }
            auto t1 = Clock::now();
            encodeTimes.push_back(std::chrono::duration<double>(t1 - t0).count());

            // Decode all TFs
            auto t2 = Clock::now();
            for (size_t i = 0; i < stf.buffers.size(); ++i) {
              auto decompressed = useDict
                                    ? comp->decompressWithDict(compressedBufs[i], stf.buffers[i].size(), dict)
                                    : comp->decompress(compressedBufs[i], stf.buffers[i].size());
              (void)decompressed;
            }
            auto t3 = Clock::now();
            decodeTimes.push_back(std::chrono::duration<double>(t3 - t2).count());

            // Verify round-trip on first iteration
            if (iter == 0 && !useDict) {
              for (size_t i = 0; i < stf.buffers.size(); ++i) {
                auto decoded = comp->decompress(compressedBufs[i], stf.buffers[i].size());
                if (decoded != stf.buffers[i]) {
                  std::fprintf(stderr, "MISMATCH: %s/%s TF %zu\n",
                               layoutName(stf.layout), comp->name().c_str(), i);
                  break;
                }
              }
            }
          }

          double encMed = median(encodeTimes);
          double decMed = median(decodeTimes);
          double ratio = double(stf.totalBytes) / totalCompressed;
          double encodeMBs = rawMB / encMed;
          double decodeMBs = rawMB / decMed;
          const char* dictStr = useDict ? "yes" : "no";

          // Pretty stdout
          std::printf("%-6s %-8s %5d %5s %12zu %9.2fx %11.1f %11.1f\n",
                      layoutName(stf.layout), comp->name().c_str(), level,
                      dictStr, totalCompressed, ratio, encodeMBs, decodeMBs);

          // CSV file
          if (csv) {
            std::fprintf(csv, "%s,%s,%d,%s,%zu,%.4f,%.2f,%.2f\n",
                         layoutName(stf.layout), comp->name().c_str(), level,
                         dictStr, totalCompressed, ratio, encodeMBs, decodeMBs);
          }
        }
      }
    }
  }

  if (csv) {
    std::fclose(csv);
    std::fprintf(stderr, "Results written to results.csv\n");
  }

  return 0;
}
