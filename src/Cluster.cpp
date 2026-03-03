#include "Cluster.h"

#include <print>

namespace ccs
{

void Cluster::print() const
{
  std::printf("Header: sens:%u row:%u col:%u height:%u width:%u\n", sensorId, row, col, height, width);
  for (uint16_t r = 0; r < height; ++r) {
    for (uint16_t c = 0; c < width; ++c)
      std::printf("%c", bit(r, c) ? 'X' : '.');
    std::printf("\n");
  }
}

} // namespace ccs
